/* SPDX-License-Identifier: MIT
 * WebGPU invalid objects after device.destroy() on the selected backend: every
 * creation returns an object, getters report its descriptor, every operation
 * using it fails silently (no error after loss), every callback is delivered
 * exactly once with a non-success or invalid-object result, and releasing
 * everything frees it (ASan in check-sanitizers). Objects created while the
 * device was alive are used afterwards too. */
#include "consumer.h"

static unsigned errors, losses, callbacks;
static void on_error(WGPUDevice const *device, WGPUErrorType type, WGPUStringView message, void *userdata, void *unused)
{
    (void)device; (void)type; (void)userdata; (void)unused;
    ++errors;
    fprintf(stderr, "unexpected error after loss: %.*s\n", (int)message.length, message.data);
}
static void on_lost(WGPUDevice const *device, WGPUDeviceLostReason reason, WGPUStringView message, void *userdata, void *unused)
{
    (void)device; (void)message; (void)userdata; (void)unused;
    assert(reason == WGPUDeviceLostReason_Destroyed);
    ++losses;
}
static void on_adapter(WGPURequestAdapterStatus status, WGPUAdapter adapter, WGPUStringView message, void *userdata, void *unused)
{
    (void)message; (void)unused;
    assert(status == WGPURequestAdapterStatus_Success);
    *(WGPUAdapter *)userdata = adapter;
}
static void on_device(WGPURequestDeviceStatus status, WGPUDevice device, WGPUStringView message, void *userdata, void *unused)
{
    (void)message; (void)unused;
    assert(status == WGPURequestDeviceStatus_Success);
    *(WGPUDevice *)userdata = device;
}
static void on_map(WGPUMapAsyncStatus status, WGPUStringView message, void *userdata, void *unused)
{
    (void)message; (void)userdata; (void)unused;
    assert(status != WGPUMapAsyncStatus_Success);
    ++callbacks;
}
static void on_done(WGPUQueueWorkDoneStatus status, WGPUStringView message, void *userdata, void *unused)
{
    (void)status; (void)message; (void)userdata; (void)unused;
    ++callbacks;
}
static void on_compute(WGPUCreatePipelineAsyncStatus status, WGPUComputePipeline pipeline, WGPUStringView message, void *userdata, void *unused)
{
    (void)message; (void)userdata; (void)unused;
    assert(status == WGPUCreatePipelineAsyncStatus_Success && pipeline);
    wgpuComputePipelineRelease(pipeline);
    ++callbacks;
}
static void on_render(WGPUCreatePipelineAsyncStatus status, WGPURenderPipeline pipeline, WGPUStringView message, void *userdata, void *unused)
{
    (void)message; (void)userdata; (void)unused;
    assert(status == WGPUCreatePipelineAsyncStatus_Success && pipeline);
    wgpuRenderPipelineRelease(pipeline);
    ++callbacks;
}
static void on_compilation(WGPUCompilationInfoRequestStatus status, const WGPUCompilationInfo *info, void *userdata, void *unused)
{
    (void)status; (void)info; (void)userdata; (void)unused;
    ++callbacks;
}
static void on_scope(WGPUPopErrorScopeStatus status, WGPUErrorType type, WGPUStringView message, void *userdata, void *unused)
{
    (void)status; (void)message; (void)userdata; (void)unused;
    assert(type == WGPUErrorType_NoError);
    ++callbacks;
}

static const char *const compute_code =
    "@group(0) @binding(0) var<storage, read_write> d: array<u32>;\n"
    "@compute @workgroup_size(1) fn main() { d[0] = 1u; }\n";
static const char *const render_code =
    "@vertex fn vs() -> @builtin(position) vec4f { return vec4f(0.0, 0.0, 0.0, 1.0); }\n"
    "@fragment fn fs() -> @location(0) vec4f { return vec4f(1.0); }\n";

static WGPUShaderModule shader(WGPUDevice device, const char *code)
{
    WGPUShaderSourceWGSL wgsl = WGPU_SHADER_SOURCE_WGSL_INIT;
    wgsl.code = (WGPUStringView){code, WGPU_STRLEN};
    WGPUShaderModuleDescriptor descriptor = {.nextInChain = &wgsl.chain};
    WGPUShaderModule module = wgpuDeviceCreateShaderModule(device, &descriptor);
    assert(module);
    return module;
}

typedef struct objects {
    WGPUBuffer buffer, mappable;
    WGPUTexture texture;
    WGPUTextureView view;
    WGPUSampler sampler;
    WGPUShaderModule compute_shader, render_shader;
    WGPUComputePipeline compute;
    WGPURenderPipeline render;
    WGPUBindGroupLayout layout;
    WGPUPipelineLayout pipeline_layout;
    WGPUBindGroup group;
    WGPUQuerySet queries;
} objects;

static void create(WGPUDevice device, objects *o, bool lost)
{
    WGPUBufferDescriptor storage = {.size = 256, .usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc |
        WGPUBufferUsage_CopyDst | WGPUBufferUsage_Indirect | WGPUBufferUsage_QueryResolve};
    o->buffer = wgpuDeviceCreateBuffer(device, &storage);
    WGPUBufferDescriptor mappable = {.size = 64, .usage = WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst};
    o->mappable = wgpuDeviceCreateBuffer(device, &mappable);
    WGPUTextureDescriptor texture = WGPU_TEXTURE_DESCRIPTOR_INIT;
    texture.size = (WGPUExtent3D){8, 4, 1};
    texture.format = WGPUTextureFormat_RGBA8Unorm;
    texture.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc | WGPUTextureUsage_CopyDst |
        WGPUTextureUsage_TextureBinding;
    o->texture = wgpuDeviceCreateTexture(device, &texture);
    o->view = wgpuTextureCreateView(o->texture, NULL);
    o->sampler = wgpuDeviceCreateSampler(device, NULL);
    o->compute_shader = shader(device, compute_code);
    o->render_shader = shader(device, render_code);
    WGPUComputePipelineDescriptor compute = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
    compute.compute.module = o->compute_shader;
    o->compute = wgpuDeviceCreateComputePipeline(device, &compute);
    WGPUColorTargetState target = WGPU_COLOR_TARGET_STATE_INIT;
    target.format = WGPUTextureFormat_RGBA8Unorm;
    WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
    fragment.module = o->render_shader;
    fragment.targetCount = 1;
    fragment.targets = &target;
    WGPURenderPipelineDescriptor render = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    render.vertex.module = o->render_shader;
    render.fragment = &fragment;
    o->render = wgpuDeviceCreateRenderPipeline(device, &render);
    o->layout = o->compute ? wgpuComputePipelineGetBindGroupLayout(o->compute, 0) : NULL;
    WGPUPipelineLayoutDescriptor pipeline_layout = {.bindGroupLayoutCount = 1, .bindGroupLayouts = &o->layout};
    o->pipeline_layout = wgpuDeviceCreatePipelineLayout(device, &pipeline_layout);
    WGPUBindGroupEntry entry = {.binding = 0, .buffer = o->buffer, .size = 256};
    WGPUBindGroupDescriptor group = {.layout = o->layout, .entryCount = 1, .entries = &entry};
    o->group = wgpuDeviceCreateBindGroup(device, &group);
    WGPUQuerySetDescriptor queries = {.type = WGPUQueryType_Occlusion, .count = 4};
    if (lost || adapter_class_backend() == WGPUBackendType_Vulkan)
        o->queries = wgpuDeviceCreateQuerySet(device, &queries);
    assert(o->buffer && o->mappable && o->texture && o->view && o->sampler && o->compute && o->render &&
        o->layout && o->pipeline_layout && o->group);
    assert(!lost || o->queries);
}

/* Every recording, submission and queue operation over the objects. */
static void use(WGPUInstance instance, WGPUDevice device, WGPUQueue queue, objects *o)
{
    assert(wgpuBufferGetSize(o->buffer) == 256 && wgpuBufferGetSize(o->mappable) == 64);
    assert(wgpuBufferGetUsage(o->mappable) == (WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst));
    assert(wgpuBufferGetMapState(o->mappable) == WGPUBufferMapState_Unmapped);
    assert(wgpuTextureGetWidth(o->texture) == 8 && wgpuTextureGetHeight(o->texture) == 4);
    assert(wgpuTextureGetFormat(o->texture) == WGPUTextureFormat_RGBA8Unorm);
    if (o->queries) assert(wgpuQuerySetGetCount(o->queries) == 4 && wgpuQuerySetGetType(o->queries) == WGPUQueryType_Occlusion);
    WGPUTextureView extra = wgpuTextureCreateView(o->texture, NULL);
    assert(extra);
    WGPUBindGroupLayout layout = wgpuRenderPipelineGetBindGroupLayout(o->render, 0);
    assert(layout);
    wgpuBindGroupLayoutRelease(layout);
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, NULL);
    assert(encoder);
    WGPUComputePassEncoder compute = wgpuCommandEncoderBeginComputePass(encoder, NULL);
    assert(compute);
    wgpuComputePassEncoderSetPipeline(compute, o->compute);
    wgpuComputePassEncoderSetBindGroup(compute, 0, o->group, 0, NULL);
    wgpuComputePassEncoderDispatchWorkgroups(compute, 1, 1, 1);
    wgpuComputePassEncoderDispatchWorkgroupsIndirect(compute, o->buffer, 0);
    wgpuComputePassEncoderEnd(compute);
    wgpuComputePassEncoderRelease(compute);
    WGPURenderPassColorAttachment color = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    color.view = extra;
    color.loadOp = WGPULoadOp_Clear;
    color.storeOp = WGPUStoreOp_Store;
    WGPURenderPassDescriptor pass_desc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
    pass_desc.colorAttachmentCount = 1;
    pass_desc.colorAttachments = &color;
    pass_desc.occlusionQuerySet = o->queries;
    WGPURenderPassEncoder render = wgpuCommandEncoderBeginRenderPass(encoder, &pass_desc);
    assert(render);
    wgpuRenderPassEncoderSetPipeline(render, o->render);
    wgpuRenderPassEncoderBeginOcclusionQuery(render, 0);
    wgpuRenderPassEncoderDraw(render, 3, 1, 0, 0);
    wgpuRenderPassEncoderEndOcclusionQuery(render);
    wgpuRenderPassEncoderDrawIndirect(render, o->buffer, 0);
    wgpuRenderPassEncoderEnd(render);
    wgpuRenderPassEncoderRelease(render);
    wgpuCommandEncoderCopyBufferToBuffer(encoder, o->buffer, 0, o->mappable, 0, 64);
    wgpuCommandEncoderClearBuffer(encoder, o->buffer, 0, 64);
    WGPUTexelCopyTextureInfo image = {.texture = o->texture};
    WGPUTexelCopyBufferInfo bytes = {.buffer = o->buffer, .layout = {.bytesPerRow = 256, .rowsPerImage = 1}};
    WGPUExtent3D extent = {8, 1, 1};
    wgpuCommandEncoderCopyTextureToBuffer(encoder, &image, &bytes, &extent);
    wgpuCommandEncoderCopyBufferToTexture(encoder, &bytes, &image, &extent);
    wgpuCommandEncoderResolveQuerySet(encoder, o->queries, 0, 1, o->buffer, 0);
    WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, NULL);
    wgpuCommandEncoderRelease(encoder);
    if (commands) {
        wgpuQueueSubmit(queue, 1, &commands);
        wgpuCommandBufferRelease(commands);
    }
    const uint32_t words[4] = {1, 2, 3, 4};
    wgpuQueueWriteBuffer(queue, o->buffer, 0, words, sizeof(words));
    WGPUTexelCopyBufferLayout layout_bytes = {.bytesPerRow = 256, .rowsPerImage = 1};
    WGPUExtent3D one = {1, 1, 1};
    wgpuQueueWriteTexture(queue, &image, words, sizeof(words), &layout_bytes, &one);
    WGPUBufferMapCallbackInfo map = {.mode = WGPUCallbackMode_AllowSpontaneous, .callback = on_map};
    const unsigned before = callbacks;
    wgpuBufferMapAsync(o->mappable, WGPUMapMode_Read, 0, 64, map);
    assert(!wgpuBufferGetConstMappedRange(o->mappable, 0, 64));
    wgpuBufferUnmap(o->mappable);
    WGPUQueueWorkDoneCallbackInfo done = {.mode = WGPUCallbackMode_AllowSpontaneous, .callback = on_done};
    wgpuQueueOnSubmittedWorkDone(queue, done);
    WGPUComputePipelineDescriptor compute_desc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
    compute_desc.compute.module = o->compute_shader;
    WGPUCreateComputePipelineAsyncCallbackInfo compute_async = {.mode = WGPUCallbackMode_AllowSpontaneous, .callback = on_compute};
    wgpuDeviceCreateComputePipelineAsync(device, &compute_desc, compute_async);
    WGPUColorTargetState target = WGPU_COLOR_TARGET_STATE_INIT;
    target.format = WGPUTextureFormat_RGBA8Unorm;
    WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
    fragment.module = o->render_shader;
    fragment.targetCount = 1;
    fragment.targets = &target;
    WGPURenderPipelineDescriptor render_desc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    render_desc.vertex.module = o->render_shader;
    render_desc.fragment = &fragment;
    WGPUCreateRenderPipelineAsyncCallbackInfo render_async = {.mode = WGPUCallbackMode_AllowSpontaneous, .callback = on_render};
    wgpuDeviceCreateRenderPipelineAsync(device, &render_desc, render_async);
    WGPUCompilationInfoCallbackInfo compilation = {.mode = WGPUCallbackMode_AllowSpontaneous, .callback = on_compilation};
    wgpuShaderModuleGetCompilationInfo(o->compute_shader, compilation);
    wgpuDevicePushErrorScope(device, WGPUErrorFilter_Validation);
    WGPUBindGroupEntry bad = {.binding = 7, .buffer = o->buffer, .size = 4};
    WGPUBindGroupDescriptor bad_group = {.layout = o->layout, .entryCount = 1, .entries = &bad};
    WGPUBindGroup invalid = wgpuDeviceCreateBindGroup(device, &bad_group);
    if (invalid) wgpuBindGroupRelease(invalid);
    WGPUPopErrorScopeCallbackInfo scope = {.mode = WGPUCallbackMode_AllowSpontaneous, .callback = on_scope};
    wgpuDevicePopErrorScope(device, scope);
    wgpuInstanceProcessEvents(instance);
    assert(callbacks == before + 6);
    wgpuBufferDestroy(o->buffer);
    wgpuTextureDestroy(o->texture);
    wgpuQuerySetDestroy(o->queries);
    wgpuTextureViewRelease(extra);
}

static void release(objects *o)
{
    wgpuQuerySetRelease(o->queries);
    wgpuBindGroupRelease(o->group);
    wgpuPipelineLayoutRelease(o->pipeline_layout);
    wgpuBindGroupLayoutRelease(o->layout);
    wgpuRenderPipelineRelease(o->render);
    wgpuComputePipelineRelease(o->compute);
    wgpuShaderModuleRelease(o->render_shader);
    wgpuShaderModuleRelease(o->compute_shader);
    wgpuSamplerRelease(o->sampler);
    wgpuTextureViewRelease(o->view);
    wgpuTextureRelease(o->texture);
    wgpuBufferRelease(o->mappable);
    wgpuBufferRelease(o->buffer);
}

int main(void)
{
    WGPUInstance instance = wgpuCreateInstance(NULL);
    WGPUAdapter adapter = NULL;
    WGPUDevice device = NULL;
    WGPURequestAdapterOptions options = WGPU_REQUEST_ADAPTER_OPTIONS_INIT;
    adapter_class_options(&options);
    assert(wgpuInstanceRequestAdapter(instance, &options,
        (WGPURequestAdapterCallbackInfo){.mode = WGPUCallbackMode_AllowSpontaneous, .callback = on_adapter, .userdata1 = &adapter}).id);
    adapter_class_require(adapter);
    WGPUDeviceDescriptor descriptor = WGPU_DEVICE_DESCRIPTOR_INIT;
    descriptor.uncapturedErrorCallbackInfo = (WGPUUncapturedErrorCallbackInfo){.callback = on_error};
    descriptor.deviceLostCallbackInfo = (WGPUDeviceLostCallbackInfo){.mode = WGPUCallbackMode_AllowSpontaneous, .callback = on_lost};
    assert(wgpuAdapterRequestDevice(adapter, &descriptor,
        (WGPURequestDeviceCallbackInfo){.mode = WGPUCallbackMode_AllowSpontaneous, .callback = on_device, .userdata1 = &device}).id);
    WGPUQueue queue = wgpuDeviceGetQueue(device);
    objects before = {0}, after = {0};
    create(device, &before, false);
    wgpuDeviceDestroy(device);
    assert(losses == 1);
    create(device, &after, true); /* Every creation still returns an object. */
    use(instance, device, queue, &before);
    use(instance, device, queue, &after);
    assert(!errors && losses == 1);
    release(&before);
    release(&after);
    wgpuQueueRelease(queue);
    wgpuDeviceRelease(device);
    wgpuAdapterRelease(adapter);
    wgpuInstanceRelease(instance);
    printf("PASS lost objects: every creation after destroy() returns an invalid object with its descriptor; every use fails silently; callbacks once; clean release %s\n", adapter_class_scope());
    return 0;
}
