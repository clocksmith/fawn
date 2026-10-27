// Actual GPU broker/GLX resource control. No GL context, device, draw or dispatch.
// Compiled with the pinned owning TU flags; checks stay enabled in release builds.
#include "content/common/gpu_pre_sandbox_hook_linux.cc"
#include "third_party/vulkan-headers/src/include/vulkan/vulkan_core.h"
#include <X11/Xlib.h>
#include <dirent.h>
#include <sys/prctl.h>
#include "sandbox/policy/sandbox_type.h"
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include "base/at_exit.h"
#include "base/check.h"
#include "base/functional/bind.h"
#include "sandbox/linux/syscall_broker/broker_client.h"
#include "sandbox/linux/syscall_broker/broker_permission_list.h"

using sandbox::syscall_broker::BrokerFilePermission;
using sandbox::syscall_broker::BrokerProcess;
using sandbox::syscall_broker::BrokerSandboxConfig;

namespace {
bool fail_metadata_query = false;
unsigned metadata_queries = 0;

VKAPI_ATTR VkResult VKAPI_CALL FailedMetadataQuery(
    const char*, uint32_t* count, VkExtensionProperties*) {
  ++metadata_queries;
  *count = 7;
  return VK_ERROR_OUT_OF_HOST_MEMORY;
}
}  // namespace

extern "C" void* __real_dlsym(void*, const char*);
extern "C" void* __wrap_dlsym(void* library, const char* name) {
  if (fail_metadata_query &&
      strcmp(name, "vkEnumerateInstanceExtensionProperties") == 0) {
    return reinterpret_cast<void*>(&FailedMetadataQuery);
  }
  return __real_dlsym(library, name);
}

unsigned ExpectedHex(const char* name) {
  unsigned value = 0;
  CHECK(base::HexStringToUInt(
      base::CommandLine::ForCurrentProcess()->GetSwitchValueASCII(name), &value));
  return value;
}

unsigned CountThreads() {
  DIR* dir = opendir("/proc/self/task");
  CHECK(dir);
  unsigned count = 0;
  while (auto* entry = readdir(dir)) {
    if (entry->d_name[0] >= '0' && entry->d_name[0] <= '9') ++count;
  }
  CHECK_EQ(closedir(dir), 0);
  return count;
}

// Metadata admission never constructs a GL context or submits GPU commands.
void ObserveGlx(Display* display, void* library) {
  using QueryVersion = Bool (*)(Display*, int*, int*);
  using Extensions = const char* (*)(Display*, int);
  using Configs = void** (*)(Display*, int, const int*, int*);
  using GetProc = void* (*)(const unsigned char*);
  using QueryRenderer = Bool (*)(Display*, int, int, int, unsigned int*);
  auto version = reinterpret_cast<QueryVersion>(dlsym(library, "glXQueryVersion"));
  auto extensions = reinterpret_cast<Extensions>(dlsym(library, "glXQueryExtensionsString"));
  auto configs = reinterpret_cast<Configs>(dlsym(library, "glXChooseFBConfig"));
  auto get_proc = reinterpret_cast<GetProc>(dlsym(library, "glXGetProcAddress"));
  CHECK(version && extensions && configs && get_proc);
  int major_version = 0, minor_version = 0, count = 0;
  CHECK(version(display, &major_version, &minor_version));
  const char* names = extensions(display, DefaultScreen(display));
  CHECK(names);
  void** list = configs(display, DefaultScreen(display), nullptr, &count);
  CHECK(list);
  XFree(list);
  auto renderer = reinterpret_cast<QueryRenderer>(get_proc(
      reinterpret_cast<const unsigned char*>("glXQueryRendererIntegerMESA")));
  CHECK(renderer);
  unsigned int vendor = 0, device = 0, accelerated = 0;
  // These tokens are from the pinned ANGLE GLX/glxext.h (MESA_query_renderer).
  CHECK(renderer(display, DefaultScreen(display), 0, 0x8183, &vendor));
  CHECK(renderer(display, DefaultScreen(display), 0, 0x8184, &device));
  CHECK(renderer(display, DefaultScreen(display), 0, 0x8186, &accelerated));
  printf("GLX_METADATA version=%d.%d configs=%d extensions=%zu vendor=%x device=%x accelerated=%u\n",
         major_version, minor_version, count, strlen(names), vendor, device, accelerated);
  fflush(stdout);
  CHECK_EQ(vendor, ExpectedHex("expected-vendor"));
  CHECK_EQ(device, ExpectedHex("expected-device"));
  CHECK_EQ(accelerated, 1u);
  CHECK_GT(count, 0);
  const auto expected = base::CommandLine::ForCurrentProcess()->GetSwitchValueASCII("expected-configs");
  if (!expected.empty()) {
    int expected_count = 0;
    CHECK(base::StringToInt(expected, &expected_count));
    CHECK_EQ(count, expected_count);
  }
}

void ObserveVulkanMetadata() {
  void* library = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
  CHECK(library);
  auto enumerate = reinterpret_cast<PFN_vkEnumerateInstanceExtensionProperties>(
      dlsym(library, "vkEnumerateInstanceExtensionProperties"));
  CHECK(enumerate);
  uint32_t count = 0;
  CHECK_EQ(enumerate(nullptr, &count, nullptr), VK_SUCCESS);
  CHECK_GT(count, 0u);
  CHECK_LT(count, 1024u);
  std::vector<VkExtensionProperties> properties(count);
  CHECK_EQ(enumerate(nullptr, &count, properties.data()), VK_SUCCESS);
  CHECK_LE(count, properties.size());
  bool surface = false;
  printf("VULKAN_METADATA count=%u\n", count);
  for (uint32_t i = 0; i < count; ++i) {
    printf("VULKAN_EXTENSION %s\n", properties[i].extensionName);
    surface |= strcmp(properties[i].extensionName, "VK_KHR_surface") == 0;
  }
  CHECK(surface);  // Loader-only extensions cannot satisfy the hardware fixture.
  dlclose(library);
}

void MetadataControl(const sandbox::policy::SandboxLinux::Options& options) {
  const auto* command_line = base::CommandLine::ForCurrentProcess();
  Display* display = XOpenDisplay(nullptr);
  CHECK(display);
  CHECK_EQ(CountThreads(), 1u);
  const bool seal = command_line->HasSwitch("metadata-sandbox");
  if (seal) {
    auto hook = base::BindOnce([](sandbox::policy::SandboxLinux::Options bound) {
      CHECK_EQ(CountThreads(), 1u);
      CHECK(content::GpuPreSandboxHookWithEarlyGlxResources(true, bound));
      CHECK_EQ(CountThreads(), 1u);
      printf("PRE_SEAL fixed library/resource preparation threads=1\n");
      fflush(stdout);
      return true;
    });
    CHECK(sandbox::policy::SandboxLinux::GetInstance()->InitializeSandbox(
        sandbox::policy::SandboxTypeFromCommandLine(*command_line),
        std::move(hook), options));
    CHECK_EQ(prctl(PR_GET_SECCOMP, 0, 0, 0, 0), 2);
    CHECK_EQ(open("/sys/kernel/uevent_seqnum", O_RDONLY), -1);
    CHECK_EQ(errno, EACCES);
    CHECK_EQ(open("/dev/null", O_RDWR), -1);
    CHECK_EQ(errno, EACCES);
  } else {
    CHECK(content::LoadLibrariesForGpu(options, true));
    CHECK_EQ(CountThreads(), 1u);
  }
  void* library = nullptr;
  if (command_line->HasSwitch("vulkan-metadata")) {
    ObserveVulkanMetadata();
  } else {
    library = dlopen("libGL.so.1", RTLD_NOW | RTLD_NOLOAD);
    CHECK(library);
    ObserveGlx(display, library);
  }
  CHECK_EQ(XCloseDisplay(display), 0);
  if (library) dlclose(library);
  printf("PASS metadata-only native hardware identity seal=%d; NO GL context/device/draw/dispatch\n", seal);
  fflush(stdout);
}

int main(int argc, char** argv) {
  base::AtExitManager at_exit;
  CHECK(base::CommandLine::Init(argc, argv));
  const auto* command_line = base::CommandLine::ForCurrentProcess();
  const bool expect = command_line->HasSwitch("expect-glx-resources");
  sandbox::policy::SandboxLinux::Options options;
  const unsigned vendor_id = ExpectedHex("expected-vendor");
  options.use_intel_specific_policies = vendor_id == 0x8086;
  options.use_amd_specific_policies = vendor_id == 0x1002;
  options.use_nvidia_specific_policies = vendor_id == 0x10de;
  options.use_virtio_specific_policies = vendor_id == 0x1af4;
  CHECK(options.use_intel_specific_policies || options.use_amd_specific_policies ||
        options.use_nvidia_specific_policies || options.use_virtio_specific_policies);
  const bool early = !command_line->HasSwitch("late-policy");
  if (command_line->HasSwitch("prewarm-query-error")) {
    fail_metadata_query = true;
    CHECK_EQ(content::LoadLibrariesForGpu(options, early), !expect);
    CHECK_EQ(metadata_queries, expect ? 1u : 0u);
    CHECK_EQ(CountThreads(), 1u);
    printf("PASS metadata failure transport/route queries=%u externalGlx=%d\n",
           metadata_queries, expect);
    return 0;
  }
  if (command_line->HasSwitch("metadata-baseline") || command_line->HasSwitch("metadata-sandbox")) {
    MetadataControl(options);
    return 0;
  }
  const std::string node = command_line->GetSwitchValuePath("drm-node").value();
  CHECK(!node.empty());
  struct stat info;
  CHECK_EQ(lstat(node.c_str(), &info), 0);
  CHECK(S_ISCHR(info.st_mode));
  CHECK_EQ(major(info.st_rdev), 226u);
  CHECK_EQ(CountThreads(), 1u);

  // Test the real existing DRM owner rejects wrong classes/vendor identities.
  std::vector<BrokerFilePermission> rejected;
  struct stat regular = info;
  regular.st_mode = S_IFREG | 0600;
  CHECK(!content::AddExternalGlxDrmPermissions(&rejected, node, regular, options));
  struct stat other_device;
  CHECK_EQ(lstat("/dev/null", &other_device), 0);
  CHECK(!content::AddExternalGlxDrmPermissions(&rejected, "/dev/null", other_device, options));
  auto wrong_vendor = options;
  wrong_vendor.use_intel_specific_policies = vendor_id != 0x8086;
  wrong_vendor.use_amd_specific_policies = vendor_id == 0x8086;
  wrong_vendor.use_nvidia_specific_policies = false;
  wrong_vendor.use_virtio_specific_policies = false;
  CHECK(!content::AddExternalGlxDrmPermissions(&rejected, node, info, wrong_vendor));
  CHECK(rejected.empty());

  const base::FilePath alias(base::StringPrintf("/sys/dev/char/%u:%u", major(info.st_rdev), minor(info.st_rdev)));
  const base::FilePath device = base::MakeAbsoluteFilePath(alias.Append("device"));
  CHECK(device.value().starts_with("/sys/devices/"));
  auto permissions = content::FilePermissionsForGpu(options, early);
  const auto commands = content::CommandSetForGPU(options, early);
  sandbox::syscall_broker::BrokerPermissionList decisions(EACCES, permissions);
  CHECK_EQ(decisions.GetFileNameIfAllowedToOpen(node.c_str(), O_RDWR).first != nullptr, expect);
  CHECK_EQ(commands.test(sandbox::syscall_broker::COMMAND_READLINK), expect);
  for (const auto& path : {device.Append("vendor").value(), device.Append("device").value(), device.Append("uevent").value()}) {
    CHECK_EQ(decisions.GetFileNameIfAllowedToOpen(path.c_str(), O_RDONLY).first != nullptr, expect);
    CHECK(!decisions.GetFileNameIfAllowedToOpen(path.c_str(), O_WRONLY).first);
    CHECK(!decisions.GetFileNameIfAllowedToOpen(path.c_str(), O_RDWR).first);
  }
  for (const auto& path : {std::string("/dev/null"), std::string("/dev/dri/renderD999"),
                           std::string("/sys/kernel/uevent_seqnum"), device.Append("config").value(),
                           device.Append("resource0").value(), std::string("/etc/passwd"),
                           std::string("/usr/share/drirc.d/unlisted.conf"),
                           base::FilePath(getenv("HOME")).Append(".drirc").value()}) {
    CHECK(!decisions.GetFileNameIfAllowedToOpen(path.c_str(), O_RDONLY).first);
    CHECK(!decisions.GetFileNameIfAllowedToOpen(path.c_str(), O_WRONLY).first);
  }

  BrokerProcess broker(std::optional<BrokerSandboxConfig>(std::in_place, commands, std::move(permissions), EACCES),
                       BrokerProcess::BrokerType::SIGNAL_BASED,
                       /*fast_check_in_client=*/false);
  CHECK(broker.Fork(base::BindOnce([](const BrokerSandboxConfig&) { return true; })));
  const auto* client = broker.GetBrokerClientSignalBased();
  CHECK(client);
  const std::string vendor = device.Append("vendor").value();
  const int fd = client->Open(vendor.c_str(), O_RDONLY);
  if (expect) {
    CHECK_GE(fd, 0);
    char buffer[32] = {};
    CHECK_GT(read(fd, buffer, sizeof(buffer)), 0);
    CHECK_EQ(close(fd), 0);
    printf("actualBrokerReadVendor=%s", buffer);
    char target[4096];
    CHECK_GT(client->Readlink(alias.value().c_str(), target, sizeof(target)), 0);
  } else {
    CHECK_EQ(fd, -EACCES);
  }
  CHECK_EQ(client->Open(vendor.c_str(), O_WRONLY), -EACCES);
  CHECK_EQ(client->Open(device.Append("resource0").value().c_str(), O_RDONLY), -EACCES);
  CHECK_EQ(client->Open("/dev/null", O_RDWR), -EACCES);
  CHECK_EQ(client->Open("/sys/kernel/uevent_seqnum", O_RDONLY), -EACCES);
  CHECK_EQ(client->Open(base::FilePath(getenv("HOME")).Append(".drirc").value().c_str(), O_RDONLY), -EACCES);
  const std::string config = command_line->GetSwitchValuePath("drirc-file").value();
  CHECK(!config.empty());
  CHECK_EQ(decisions.GetFileNameIfAllowedToOpen(config.c_str(), O_RDONLY).first != nullptr, expect);
  CHECK(!decisions.GetFileNameIfAllowedToOpen(config.c_str(), O_WRONLY).first);
  CHECK_EQ(client->Open(config.c_str(), O_WRONLY), -EACCES);
  const int config_fd = client->Open(config.c_str(), O_RDONLY);
  if (expect) { CHECK_GE(config_fd, 0); CHECK_EQ(close(config_fd), 0); }
  else { CHECK_EQ(config_fd, -EACCES); }
  const int node_fd = client->Open(node.c_str(), O_RDWR);
  if (expect) {
    CHECK_GE(node_fd, 0); struct stat opened; CHECK_EQ(fstat(node_fd, &opened), 0);
    CHECK_EQ(opened.st_rdev, info.st_rdev); CHECK_EQ(close(node_fd), 0);
  } else { CHECK_EQ(node_fd, -EACCES); }
  CHECK_EQ(CountThreads(), 1u);
  printf("PASS actual broker host+client positive/negative externalGlx=%d platform=%s early=%d no GL/GPU\n",
         expect, ui::OzonePlatform::GetPlatformName().c_str(), early);
  return 0;
}
