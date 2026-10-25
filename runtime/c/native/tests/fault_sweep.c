/* SPDX-License-Identifier: MIT
 * Systematic failure injection across one complete public workload on
 * software Vulkan: a zero-initialized texture (batched init path), buffers,
 * queue writes, a shader, a pipeline, a bind group, a dispatch, copies,
 * submission with immediate caller release, work done and a map; run twice per
 * device so the second round reuses the retained pipeline and the spare
 * command pool and fence of the first.
 * - Host allocation: every malloc/calloc of the C owners inside the workload
 *   fails in turn (GNU --wrap at the real allocator boundary).
 * - Native calls: every result-returning Vulkan call of the workload fails in
 *   turn through the device context's own function table: creation and
 *   allocation report out-of-memory, vkQueueSubmit refuses, and fence, wait and
 *   idle calls report device loss while the real queue may still be running.
 * Each case uses a fresh device. It must settle without a crash or hang: every
 * map completes, a successful map holds the exact result, an injected failure
 * that changed the outcome is reported, and device release reclaims
 * everything (run under ASan with leak detection for the leak half). */
#include "../src/internal.h"
#include "../../backends/vulkan/src/internal.h" /* The device context's function table. */
#include "consumer.h"
#include "doe_native_programs.h"
#include <errno.h>
#include <execinfo.h>
#include <stdlib.h>

enum { WORDS = 64, BYTES = WORDS * 4 };

/* Host allocation boundary. */
static _Atomic bool counting;
static atomic_uint allocations, fail_at, failures;
void *__real_malloc(size_t);
void *__real_calloc(size_t, size_t);
static bool inject(void)
{
    if (!atomic_load(&counting)) return false;
    const unsigned index = atomic_fetch_add(&allocations, 1) + 1;
    if (index != atomic_load(&fail_at)) return false;
    if (getenv("FAULT_SWEEP_TRACE")) {
        void *frames[16];
        backtrace_symbols_fd(frames, backtrace(frames, 16), 2);
    }
    atomic_fetch_add(&failures, 1);
    errno = ENOMEM;
    return true;
}
void *__wrap_malloc(size_t size) { return inject() ? NULL : __real_malloc(size); }
void *__wrap_calloc(size_t count, size_t size) { return inject() ? NULL : __real_calloc(count, size); }

/* Native call boundary: the device context's function table. */
static doe_vk_functions real;
static _Atomic bool native_counting;
static atomic_uint native_calls, native_fail_at, native_failures;
static bool native_inject_named(const char *name)
{
    if (!atomic_load(&native_counting)) return false;
    const unsigned index = atomic_fetch_add(&native_calls, 1) + 1;
    if (getenv("FAULT_SWEEP_TRACE") && index == atomic_load(&native_fail_at)) fprintf(stderr, "  fails %s\n", name);
    if (index != atomic_load(&native_fail_at)) return false;
    atomic_fetch_add(&native_failures, 1);
    return true;
}
enum { OUT_OF_DEVICE_MEMORY = -2, OUT_OF_HOST_MEMORY = -1, DEVICE_LOST = -4 };
static doe_vk_context *faulted; /* The context whose table is replaced. */
/* An injected loss reports DEVICE_LOST while the real queue may still be
 * executing: nothing may be freed before the queue has drained. */
static VkResult lose(void)
{
    return DEVICE_LOST;
}
#define FAULT(name, code, params, args) \
    static VkResult fault_##name params { return native_inject_named(#name) ? code : real.name args; }
#define LOSS(name, params, args) \
    static VkResult fault_##name params { return native_inject_named(#name) ? lose() : real.name args; }
FAULT(vkCreateCommandPool, OUT_OF_DEVICE_MEMORY, (VkDevice d, const VkCommandPoolCreateInfo *i, const VkAllocationCallbacks *a, VkCommandPool *o), (d, i, a, o))
FAULT(vkAllocateCommandBuffers, OUT_OF_DEVICE_MEMORY, (VkDevice d, const VkCommandBufferAllocateInfo *i, VkCommandBuffer *o), (d, i, o))
FAULT(vkCreateFence, OUT_OF_DEVICE_MEMORY, (VkDevice d, const VkFenceCreateInfo *i, const VkAllocationCallbacks *a, VkFence *o), (d, i, a, o))
FAULT(vkBeginCommandBuffer, OUT_OF_DEVICE_MEMORY, (VkCommandBuffer c, const VkCommandBufferBeginInfo *i), (c, i))
FAULT(vkEndCommandBuffer, OUT_OF_DEVICE_MEMORY, (VkCommandBuffer c), (c))
FAULT(vkQueueSubmit, OUT_OF_HOST_MEMORY, (VkQueue q, uint32_t n, const VkSubmitInfo *s, VkFence f), (q, n, s, f))
FAULT(vkCreateDescriptorPool, OUT_OF_DEVICE_MEMORY, (VkDevice d, const VkDescriptorPoolCreateInfo *i, const VkAllocationCallbacks *a, VkDescriptorPool *o), (d, i, a, o))
FAULT(vkAllocateDescriptorSets, OUT_OF_DEVICE_MEMORY, (VkDevice d, const VkDescriptorSetAllocateInfo *i, VkDescriptorSet *o), (d, i, o))
FAULT(vkCreateBuffer, OUT_OF_DEVICE_MEMORY, (VkDevice d, const VkBufferCreateInfo *i, const VkAllocationCallbacks *a, VkBuffer *o), (d, i, a, o))
FAULT(vkAllocateMemory, OUT_OF_DEVICE_MEMORY, (VkDevice d, const VkMemoryAllocateInfo *i, const VkAllocationCallbacks *a, VkDeviceMemory *o), (d, i, a, o))
FAULT(vkBindBufferMemory, OUT_OF_DEVICE_MEMORY, (VkDevice d, VkBuffer b, VkDeviceMemory m, VkDeviceSize o), (d, b, m, o))
FAULT(vkMapMemory, OUT_OF_DEVICE_MEMORY, (VkDevice d, VkDeviceMemory m, VkDeviceSize o, VkDeviceSize s, VkFlags f, void **p), (d, m, o, s, f, p))
FAULT(vkCreateImage, OUT_OF_DEVICE_MEMORY, (VkDevice d, const VkImageCreateInfo *i, const VkAllocationCallbacks *a, VkImage *o), (d, i, a, o))
FAULT(vkBindImageMemory, OUT_OF_DEVICE_MEMORY, (VkDevice d, VkImage im, VkDeviceMemory m, VkDeviceSize o), (d, im, m, o))
FAULT(vkCreateImageView, OUT_OF_DEVICE_MEMORY, (VkDevice d, const VkImageViewCreateInfo *i, const VkAllocationCallbacks *a, VkImageView *o), (d, i, a, o))
FAULT(vkCreateShaderModule, OUT_OF_DEVICE_MEMORY, (VkDevice d, const VkShaderModuleCreateInfo *i, const VkAllocationCallbacks *a, VkShaderModule *o), (d, i, a, o))
FAULT(vkCreateComputePipelines, OUT_OF_DEVICE_MEMORY, (VkDevice d, VkPipelineCache c, uint32_t n, const VkComputePipelineCreateInfo *i, const VkAllocationCallbacks *a, VkPipeline *o), (d, c, n, i, a, o))
FAULT(vkCreatePipelineLayout, OUT_OF_DEVICE_MEMORY, (VkDevice d, const VkPipelineLayoutCreateInfo *i, const VkAllocationCallbacks *a, VkPipelineLayout *o), (d, i, a, o))
FAULT(vkCreateDescriptorSetLayout, OUT_OF_DEVICE_MEMORY, (VkDevice d, const VkDescriptorSetLayoutCreateInfo *i, const VkAllocationCallbacks *a, VkDescriptorSetLayout *o), (d, i, a, o))
FAULT(vkCreateSampler, OUT_OF_DEVICE_MEMORY, (VkDevice d, const VkSamplerCreateInfo *i, const VkAllocationCallbacks *a, VkSampler *o), (d, i, a, o))
FAULT(vkCreateRenderPass, OUT_OF_DEVICE_MEMORY, (VkDevice d, const VkRenderPassCreateInfo *i, const VkAllocationCallbacks *a, VkRenderPass *o), (d, i, a, o))
FAULT(vkCreateFramebuffer, OUT_OF_DEVICE_MEMORY, (VkDevice d, const VkFramebufferCreateInfo *i, const VkAllocationCallbacks *a, VkFramebuffer *o), (d, i, a, o))
FAULT(vkCreateGraphicsPipelines, OUT_OF_DEVICE_MEMORY, (VkDevice d, VkPipelineCache c, uint32_t n, const VkGraphicsPipelineCreateInfo *i, const VkAllocationCallbacks *a, VkPipeline *o), (d, c, n, i, a, o))
FAULT(vkCreateQueryPool, OUT_OF_DEVICE_MEMORY, (VkDevice d, const VkQueryPoolCreateInfo *i, const VkAllocationCallbacks *a, VkQueryPool *o), (d, i, a, o))
FAULT(vkResetCommandPool, OUT_OF_DEVICE_MEMORY, (VkDevice d, VkCommandPool p, VkFlags f), (d, p, f))
FAULT(vkResetFences, OUT_OF_DEVICE_MEMORY, (VkDevice d, uint32_t n, const VkFence *f), (d, n, f))
LOSS(vkGetFenceStatus, (VkDevice d, VkFence f), (d, f))
LOSS(vkWaitForFences, (VkDevice d, uint32_t n, const VkFence *f, VkBool32 w, uint64_t t), (d, n, f, w, t))
LOSS(vkQueueWaitIdle, (VkQueue q), (q))

static void install(doe_vk_context *context)
{
    faulted = context;
    real = context->vk;
#define INSTALL(name) context->vk.name = fault_##name;
    INSTALL(vkCreateCommandPool) INSTALL(vkAllocateCommandBuffers) INSTALL(vkCreateFence)
    INSTALL(vkBeginCommandBuffer) INSTALL(vkEndCommandBuffer) INSTALL(vkQueueSubmit)
    INSTALL(vkCreateDescriptorPool) INSTALL(vkAllocateDescriptorSets) INSTALL(vkCreateBuffer)
    INSTALL(vkAllocateMemory) INSTALL(vkBindBufferMemory) INSTALL(vkMapMemory) INSTALL(vkCreateImage)
    INSTALL(vkBindImageMemory) INSTALL(vkCreateImageView) INSTALL(vkCreateShaderModule)
    INSTALL(vkCreateComputePipelines) INSTALL(vkCreatePipelineLayout) INSTALL(vkCreateDescriptorSetLayout)
    INSTALL(vkResetCommandPool) INSTALL(vkResetFences) INSTALL(vkGetFenceStatus)
    INSTALL(vkWaitForFences) INSTALL(vkQueueWaitIdle)
    INSTALL(vkCreateSampler) INSTALL(vkCreateRenderPass) INSTALL(vkCreateFramebuffer)
    INSTALL(vkCreateGraphicsPipelines) INSTALL(vkCreateQueryPool)
#undef INSTALL
}

static const char *const code =
    "@group(0) @binding(0) var<storage, read_write> data: array<u32>;\n"
    "@compute @workgroup_size(64) fn main(@builtin(global_invocation_id) id: vec3u) {\n"
    "  data[id.x] = data[id.x] * 3u + 1u;\n"
    "}\n";

typedef struct observed {
    atomic_uint errors, lost, done;
    WGPUMapAsyncStatus map;
    bool done_admitted, map_refused, map_failed;
    unsigned done_expected;
} observed;
static void sweep_error(WGPUDevice const *device, WGPUErrorType type, WGPUStringView message, void *userdata, void *unused)
{
    (void)device; (void)type; (void)message; (void)unused;
    atomic_fetch_add(&((observed *)userdata)->errors, 1);
}
static void lost_reply(WGPUDevice const *device, WGPUDeviceLostReason reason, WGPUStringView message, void *userdata, void *unused)
{
    (void)device; (void)message; (void)unused;
    if (reason != WGPUDeviceLostReason_CallbackCancelled) atomic_fetch_add(&((observed *)userdata)->lost, 1);
}
static void done_reply(WGPUQueueWorkDoneStatus status, WGPUStringView message, void *userdata, void *unused)
{
    (void)status; (void)message; (void)unused;
    atomic_fetch_add(&((observed *)userdata)->done, 1);
}
static void map_reply(WGPUMapAsyncStatus status, WGPUStringView message, void *userdata, void *unused)
{
    (void)message; (void)unused;
    ((observed *)userdata)->map = status;
    if (status != WGPUMapAsyncStatus_Success) ((observed *)userdata)->map_failed = true;
}
/* Request callbacks record their status; creation-time failures are allowed
 * and must arrive through these callbacks (or a NULL instance). */
static int adapter_status, device_status;
static void sweep_adapter(WGPURequestAdapterStatus status, WGPUAdapter adapter, WGPUStringView message, void *userdata, void *unused)
{
    (void)message; (void)unused;
    assert((status == WGPURequestAdapterStatus_Success) == (adapter != NULL));
    adapter_status = (int)status;
    *(WGPUAdapter *)userdata = adapter;
}
static void sweep_device(WGPURequestDeviceStatus status, WGPUDevice device, WGPUStringView message, void *userdata, void *unused)
{
    (void)message; (void)unused;
    assert((status == WGPURequestDeviceStatus_Success) == (device != NULL));
    device_status = (int)status;
    *(WGPUDevice *)userdata = device;
}

typedef enum outcome { EXACT, FAILED } outcome;

/* Compute round; any handle may be NULL after an injected failure. */
static outcome workload_round(WGPUInstance instance, WGPUDevice device, WGPUQueue queue, observed *seen)
{
    uint32_t input[WORDS];
    for (unsigned i = 0; i < WORDS; ++i) input[i] = i * 11u + 5u;
    WGPUTextureDescriptor texture_desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    texture_desc.size = (WGPUExtent3D){4, 1, 1};
    texture_desc.format = WGPUTextureFormat_RGBA8Unorm;
    texture_desc.usage = WGPUTextureUsage_CopySrc | WGPUTextureUsage_TextureBinding;
    WGPUTexture texture = wgpuDeviceCreateTexture(device, &texture_desc);
    WGPUBufferDescriptor storage_desc = {.size = BYTES, .usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst};
    WGPUBufferDescriptor readback_desc = {.size = BYTES + 256, .usage = WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst};
    WGPUBuffer storage = wgpuDeviceCreateBuffer(device, &storage_desc);
    WGPUBuffer readback = wgpuDeviceCreateBuffer(device, &readback_desc);
    WGPUBufferDescriptor indirect_desc = {.size = 12, .usage = WGPUBufferUsage_Indirect | WGPUBufferUsage_CopyDst};
    WGPUBuffer indirect = wgpuDeviceCreateBuffer(device, &indirect_desc);
    const uint32_t workgroups[3] = {1, 1, 1};
    if (indirect) wgpuQueueWriteBuffer(queue, indirect, 0, workgroups, sizeof(workgroups));
    if (storage) wgpuQueueWriteBuffer(queue, storage, 0, input, sizeof(input));
    WGPUShaderSourceWGSL wgsl = WGPU_SHADER_SOURCE_WGSL_INIT;
    wgsl.code = (WGPUStringView){code, WGPU_STRLEN};
    WGPUShaderModuleDescriptor shader_desc = {.nextInChain = &wgsl.chain};
    WGPUShaderModule shader = wgpuDeviceCreateShaderModule(device, &shader_desc);
    WGPUComputePipeline pipeline = NULL;
    if (shader) {
        WGPUComputePipelineDescriptor descriptor = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
        descriptor.compute.module = shader;
        pipeline = wgpuDeviceCreateComputePipeline(device, &descriptor);
        wgpuShaderModuleRelease(shader);
    }
    WGPUBindGroup group = NULL;
    if (pipeline && storage) {
        WGPUBindGroupLayout layout = wgpuComputePipelineGetBindGroupLayout(pipeline, 0);
        if (layout) {
            WGPUBindGroupEntry entry = {.binding = 0, .buffer = storage, .size = BYTES};
            WGPUBindGroupDescriptor group_desc = {.layout = layout, .entryCount = 1, .entries = &entry};
            group = wgpuDeviceCreateBindGroup(device, &group_desc);
            wgpuBindGroupLayoutRelease(layout);
        }
    }
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, NULL);
    WGPUCommandBuffer commands = NULL;
    if (encoder) {
        if (pipeline && group) {
            WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, NULL);
            if (pass) {
                wgpuComputePassEncoderSetPipeline(pass, pipeline);
                wgpuComputePassEncoderSetBindGroup(pass, 0, group, 0, NULL);
                wgpuComputePassEncoderDispatchWorkgroups(pass, 1, 1, 1);
                if (indirect) wgpuComputePassEncoderDispatchWorkgroupsIndirect(pass, indirect, 0);
                wgpuComputePassEncoderEnd(pass);
                wgpuComputePassEncoderRelease(pass);
            }
        }
        if (storage && readback) wgpuCommandEncoderCopyBufferToBuffer(encoder, storage, 0, readback, 0, BYTES);
        if (texture && readback) {
            WGPUTexelCopyTextureInfo source = {.texture = texture};
            WGPUTexelCopyBufferInfo destination = {.buffer = readback, .layout = {.offset = BYTES, .bytesPerRow = 256, .rowsPerImage = 1}};
            WGPUExtent3D extent = {4, 1, 1};
            wgpuCommandEncoderCopyTextureToBuffer(encoder, &source, &destination, &extent);
        }
        commands = wgpuCommandEncoderFinish(encoder, NULL);
        wgpuCommandEncoderRelease(encoder);
    }
    if (commands) {
        wgpuQueueSubmit(queue, 1, &commands);
        wgpuCommandBufferRelease(commands);
    }
    /* Immediate caller release of every input after submission. */
    if (group) wgpuBindGroupRelease(group);
    if (pipeline) wgpuComputePipelineRelease(pipeline);
    if (storage) wgpuBufferRelease(storage);
    if (indirect) wgpuBufferRelease(indirect);
    if (texture) wgpuTextureRelease(texture);
    WGPUQueueWorkDoneCallbackInfo done = {.mode = WGPUCallbackMode_AllowSpontaneous, .callback = done_reply, .userdata1 = seen};
    const bool admitted = wgpuQueueOnSubmittedWorkDone(queue, done).id != 0;
    seen->done_expected += admitted;
    seen->done_admitted = seen->done_admitted && admitted;
    outcome result = FAILED;
    if (readback) {
        seen->map = (WGPUMapAsyncStatus)0;
        WGPUBufferMapCallbackInfo map = {.mode = WGPUCallbackMode_AllowSpontaneous, .callback = map_reply, .userdata1 = seen};
        WGPUFuture future = wgpuBufferMapAsync(readback, WGPUMapMode_Read, 0, BYTES + 256, map);
        wgpuInstanceProcessEvents(instance);
        assert(!future.id || seen->map != 0); /* An admitted map settles; nothing stays pending. */
        seen->map_refused |= !future.id;
        if (seen->map == WGPUMapAsyncStatus_Success) {
            const uint32_t *output = wgpuBufferGetConstMappedRange(readback, 0, BYTES + 256);
            if (output) {
                bool exact = true;
                for (unsigned i = 0; i < WORDS; ++i) exact &= output[i] == (input[i] * 3u + 1u) * 3u + 1u; /* direct, then indirect */
                for (unsigned i = 0; i < 4; ++i) exact &= output[WORDS + i] == 0;
                result = exact ? EXACT : FAILED;
            }
            wgpuBufferUnmap(readback);
        }
        wgpuBufferRelease(readback);
    }
    wgpuInstanceProcessEvents(instance);
    return result;
}


/* Requests work done, maps readback and returns its bytes, or NULL. */
static const uint32_t *settle_and_map(WGPUInstance instance, WGPUQueue queue, WGPUBuffer readback, size_t size, observed *seen)
{
    WGPUQueueWorkDoneCallbackInfo done = {.mode = WGPUCallbackMode_AllowSpontaneous, .callback = done_reply, .userdata1 = seen};
    const bool admitted = wgpuQueueOnSubmittedWorkDone(queue, done).id != 0;
    seen->done_expected += admitted;
    seen->done_admitted = seen->done_admitted && admitted;
    if (!readback) return NULL;
    seen->map = (WGPUMapAsyncStatus)0;
    WGPUBufferMapCallbackInfo map = {.mode = WGPUCallbackMode_AllowSpontaneous, .callback = map_reply, .userdata1 = seen};
    WGPUFuture future = wgpuBufferMapAsync(readback, WGPUMapMode_Read, 0, size, map);
    wgpuInstanceProcessEvents(instance);
    assert(!future.id || seen->map != 0);
    seen->map_refused |= !future.id;
    return seen->map == WGPUMapAsyncStatus_Success ? wgpuBufferGetConstMappedRange(readback, 0, size) : NULL;
}

static const char *const render_code =
    "@vertex fn vs(@builtin(vertex_index) i: u32) -> @builtin(position) vec4f {\n"
    "  let p = array(vec2f(-1.0, -1.0), vec2f(3.0, -1.0), vec2f(-1.0, 3.0));\n"
    "  return vec4f(p[i], 0.0, 1.0);\n"
    "}\n"
    "@fragment fn fs() -> @location(0) vec4f { return vec4f(1.0, 0.0, 0.0, 1.0); }\n";

/* Render round: a render pipeline, a pass with a direct draw inside an
 * occlusion query and an indirect draw, the query resolved and the target
 * read back: every texel red, samples passed. */
static outcome render_round(WGPUInstance instance, WGPUDevice device, WGPUQueue queue, observed *seen)
{
    WGPUTextureDescriptor target_desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    target_desc.size = (WGPUExtent3D){4, 4, 1};
    target_desc.format = WGPUTextureFormat_RGBA8Unorm;
    target_desc.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc;
    WGPUTexture target = wgpuDeviceCreateTexture(device, &target_desc);
    WGPUTextureView view = target ? wgpuTextureCreateView(target, NULL) : NULL;
    WGPUShaderSourceWGSL wgsl = WGPU_SHADER_SOURCE_WGSL_INIT;
    wgsl.code = (WGPUStringView){render_code, WGPU_STRLEN};
    WGPUShaderModuleDescriptor shader_desc = {.nextInChain = &wgsl.chain};
    WGPUShaderModule shader = wgpuDeviceCreateShaderModule(device, &shader_desc);
    WGPURenderPipeline pipeline = NULL;
    if (shader) {
        WGPUColorTargetState color = WGPU_COLOR_TARGET_STATE_INIT;
        color.format = WGPUTextureFormat_RGBA8Unorm;
        WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
        fragment.module = shader;
        fragment.targetCount = 1;
        fragment.targets = &color;
        WGPURenderPipelineDescriptor descriptor = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
        descriptor.vertex.module = shader;
        descriptor.fragment = &fragment;
        pipeline = wgpuDeviceCreateRenderPipeline(device, &descriptor);
        wgpuShaderModuleRelease(shader);
    }
    WGPUQuerySetDescriptor query_desc = {.type = WGPUQueryType_Occlusion, .count = 1};
    WGPUQuerySet queries = wgpuDeviceCreateQuerySet(device, &query_desc);
    WGPUBufferDescriptor indirect_desc = {.size = 16, .usage = WGPUBufferUsage_Indirect | WGPUBufferUsage_CopyDst};
    WGPUBuffer indirect = wgpuDeviceCreateBuffer(device, &indirect_desc);
    const uint32_t draw[4] = {3, 1, 0, 0};
    if (indirect) wgpuQueueWriteBuffer(queue, indirect, 0, draw, sizeof(draw));
    WGPUBufferDescriptor resolve_desc = {.size = 256, .usage = WGPUBufferUsage_QueryResolve | WGPUBufferUsage_CopySrc};
    WGPUBuffer resolved = wgpuDeviceCreateBuffer(device, &resolve_desc);
    WGPUBufferDescriptor readback_desc = {.size = 256 * 4 + 8, .usage = WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst};
    WGPUBuffer readback = wgpuDeviceCreateBuffer(device, &readback_desc);
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, NULL);
    WGPUCommandBuffer commands = NULL;
    if (encoder) {
        if (view && pipeline && queries) {
            WGPURenderPassColorAttachment attachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
            attachment.view = view;
            attachment.loadOp = WGPULoadOp_Clear;
            attachment.storeOp = WGPUStoreOp_Store;
            WGPURenderPassDescriptor pass_desc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
            pass_desc.colorAttachmentCount = 1;
            pass_desc.colorAttachments = &attachment;
            pass_desc.occlusionQuerySet = queries;
            WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &pass_desc);
            if (pass) {
                wgpuRenderPassEncoderSetPipeline(pass, pipeline);
                wgpuRenderPassEncoderBeginOcclusionQuery(pass, 0);
                wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
                wgpuRenderPassEncoderEndOcclusionQuery(pass);
                if (indirect) wgpuRenderPassEncoderDrawIndirect(pass, indirect, 0);
                wgpuRenderPassEncoderEnd(pass);
                wgpuRenderPassEncoderRelease(pass);
            }
        }
        if (queries && resolved) wgpuCommandEncoderResolveQuerySet(encoder, queries, 0, 1, resolved, 0);
        if (target && readback) {
            WGPUTexelCopyTextureInfo source = {.texture = target};
            WGPUTexelCopyBufferInfo destination = {.buffer = readback, .layout = {.bytesPerRow = 256, .rowsPerImage = 4}};
            WGPUExtent3D extent = {4, 4, 1};
            wgpuCommandEncoderCopyTextureToBuffer(encoder, &source, &destination, &extent);
        }
        if (resolved && readback) wgpuCommandEncoderCopyBufferToBuffer(encoder, resolved, 0, readback, 256 * 4, 8);
        commands = wgpuCommandEncoderFinish(encoder, NULL);
        wgpuCommandEncoderRelease(encoder);
    }
    if (commands) {
        wgpuQueueSubmit(queue, 1, &commands);
        wgpuCommandBufferRelease(commands);
    }
    if (view) wgpuTextureViewRelease(view);
    if (target) wgpuTextureRelease(target);
    if (pipeline) wgpuRenderPipelineRelease(pipeline);
    if (queries) wgpuQuerySetRelease(queries);
    if (indirect) wgpuBufferRelease(indirect);
    if (resolved) wgpuBufferRelease(resolved);
    outcome result = FAILED;
    const uint32_t *output = settle_and_map(instance, queue, readback, 256 * 4 + 8, seen);
    if (output) {
        bool exact = true;
        for (unsigned row = 0; row < 4; ++row)
            for (unsigned x = 0; x < 4; ++x) exact &= output[row * 64 + x] == 0xff0000ffu; /* RGBA8 red */
        exact &= output[256] != 0 || output[257] != 0; /* Samples passed. */
        result = exact ? EXACT : FAILED;
    }
    if (readback) {
        if (seen->map == WGPUMapAsyncStatus_Success) wgpuBufferUnmap(readback);
        wgpuBufferRelease(readback);
    }
    wgpuInstanceProcessEvents(instance);
    return result;
}

/* Compute-program round: one recorded dispatch and copy, prepared as a
 * gpu-recorded program and submitted twice, then read back. */
static outcome program_round(WGPUInstance instance, WGPUDevice device, WGPUQueue queue, observed *seen)
{
    uint32_t input[WORDS];
    for (unsigned i = 0; i < WORDS; ++i) input[i] = i + 2u;
    WGPUBufferDescriptor storage_desc = {.size = BYTES, .usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst};
    WGPUBufferDescriptor readback_desc = {.size = BYTES, .usage = WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst};
    WGPUBuffer storage = wgpuDeviceCreateBuffer(device, &storage_desc);
    WGPUBuffer readback = wgpuDeviceCreateBuffer(device, &readback_desc);
    if (storage) wgpuQueueWriteBuffer(queue, storage, 0, input, sizeof(input));
    WGPUShaderSourceWGSL wgsl = WGPU_SHADER_SOURCE_WGSL_INIT;
    wgsl.code = (WGPUStringView){code, WGPU_STRLEN};
    WGPUShaderModuleDescriptor shader_desc = {.nextInChain = &wgsl.chain};
    WGPUShaderModule shader = wgpuDeviceCreateShaderModule(device, &shader_desc);
    WGPUComputePipeline pipeline = NULL;
    if (shader) {
        WGPUComputePipelineDescriptor descriptor = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
        descriptor.compute.module = shader;
        pipeline = wgpuDeviceCreateComputePipeline(device, &descriptor);
        wgpuShaderModuleRelease(shader);
    }
    WGPUBindGroup group = NULL;
    if (pipeline && storage) {
        WGPUBindGroupLayout layout = wgpuComputePipelineGetBindGroupLayout(pipeline, 0);
        if (layout) {
            WGPUBindGroupEntry entry = {.binding = 0, .buffer = storage, .size = BYTES};
            WGPUBindGroupDescriptor group_desc = {.layout = layout, .entryCount = 1, .entries = &entry};
            group = wgpuDeviceCreateBindGroup(device, &group_desc);
            wgpuBindGroupLayoutRelease(layout);
        }
    }
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, NULL);
    WGPUCommandBuffer commands = NULL;
    if (encoder) {
        if (pipeline && group) {
            WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, NULL);
            if (pass) {
                wgpuComputePassEncoderSetPipeline(pass, pipeline);
                wgpuComputePassEncoderSetBindGroup(pass, 0, group, 0, NULL);
                wgpuComputePassEncoderDispatchWorkgroups(pass, 1, 1, 1);
                wgpuComputePassEncoderEnd(pass);
                wgpuComputePassEncoderRelease(pass);
            }
        }
        if (storage && readback) wgpuCommandEncoderCopyBufferToBuffer(encoder, storage, 0, readback, 0, BYTES);
        commands = wgpuCommandEncoderFinish(encoder, NULL);
        wgpuCommandEncoderRelease(encoder);
    }
    unsigned runs = 0;
    if (commands) {
        void *program = doeNativeComputeProgramPrepare(queue, commands);
        wgpuCommandBufferRelease(commands);
        if (program) {
            runs += doeNativeComputeProgramSubmit(program);
            runs += doeNativeComputeProgramSubmit(program);
            doeNativeComputeProgramRelease(program);
        }
    }
    if (group) wgpuBindGroupRelease(group);
    if (pipeline) wgpuComputePipelineRelease(pipeline);
    if (storage) wgpuBufferRelease(storage);
    outcome result = FAILED;
    const uint32_t *output = settle_and_map(instance, queue, readback, BYTES, seen);
    if (output && runs == 2) {
        bool exact = true;
        for (unsigned i = 0; i < WORDS; ++i) exact &= output[i] == (input[i] * 3u + 1u) * 3u + 1u;
        result = exact ? EXACT : FAILED;
    }
    if (readback) {
        if (seen->map == WGPUMapAsyncStatus_Success) wgpuBufferUnmap(readback);
        wgpuBufferRelease(readback);
    }
    wgpuInstanceProcessEvents(instance);
    return result;
}

static outcome workload(WGPUInstance instance, WGPUDevice device, WGPUQueue queue, observed *seen)
{
    const outcome first = workload_round(instance, device, queue, seen);
    const outcome render = render_round(instance, device, queue, seen);
    const outcome program = program_round(instance, device, queue, seen);
    const outcome second = workload_round(instance, device, queue, seen);
    return first == EXACT && render == EXACT && program == EXACT && second == EXACT ? EXACT : FAILED;
}

typedef struct session {
    WGPUInstance instance;
    WGPUAdapter adapter;
    WGPUDevice device;
    WGPUQueue queue;
    observed seen;
} session;

/* Instance, adapter, device and queue; false when a creation failed, which
 * must have been reported through its callback. */
static bool try_open_session(session *s)
{
    memset(s, 0, sizeof(*s));
    s->seen.done_admitted = true;
    adapter_status = device_status = 0;
    s->instance = wgpuCreateInstance(NULL);
    if (!s->instance) return false;
    WGPURequestAdapterOptions options = WGPU_REQUEST_ADAPTER_OPTIONS_INIT;
    adapter_class_options(&options);
    WGPURequestAdapterCallbackInfo adapter = {.mode = WGPUCallbackMode_AllowSpontaneous, .callback = sweep_adapter, .userdata1 = &s->adapter};
    (void)wgpuInstanceRequestAdapter(s->instance, &options, adapter);
    if (!s->adapter) {
        assert(adapter_status != 0 && adapter_status != WGPURequestAdapterStatus_Success); /* Reported, not dropped. */
        return false;
    }
    WGPUDeviceDescriptor descriptor = WGPU_DEVICE_DESCRIPTOR_INIT;
    descriptor.uncapturedErrorCallbackInfo = (WGPUUncapturedErrorCallbackInfo){.callback = sweep_error, .userdata1 = &s->seen};
    descriptor.deviceLostCallbackInfo = (WGPUDeviceLostCallbackInfo){.mode = WGPUCallbackMode_AllowSpontaneous,
        .callback = lost_reply, .userdata1 = &s->seen};
    WGPURequestDeviceCallbackInfo device = {.mode = WGPUCallbackMode_AllowSpontaneous, .callback = sweep_device, .userdata1 = &s->device};
    (void)wgpuAdapterRequestDevice(s->adapter, &descriptor, device);
    if (!s->device) {
        assert(device_status != 0 && device_status != WGPURequestDeviceStatus_Success);
        return false;
    }
    s->queue = wgpuDeviceGetQueue(s->device);
    return s->queue != NULL;
}
static void abandon_session(session *s)
{
    if (s->queue) wgpuQueueRelease(s->queue);
    if (s->device) wgpuDeviceRelease(s->device);
    if (s->adapter) wgpuAdapterRelease(s->adapter);
    if (s->instance) wgpuInstanceRelease(s->instance);
}

static void open_session(session *s)
{
    memset(s, 0, sizeof(*s));
    s->seen.done_admitted = true;
    s->instance = wgpuCreateInstance(NULL);
    WGPURequestAdapterOptions options = WGPU_REQUEST_ADAPTER_OPTIONS_INIT;
    adapter_class_options(&options);
    WGPURequestAdapterCallbackInfo adapter = {.mode = WGPUCallbackMode_AllowSpontaneous, .callback = sweep_adapter, .userdata1 = &s->adapter};
    assert(wgpuInstanceRequestAdapter(s->instance, &options, adapter).id && s->adapter);
    WGPUDeviceDescriptor descriptor = WGPU_DEVICE_DESCRIPTOR_INIT;
    descriptor.uncapturedErrorCallbackInfo = (WGPUUncapturedErrorCallbackInfo){.callback = sweep_error, .userdata1 = &s->seen};
    descriptor.deviceLostCallbackInfo = (WGPUDeviceLostCallbackInfo){.mode = WGPUCallbackMode_AllowSpontaneous,
        .callback = lost_reply, .userdata1 = &s->seen};
    WGPURequestDeviceCallbackInfo device = {.mode = WGPUCallbackMode_AllowSpontaneous, .callback = sweep_device, .userdata1 = &s->device};
    assert(wgpuAdapterRequestDevice(s->adapter, &descriptor, device).id && s->device);
    s->queue = wgpuDeviceGetQueue(s->device);
}
static void close_session(session *s)
{
    wgpuQueueRelease(s->queue);
    wgpuDeviceRelease(s->device);
    wgpuAdapterRelease(s->adapter);
    wgpuInstanceRelease(s->instance);
}

/* A failure that changed the result must be visible to the application. */
static void check(const session *s, outcome result, bool injected, const char *kind, unsigned index)
{
    const bool reported = atomic_load(&s->seen.errors) || atomic_load(&s->seen.lost) || s->seen.map_failed;
    /* An admitted work-done future is delivered; a refused one or a refused
     * map future is reported as an error, never silently dropped. */
    if (atomic_load(&s->seen.done) != s->seen.done_expected ||
        ((!s->seen.done_admitted || s->seen.map_refused) && !atomic_load(&s->seen.errors))) {
        fprintf(stderr, "%s %u: work done admitted %u delivered %u, map refused %u, errors %u\n", kind, index,
            s->seen.done_admitted, atomic_load(&s->seen.done), s->seen.map_refused, atomic_load(&s->seen.errors));
        abort();
    }
    if (result != EXACT && !(injected && reported)) {
        fprintf(stderr, "%s %u: result %s, injected %u, errors %u, lost %u, map failed %d\n", kind, index,
            result == EXACT ? "exact" : "failed", injected, atomic_load(&s->seen.errors),
            atomic_load(&s->seen.lost), (int)s->seen.map_failed);
        abort();
    }
}

int main(void)
{
    /* Probe: count the workload's host allocations and native calls. */
    session s;
    open_session(&s);
    install(s.device ? ((native_device *)s.device)->backend.value.vulkan.context : NULL);
    atomic_store(&counting, true);
    atomic_store(&native_counting, true);
    assert(workload(s.instance, s.device, s.queue, &s.seen) == EXACT);
    atomic_store(&counting, false);
    atomic_store(&native_counting, false);
    const unsigned total_allocations = atomic_load(&allocations);
    const unsigned total_calls = atomic_load(&native_calls);
    assert(!atomic_load(&s.seen.errors) && total_allocations && total_calls);
    close_session(&s);

    unsigned changed = 0;
    /* Host allocations. The count can vary with progress-worker timing, so
     * sweep past the probe and count only cases whose failure was taken. */
    for (unsigned index = 1; index <= total_allocations + 8; ++index) {
        if (getenv("FAULT_SWEEP_TRACE")) fprintf(stderr, "allocation %u\n", index);
        open_session(&s);
        atomic_store(&allocations, 0);
        atomic_store(&failures, 0);
        atomic_store(&fail_at, index);
        atomic_store(&counting, true);
        const outcome result = workload(s.instance, s.device, s.queue, &s.seen);
        atomic_store(&counting, false);
        check(&s, result, atomic_load(&failures) != 0, "allocation", index);
        changed += result != EXACT;
        close_session(&s);
    }
    const unsigned allocation_changed = changed;
    changed = 0;
    for (unsigned index = 1; index <= total_calls + 4; ++index) {
        if (getenv("FAULT_SWEEP_TRACE")) fprintf(stderr, "native call %u\n", index);
        open_session(&s);
        install(((native_device *)s.device)->backend.value.vulkan.context);
        atomic_store(&native_calls, 0);
        atomic_store(&native_failures, 0);
        atomic_store(&native_fail_at, index);
        atomic_store(&native_counting, true);
        const outcome result = workload(s.instance, s.device, s.queue, &s.seen);
        atomic_store(&native_counting, false);
        check(&s, result, atomic_load(&native_failures) != 0, "native call", index);
        changed += result != EXACT;
        close_session(&s);
    }
    /* Creation: every host allocation from wgpuCreateInstance through
     * requestAdapter, requestDevice and one compute round fails in turn. */
    atomic_store(&allocations, 0);
    atomic_store(&fail_at, 0);
    atomic_store(&counting, true);
    assert(try_open_session(&s));
    atomic_store(&counting, false);
    const unsigned creation_allocations = atomic_load(&allocations);
    abandon_session(&s);
    unsigned creation_failed = 0;
    for (unsigned index = 1; index <= creation_allocations + 8; ++index) {
        if (getenv("FAULT_SWEEP_TRACE")) fprintf(stderr, "creation %u\n", index);
        atomic_store(&allocations, 0);
        atomic_store(&failures, 0);
        atomic_store(&fail_at, index);
        atomic_store(&counting, true);
        const bool opened = try_open_session(&s);
        outcome result = FAILED;
        if (opened) result = workload_round(s.instance, s.device, s.queue, &s.seen);
        atomic_store(&counting, false);
        if (opened) check(&s, result, atomic_load(&failures) != 0, "creation", index);
        creation_failed += !opened;
        abandon_session(&s);
    }
    printf("fault sweep: %u creation allocations (%u failed a creation, each reported), %u host allocations (%u changed the result, each reported), %u native calls (%u changed it, each reported); "
        "every map settled, successful maps exact\n", creation_allocations, creation_failed, total_allocations, allocation_changed, total_calls, changed);
    return 0;
}
