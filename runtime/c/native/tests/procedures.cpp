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
    assert(!wgpuGetProcAddress({"wgpuDeviceCreateRenderPipeline-extra", WGPU_STRLEN}));
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
    auto render = reinterpret_cast<WGPUProcDeviceCreateRenderPipeline>(wgpuGetProcAddress({"wgpuDeviceCreateRenderPipeline", WGPU_STRLEN}));
    auto async_render = reinterpret_cast<WGPUProcDeviceCreateRenderPipelineAsync>(wgpuGetProcAddress({"wgpuDeviceCreateRenderPipelineAsync", WGPU_STRLEN}));
    auto begin = reinterpret_cast<WGPUProcCommandEncoderBeginRenderPass>(wgpuGetProcAddress({"wgpuCommandEncoderBeginRenderPass", WGPU_STRLEN}));
    auto bundle = reinterpret_cast<WGPUProcRenderBundleEncoderFinish>(wgpuGetProcAddress({"wgpuRenderBundleEncoderFinish", WGPU_STRLEN}));
    assert(render && async_render && begin && bundle);
    assert(!render(nullptr, nullptr) && !async_render(nullptr, nullptr, {}).id);
    assert(!begin(nullptr, nullptr) && !bundle(nullptr, nullptr));
    auto instance_features = reinterpret_cast<WGPUProcGetInstanceFeatures>(wgpuGetProcAddress({"wgpuGetInstanceFeatures", WGPU_STRLEN}));
    auto instance_limits = reinterpret_cast<WGPUProcGetInstanceLimits>(wgpuGetProcAddress({"wgpuGetInstanceLimits", WGPU_STRLEN}));
    auto has_instance_feature = reinterpret_cast<WGPUProcHasInstanceFeature>(wgpuGetProcAddress({"wgpuHasInstanceFeature", WGPU_STRLEN}));
    auto free_instance_features = reinterpret_cast<WGPUProcSupportedInstanceFeaturesFreeMembers>(wgpuGetProcAddress({"wgpuSupportedInstanceFeaturesFreeMembers", WGPU_STRLEN}));
    assert(instance_features && instance_limits && has_instance_feature && free_instance_features);
    WGPUSupportedInstanceFeatures features = WGPU_SUPPORTED_INSTANCE_FEATURES_INIT;
    instance_features(&features);
    assert(!features.featureCount && !features.features);
    free_instance_features(features);
    WGPUInstanceLimits limits = WGPU_INSTANCE_LIMITS_INIT;
    assert(instance_limits(&limits) == WGPUStatus_Success && !limits.timedWaitAnyMaxCount);
    assert(!has_instance_feature(WGPUInstanceFeatureName_TimedWaitAny));
    assert(!has_instance_feature(WGPUInstanceFeatureName_ShaderSourceSPIRV));
    assert(instance_limits(nullptr) == WGPUStatus_Error);
    WGPUChainedStruct chain = {};
    limits.nextInChain = &chain;
    limits.timedWaitAnyMaxCount = 17;
    assert(instance_limits(&limits) == WGPUStatus_Error && limits.timedWaitAnyMaxCount == 17);
    auto push = reinterpret_cast<WGPUProcCommandEncoderPushDebugGroup>(wgpuGetProcAddress({"wgpuCommandEncoderPushDebugGroup", WGPU_STRLEN}));
    auto pop = reinterpret_cast<WGPUProcRenderPassEncoderPopDebugGroup>(wgpuGetProcAddress({"wgpuRenderPassEncoderPopDebugGroup", WGPU_STRLEN}));
    assert(push && pop);
    push(nullptr, {"typed marker", WGPU_STRLEN});
    pop(nullptr);
    release(instance);
    std::puts("PASS C++ pinned typed procedure calls and unsupported-name rejection");
}
