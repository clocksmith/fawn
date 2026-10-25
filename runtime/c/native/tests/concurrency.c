/* SPDX-License-Identifier: MIT
 * Concurrent public work on software Vulkan, meant to run under TSan and ASan
 * as well as plainly:
 * - several threads on one device create buffers, textures (zero-initialized
 *   through the batched init path), shaders and pipelines (shared and new),
 *   record, submit, map and release, some immediately after submit;
 * - threads on separate devices of one instance do the same;
 * - render rounds (render pipeline, occlusion query, direct and indirect
 *   draws, query resolve) and compute-program rounds run among them, and a
 *   compute dispatch is also issued indirectly;
 * - another thread polls instance events and queue work done meanwhile, and
 *   another keeps requesting, using and releasing adapters and devices;
 * - threads keep submitting and mapping while the device is destroyed from
 *   another thread: every map then settles (success with exact data, or an
 *   error/abort status), nothing hangs, and the loss callback runs once.
 * Every successful readback is checked against the host computation. */
#include "consumer.h"
#include "doe_native_programs.h"
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <sched.h>
#include <stdlib.h>

enum { WORDS = 64, BYTES = WORDS * 4, ROUNDS = 200, SHARED_THREADS = 4, DEVICE_THREADS = 2 };

static const char *const code =
    "@group(0) @binding(0) var<storage, read_write> data: array<u32>;\n"
    "@compute @workgroup_size(64) fn main(@builtin(global_invocation_id) id: vec3u) {\n"
    "  data[id.x] = data[id.x] * 3u + %uu;\n"
    "}\n";

typedef struct shared_device {
    WGPUInstance instance;
    WGPUAdapter adapter;
    WGPUDevice device;
    WGPUQueue queue;
    WGPUComputePipeline pipeline; /* Created once; offset 1. */
    atomic_uint errors;
} shared_device;

static void device_error(WGPUDevice const *device, WGPUErrorType type, WGPUStringView message, void *userdata, void *unused)
{
    (void)device; (void)type; (void)unused;
    atomic_fetch_add(&((shared_device *)userdata)->errors, 1);
    fprintf(stderr, "native error: %.*s\n", (int)message.length, message.data);
}
static void adapter_ready(WGPURequestAdapterStatus status, WGPUAdapter adapter, WGPUStringView message, void *userdata, void *unused)
{
    (void)message; (void)unused;
    assert(status == WGPURequestAdapterStatus_Success);
    *(WGPUAdapter *)userdata = adapter;
}
static void device_ready(WGPURequestDeviceStatus status, WGPUDevice device, WGPUStringView message, void *userdata, void *unused)
{
    (void)message; (void)unused;
    assert(status == WGPURequestDeviceStatus_Success);
    *(WGPUDevice *)userdata = device;
}

static WGPUComputePipeline make_pipeline(WGPUDevice device, unsigned offset)
{
    char source[512];
    snprintf(source, sizeof(source), code, offset);
    WGPUShaderSourceWGSL wgsl = WGPU_SHADER_SOURCE_WGSL_INIT;
    wgsl.code = (WGPUStringView){source, WGPU_STRLEN};
    WGPUShaderModuleDescriptor shader_desc = {.nextInChain = &wgsl.chain};
    WGPUShaderModule shader = wgpuDeviceCreateShaderModule(device, &shader_desc);
    assert(shader);
    WGPUComputePipelineDescriptor descriptor = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
    descriptor.compute.module = shader;
    WGPUComputePipeline pipeline = wgpuDeviceCreateComputePipeline(device, &descriptor);
    wgpuShaderModuleRelease(shader);
    assert(pipeline);
    return pipeline;
}

static void open_device(shared_device *s, WGPUInstance instance)
{
    s->instance = instance;
    WGPURequestAdapterOptions options = WGPU_REQUEST_ADAPTER_OPTIONS_INIT;
    adapter_class_options(&options);
    WGPURequestAdapterCallbackInfo adapter = {.mode = WGPUCallbackMode_AllowSpontaneous, .callback = adapter_ready, .userdata1 = &s->adapter};
    assert(wgpuInstanceRequestAdapter(instance, &options, adapter).id && s->adapter);
    WGPUDeviceDescriptor descriptor = WGPU_DEVICE_DESCRIPTOR_INIT;
    descriptor.uncapturedErrorCallbackInfo = (WGPUUncapturedErrorCallbackInfo){.callback = device_error, .userdata1 = s};
    WGPURequestDeviceCallbackInfo device = {.mode = WGPUCallbackMode_AllowSpontaneous, .callback = device_ready, .userdata1 = &s->device};
    assert(wgpuAdapterRequestDevice(s->adapter, &descriptor, device).id && s->device);
    s->queue = wgpuDeviceGetQueue(s->device);
    s->pipeline = make_pipeline(s->device, 1);
}

static void close_device(shared_device *s)
{
    wgpuComputePipelineRelease(s->pipeline);
    wgpuQueueRelease(s->queue);
    wgpuDeviceRelease(s->device);
    wgpuAdapterRelease(s->adapter);
    assert(!atomic_load(&s->errors));
}

static void mapped_reply(WGPUMapAsyncStatus status, WGPUStringView message, void *userdata, void *unused)
{
    (void)message; (void)unused;
    *(WGPUMapAsyncStatus *)userdata = status;
}
static void done_reply(WGPUQueueWorkDoneStatus status, WGPUStringView message, void *userdata, void *unused)
{
    (void)message; (void)unused;
    assert(status == WGPUQueueWorkDoneStatus_Success);
    atomic_fetch_add((atomic_uint *)userdata, 1);
}

typedef struct worker {
    shared_device *device;
    unsigned seed;
    unsigned rounds_checked;
} worker;

static WGPUBuffer make_buffer(WGPUDevice device, uint64_t size, WGPUBufferUsage usage)
{
    WGPUBufferDescriptor descriptor = {.size = size, .usage = usage};
    WGPUBuffer buffer = wgpuDeviceCreateBuffer(device, &descriptor);
    assert(buffer);
    return buffer;
}

/* One round: optional fresh pipeline, a zero-initialized texture read back
 * through the same submission, a dispatch and a copy; early release for odd
 * rounds; readback checked against the host. */
static void round_trip(worker *w, unsigned round)
{
    shared_device *s = w->device;
    const unsigned offset = round % 3 == 0 ? 1 + round % 5 : 1;
    WGPUComputePipeline pipeline = offset == 1 ? s->pipeline : make_pipeline(s->device, offset);
    if (offset == 1) wgpuComputePipelineAddRef(pipeline);
    uint32_t input[WORDS];
    for (unsigned i = 0; i < WORDS; ++i) input[i] = w->seed * 131u + round * 7u + i;
    WGPUBuffer storage = make_buffer(s->device, BYTES, WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst);
    WGPUBuffer readback = make_buffer(s->device, BYTES + 256, WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst);
    WGPUBuffer indirect = make_buffer(s->device, 12, WGPUBufferUsage_Indirect | WGPUBufferUsage_CopyDst);
    const uint32_t workgroups[3] = {1, 1, 1};
    wgpuQueueWriteBuffer(s->queue, indirect, 0, workgroups, sizeof(workgroups));
    wgpuQueueWriteBuffer(s->queue, storage, 0, input, sizeof(input));
    WGPUTextureDescriptor texture_desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    texture_desc.size = (WGPUExtent3D){4, 1, 1};
    texture_desc.format = WGPUTextureFormat_RGBA8Unorm;
    texture_desc.usage = WGPUTextureUsage_CopySrc | WGPUTextureUsage_TextureBinding;
    WGPUTexture texture = wgpuDeviceCreateTexture(s->device, &texture_desc);
    assert(texture);
    WGPUBindGroupLayout layout = wgpuComputePipelineGetBindGroupLayout(pipeline, 0);
    WGPUBindGroupEntry entry = {.binding = 0, .buffer = storage, .size = BYTES};
    WGPUBindGroupDescriptor group_desc = {.layout = layout, .entryCount = 1, .entries = &entry};
    WGPUBindGroup group = wgpuDeviceCreateBindGroup(s->device, &group_desc);
    wgpuBindGroupLayoutRelease(layout);
    assert(group);
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device, NULL);
    WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, NULL);
    wgpuComputePassEncoderSetPipeline(pass, pipeline);
    wgpuComputePassEncoderSetBindGroup(pass, 0, group, 0, NULL);
    wgpuComputePassEncoderDispatchWorkgroups(pass, 1, 1, 1);
    wgpuComputePassEncoderDispatchWorkgroupsIndirect(pass, indirect, 0);
    wgpuComputePassEncoderEnd(pass);
    wgpuComputePassEncoderRelease(pass);
    wgpuCommandEncoderCopyBufferToBuffer(encoder, storage, 0, readback, 0, BYTES);
    WGPUTexelCopyTextureInfo source = {.texture = texture};
    WGPUTexelCopyBufferInfo destination = {.buffer = readback, .layout = {.offset = BYTES, .bytesPerRow = 256, .rowsPerImage = 1}};
    WGPUExtent3D extent = {4, 1, 1};
    wgpuCommandEncoderCopyTextureToBuffer(encoder, &source, &destination, &extent);
    WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, NULL);
    wgpuCommandEncoderRelease(encoder);
    assert(commands);
    wgpuQueueSubmit(s->queue, 1, &commands);
    wgpuCommandBufferRelease(commands);
    /* Odd rounds release every input right after submit. */
    if (round & 1) {
        wgpuBindGroupRelease(group);
        wgpuBufferRelease(storage);
        wgpuTextureRelease(texture);
        wgpuComputePipelineRelease(pipeline);
        group = NULL, storage = NULL, texture = NULL, pipeline = NULL;
    }
    WGPUMapAsyncStatus status = (WGPUMapAsyncStatus)0;
    WGPUBufferMapCallbackInfo callback = {.mode = WGPUCallbackMode_WaitAnyOnly, .callback = mapped_reply, .userdata1 = &status};
    WGPUFuture future = wgpuBufferMapAsync(readback, WGPUMapMode_Read, 0, BYTES + 256, callback);
    assert(future.id);
    WGPUFutureWaitInfo wait = {.future = future};
    assert(wgpuInstanceWaitAny(s->instance, 1, &wait, 0) == WGPUWaitStatus_Success && wait.completed);
    assert(status == WGPUMapAsyncStatus_Success);
    const uint32_t *output = wgpuBufferGetConstMappedRange(readback, 0, BYTES + 256);
    assert(output);
    for (unsigned i = 0; i < WORDS; ++i) assert(output[i] == (input[i] * 3u + offset) * 3u + offset); /* direct, indirect */
    for (unsigned i = 0; i < 4; ++i) assert(output[WORDS + i] == 0); /* Zero-initialized texels. */
    wgpuBufferUnmap(readback);
    wgpuBufferRelease(readback);
    wgpuBufferRelease(indirect);
    if (group) wgpuBindGroupRelease(group);
    if (storage) wgpuBufferRelease(storage);
    if (texture) wgpuTextureRelease(texture);
    if (pipeline) wgpuComputePipelineRelease(pipeline);
    ++w->rounds_checked;
}


static const char *const render_code =
    "@vertex fn vs(@builtin(vertex_index) i: u32) -> @builtin(position) vec4f {\n"
    "  let p = array(vec2f(-1.0, -1.0), vec2f(3.0, -1.0), vec2f(-1.0, 3.0));\n"
    "  return vec4f(p[i], 0.0, 1.0);\n"
    "}\n"
    "@fragment fn fs() -> @location(0) vec4f { return vec4f(0.0, 1.0, 0.0, 1.0); }\n";

static WGPURenderPipeline make_render_pipeline(WGPUDevice device)
{
    WGPUShaderSourceWGSL wgsl = WGPU_SHADER_SOURCE_WGSL_INIT;
    wgsl.code = (WGPUStringView){render_code, WGPU_STRLEN};
    WGPUShaderModuleDescriptor shader_desc = {.nextInChain = &wgsl.chain};
    WGPUShaderModule shader = wgpuDeviceCreateShaderModule(device, &shader_desc);
    assert(shader);
    WGPUColorTargetState color = WGPU_COLOR_TARGET_STATE_INIT;
    color.format = WGPUTextureFormat_RGBA8Unorm;
    WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
    fragment.module = shader;
    fragment.targetCount = 1;
    fragment.targets = &color;
    WGPURenderPipelineDescriptor descriptor = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    descriptor.vertex.module = shader;
    descriptor.fragment = &fragment;
    WGPURenderPipeline pipeline = wgpuDeviceCreateRenderPipeline(device, &descriptor);
    wgpuShaderModuleRelease(shader);
    assert(pipeline);
    return pipeline;
}

static const uint32_t *map_wait(shared_device *s, WGPUBuffer buffer, size_t size)
{
    WGPUMapAsyncStatus status = (WGPUMapAsyncStatus)0;
    WGPUBufferMapCallbackInfo callback = {.mode = WGPUCallbackMode_WaitAnyOnly, .callback = mapped_reply, .userdata1 = &status};
    WGPUFuture future = wgpuBufferMapAsync(buffer, WGPUMapMode_Read, 0, size, callback);
    WGPUFutureWaitInfo wait = {.future = future};
    assert(future.id && wgpuInstanceWaitAny(s->instance, 1, &wait, 0) == WGPUWaitStatus_Success);
    assert(status == WGPUMapAsyncStatus_Success);
    const uint32_t *output = wgpuBufferGetConstMappedRange(buffer, 0, size);
    assert(output);
    return output;
}

/* Render round: a fresh render pipeline, an occlusion query around a direct
 * draw, an indirect draw, a resolve and a readback (green, samples passed). */
static void render_round(worker *w)
{
    shared_device *s = w->device;
    WGPURenderPipeline pipeline = make_render_pipeline(s->device);
    WGPUTextureDescriptor target_desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    target_desc.size = (WGPUExtent3D){4, 4, 1};
    target_desc.format = WGPUTextureFormat_RGBA8Unorm;
    target_desc.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc;
    WGPUTexture target = wgpuDeviceCreateTexture(s->device, &target_desc);
    assert(target);
    WGPUTextureView view = wgpuTextureCreateView(target, NULL);
    WGPUQuerySetDescriptor query_desc = {.type = WGPUQueryType_Occlusion, .count = 1};
    WGPUQuerySet queries = wgpuDeviceCreateQuerySet(s->device, &query_desc);
    WGPUBuffer indirect = make_buffer(s->device, 16, WGPUBufferUsage_Indirect | WGPUBufferUsage_CopyDst);
    const uint32_t draw[4] = {3, 1, 0, 0};
    wgpuQueueWriteBuffer(s->queue, indirect, 0, draw, sizeof(draw));
    WGPUBuffer resolved = make_buffer(s->device, 256, WGPUBufferUsage_QueryResolve | WGPUBufferUsage_CopySrc);
    WGPUBuffer readback = make_buffer(s->device, 256 * 4 + 8, WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst);
    assert(view && queries);
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device, NULL);
    WGPURenderPassColorAttachment attachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    attachment.view = view;
    attachment.loadOp = WGPULoadOp_Clear;
    attachment.storeOp = WGPUStoreOp_Store;
    WGPURenderPassDescriptor pass_desc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
    pass_desc.colorAttachmentCount = 1;
    pass_desc.colorAttachments = &attachment;
    pass_desc.occlusionQuerySet = queries;
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &pass_desc);
    wgpuRenderPassEncoderSetPipeline(pass, pipeline);
    wgpuRenderPassEncoderBeginOcclusionQuery(pass, 0);
    wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
    wgpuRenderPassEncoderEndOcclusionQuery(pass);
    wgpuRenderPassEncoderDrawIndirect(pass, indirect, 0);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
    wgpuCommandEncoderResolveQuerySet(encoder, queries, 0, 1, resolved, 0);
    WGPUTexelCopyTextureInfo source = {.texture = target};
    WGPUTexelCopyBufferInfo destination = {.buffer = readback, .layout = {.bytesPerRow = 256, .rowsPerImage = 4}};
    WGPUExtent3D extent = {4, 4, 1};
    wgpuCommandEncoderCopyTextureToBuffer(encoder, &source, &destination, &extent);
    wgpuCommandEncoderCopyBufferToBuffer(encoder, resolved, 0, readback, 256 * 4, 8);
    WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, NULL);
    wgpuCommandEncoderRelease(encoder);
    assert(commands);
    wgpuQueueSubmit(s->queue, 1, &commands);
    wgpuCommandBufferRelease(commands);
    wgpuRenderPipelineRelease(pipeline);
    wgpuTextureViewRelease(view);
    wgpuTextureRelease(target);
    wgpuQuerySetRelease(queries);
    wgpuBufferRelease(indirect);
    wgpuBufferRelease(resolved);
    const uint32_t *output = map_wait(s, readback, 256 * 4 + 8);
    for (unsigned row = 0; row < 4; ++row)
        for (unsigned x = 0; x < 4; ++x) assert(output[row * 64 + x] == 0xff00ff00u); /* RGBA8 green */
    assert(output[256] || output[257]);
    wgpuBufferUnmap(readback);
    wgpuBufferRelease(readback);
}

/* Compute-program round: one dispatch and copy prepared as a gpu-recorded
 * program and submitted twice. */
static void program_round(worker *w, unsigned round)
{
    shared_device *s = w->device;
    uint32_t input[WORDS];
    for (unsigned i = 0; i < WORDS; ++i) input[i] = w->seed + round + i;
    WGPUBuffer storage = make_buffer(s->device, BYTES, WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst);
    WGPUBuffer readback = make_buffer(s->device, BYTES, WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst);
    wgpuQueueWriteBuffer(s->queue, storage, 0, input, sizeof(input));
    WGPUBindGroupLayout layout = wgpuComputePipelineGetBindGroupLayout(s->pipeline, 0);
    WGPUBindGroupEntry entry = {.binding = 0, .buffer = storage, .size = BYTES};
    WGPUBindGroupDescriptor group_desc = {.layout = layout, .entryCount = 1, .entries = &entry};
    WGPUBindGroup group = wgpuDeviceCreateBindGroup(s->device, &group_desc);
    wgpuBindGroupLayoutRelease(layout);
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device, NULL);
    WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, NULL);
    wgpuComputePassEncoderSetPipeline(pass, s->pipeline);
    wgpuComputePassEncoderSetBindGroup(pass, 0, group, 0, NULL);
    wgpuComputePassEncoderDispatchWorkgroups(pass, 1, 1, 1);
    wgpuComputePassEncoderEnd(pass);
    wgpuComputePassEncoderRelease(pass);
    wgpuCommandEncoderCopyBufferToBuffer(encoder, storage, 0, readback, 0, BYTES);
    WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, NULL);
    wgpuCommandEncoderRelease(encoder);
    void *program = doeNativeComputeProgramPrepare(s->queue, commands);
    wgpuCommandBufferRelease(commands);
    assert(program);
    assert(doeNativeComputeProgramSubmit(program) == 1 && doeNativeComputeProgramSubmit(program) == 1);
    doeNativeComputeProgramRelease(program);
    wgpuBindGroupRelease(group);
    wgpuBufferRelease(storage);
    const uint32_t *output = map_wait(s, readback, BYTES);
    for (unsigned i = 0; i < WORDS; ++i) assert(output[i] == (input[i] * 3u + 1u) * 3u + 1u);
    wgpuBufferUnmap(readback);
    wgpuBufferRelease(readback);
}

/* Requests, uses and releases adapters and devices while the workers run. */
typedef struct churner {
    WGPUInstance instance;
    unsigned devices;
} churner;
static void *run_churner(void *userdata)
{
    churner *c = userdata;
    for (unsigned i = 0; i < 24; ++i) {
        shared_device d = {0};
        open_device(&d, c->instance);
        WGPUBuffer buffer = make_buffer(d.device, 64, WGPUBufferUsage_CopyDst);
        const uint32_t word = i;
        wgpuQueueWriteBuffer(d.queue, buffer, 0, &word, sizeof(word));
        wgpuBufferRelease(buffer);
        if (i % 2) wgpuDeviceDestroy(d.device);
        close_device(&d);
        ++c->devices;
    }
    return NULL;
}

static void *run_worker(void *userdata)
{
    worker *w = userdata;
    for (unsigned round = 0; round < ROUNDS; ++round) {
        round_trip(w, round);
        if (round % 4 == 1) render_round(w);
        if (round % 5 == 2) program_round(w, round);
    }
    return NULL;
}

typedef struct poller {
    shared_device *devices;
    size_t count;
    atomic_bool stop;
    atomic_uint done;
} poller;

static void *run_poller(void *userdata)
{
    poller *p = userdata;
    while (!atomic_load(&p->stop)) {
        for (size_t i = 0; i < p->count; ++i) {
            WGPUQueueWorkDoneCallbackInfo info = {.mode = WGPUCallbackMode_AllowSpontaneous, .callback = done_reply, .userdata1 = &p->done};
            assert(wgpuQueueOnSubmittedWorkDone(p->devices[i].queue, info).id);
            wgpuInstanceProcessEvents(p->devices[i].instance);
        }
    }
    return NULL;
}

typedef struct doomed {
    shared_device device;
    atomic_uint lost, maps_ok, maps_failed, rounds;
    atomic_bool stop, destroying;
} doomed;

static void lost_reply(WGPUDevice const *device, WGPUDeviceLostReason reason, WGPUStringView message, void *userdata, void *unused)
{
    (void)device; (void)reason; (void)message; (void)unused;
    atomic_fetch_add(&((doomed *)userdata)->lost, 1);
}
static void tolerated_error(WGPUDevice const *device, WGPUErrorType type, WGPUStringView message, void *userdata, void *unused)
{
    (void)device; (void)type; (void)message; (void)userdata; (void)unused;
}

/* Copy-and-map rounds that tolerate the device going away underneath. */
static void *run_doomed(void *userdata)
{
    doomed *d = userdata;
    shared_device *s = &d->device;
    for (unsigned round = 0; !atomic_load(&d->stop) && round < 100000; ++round) {
        uint32_t input[WORDS];
        for (unsigned i = 0; i < WORDS; ++i) input[i] = round * 5u + i;
        WGPUBufferDescriptor source_desc = {.size = BYTES, .usage = WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst};
        WGPUBufferDescriptor readback_desc = {.size = BYTES, .usage = WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst};
        WGPUBuffer source = wgpuDeviceCreateBuffer(s->device, &source_desc);
        WGPUBuffer readback = wgpuDeviceCreateBuffer(s->device, &readback_desc);
        if (source) wgpuQueueWriteBuffer(s->queue, source, 0, input, sizeof(input));
        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device, NULL);
        if (encoder && source && readback) wgpuCommandEncoderCopyBufferToBuffer(encoder, source, 0, readback, 0, BYTES);
        WGPUCommandBuffer commands = encoder ? wgpuCommandEncoderFinish(encoder, NULL) : NULL;
        if (encoder) wgpuCommandEncoderRelease(encoder);
        if (commands) {
            wgpuQueueSubmit(s->queue, 1, &commands);
            wgpuCommandBufferRelease(commands);
        }
        if (source) wgpuBufferRelease(source); /* Released before the map. */
        if (readback) {
            WGPUMapAsyncStatus status = (WGPUMapAsyncStatus)0;
            WGPUBufferMapCallbackInfo callback = {.mode = WGPUCallbackMode_AllowSpontaneous, .callback = mapped_reply, .userdata1 = &status};
            wgpuBufferMapAsync(readback, WGPUMapMode_Read, 0, BYTES, callback);
            wgpuInstanceProcessEvents(s->instance);
            assert(status != 0); /* Every map settles: no pending map survives the call and event processing. */
            if (status == WGPUMapAsyncStatus_Success) {
                const uint32_t *output = wgpuBufferGetConstMappedRange(readback, 0, BYTES);
                /* Destroy unmaps every buffer, so a range may vanish only once it began. */
                assert(output || atomic_load(&d->destroying));
                if (output) {
                    for (unsigned i = 0; i < WORDS; ++i) assert(output[i] == input[i]);
                    atomic_fetch_add(&d->maps_ok, 1);
                }
                wgpuBufferUnmap(readback);
            } else {
                atomic_fetch_add(&d->maps_failed, 1);
            }
            wgpuBufferRelease(readback);
        }
        atomic_fetch_add(&d->rounds, 1);
    }
    return NULL;
}

static void destroy_under_load(WGPUInstance instance)
{
    doomed d = {0};
    shared_device *s = &d.device;
    s->instance = instance;
    WGPURequestAdapterOptions options = WGPU_REQUEST_ADAPTER_OPTIONS_INIT;
    adapter_class_options(&options);
    WGPURequestAdapterCallbackInfo adapter = {.mode = WGPUCallbackMode_AllowSpontaneous, .callback = adapter_ready, .userdata1 = &s->adapter};
    assert(wgpuInstanceRequestAdapter(instance, &options, adapter).id && s->adapter);
    WGPUDeviceDescriptor descriptor = WGPU_DEVICE_DESCRIPTOR_INIT;
    descriptor.uncapturedErrorCallbackInfo = (WGPUUncapturedErrorCallbackInfo){.callback = tolerated_error};
    descriptor.deviceLostCallbackInfo = (WGPUDeviceLostCallbackInfo){.mode = WGPUCallbackMode_AllowSpontaneous,
        .callback = lost_reply, .userdata1 = &d};
    WGPURequestDeviceCallbackInfo device = {.mode = WGPUCallbackMode_AllowSpontaneous, .callback = device_ready, .userdata1 = &s->device};
    assert(wgpuAdapterRequestDevice(s->adapter, &descriptor, device).id && s->device);
    s->queue = wgpuDeviceGetQueue(s->device);
    pthread_t threads[3];
    for (unsigned i = 0; i < 3; ++i) assert(!pthread_create(&threads[i], NULL, run_doomed, &d));
    while (atomic_load(&d.rounds) < 64) sched_yield();
    atomic_store(&d.destroying, true);
    wgpuDeviceDestroy(s->device);
    for (unsigned spins = 0; !atomic_load(&d.lost) && spins < 1000000; ++spins) {
        wgpuInstanceProcessEvents(instance);
        sched_yield();
    }
    /* Keep working on the lost device: creation, submission and maps settle. */
    const unsigned after_loss = atomic_load(&d.rounds) + 96;
    while (atomic_load(&d.lost) && atomic_load(&d.rounds) < after_loss) sched_yield();
    atomic_store(&d.stop, true);
    for (unsigned i = 0; i < 3; ++i) assert(!pthread_join(threads[i], NULL));
    wgpuInstanceProcessEvents(instance);
    assert(atomic_load(&d.lost) == 1);
    assert(atomic_load(&d.maps_ok) > 0 && atomic_load(&d.maps_failed) > 0);
    wgpuQueueRelease(s->queue);
    wgpuDeviceRelease(s->device);
    wgpuAdapterRelease(s->adapter);
    printf("destroy under load: %u rounds, %u exact maps before loss, %u failed maps after, one loss callback\n",
        atomic_load(&d.rounds), atomic_load(&d.maps_ok), atomic_load(&d.maps_failed));
}

int main(void)
{
    WGPUInstance instance = wgpuCreateInstance(NULL);
    assert(instance);
    shared_device devices[1 + DEVICE_THREADS];
    memset(devices, 0, sizeof(devices));
    for (unsigned i = 0; i < 1 + DEVICE_THREADS; ++i) open_device(&devices[i], instance);
    poller poll = {.devices = devices, .count = 1 + DEVICE_THREADS};
    pthread_t poll_thread;
    assert(!pthread_create(&poll_thread, NULL, run_poller, &poll));
    churner churn = {.instance = instance};
    pthread_t churn_thread;
    assert(!pthread_create(&churn_thread, NULL, run_churner, &churn));
    worker workers[SHARED_THREADS + DEVICE_THREADS];
    pthread_t threads[SHARED_THREADS + DEVICE_THREADS];
    for (unsigned i = 0; i < SHARED_THREADS + DEVICE_THREADS; ++i) {
        workers[i] = (worker){.device = i < SHARED_THREADS ? &devices[0] : &devices[1 + i - SHARED_THREADS], .seed = i};
        assert(!pthread_create(&threads[i], NULL, run_worker, &workers[i]));
    }
    for (unsigned i = 0; i < SHARED_THREADS + DEVICE_THREADS; ++i) {
        assert(!pthread_join(threads[i], NULL));
        assert(workers[i].rounds_checked == ROUNDS);
    }
    assert(!pthread_join(churn_thread, NULL) && churn.devices == 24);
    atomic_store(&poll.stop, true);
    assert(!pthread_join(poll_thread, NULL));
    assert(atomic_load(&poll.done) > 0);
    for (unsigned i = 0; i < 1 + DEVICE_THREADS; ++i) close_device(&devices[i]);
    destroy_under_load(instance);
    wgpuInstanceRelease(instance);
    printf("concurrency: %u threads on one device and %u on their own devices, %u rounds each (direct and indirect compute, render with occlusion query and indirect draw, compute programs), 24 device lifetimes alongside, checked readback, early release, init-batch textures\n",
        (unsigned)SHARED_THREADS, (unsigned)DEVICE_THREADS, (unsigned)ROUNDS);
    return 0;
}
