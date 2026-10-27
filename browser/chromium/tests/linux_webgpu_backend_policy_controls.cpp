// Always-on default-selection controls. No adapter, device or GPU work.
#include "gpu/command_buffer/service/webgpu_linux_backend_policy.h"

#include <cstdio>

int main() {
  using gpu::webgpu::LinuxWebGPURuntimeRoute;
  using gpu::webgpu::SelectsLinuxDefaultVulkan;
  unsigned cases = 0;
  for (unsigned contexts = 0; contexts < 8; ++contexts) {
    const bool compositor = (contexts & 1) != 0;
    const bool interop = (contexts & 2) != 0;
    const bool graphite = (contexts & 4) != 0;
    const bool native = SelectsLinuxDefaultVulkan(
        LinuxWebGPURuntimeRoute::kNativeDawn, compositor, interop, graphite);
    const bool external = SelectsLinuxDefaultVulkan(
        LinuxWebGPURuntimeRoute::kForcedExternalDoe, compositor, interop, graphite);
    if (native != (contexts != 0) || !external) {
      std::fprintf(stderr, "FAIL contexts=%u native=%d external=%d\n",
                   contexts, native, external);
      return 1;
    }
    std::printf("PASS contexts=%u nativeVulkan=%d externalVulkan=%d\n",
                contexts, native, external);
    cases += 2;
  }
  std::printf("PASS %u default backend policy cases; no adapter/device/GPU\n", cases);
  return 0;
}
