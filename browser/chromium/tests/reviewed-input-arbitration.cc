// Compile against the patched Chromium checkout. Uses the production gate in a
// real shared mapping across two processes, without a renderer or a GPU.
#include "components/viz/common/surfaces/fawn_visual_review.h"

#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cassert>
#include <cstdio>
#include <new>

#ifdef NDEBUG
#error "Run this control with assertions enabled"
#endif

namespace {
using Gate = viz::FawnVisualReviewGate;
enum class Order { kDamageFirst, kInputFirst, kConcurrent };

void Transfer(int fd, bool send) {
  char value = 'x';
  const auto n = send ? write(fd, &value, 1) : read(fd, &value, 1);
  assert(n == 1);
}

void Check(Order order) {
  void* mem = mmap(nullptr, sizeof(Gate), PROT_READ | PROT_WRITE,
                   MAP_SHARED | MAP_ANONYMOUS, -1, 0);
  assert(mem != MAP_FAILED);
  auto* gate = new (mem) Gate;
  int release[2], done[2];
  assert(pipe(release) == 0 && pipe(done) == 0);
  const pid_t child = fork();
  assert(child >= 0);
  if (child == 0) {
    close(release[1]);
    close(done[0]);
    Transfer(release[0], false);
    gate->Invalidate();
    Transfer(done[1], true);
    _exit(0);
  }
  close(release[0]);
  close(done[1]);
  if (order != Order::kInputFirst)
    Transfer(release[1], true);
  if (order == Order::kDamageFirst)
    Transfer(done[0], false);
  const bool admitted = gate->Admit();
  if (order == Order::kInputFirst)
    Transfer(release[1], true);
  if (order != Order::kDamageFirst)
    Transfer(done[0], false);
  int status;
  assert(waitpid(child, &status, 0) == child && WIFEXITED(status) &&
         WEXITSTATUS(status) == 0);
  assert(!gate->Open() && !gate->Admit());
  assert(gate->state.load() == (admitted ? Gate::kAdmitted : Gate::kStale));
  if (order != Order::kConcurrent)
    assert(admitted == (order == Order::kInputFirst));
  // Viz names the first replacement frame whichever side won.
  assert(gate->replaced_by.load() == 0);
  gate->RecordReplacement(7);
  gate->RecordReplacement(9);
  assert(gate->replaced_by.load() == 7);
  close(done[0]);
  close(release[1]);
  gate->~Gate();
  assert(munmap(mem, sizeof(Gate)) == 0);
}
}  // namespace

int main() {
  Check(Order::kDamageFirst);
  Check(Order::kInputFirst);
  for (int i = 0; i < 128; ++i)
    Check(Order::kConcurrent);
  puts("Passed invalidation-first, admission-first, and 128 unordered races.");
}
