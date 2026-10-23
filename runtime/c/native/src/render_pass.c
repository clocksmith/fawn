/* SPDX-License-Identifier: MIT */
#include "render_internal.h"

WGPURenderPassEncoder wgpuCommandEncoderBeginRenderPass(WGPUCommandEncoder handle,
    const WGPURenderPassDescriptor *descriptor)
{
    doe_resource_encoder *encoder = (doe_resource_encoder *)handle;
    native_device *device = doe_resource_encoder_device(encoder);
    if (!device) return NULL;
    /* Slot count beyond the device limit is canonical resource admission. */
    bool valid = descriptor && descriptor->colorAttachmentCount <= DOE_RESOURCE_MAX_COLOR_ATTACHMENTS &&
        (!descriptor->colorAttachmentCount || descriptor->colorAttachments);
    doe_resource_status rejected = valid || !descriptor || descriptor->colorAttachmentCount <= DOE_RESOURCE_MAX_COLOR_ATTACHMENTS ?
        DOE_RESOURCE_UNSUPPORTED : DOE_RESOURCE_RANGE;
    const WGPURenderPassMaxDrawCount *budget = NULL;
    if (valid && descriptor->nextInChain) {
        const WGPUChainedStruct *chain = descriptor->nextInChain;
        valid = chain->sType == WGPUSType_RenderPassMaxDrawCount && !chain->next;
        if (valid) budget = (const WGPURenderPassMaxDrawCount *)chain;
    }
    doe_resource_render_targets targets = {0};
    const WGPUPassTimestampWrites *writes = valid ? descriptor->timestampWrites : NULL;
    doe_resource_pass_timestamps timestamps = {0};
    if (valid) {
        targets.occlusion_queries = (doe_resource_query_set *)descriptor->occlusionQuerySet;
        targets.color_count = (uint32_t)descriptor->colorAttachmentCount;
        for (uint32_t i = 0; valid && i < targets.color_count; ++i) {
            const WGPURenderPassColorAttachment *color = &descriptor->colorAttachments[i];
            if (!color->view) {
                /* WebGPU null slot: no operations or resolve, as resource admission checks. */
                valid = !color->nextInChain && !color->resolveTarget;
                continue;
            }
            const doe_resource_texture_desc *parent = doe_resource_texture_description(
                doe_resource_view_texture((doe_resource_view *)color->view));
            valid = !color->nextInChain && color->depthSlice == WGPU_DEPTH_SLICE_UNDEFINED &&
                (color->storeOp == WGPUStoreOp_Store ||
                 (parent && parent->samples > 1 && color->storeOp == WGPUStoreOp_Discard));
            targets.colors[i] = (doe_resource_color_attachment){.view=(doe_resource_view *)color->view,
                .resolve_target=(doe_resource_view *)color->resolveTarget,.load=color->loadOp,.store=color->storeOp,
                .clear={color->clearValue.r,color->clearValue.g,color->clearValue.b,color->clearValue.a}};
            const doe_resource_view_desc *view = doe_resource_view_description(targets.colors[i].view);
            doe_vk_clear_color converted;
            if (valid && color->loadOp == WGPULoadOp_Clear &&
                (!view || !native_render_clear(view->format, targets.colors[i].clear, &converted))) valid = false;
        }
        if (descriptor->depthStencilAttachment) {
            const WGPURenderPassDepthStencilAttachment *depth = descriptor->depthStencilAttachment;
            if (depth->nextInChain) valid = false;
            targets.depth_stencil = (doe_resource_view *)depth->view;
            targets.depth_load = depth->depthLoadOp;
            targets.depth_store = depth->depthStoreOp;
            targets.clear_depth = depth->depthClearValue;
            targets.depth_read_only = depth->depthReadOnly;
            targets.stencil_load = depth->stencilLoadOp;
            targets.stencil_store = depth->stencilStoreOp;
            targets.clear_stencil = depth->stencilClearValue;
            targets.stencil_read_only = depth->stencilReadOnly;
            if (!targets.depth_stencil) valid = false;
        }
        if (budget) {
            targets.has_max_draw_count = true;
            targets.max_draw_count = budget->maxDrawCount;
        }
        if (writes) {
            if (writes->nextInChain || !device->timestamp_enabled) valid = false;
            timestamps = (doe_resource_pass_timestamps){(doe_resource_query_set *)writes->querySet,
                writes->beginningOfPassWriteIndex,writes->endOfPassWriteIndex};
        }
    }
    doe_resource_render_pass *pass = NULL;
    native_lock(device);
    doe_resource_status status = valid ? doe_resource_encoder_begin_render_with_timestamps(encoder,
        &targets, writes ? &timestamps : NULL, &pass) : doe_resource_encoder_invalidate(encoder, rejected);
    native_unlock(device);
    if (status != DOE_RESOURCE_OK) native_error(device, native_resource_error(status), "render pass descriptor invalid or unsupported");
    if (pass) native_label(device, pass, descriptor->label);
    return (WGPURenderPassEncoder)pass;
}
void wgpuRenderPassEncoderAddRef(WGPURenderPassEncoder pass) { doe_resource_render_pass_retain((doe_resource_render_pass *)pass); }
void wgpuRenderPassEncoderRelease(WGPURenderPassEncoder handle)
{
    doe_resource_render_pass *pass = (doe_resource_render_pass *)handle;
    native_device *device = doe_resource_render_pass_device(pass);
    if (!device) return;
    native_device_retain(device);
    native_lock(device);
    doe_resource_render_pass_release(pass);
    native_unlock(device);
    native_device_release(device);
}
void wgpuRenderPassEncoderSetLabel(WGPURenderPassEncoder pass, WGPUStringView label)
{
    native_label(doe_resource_render_pass_device((doe_resource_render_pass *)pass), pass, label);
}
void wgpuRenderPassEncoderSetPipeline(WGPURenderPassEncoder handle, WGPURenderPipeline pipeline)
{
    doe_resource_render_pass *pass = (doe_resource_render_pass *)handle;
    native_device *device = doe_resource_render_pass_device(pass);
    if (!device) return;
    native_lock(device);
    doe_resource_status status = doe_resource_render_pass_set_pipeline(pass, (doe_resource_render_pipeline *)pipeline);
    native_unlock(device);
    if (status != DOE_RESOURCE_OK) native_error(device, native_resource_error(status), "render pipeline binding failed");
}
void wgpuRenderPassEncoderSetBindGroup(WGPURenderPassEncoder handle, uint32_t index,
    WGPUBindGroup group, size_t count, const uint32_t *offsets)
{
    doe_resource_render_pass *pass = (doe_resource_render_pass *)handle;
    native_device *device = doe_resource_render_pass_device(pass);
    if (!device) return;
    doe_resource_dispatch_group binding = {(doe_resource_bind_group *)group,offsets,count};
    native_lock(device);
    doe_resource_status status = doe_resource_render_pass_set_group(pass,index,&binding);
    native_unlock(device);
    if (status != DOE_RESOURCE_OK) native_error(device, native_resource_error(status), "render bind group binding failed");
}
void wgpuRenderPassEncoderSetVertexBuffer(WGPURenderPassEncoder handle, uint32_t slot,
    WGPUBuffer buffer, uint64_t offset, uint64_t size)
{
    doe_resource_render_pass *pass = (doe_resource_render_pass *)handle;
    native_device *device = doe_resource_render_pass_device(pass);
    if (!device) return;
    native_lock(device);
    doe_resource_status status = doe_resource_render_pass_set_vertex(pass,slot,(doe_resource_buffer *)buffer,offset,size);
    native_unlock(device);
    if (status != DOE_RESOURCE_OK) native_error(device, native_resource_error(status), "render vertex binding failed");
}
void wgpuRenderPassEncoderSetIndexBuffer(WGPURenderPassEncoder handle, WGPUBuffer buffer,
    WGPUIndexFormat format, uint64_t offset, uint64_t size)
{
    doe_resource_render_pass *pass = (doe_resource_render_pass *)handle;
    native_device *device = doe_resource_render_pass_device(pass);
    if (!device) return;
    native_lock(device);
    doe_resource_status status = doe_resource_render_pass_set_index(pass,(doe_resource_buffer *)buffer,format,offset,size);
    native_unlock(device);
    if (status != DOE_RESOURCE_OK) native_error(device, native_resource_error(status), "render index binding failed");
}
void wgpuRenderPassEncoderSetViewport(WGPURenderPassEncoder handle, float x, float y, float width,
    float height, float minimum, float maximum)
{
    doe_resource_render_pass *pass = (doe_resource_render_pass *)handle;
    native_device *device = doe_resource_render_pass_device(pass);
    if (!device) return;
    native_lock(device);
    doe_resource_render_state state = *doe_resource_render_pass_state(pass);
    state.viewport[0] = x;
    state.viewport[1] = y;
    state.viewport[2] = width;
    state.viewport[3] = height;
    state.viewport[4] = minimum;
    state.viewport[5] = maximum;
    doe_resource_status status = doe_resource_render_pass_set_state(pass,&state);
    native_unlock(device);
    if (status != DOE_RESOURCE_OK) native_error(device, native_resource_error(status), "render viewport failed");
}
void wgpuRenderPassEncoderSetScissorRect(WGPURenderPassEncoder handle, uint32_t x, uint32_t y, uint32_t width, uint32_t height)
{
    doe_resource_render_pass *pass = (doe_resource_render_pass *)handle;
    native_device *device = doe_resource_render_pass_device(pass);
    if (!device) return;
    native_lock(device);
    doe_resource_render_state state = *doe_resource_render_pass_state(pass);
    state.scissor[0] = x;
    state.scissor[1] = y;
    state.scissor[2] = width;
    state.scissor[3] = height;
    doe_resource_status status = doe_resource_render_pass_set_state(pass,&state);
    native_unlock(device);
    if (status != DOE_RESOURCE_OK) native_error(device, native_resource_error(status), "render scissor failed");
}
void wgpuRenderPassEncoderSetBlendConstant(WGPURenderPassEncoder handle, const WGPUColor *color)
{
    doe_resource_render_pass *pass = (doe_resource_render_pass *)handle;
    native_device *device = doe_resource_render_pass_device(pass);
    if (!device) return;
    native_lock(device);
    doe_resource_render_state state = *doe_resource_render_pass_state(pass);
    if (color) {
        state.blend[0] = color->r;
        state.blend[1] = color->g;
        state.blend[2] = color->b;
        state.blend[3] = color->a;
    }
    doe_resource_status status = color ? doe_resource_render_pass_set_state(pass,&state) :
        doe_resource_render_pass_set_state(pass,NULL);
    native_unlock(device);
    if (status != DOE_RESOURCE_OK) native_error(device, native_resource_error(status), "render blend constant failed");
}
void wgpuRenderPassEncoderSetStencilReference(WGPURenderPassEncoder handle, uint32_t reference)
{
    doe_resource_render_pass *pass = (doe_resource_render_pass *)handle;
    native_device *device = doe_resource_render_pass_device(pass);
    if (!device) return;
    native_lock(device);
    doe_resource_render_state state = *doe_resource_render_pass_state(pass);
    state.stencil_reference = reference;
    doe_resource_status status = doe_resource_render_pass_set_state(pass,&state);
    native_unlock(device);
    if (status != DOE_RESOURCE_OK) native_error(device, native_resource_error(status), "render stencil reference failed");
}
void wgpuRenderPassEncoderDraw(WGPURenderPassEncoder handle, uint32_t count,
    uint32_t instances, uint32_t first, uint32_t first_instance)
{
    doe_resource_render_pass *pass = (doe_resource_render_pass *)handle;
    native_device *device = doe_resource_render_pass_device(pass);
    if (!device) return;
    native_lock(device);
    doe_resource_status status = doe_resource_render_pass_draw(pass,count,instances,first,first_instance);
    native_unlock(device);
    if (status != DOE_RESOURCE_OK) native_error(device, native_resource_error(status), "render draw failed");
}
void wgpuRenderPassEncoderDrawIndexed(WGPURenderPassEncoder handle, uint32_t count,
    uint32_t instances, uint32_t first_index, int32_t base_vertex, uint32_t first_instance)
{
    doe_resource_render_pass *pass = (doe_resource_render_pass *)handle;
    native_device *device = doe_resource_render_pass_device(pass);
    if (!device) return;
    native_lock(device);
    doe_resource_status status = doe_resource_render_pass_draw_indexed(pass,count,instances,first_index,base_vertex,first_instance);
    native_unlock(device);
    if (status != DOE_RESOURCE_OK) native_error(device, native_resource_error(status), "render indexed draw failed");
}
void wgpuRenderPassEncoderBeginOcclusionQuery(WGPURenderPassEncoder handle, uint32_t index)
{
    doe_resource_render_pass *pass = (doe_resource_render_pass *)handle;
    native_device *device = doe_resource_render_pass_device(pass);
    if (!device) return;
    native_lock(device);
    doe_resource_status status = doe_resource_render_pass_begin_occlusion(pass,index);
    native_unlock(device);
    if (status != DOE_RESOURCE_OK) native_error(device, native_resource_error(status), "occlusion begin failed");
}
void wgpuRenderPassEncoderEndOcclusionQuery(WGPURenderPassEncoder handle)
{
    doe_resource_render_pass *pass = (doe_resource_render_pass *)handle;
    native_device *device = doe_resource_render_pass_device(pass);
    if (!device) return;
    native_lock(device);
    doe_resource_status status = doe_resource_render_pass_end_occlusion(pass);
    native_unlock(device);
    if (status != DOE_RESOURCE_OK) native_error(device, native_resource_error(status), "occlusion end failed");
}
void wgpuRenderPassEncoderEnd(WGPURenderPassEncoder handle)
{
    doe_resource_render_pass *pass = (doe_resource_render_pass *)handle;
    native_device *device = doe_resource_render_pass_device(pass);
    if (!device) return;
    native_lock(device);
    doe_resource_status status = doe_resource_render_pass_end(pass);
    native_unlock(device);
    if (status != DOE_RESOURCE_OK) native_error(device, native_resource_error(status), "render pass end failed");
}
