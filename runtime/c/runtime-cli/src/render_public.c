/* SPDX-License-Identifier: MIT
 * Explicit delegate renderer: retained full/render/wgpu_render_commands.zig
 * and wgpu_render_draw_loops.zig. Native proxy rendering stays in render.c. */
#include "render_public.h"

static WGPUTextureView depth_view(doe_runtime *runtime, const doe_render_draw *draw)
{
    public_render_state *state = runtime->public_render;
    if (!state->depth || state->depth_width != draw->target_width || state->depth_height != draw->target_height) {
        WGPUTextureDescriptor descriptor = WGPU_TEXTURE_DESCRIPTOR_INIT;
        descriptor.usage = WGPUTextureUsage_RenderAttachment;
        descriptor.format = WGPUTextureFormat_Depth24PlusStencil8;
        descriptor.size = (WGPUExtent3D){draw->target_width, draw->target_height, 1};
        WGPUTexture replacement = wgpuDeviceCreateTexture(runtime->device, &descriptor);
        if (!replacement) return NULL;
        if (state->depth) wgpuTextureRelease(state->depth);
        state->depth = replacement;
        state->depth_width = draw->target_width;
        state->depth_height = draw->target_height;
    }
    return wgpuTextureCreateView(state->depth, NULL);
}

static WGPURenderBundle encode_bundle(doe_runtime *runtime, const doe_render_draw *draw, WGPURenderPipeline pipeline,
    WGPUBuffer indices, uint64_t index_size, WGPUBuffer arguments, uint32_t stride, uint32_t dynamic_offset)
{
    WGPUTextureFormat format = runtime_webgpu_format(draw->target_format);
    WGPURenderBundleEncoderDescriptor descriptor = WGPU_RENDER_BUNDLE_ENCODER_DESCRIPTOR_INIT;
    descriptor.colorFormatCount = 1;
    descriptor.colorFormats = &format;
    descriptor.depthStencilFormat = WGPUTextureFormat_Depth24PlusStencil8;
    descriptor.depthReadOnly = false;
    descriptor.stencilReadOnly = false;
    WGPURenderBundleEncoder encoder = wgpuDeviceCreateRenderBundleEncoder(runtime->device, &descriptor);
    if (!encoder) return NULL;
    public_render_state *state = runtime->public_render;
    wgpuRenderBundleEncoderSetVertexBuffer(encoder, 0, state->vertex, 0, WGPU_WHOLE_SIZE);
    if (indices) wgpuRenderBundleEncoderSetIndexBuffer(encoder, indices,
        draw->index_uint32 ? WGPUIndexFormat_Uint32 : WGPUIndexFormat_Uint16, 0, index_size);
    if (!draw->redundant_pipeline) wgpuRenderBundleEncoderSetPipeline(encoder, pipeline);
    if (!draw->redundant_bind_group) wgpuRenderBundleEncoderSetBindGroup(encoder, 0, state->group, 1, &dynamic_offset);
    for (uint32_t i = 0; i < draw->draw_count; i++) {
        if (draw->redundant_pipeline) wgpuRenderBundleEncoderSetPipeline(encoder, pipeline);
        if (draw->redundant_bind_group) wgpuRenderBundleEncoderSetBindGroup(encoder, 0, state->group, 1, &dynamic_offset);
        if (arguments) {
            uint64_t offset = (uint64_t)i * stride;
            if (indices) wgpuRenderBundleEncoderDrawIndexedIndirect(encoder, arguments, offset);
            else wgpuRenderBundleEncoderDrawIndirect(encoder, arguments, offset);
        } else if (indices) wgpuRenderBundleEncoderDrawIndexed(encoder, draw->index_count, draw->instance_count,
            draw->first_index, draw->base_vertex, draw->first_instance);
        else wgpuRenderBundleEncoderDraw(encoder, draw->vertex_count, draw->instance_count,
            draw->first_vertex, draw->first_instance);
    }
    WGPURenderBundle bundle = wgpuRenderBundleEncoderFinish(encoder, NULL);
    wgpuRenderBundleEncoderRelease(encoder);
    return bundle;
}

static void encode_pass(WGPURenderPassEncoder pass, const public_render_state *state, const doe_render_draw *draw,
    WGPURenderPipeline pipeline, WGPUBuffer indices, uint64_t index_size, WGPUBuffer arguments, uint32_t stride,
    uint32_t dynamic_offset)
{
    wgpuRenderPassEncoderSetVertexBuffer(pass, 0, state->vertex, 0, WGPU_WHOLE_SIZE);
    if (indices) wgpuRenderPassEncoderSetIndexBuffer(pass, indices,
        draw->index_uint32 ? WGPUIndexFormat_Uint32 : WGPUIndexFormat_Uint16, 0, index_size);
    if (!draw->redundant_pipeline) wgpuRenderPassEncoderSetPipeline(pass, pipeline);
    if (!draw->redundant_bind_group) wgpuRenderPassEncoderSetBindGroup(pass, 0, state->group, 1, &dynamic_offset);
    for (uint32_t i = 0; i < draw->draw_count; i++) {
        if (draw->redundant_pipeline) wgpuRenderPassEncoderSetPipeline(pass, pipeline);
        if (draw->redundant_bind_group) wgpuRenderPassEncoderSetBindGroup(pass, 0, state->group, 1, &dynamic_offset);
        if (arguments) {
            uint64_t offset = (uint64_t)i * stride;
            if (indices) wgpuRenderPassEncoderDrawIndexedIndirect(pass, arguments, offset);
            else wgpuRenderPassEncoderDrawIndirect(pass, arguments, offset);
        } else if (indices) wgpuRenderPassEncoderDrawIndexed(pass, draw->index_count, draw->instance_count,
            draw->first_index, draw->base_vertex, draw->first_instance);
        else wgpuRenderPassEncoderDraw(pass, draw->vertex_count, draw->instance_count, draw->first_vertex, draw->first_instance);
    }
}

bool runtime_execute_public_render(doe_runtime *runtime, const doe_command *command, doe_trace_execution *execution)
{
    const doe_render_draw *draw = &command->as.render;
    uint32_t dynamic_offset = draw->dynamic_offset_count ? draw->dynamic_offsets[0] : 0;
    if (draw->dynamic_offset_count > 1 || dynamic_offset % PUBLIC_RENDER_UNIFORM_STRIDE ||
        (uint64_t)dynamic_offset + PUBLIC_RENDER_BINDING_BYTES > PUBLIC_RENDER_UNIFORM_BYTES)
        return runtime_fail(execution, "render_dynamic_offsets", "public renderer needs one 256-byte-aligned offset within 16384 bytes");
    if ((draw->has_viewport_width && draw->viewport_width <= 0) ||
        (draw->has_viewport_height && draw->viewport_height <= 0) ||
        (draw->has_scissor_width && !draw->scissor_width) || (draw->has_scissor_height && !draw->scissor_height))
        return runtime_fail(execution, "render_viewport_scissor", "public renderer viewport and scissor dimensions must be positive");
    uint64_t start = doe_runtime_monotonic_ns();
    runtime_push_scopes(runtime);
    WGPURenderPipeline pipeline = runtime_public_render_pipeline(runtime, draw->target_format, execution);
    doe_texture_desc target_descriptor = {.handle = draw->target_handle, .width = draw->target_width,
        .height = draw->target_height, .depth = 1, .format = draw->target_format,
        .usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc | WGPUTextureUsage_CopyDst};
    WGPUTexture target = pipeline ? runtime_ensure_texture(runtime, &target_descriptor, execution) : NULL;
    WGPUTexture temporary = NULL;
    bool stage = draw->temporary_render_texture && draw->temporary_render_min_mip == 0 &&
        (draw->target_format == DOE_FORMAT_R8_UNORM || draw->target_format == DOE_FORMAT_RG8_UNORM);
    if (target && stage) {
        /* Public source uses this wrapping handle offset and renders at mip0. */
        target_descriptor.handle += UINT64_C(0xBEEF000000000001);
        temporary = runtime_ensure_texture(runtime, &target_descriptor, execution);
    }
    WGPUTexture attachment = stage ? temporary : target;
    WGPUTextureView view = attachment ? wgpuTextureCreateView(attachment, NULL) : NULL;
    WGPUTextureView depth = view ? depth_view(runtime, draw) : NULL;
    bool ok = pipeline && view && depth;
    if (!ok && execution->ok) runtime_fail(execution, "render_public_target_failed", "public renderer target or depth view was not created");
    uint64_t index_size = 0;
    WGPUBuffer indices = ok && draw->indexed ? runtime_render_indices(runtime, draw, &index_size) : NULL;
    if (ok && draw->indexed && !indices) ok = runtime_fail(execution, "render_index_buffer_failed", "index buffer creation failed");
    bool indirect = command->kind == DOE_COMMAND_DRAW_INDIRECT || command->kind == DOE_COMMAND_DRAW_INDEXED_INDIRECT;
    uint32_t stride = 0;
    WGPUBuffer arguments = ok && indirect ? runtime_render_arguments(runtime, draw, &stride, execution) : NULL;
    if (ok && indirect && !arguments) ok = false;
    uint64_t setup_end = doe_runtime_monotonic_ns(), encode_end = setup_end;
    WGPURenderBundle bundle = ok && !draw->render_pass ? encode_bundle(runtime, draw, pipeline, indices,
        index_size, arguments, stride, dynamic_offset) : NULL;
    if (ok && !draw->render_pass && !bundle) ok = runtime_fail(execution, "render_bundle_failed", "public render bundle creation failed");
    if (ok) {
        WGPURenderPassColorAttachment color = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
        color.view = view;
        color.loadOp = WGPULoadOp_Clear;
        color.storeOp = WGPUStoreOp_Store;
        color.clearValue = (WGPUColor){0, 0, 0, 1};
        WGPURenderPassDepthStencilAttachment depth_attachment = WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
        depth_attachment.view = depth;
        depth_attachment.depthLoadOp = WGPULoadOp_Clear;
        depth_attachment.depthStoreOp = WGPUStoreOp_Store;
        depth_attachment.depthClearValue = 1;
        depth_attachment.stencilLoadOp = WGPULoadOp_Clear;
        depth_attachment.stencilStoreOp = WGPUStoreOp_Store;
        WGPURenderPassDescriptor descriptor = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
        descriptor.colorAttachmentCount = 1;
        descriptor.colorAttachments = &color;
        descriptor.depthStencilAttachment = &depth_attachment;
        WGPURenderPassMaxDrawCount draw_limit = WGPU_RENDER_PASS_MAX_DRAW_COUNT_INIT;
        descriptor.nextInChain = &draw_limit.chain;
        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(runtime->device, NULL);
        WGPURenderPassEncoder pass = encoder ? wgpuCommandEncoderBeginRenderPass(encoder, &descriptor) : NULL;
        if (pass) {
            wgpuRenderPassEncoderSetViewport(pass, draw->viewport_x, draw->viewport_y,
                draw->has_viewport_width ? draw->viewport_width : (float)draw->target_width,
                draw->has_viewport_height ? draw->viewport_height : (float)draw->target_height,
                draw->viewport_min_depth, draw->viewport_max_depth);
            wgpuRenderPassEncoderSetScissorRect(pass, draw->scissor_x, draw->scissor_y,
                draw->has_scissor_width ? draw->scissor_width : draw->target_width,
                draw->has_scissor_height ? draw->scissor_height : draw->target_height);
            if (draw->has_blend_constant) {
                WGPUColor blend = {draw->blend[0], draw->blend[1], draw->blend[2], draw->blend[3]};
                wgpuRenderPassEncoderSetBlendConstant(pass, &blend);
            }
            if (draw->has_stencil_reference) wgpuRenderPassEncoderSetStencilReference(pass, draw->stencil_reference);
            if (bundle) wgpuRenderPassEncoderExecuteBundles(pass, 1, &bundle);
            else encode_pass(pass, runtime->public_render, draw, pipeline, indices, index_size, arguments, stride, dynamic_offset);
            wgpuRenderPassEncoderEnd(pass);
            wgpuRenderPassEncoderRelease(pass);
            if (temporary) runtime_render_copy_back(encoder, temporary, target, draw);
            WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, NULL);
            encode_end = doe_runtime_monotonic_ns();
            ok = runtime_submit(runtime, commands, execution);
            execution->submit_count = 1;
            execution->dispatch_count = draw->draw_count;
        } else ok = runtime_fail(execution, "render_public_pass_failed", "public render pass creation failed");
        if (encoder) wgpuCommandEncoderRelease(encoder);
    }
    uint64_t end = doe_runtime_monotonic_ns();
    if (bundle) wgpuRenderBundleRelease(bundle);
    if (arguments) wgpuBufferRelease(arguments);
    if (indices) wgpuBufferRelease(indices);
    if (depth) wgpuTextureViewRelease(depth);
    if (view) wgpuTextureViewRelease(view);
    if (!runtime_pop_scopes(runtime, execution)) ok = false;
    execution->setup_ns = setup_end - start;
    execution->encode_ns = encode_end - setup_end;
    execution->submit_wait_ns = end - encode_end;
    return ok;
}
