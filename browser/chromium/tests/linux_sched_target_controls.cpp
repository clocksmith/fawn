// Real GPU BPF policy controls. No display, context, provider or GPU work.
#include "content/common/gpu_pre_sandbox_hook_linux.h"

#include <atomic>
#include <cerrno>
#include <cstdio>
#include <pthread.h>
#include <sched.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#include "base/at_exit.h"
#include "base/check.h"
#include "base/command_line.h"
#include "base/functional/bind.h"
#include "sandbox/policy/linux/sandbox_linux.h"
#include "sandbox/policy/sandbox_type.h"

struct Scheduling {
  int policy;
  int priority;
};

Scheduling ObserveSelf() {
  sched_param param{};
  CHECK_EQ(sched_getparam(0, &param), 0);
  const int policy = sched_getscheduler(0);
  CHECK_GE(policy, 0);
  return {policy, param.sched_priority};
}

void CheckUnchanged(const char* target, Scheduling before, Scheduling after) {
  printf("TARGET %s policy=%d/%d priority=%d/%d\n", target, before.policy, after.policy, before.priority, after.priority);
  CHECK_EQ(before.policy, after.policy);
  CHECK_EQ(before.priority, after.priority);
}

struct Worker {
  std::atomic<int> stage{0};
  std::atomic<pid_t> tid{0};
  Scheduling before{};
  Scheduling after{};
  bool test_self = false;
};

void* RunWorker(void* opaque) {
  auto& worker = *static_cast<Worker*>(opaque);
  worker.before = ObserveSelf();
  worker.tid = syscall(SYS_gettid);
  if (worker.test_self) {
    sched_param param{};
    CHECK_EQ(sched_setscheduler(0, SCHED_OTHER, &param), 0);
    CHECK_EQ(sched_setscheduler(worker.tid, SCHED_OTHER, &param), 0);
  }
  worker.stage.store(1);
  while (worker.stage.load() == 1) sched_yield();
  worker.after = ObserveSelf();
  return nullptr;
}

int main(int argc, char** argv) {
  base::AtExitManager at_exit;
  CHECK(base::CommandLine::Init(argc, argv));
  const auto* command = base::CommandLine::ForCurrentProcess();
  const auto operation = command->GetSwitchValueASCII("operation");
  const auto mode = command->GetSwitchValueASCII("policy");
  CHECK(!operation.empty());
  sandbox::policy::SandboxLinux::Options options;
#ifndef DOE_TEST_BASELINE
  const bool selected = content::UsesEarlyExternalGlxResources(
      !command->HasSwitch("late-policy"));
  CHECK_EQ(selected, command->HasSwitch("expect-selected"));
  if (mode == "scoped" && selected) {
    options.gpu_sched_setscheduler_failure =
        sandbox::SchedTargetFailurePolicy::kReturnEPERM;
  }
#endif
  int requests[2], responses[2];
  CHECK_EQ(pipe(requests), 0);
  CHECK_EQ(pipe(responses), 0);
  // An owned, unsandboxed foreign fixture reports its own scheduler. It exits
  // on parent-pipe EOF even when an intentionally fatal policy kills the parent.
  const pid_t foreign = fork();
  CHECK_GE(foreign, 0);
  if (!foreign) {
    close(requests[1]);
    close(responses[0]);
    char request;
    while (read(requests[0], &request, 1) == 1) {
      if (request == 'q') break;
      const Scheduling state = ObserveSelf();
      CHECK_EQ(write(responses[1], &state, sizeof(state)), static_cast<ssize_t>(sizeof(state)));
    }
    _exit(0);
  }
  close(requests[0]);
  close(responses[1]);
  auto foreign_state = [&] {
    const char request = 's';
    CHECK_EQ(write(requests[1], &request, 1), 1);
    Scheduling state{};
    CHECK_EQ(read(responses[0], &state, sizeof(state)), static_cast<ssize_t>(sizeof(state)));
    return state;
  };
  const auto foreign_before = foreign_state();
  const auto main_before = ObserveSelf();
  CHECK_EQ(main_before.policy, SCHED_OTHER);
  CHECK(sandbox::policy::SandboxTypeFromCommandLine(*command) ==
        sandbox::mojom::Sandbox::kGpu);
  CHECK(sandbox::policy::SandboxLinux::GetInstance()->InitializeSandbox(
      sandbox::mojom::Sandbox::kGpu,
      base::BindOnce([](sandbox::policy::SandboxLinux::Options bound) {
        sandbox::policy::SandboxLinux::GetInstance()->StartBrokerProcess(
            {}, {}, bound);
        return true;
      }), options));
  CHECK_EQ(prctl(PR_GET_SECCOMP, 0, 0, 0, 0), 2);
  Worker worker;
  worker.test_self = operation == "self-worker";
  pthread_t thread;
  CHECK_EQ(pthread_create(&thread, nullptr, RunWorker, &worker), 0);
  while (worker.stage.load() != 1) sched_yield();
  printf("SEALED operation=%s mode=%s main=%d worker=%d foreign=%d\n",
         operation.c_str(), mode.c_str(), getpid(), worker.tid.load(), foreign);
  fflush(stdout);
  sched_param param{};
  if (operation == "self-main") {
    CHECK_EQ(sched_setscheduler(0, SCHED_OTHER, &param), 0);
    CHECK_EQ(sched_setscheduler(getpid(), SCHED_OTHER, &param), 0);
  } else if (operation == "sibling" || operation == "foreign") {
    const pid_t target = operation == "sibling" ? worker.tid.load() : foreign;
    printf("EXPECT_DENIED_SYSCALL nr=0x%lx\n", static_cast<long>(SYS_sched_setscheduler));
    fflush(stdout);
    errno = 0;
    CHECK_EQ(sched_setscheduler(target, SCHED_BATCH, &param), -1);
    CHECK_EQ(errno, EPERM);
    printf("DENIED target=%d errno=%d\n", target, errno);
  } else if (operation == "affinity-sibling") {
    cpu_set_t cpus;
    CPU_ZERO(&cpus);
    CPU_SET(0, &cpus);
    printf("EXPECT_DENIED_SYSCALL nr=0x%lx\n", static_cast<long>(SYS_sched_setaffinity));
    fflush(stdout);
    // Must retain the existing fatal restriction; this is not the repaired call.
    CHECK_EQ(sched_setaffinity(worker.tid, sizeof(cpus), &cpus), 0);
    CHECK(false) << "Nonself affinity unexpectedly returned";
  } else {
    CHECK_EQ(operation, "self-worker");
  }
  CheckUnchanged("main", main_before, ObserveSelf());
  CheckUnchanged("foreign", foreign_before, foreign_state());
  worker.stage.store(2);
  CHECK_EQ(pthread_join(thread, nullptr), 0);
  CheckUnchanged("worker", worker.before, worker.after);
  const char stop = 'q';
  CHECK_EQ(write(requests[1], &stop, 1), 1);
  close(requests[1]);
  close(responses[0]);
  int status = -1;
  CHECK_EQ(waitpid(foreign, &status, 0), foreign);
  CHECK(WIFEXITED(status));
  CHECK_EQ(WEXITSTATUS(status), 0);
  printf("PASS seccomp=2 self/current-TID retained; target priorities unchanged; no GPU\n");
  fflush(stdout);
  return 0;
}
