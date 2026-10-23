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
    auto lost = reinterpret_cast<WGPUProcDeviceGetLostFuture>(wgpuGetProcAddress({"wgpuDeviceGetLostFuture", WGPU_STRLEN}));
    auto compilation = reinterpret_cast<WGPUProcShaderModuleGetCompilationInfo>(wgpuGetProcAddress({"wgpuShaderModuleGetCompilationInfo", WGPU_STRLEN}));
    auto pipeline = reinterpret_cast<WGPUProcDeviceCreateComputePipelineAsync>(wgpuGetProcAddress({"wgpuDeviceCreateComputePipelineAsync", WGPU_STRLEN}));
    auto label = reinterpret_cast<WGPUProcBufferSetLabel>(wgpuGetProcAddress({"wgpuBufferSetLabel", WGPU_STRLEN}));
    assert(lost && compilation && pipeline && label);
    assert(!lost(nullptr).id);
    assert(!compilation(nullptr, {}).id);
    assert(!pipeline(nullptr, nullptr, {}).id);
    label(nullptr, {"not a live object", WGPU_STRLEN});
    auto query_count = reinterpret_cast<WGPUProcQuerySetGetCount>(wgpuGetProcAddress({"wgpuQuerySetGetCount", WGPU_STRLEN}));
    auto query_release = reinterpret_cast<WGPUProcQuerySetRelease>(wgpuGetProcAddress({"wgpuQuerySetRelease", WGPU_STRLEN}));
    assert(query_count && query_release && query_count(nullptr) == 0);
    query_release(nullptr);
    release(instance);
    std::puts("PASS C++ pinned typed procedure calls and unsupported-name rejection");
}
