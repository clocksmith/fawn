/* SPDX-License-Identifier: MIT
 * Accepted submissions publish texture state and later submissions start
 * while earlier ones may still run (owner authorized 2026-10-24: async submit
 * A+B, D2, fence-wait completion; futures later; reconfirmed after the section
 * 7 risk review). That is ordered only if every memory-touching command is
 * preceded, within its own command buffer, by a pipeline barrier whose first
 * scope covers all earlier commands (ALL_COMMANDS): a barrier's first scope
 * includes every command earlier in submission order on the queue, earlier
 * batches included. The real context's command table is wrapped to log each
 * command per command buffer while public work of every recording kind runs
 * on software Vulkan. Draws inside a render pass are covered by the barrier
 * before vkCmdBeginRenderPass. */
#include "../src/internal.h"
#include "../../backends/vulkan/src/internal.h"
#include "consumer.h"
#include <string.h>

enum { ALL_COMMANDS = 0x00010000, MAX_BUFFERS = 64 };
typedef struct log_entry { VkCommandBuffer command; bool barrier_since_work; unsigned work; } log_entry;
static log_entry logs[MAX_BUFFERS];
static unsigned violations, works;
static doe_vk_functions saved;

static log_entry *entry(VkCommandBuffer command)
{
    for (unsigned i = 0; i < MAX_BUFFERS; ++i) if (logs[i].command == command) return &logs[i];
    for (unsigned i = 0; i < MAX_BUFFERS; ++i) {
        if (logs[i].command) continue;
        logs[i] = (log_entry){.command = command};
        return &logs[i];
    }
    /* Recycled command buffers reuse their slot below. */
    memset(logs, 0, sizeof(logs));
    logs[0].command = command;
    return &logs[0];
}
static void work(VkCommandBuffer command)
{
    log_entry *log = entry(command);
    if (!log->barrier_since_work) ++violations;
    log->barrier_since_work = false;
    ++log->work;
    ++works;
}
static VkResult cmd_begin(VkCommandBuffer command, const VkCommandBufferBeginInfo *info)
{
    *entry(command) = (log_entry){.command = command}; /* A new recording starts uncovered. */
    return saved.vkBeginCommandBuffer(command, info);
}
static void cmd_barrier(VkCommandBuffer command, VkFlags src, VkFlags dst, VkFlags flags, uint32_t memory,
    const void *memory_barriers, uint32_t buffers, const void *buffer_barriers, uint32_t images, const void *image_barriers)
{
    if (src & ALL_COMMANDS) entry(command)->barrier_since_work = true;
    saved.vkCmdPipelineBarrier(command, src, dst, flags, memory, memory_barriers, buffers, buffer_barriers, images, image_barriers);
}
static void cmd_dispatch(VkCommandBuffer c, uint32_t x, uint32_t y, uint32_t z) { work(c); saved.vkCmdDispatch(c, x, y, z); }
static void dispatch_indirect(VkCommandBuffer c, VkBuffer b, VkDeviceSize o) { work(c); saved.vkCmdDispatchIndirect(c, b, o); }
static void cmd_copy(VkCommandBuffer c, VkBuffer s, VkBuffer d, uint32_t n, const VkBufferCopy *r) { work(c); saved.vkCmdCopyBuffer(c, s, d, n, r); }
static void cmd_fill(VkCommandBuffer c, VkBuffer b, VkDeviceSize o, VkDeviceSize s, uint32_t v) { work(c); saved.vkCmdFillBuffer(c, b, o, s, v); }
static void to_image(VkCommandBuffer c, VkBuffer b, VkImage i, uint32_t l, uint32_t n, const VkBufferImageCopy *r)
{ work(c); saved.vkCmdCopyBufferToImage(c, b, i, l, n, r); }
static void to_buffer(VkCommandBuffer c, VkImage i, uint32_t l, VkBuffer b, uint32_t n, const VkBufferImageCopy *r)
{ work(c); saved.vkCmdCopyImageToBuffer(c, i, l, b, n, r); }
static void image_copy(VkCommandBuffer c, VkImage s, uint32_t sl, VkImage d, uint32_t dl, uint32_t n, const VkImageCopy *r)
{ work(c); saved.vkCmdCopyImage(c, s, sl, d, dl, n, r); }
static void begin_pass(VkCommandBuffer c, const VkRenderPassBeginInfo *info, uint32_t contents)
{ work(c); saved.vkCmdBeginRenderPass(c, info, contents); }
static void query_copy(VkCommandBuffer c, VkQueryPool p, uint32_t f, uint32_t n, VkBuffer b, VkDeviceSize o, VkDeviceSize s, VkFlags flags)
{ work(c); saved.vkCmdCopyQueryPoolResults(c, p, f, n, b, o, s, flags); }

static void wrap(native_device *device)
{
    doe_vk_context *context = device->backend.value.vulkan.context;
    saved = context->vk;
    context->vk.vkBeginCommandBuffer = cmd_begin;
    context->vk.vkCmdPipelineBarrier = cmd_barrier;
    context->vk.vkCmdDispatch = cmd_dispatch;
    context->vk.vkCmdDispatchIndirect = dispatch_indirect;
    context->vk.vkCmdCopyBuffer = cmd_copy;
    context->vk.vkCmdFillBuffer = cmd_fill;
    context->vk.vkCmdCopyBufferToImage = to_image;
    context->vk.vkCmdCopyImageToBuffer = to_buffer;
    context->vk.vkCmdCopyImage = image_copy;
    context->vk.vkCmdBeginRenderPass = begin_pass;
    context->vk.vkCmdCopyQueryPoolResults = query_copy;
}

static WGPURenderPipeline render_pipeline(state *s)
{
    WGPUShaderSourceWGSL source = WGPU_SHADER_SOURCE_WGSL_INIT;
    source.code = (WGPUStringView){
        "@vertex fn vertex(@builtin(vertex_index) i: u32) -> @builtin(position) vec4f {"
        "if (i == 0u) { return vec4f(-1.0, -1.0, 0.0, 1.0); }"
        "if (i == 1u) { return vec4f(3.0, -1.0, 0.0, 1.0); }"
        "return vec4f(-1.0, 3.0, 0.0, 1.0); }"
        "@fragment fn fragment() -> @location(0) vec4f { return vec4f(1.0, 0.0, 0.0, 1.0); }", WGPU_STRLEN};
    WGPUShaderModuleDescriptor module_desc = {.nextInChain = &source.chain};
    WGPUShaderModule module = wgpuDeviceCreateShaderModule(s->device, &module_desc);
    assert(module);
    WGPUColorTargetState color = {.format = WGPUTextureFormat_RGBA8Unorm, .writeMask = WGPUColorWriteMask_All};
    WGPUFragmentState fragment = {.module = module, .targetCount = 1, .targets = &color};
    WGPURenderPipelineDescriptor descriptor = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    descriptor.vertex.module = module;
    descriptor.fragment = &fragment;
    WGPURenderPipeline result = wgpuDeviceCreateRenderPipeline(s->device, &descriptor);
    wgpuShaderModuleRelease(module);
    assert(result);
    return result;
}

int main(void)
{
    state s = {0};
    WGPUFeatureName timestamps = WGPUFeatureName_TimestampQuery;
    consumer_init_features(&s, &timestamps, 1);
    wrap((native_device *)s.device);
    const uint32_t words[4] = {1, 2, 3, 4};
    WGPUBuffer source = buffer(&s, 256, WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst);
    WGPUBuffer target = buffer(&s, 256, WGPUBufferUsage_CopyDst | WGPUBufferUsage_CopySrc | WGPUBufferUsage_Storage);
    WGPUBuffer indirect = buffer(&s, 32, WGPUBufferUsage_Indirect | WGPUBufferUsage_CopyDst);
    wgpuQueueWriteBuffer(s.queue, source, 0, words, sizeof(words));
    const uint32_t groups[4] = {1, 1, 1, 0}, draw_args[4] = {3, 1, 0, 0};
    wgpuQueueWriteBuffer(s.queue, indirect, 0, groups, sizeof(groups));
    wgpuQueueWriteBuffer(s.queue, indirect, 16, draw_args, sizeof(draw_args));

    /* Buffer copy and clear. */
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s.device, NULL);
    wgpuCommandEncoderCopyBufferToBuffer(encoder, source, 0, target, 0, 16);
    wgpuCommandEncoderClearBuffer(encoder, target, 16, 16);
    submit(&s, encoder);

    /* Direct and indirect dispatch. */
    WGPUComputePipeline compute = pipeline(&s,
        "@group(0) @binding(0) var<storage, read_write> data: array<u32>;"
        "@compute @workgroup_size(1) fn main() { data[0] = data[0] + 1u; }");
    WGPUBindGroupEntry binding = {.binding = 0, .buffer = target, .size = 256};
    WGPUBindGroup bound = group(&s, compute, &binding, 1);
    encoder = wgpuDeviceCreateCommandEncoder(s.device, NULL);
    WGPUComputePassEncoder compute_pass = wgpuCommandEncoderBeginComputePass(encoder, NULL);
    wgpuComputePassEncoderSetPipeline(compute_pass, compute);
    wgpuComputePassEncoderSetBindGroup(compute_pass, 0, bound, 0, NULL);
    wgpuComputePassEncoderDispatchWorkgroups(compute_pass, 1, 1, 1);
    wgpuComputePassEncoderDispatchWorkgroupsIndirect(compute_pass, indirect, 0);
    wgpuComputePassEncoderEnd(compute_pass);
    wgpuComputePassEncoderRelease(compute_pass);
    submit(&s, encoder);

    /* Render pass with direct and indirect draws into a fresh texture. */
    WGPUTextureDescriptor texture_desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    texture_desc.format = WGPUTextureFormat_RGBA8Unorm;
    texture_desc.size = (WGPUExtent3D){4, 4, 1};
    texture_desc.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc | WGPUTextureUsage_CopyDst;
    WGPUTexture color = wgpuDeviceCreateTexture(s.device, &texture_desc);
    WGPUTexture other = wgpuDeviceCreateTexture(s.device, &texture_desc);
    assert(color && other);
    WGPUTextureView view = wgpuTextureCreateView(color, NULL);
    WGPURenderPipeline render = render_pipeline(&s);
    WGPURenderPassColorAttachment attachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    attachment.view = view;
    attachment.loadOp = WGPULoadOp_Clear;
    attachment.storeOp = WGPUStoreOp_Store;
    WGPURenderPassDescriptor pass_desc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
    pass_desc.colorAttachmentCount = 1;
    pass_desc.colorAttachments = &attachment;
    encoder = wgpuDeviceCreateCommandEncoder(s.device, NULL);
    WGPURenderPassEncoder render_pass = wgpuCommandEncoderBeginRenderPass(encoder, &pass_desc);
    wgpuRenderPassEncoderSetPipeline(render_pass, render);
    wgpuRenderPassEncoderDraw(render_pass, 3, 1, 0, 0);
    wgpuRenderPassEncoderDrawIndirect(render_pass, indirect, 16);
    wgpuRenderPassEncoderEnd(render_pass);
    wgpuRenderPassEncoderRelease(render_pass);
    submit(&s, encoder);

    /* Texture uploads and copies: queue write, texture to buffer, buffer to
     * texture and texture to texture. */
    WGPUTexelCopyTextureInfo color_info = {.texture = color, .aspect = WGPUTextureAspect_All};
    WGPUTexelCopyTextureInfo other_info = {.texture = other, .aspect = WGPUTextureAspect_All};
    WGPUTexelCopyBufferLayout layout = {.bytesPerRow = 256, .rowsPerImage = 1};
    WGPUExtent3D one = {1, 1, 1};
    wgpuQueueWriteTexture(s.queue, &other_info, words, sizeof(words), &layout, &one);
    WGPUBuffer readback = buffer(&s, 256 * 4, WGPUBufferUsage_CopyDst | WGPUBufferUsage_CopySrc);
    encoder = wgpuDeviceCreateCommandEncoder(s.device, NULL);
    texture_to_buffer(encoder, color, readback, 0, 4, 4);
    WGPUTexelCopyBufferInfo from_buffer = {.buffer = readback, .layout = {.bytesPerRow = 256, .rowsPerImage = 4}};
    WGPUExtent3D whole = {4, 4, 1};
    wgpuCommandEncoderCopyBufferToTexture(encoder, &from_buffer, &other_info, &whole);
    wgpuCommandEncoderCopyTextureToTexture(encoder, &other_info, &color_info, &whole);
    submit(&s, encoder);

    /* Timestamp resolve (the normalizer's private dispatch and copies). */
    WGPUQuerySetDescriptor query_desc = {.type = WGPUQueryType_Timestamp, .count = 2};
    WGPUQuerySet queries = wgpuDeviceCreateQuerySet(s.device, &query_desc);
    WGPUBuffer resolved = buffer(&s, 256, WGPUBufferUsage_QueryResolve | WGPUBufferUsage_CopySrc);
    encoder = wgpuDeviceCreateCommandEncoder(s.device, NULL);
    wgpuCommandEncoderWriteTimestamp(encoder, queries, 0);
    wgpuCommandEncoderWriteTimestamp(encoder, queries, 1);
    wgpuCommandEncoderResolveQuerySet(encoder, queries, 0, 2, resolved, 0);
    submit(&s, encoder);

    assert(works >= 12);
    printf("PASS barrier coverage: %u memory-touching commands (copy, clear, dispatch, indirect dispatch, render pass with direct and indirect draws, texture write/copies, timestamp resolve, texture initialization) each preceded in their command buffer by an ALL_COMMANDS barrier, %u uncovered %s\n",
        works, violations, adapter_class_scope());
    assert(violations == 0);
    wgpuQuerySetRelease(queries);
    wgpuBufferRelease(resolved);
    wgpuBufferRelease(readback);
    wgpuRenderPipelineRelease(render);
    wgpuTextureViewRelease(view);
    wgpuTextureRelease(color);
    wgpuTextureRelease(other);
    wgpuBindGroupRelease(bound);
    wgpuComputePipelineRelease(compute);
    wgpuBufferRelease(indirect);
    wgpuBufferRelease(target);
    wgpuBufferRelease(source);
    consumer_finish(&s);
}
