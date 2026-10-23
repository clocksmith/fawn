/* SPDX-License-Identifier: MIT */
#include "webgpu.h"
#include <cassert>
#include <cstdio>
int main()
{
    WGPUStringView name = {"wgpuCreateInstance", WGPU_STRLEN};
    auto create = reinterpret_cast<WGPUProcCreateInstance>(wgpuGetProcAddress(name));
    assert(create);
    WGPUInstance instance = create(nullptr);
    assert(instance);
    auto wait = reinterpret_cast<WGPUProcInstanceWaitAny>(wgpuGetProcAddress({"wgpuInstanceWaitAny", 19}));
    auto release = reinterpret_cast<WGPUProcInstanceRelease>(wgpuGetProcAddress({"wgpuInstanceRelease", WGPU_STRLEN}));
    assert(wait && release);
    assert(wait(instance, 0, nullptr, 0) == WGPUWaitStatus_Success);
    assert(!wgpuGetProcAddress({"wgpuDeviceCreateRenderPipeline", WGPU_STRLEN}));
    assert(!wgpuGetProcAddress({"wgpuCreateInstance-extra", WGPU_STRLEN}));
    release(instance);
    std::puts("PASS C++ pinned typed procedure calls and unsupported-name rejection");
}
