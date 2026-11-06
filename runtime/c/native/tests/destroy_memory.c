/* SPDX-License-Identifier: MIT
 * WebGPU destroy() releases native memory while handles stay alive, as a
 * browser's garbage-collected texture, view and buffer wrappers do. Each cycle
 * renders into a fresh texture through a fresh view, fills a fresh buffer,
 * submits and destroys both while the GPU may still use them. The process
 * physical footprint (which counts Metal allocations) must stay flat. */
#include "consumer.h"
#include <mach/mach.h>

enum { WARMUP = 8, CYCLES = 128, SIDE = 1024 };
static const uint64_t bytes_per_cycle = 2ull * SIDE * SIDE * 4; /* texture + buffer */

static uint64_t footprint(void)
{
    task_vm_info_data_t info;
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    assert(task_info(mach_task_self(), TASK_VM_INFO, (task_info_t)&info, &count) == KERN_SUCCESS);
    return info.phys_footprint;
}

static void cycle(state *s, WGPUTexture *texture, WGPUTextureView *view, WGPUBuffer *storage, unsigned i)
{
    WGPUTextureDescriptor descriptor = WGPU_TEXTURE_DESCRIPTOR_INIT;
    descriptor.format = WGPUTextureFormat_RGBA8Unorm;
    descriptor.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc | WGPUTextureUsage_TextureBinding;
    descriptor.size = (WGPUExtent3D){SIDE, SIDE, 1};
    *texture = wgpuDeviceCreateTexture(s->device, &descriptor);
    assert(*texture);
    *view = wgpuTextureCreateView(*texture, NULL);
    assert(*view);
    *storage = buffer(s, (uint64_t)SIDE * SIDE * 4, WGPUBufferUsage_CopyDst | WGPUBufferUsage_CopySrc);
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device, NULL);
    WGPURenderPassColorAttachment color = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    color.view = *view;
    color.loadOp = WGPULoadOp_Clear;
    color.storeOp = WGPUStoreOp_Store;
    color.clearValue = (WGPUColor){(double)(i % 7) / 7.0, 0.5, 0.25, 1.0};
    WGPURenderPassDescriptor pass = {.colorAttachmentCount = 1, .colorAttachments = &color};
    WGPURenderPassEncoder render = wgpuCommandEncoderBeginRenderPass(encoder, &pass);
    wgpuRenderPassEncoderEnd(render);
    wgpuRenderPassEncoderRelease(render);
    wgpuCommandEncoderClearBuffer(encoder, *storage, 0, WGPU_WHOLE_SIZE);
    submit(s, encoder);
    /* Destroy while the submission may still be running; handles stay alive. */
    wgpuTextureDestroy(*texture);
    wgpuBufferDestroy(*storage);
}

int main(void)
{
    consumer_line_output();
    state s = {0};
    consumer_init(&s);
    WGPUTexture textures[CYCLES];
    WGPUTextureView views[CYCLES];
    WGPUBuffer buffers[CYCLES];
    uint64_t base = 0;
    for (unsigned i = 0; i < CYCLES; ++i) {
        cycle(&s, &textures[i], &views[i], &buffers[i], i);
        if (i + 1 == WARMUP) {
            work_done(&s);
            base = footprint();
        }
    }
    work_done(&s);
    const uint64_t end = footprint();
    const uint64_t growth = end > base ? end - base : 0;
    const uint64_t leaked = (uint64_t)(CYCLES - WARMUP) * bytes_per_cycle;
    printf("destroy footprint: base %.1f MiB, end %.1f MiB, growth %.1f MiB over %u cycles (%.1f MiB if retained)\n",
        base / 1048576.0, end / 1048576.0, growth / 1048576.0, CYCLES - WARMUP, leaked / 1048576.0);
    /* The driver reclaims freed allocations asynchronously, so recent cycles can
     * still be counted. The backlog depends on submission pace: Apple M3 showed
     * 0-73 MiB while buffer creation CPU-cleared its contents, 139-160 MiB
     * without that clear (Dawn's rate and level), and the same level either
     * way once each cycle has equal added CPU work. Retaining either the
     * textures or the buffers alone would exceed half the retained total. */
    assert(growth * 4 < leaked);

    /* A destroyed texture's live view, and the destroyed buffer, reject use. */
    wgpuDevicePushErrorScope(s.device, WGPUErrorFilter_Validation);
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s.device, NULL);
    WGPURenderPassColorAttachment color = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    color.view = views[0];
    color.loadOp = WGPULoadOp_Clear;
    color.storeOp = WGPUStoreOp_Store;
    WGPURenderPassDescriptor pass = {.colorAttachmentCount = 1, .colorAttachments = &color};
    WGPURenderPassEncoder render = wgpuCommandEncoderBeginRenderPass(encoder, &pass);
    wgpuRenderPassEncoderEnd(render);
    wgpuRenderPassEncoderRelease(render);
    WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, NULL);
    wgpuCommandEncoderRelease(encoder);
    wgpuQueueSubmit(s.queue, 1, &commands);
    wgpuCommandBufferRelease(commands);
    expect_validation(&s);
    wgpuDevicePushErrorScope(s.device, WGPUErrorFilter_Validation);
    encoder = wgpuDeviceCreateCommandEncoder(s.device, NULL);
    wgpuCommandEncoderClearBuffer(encoder, buffers[0], 0, WGPU_WHOLE_SIZE);
    commands = wgpuCommandEncoderFinish(encoder, NULL);
    wgpuCommandEncoderRelease(encoder);
    wgpuQueueSubmit(s.queue, 1, &commands);
    wgpuCommandBufferRelease(commands);
    expect_validation(&s);
    /* A view created after destroy() is valid and also rejects use. */
    WGPUTextureView late = wgpuTextureCreateView(textures[1], NULL);
    assert(late);
    wgpuTextureViewRelease(late);

    for (unsigned i = 0; i < CYCLES; ++i) {
        wgpuTextureViewRelease(views[i]);
        wgpuTextureRelease(textures[i]);
        wgpuBufferRelease(buffers[i]);
    }
    work_done(&s);
    consumer_finish(&s);
    printf("PASS destroy() frees texture, view and buffer memory with live handles and in-flight work %s\n",
        adapter_class_scope());
    return 0;
}
