/* SPDX-License-Identifier: MIT
 * Public WebGPU diagnostics from full/lifecycle/wgpu_async_diagnostics_command.zig.
 * Explicit fallback policy records its retained compute/render/bundle operations;
 * it never claims an unavailable extension, submission or hardware execution. */
#include "runtime_internal.h"
#include <stdio.h>
#include <string.h>

static const char DIAGNOSTIC_SHADER[] =
    "@vertex fn vs_main(@builtin(vertex_index) idx:u32)->@builtin(position) vec4f {"
    "var p=array<vec2f,3>(vec2f(0.0,0.5),vec2f(-0.5,-0.5),vec2f(0.5,-0.5));"
    "return vec4f(p[idx],0.0,1.0); }"
    "@fragment fn fs_main()->@location(0) vec4f { return vec4f(0.0,0.0,0.0,1.0); }";

const char *doe_runtime_diagnostic_source(bool compute)
{
    return compute ? "@compute @workgroup_size(1) fn main() {}" : DIAGNOSTIC_SHADER;
}

bool doe_runtime_used_diagnostic_source(const doe_runtime *runtime, bool compute)
{
    return compute ? runtime->diagnostic_compute_compiled : runtime->diagnostic_render_compiled;
}

static WGPUStringView string_view(const char *text)
{
    return (WGPUStringView){.data = text, .length = WGPU_STRLEN};
}

static void compilation_info(WGPUCompilationInfoRequestStatus status, WGPUCompilationInfo const *info,
    void *userdata1, void *userdata2)
{
    (void)userdata2;
    doe_runtime *runtime = userdata1;
    runtime->diagnostic_done = true;
    runtime->diagnostic_ok = status == WGPUCompilationInfoRequestStatus_Success && info;
    if (info) for (size_t i = 0; i < info->messageCount; i++)
        if (info->messages[i].type == WGPUCompilationMessageType_Error) runtime->diagnostic_ok = false;
}

static void pipeline_ready(WGPUCreatePipelineAsyncStatus status, WGPURenderPipeline pipeline, WGPUStringView message,
    void *userdata1, void *userdata2)
{
    (void)message;
    (void)userdata2;
    doe_runtime *runtime = userdata1;
    runtime->diagnostic_done = true;
    runtime->diagnostic_ok = status == WGPUCreatePipelineAsyncStatus_Success && pipeline;
    if (runtime->diagnostic_abandoned) {
        if (pipeline) wgpuRenderPipelineRelease(pipeline);
    } else {
        runtime->diagnostic_pipeline = pipeline;
    }
}

void runtime_release_diagnostics(doe_runtime *runtime)
{
    runtime->diagnostic_abandoned = true;
    if (runtime->diagnostic_pipeline) wgpuRenderPipelineRelease(runtime->diagnostic_pipeline);
    runtime->diagnostic_pipeline = NULL;
}

WGPURenderPipeline runtime_diagnostic_pipeline(doe_runtime *runtime, doe_texture_format format, doe_trace_execution *execution)
{
    WGPUShaderSourceWGSL source = WGPU_SHADER_SOURCE_WGSL_INIT;
    source.code = string_view(DIAGNOSTIC_SHADER);
    WGPUShaderModuleDescriptor module_desc = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;
    module_desc.nextInChain = &source.chain;
    WGPUShaderModule shader = wgpuDeviceCreateShaderModule(runtime->device, &module_desc);
    runtime->diagnostic_render_compiled = true;
    if (!shader) { runtime_fail(execution, "diagnostic_shader_failed", "diagnostic shader creation failed"); return NULL; }
    WGPUColorTargetState color = WGPU_COLOR_TARGET_STATE_INIT;
    color.format = runtime_webgpu_format(format);
    WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
    fragment.module = shader;
    fragment.entryPoint = string_view("fs_main");
    fragment.targetCount = 1;
    fragment.targets = &color;
    WGPURenderPipelineDescriptor descriptor = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    descriptor.label = string_view("doe.async_diagnostics");
    descriptor.vertex.module = shader;
    descriptor.vertex.entryPoint = string_view("vs_main");
    descriptor.fragment = &fragment;
    WGPURenderPipeline pipeline = runtime_async_render_pipeline(runtime, shader, &descriptor, execution);
    wgpuShaderModuleRelease(shader);
    return pipeline;
}

WGPURenderPipeline runtime_async_render_pipeline(doe_runtime *runtime, WGPUShaderModule shader,
    const WGPURenderPipelineDescriptor *descriptor, doe_trace_execution *execution)
{
    runtime->diagnostic_done = false;
    runtime->diagnostic_ok = false;
    WGPUCompilationInfoCallbackInfo compilation = {.mode = WGPUCallbackMode_WaitAnyOnly, .callback = compilation_info,
        .userdata1 = runtime};
    bool ok = runtime_wait_future(runtime, wgpuShaderModuleGetCompilationInfo(shader, compilation)) &&
        runtime->diagnostic_done && runtime->diagnostic_ok;
    if (!ok) {
        runtime_fail(execution, "diagnostic_compilation_failed", "diagnostic compilation information did not report success");
        return NULL;
    }
    runtime->diagnostic_done = false;
    runtime->diagnostic_ok = false;
    runtime->diagnostic_abandoned = false;
    WGPUCreateRenderPipelineAsyncCallbackInfo callback = {.mode = WGPUCallbackMode_WaitAnyOnly, .callback = pipeline_ready,
        .userdata1 = runtime};
    WGPUFuture future = wgpuDeviceCreateRenderPipelineAsync(runtime->device, descriptor, callback);
    ok = runtime_wait_future(runtime, future) && runtime->diagnostic_done && runtime->diagnostic_ok;
    if (!ok) {
        runtime_release_diagnostics(runtime);
        runtime_fail(execution, "diagnostic_pipeline_failed", "asynchronous diagnostic render pipeline did not succeed");
        return NULL;
    }
    WGPURenderPipeline pipeline = runtime->diagnostic_pipeline;
    runtime->diagnostic_pipeline = NULL;
    return pipeline;
}

WGPUTextureView runtime_diagnostic_view(doe_runtime *runtime, doe_texture_format format, doe_trace_execution *execution)
{
    /* Same reserved diagnostic handle, 4x4 extent and usage as retained WebGPU. */
    doe_command create = {.kind = DOE_COMMAND_TEXTURE_WRITE, .label = "texture_write"};
    create.as.texture_write.texture = (doe_texture_desc){.handle = UINT64_C(0x8c9f2c0000000000),
        .width = 4, .height = 4, .depth = 1, .format = format,
        .usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopyDst | WGPUTextureUsage_CopySrc | WGPUTextureUsage_TextureBinding};
    doe_trace_execution local = {.ok = true};
    if (!runtime_execute_texture(runtime, &create, &local)) { *execution = local; return NULL; }
    texture_entry *texture = runtime_find_texture(runtime, create.as.texture_write.texture.handle);
    WGPUTextureViewDescriptor descriptor = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    descriptor.format = runtime_webgpu_format(format);
    descriptor.dimension = WGPUTextureViewDimension_2D;
    descriptor.mipLevelCount = 1;
    descriptor.arrayLayerCount = 1;
    descriptor.usage = WGPUTextureUsage_RenderAttachment;
    WGPUTextureView view = texture ? wgpuTextureCreateView(texture->texture, &descriptor) : NULL;
    if (!view) runtime_fail(execution, "diagnostic_view_failed", "diagnostic render target view creation failed");
    return view;
}

static bool capabilities(doe_runtime *runtime, doe_trace_execution *execution)
{
    WGPUSupportedInstanceFeatures instance_features = WGPU_SUPPORTED_INSTANCE_FEATURES_INIT;
    wgpuGetInstanceFeatures(&instance_features);
    for (size_t i = 0; i < instance_features.featureCount; i++)
        if (!wgpuHasInstanceFeature(instance_features.features[i])) {
            wgpuSupportedInstanceFeaturesFreeMembers(instance_features);
            return runtime_fail(execution, "diagnostic_capabilities", "instance feature enumeration and membership disagree");
        }
    wgpuSupportedInstanceFeaturesFreeMembers(instance_features);
    WGPUInstanceLimits instance_limits = WGPU_INSTANCE_LIMITS_INIT;
    if (wgpuGetInstanceLimits(&instance_limits) != WGPUStatus_Success)
        return runtime_fail(execution, "diagnostic_capabilities", "instance limits query failed");
    if (!wgpuGetProcAddress(string_view("wgpuAdapterGetInfo")))
        return runtime_fail(execution, "diagnostic_capabilities", "wgpuAdapterGetInfo procedure lookup failed");
    WGPUSupportedWGSLLanguageFeatures language = WGPU_SUPPORTED_WGSL_LANGUAGE_FEATURES_INIT;
    wgpuInstanceGetWGSLLanguageFeatures(runtime->instance, &language);
    for (size_t i = 0; i < language.featureCount; i++)
        if (!wgpuInstanceHasWGSLLanguageFeature(runtime->instance, language.features[i])) {
            wgpuSupportedWGSLLanguageFeaturesFreeMembers(language);
            return runtime_fail(execution, "diagnostic_capabilities", "WGSL feature enumeration and membership disagree");
        }
    wgpuSupportedWGSLLanguageFeaturesFreeMembers(language);
    WGPUSupportedFeatures features = WGPU_SUPPORTED_FEATURES_INIT;
    wgpuAdapterGetFeatures(runtime->adapter, &features);
    bool consistent = true;
    for (size_t i = 0; i < features.featureCount; i++) consistent &= wgpuAdapterHasFeature(runtime->adapter, features.features[i]) != 0;
    wgpuSupportedFeaturesFreeMembers(features);
    features = WGPU_SUPPORTED_FEATURES_INIT;
    wgpuDeviceGetFeatures(runtime->device, &features);
    for (size_t i = 0; i < features.featureCount; i++) consistent &= wgpuDeviceHasFeature(runtime->device, features.features[i]) != 0;
    wgpuSupportedFeaturesFreeMembers(features);
    WGPUAdapterInfo adapter_info = WGPU_ADAPTER_INFO_INIT, device_info = WGPU_ADAPTER_INFO_INIT;
    WGPUStatus adapter_status = wgpuAdapterGetInfo(runtime->adapter, &adapter_info);
    WGPUStatus device_status = wgpuDeviceGetAdapterInfo(runtime->device, &device_info);
    consistent &= adapter_status == WGPUStatus_Success && device_status == WGPUStatus_Success;
    if (consistent) consistent = adapter_info.vendorID == device_info.vendorID && adapter_info.deviceID == device_info.deviceID;
    wgpuAdapterInfoFreeMembers(adapter_info);
    wgpuAdapterInfoFreeMembers(device_info);
    WGPULimits limits = WGPU_LIMITS_INIT;
    consistent &= wgpuAdapterGetLimits(runtime->adapter, &limits) == WGPUStatus_Success;
    limits = WGPU_LIMITS_INIT;
    consistent &= wgpuDeviceGetLimits(runtime->device, &limits) == WGPUStatus_Success;
    return consistent || runtime_fail(execution, "diagnostic_capabilities", "adapter/device capability query failed or disagreed");
}

static bool record_fallback(doe_runtime *runtime, doe_texture_format format, bool pixel_local, doe_trace_execution *execution)
{
    WGPURenderPipeline pipeline = pixel_local ? runtime_diagnostic_pipeline(runtime, format, execution) : NULL;
    if (pixel_local && !pipeline) return false;
    WGPUTextureView view = runtime_diagnostic_view(runtime, format, execution);
    WGPUCommandEncoder encoder = view ? wgpuDeviceCreateCommandEncoder(runtime->device, NULL) : NULL;
    bool ok = encoder != NULL;
    if (!ok && execution->ok) runtime_fail(execution, "diagnostic_encoder_failed", "diagnostic command encoder creation failed");
    if (ok && !pixel_local) {
        WGPUComputePassEncoder compute = wgpuCommandEncoderBeginComputePass(encoder, NULL);
        if (!compute) ok = runtime_fail(execution, "diagnostic_compute_pass_failed", "diagnostic compute pass creation failed");
        else { wgpuComputePassEncoderEnd(compute); wgpuComputePassEncoderRelease(compute); }
    }
    if (ok) {
        WGPURenderPassColorAttachment color = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
        color.view = view;
        color.loadOp = WGPULoadOp_Clear;
        color.storeOp = WGPUStoreOp_Store;
        color.clearValue.a = 1.0;
        WGPURenderPassDescriptor descriptor = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
        descriptor.colorAttachmentCount = 1;
        descriptor.colorAttachments = &color;
        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &descriptor);
        if (!pass) ok = runtime_fail(execution, "diagnostic_render_pass_failed", "diagnostic render pass creation failed");
        else {
            if (pipeline) {
                wgpuRenderPassEncoderSetPipeline(pass, pipeline);
                wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
                wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
            }
            wgpuRenderPassEncoderEnd(pass);
            wgpuRenderPassEncoderRelease(pass);
        }
    }
    if (ok && !pixel_local) {
        WGPUTextureFormat color_format = runtime_webgpu_format(format);
        WGPURenderBundleEncoderDescriptor descriptor = WGPU_RENDER_BUNDLE_ENCODER_DESCRIPTOR_INIT;
        descriptor.colorFormatCount = 1;
        descriptor.colorFormats = &color_format;
        WGPURenderBundleEncoder bundle_encoder = wgpuDeviceCreateRenderBundleEncoder(runtime->device, &descriptor);
        WGPURenderBundle bundle = bundle_encoder ? wgpuRenderBundleEncoderFinish(bundle_encoder, NULL) : NULL;
        if (!bundle) ok = runtime_fail(execution, "diagnostic_bundle_failed", "diagnostic render bundle recording failed");
        if (bundle) wgpuRenderBundleRelease(bundle);
        if (bundle_encoder) wgpuRenderBundleEncoderRelease(bundle_encoder);
    }
    if (ok) {
        WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, NULL);
        if (!commands) ok = runtime_fail(execution, "diagnostic_finish_failed", "diagnostic command buffer recording failed");
        else wgpuCommandBufferRelease(commands);
    }
    /* Retained WebGPU diagnostics record these commands without submitting. */
    if (encoder) wgpuCommandEncoderRelease(encoder);
    if (view) wgpuTextureViewRelease(view);
    if (pipeline) wgpuRenderPipelineRelease(pipeline);
    return ok;
}

static bool diagnostic_once(doe_runtime *runtime, const doe_diagnostics *diagnostic, doe_trace_execution *execution)
{
    switch (diagnostic->mode) {
    case DOE_DIAG_CAPABILITIES: return capabilities(runtime, execution);
    case DOE_DIAG_LIFECYCLE: return runtime_diagnostic_lifecycle(runtime, diagnostic->format, execution);
    case DOE_DIAG_PIPELINE: {
        WGPURenderPipeline pipeline = runtime_diagnostic_pipeline(runtime, diagnostic->format, execution);
        if (!pipeline) return false;
        wgpuRenderPipelineRelease(pipeline);
        return true;
    }
    case DOE_DIAG_RESOURCE_TABLE:
    case DOE_DIAG_PIXEL_LOCAL:
        if (!diagnostic->emulate)
            return runtime_fail(execution, "diagnostic_extension_unavailable",
                "%s requires an unavailable nonstandard WebGPU extension under strict featurePolicy",
                diagnostic->mode == DOE_DIAG_RESOURCE_TABLE ? "resource-table/immediates" : "pixel-local-storage");
        return record_fallback(runtime, diagnostic->format, diagnostic->mode == DOE_DIAG_PIXEL_LOCAL, execution);
    case DOE_DIAG_FULL: {
        const doe_diagnostic_mode modes[] = {DOE_DIAG_PIPELINE, DOE_DIAG_CAPABILITIES, DOE_DIAG_RESOURCE_TABLE,
            DOE_DIAG_LIFECYCLE, DOE_DIAG_PIXEL_LOCAL};
        for (size_t i = 0; i < sizeof(modes) / sizeof(modes[0]); i++) {
            doe_diagnostics part = *diagnostic;
            part.mode = modes[i];
            if (!diagnostic_once(runtime, &part, execution)) return false;
        }
        return true;
    }
    }
    return false;
}

bool runtime_execute_diagnostics(doe_runtime *runtime, const doe_command *command, doe_trace_execution *execution)
{
    uint64_t start = doe_runtime_monotonic_ns();
    runtime_push_scopes(runtime);
    bool ok = true;
    for (uint32_t i = 0; ok && i < command->as.diagnostics.iterations; i++)
        ok = diagnostic_once(runtime, &command->as.diagnostics, execution);
    if (!runtime_pop_scopes(runtime, execution)) ok = false;
    execution->setup_ns = doe_runtime_monotonic_ns() - start;
    if (ok) snprintf(execution->message, sizeof(execution->message),
        "async_diagnostics public_webgpu mode=%u featurePolicy=%s iterations=%u recorded_without_submission timing=cpu_wall_clock comparability=directional",
        (unsigned)command->as.diagnostics.mode, command->as.diagnostics.emulate ? "emulate-when-unavailable" : "strict",
        command->as.diagnostics.iterations);
    return ok;
}
