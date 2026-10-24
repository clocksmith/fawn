/* SPDX-License-Identifier: MIT */
#include "internal.h"

WGPURenderBundleEncoder wgpuDeviceCreateRenderBundleEncoder(WGPUDevice handle,
    const WGPURenderBundleEncoderDescriptor *descriptor)
{
    native_device *device = (native_device *)handle;
    if (!device) return NULL;
    bool valid = descriptor && !descriptor->nextInChain && descriptor->colorFormatCount <= DOE_RESOURCE_MAX_COLOR_ATTACHMENTS &&
        (!descriptor->colorFormatCount || descriptor->colorFormats) &&
        (descriptor->sampleCount == 1 || descriptor->sampleCount == 4);
    doe_vk_texture_format depth;
    if (valid && descriptor->depthStencilFormat != WGPUTextureFormat_Undefined &&
        !native_depth_format(device->features, descriptor->depthStencilFormat, &depth)) valid = false;
    doe_resource_render_bundle_desc converted = {0};
    if (valid) {
        converted = (doe_resource_render_bundle_desc){.depth_stencil_format=descriptor->depthStencilFormat,
            .samples=descriptor->sampleCount,.depth_read_only=descriptor->depthReadOnly,
            .stencil_read_only=descriptor->stencilReadOnly,.color_count=(uint32_t)descriptor->colorFormatCount};
        /* Undefined entries are unused slots; formats are admitted by the resource owner. */
        for (size_t i = 0; i < descriptor->colorFormatCount; ++i) {
            converted.color_formats[i] = descriptor->colorFormats[i];
            if (!native_color_renderable(device->features, descriptor->colorFormats[i])) valid = false;
        }
    }
    doe_resource_bundle_encoder *encoder = NULL;
    native_lock(device);
    doe_resource_status status = valid ? doe_resource_bundle_encoder_create(&device->resources,&converted,&encoder) :
        descriptor && descriptor->colorFormatCount > DOE_RESOURCE_MAX_COLOR_ATTACHMENTS ? DOE_RESOURCE_RANGE : DOE_RESOURCE_UNSUPPORTED;
    /* A rejected descriptor reports here and still returns a WebGPU invalid
     * encoder: its commands record nothing and its Finish reports and yields an
     * invalid bundle. */
    if (status != DOE_RESOURCE_OK) encoder = doe_resource_bundle_encoder_rejected(&device->resources, status);
    native_unlock(device);
    if (status != DOE_RESOURCE_OK) native_error(device, native_resource_error(status), "render bundle descriptor unsupported or invalid");
    if (encoder && descriptor) native_label(device, encoder, descriptor->label);
    return (WGPURenderBundleEncoder)encoder;
}
void wgpuRenderBundleEncoderAddRef(WGPURenderBundleEncoder encoder) { doe_resource_bundle_encoder_retain((doe_resource_bundle_encoder *)encoder); }
void wgpuRenderBundleEncoderRelease(WGPURenderBundleEncoder handle)
{
    doe_resource_bundle_encoder *encoder = (doe_resource_bundle_encoder *)handle;
    native_device *device = doe_resource_bundle_encoder_device(encoder);
    if (!device) return;
    native_device_retain(device);
    native_lock(device);
    doe_resource_bundle_encoder_release(encoder);
    native_unlock(device);
    native_device_release(device);
}
void wgpuRenderBundleEncoderSetLabel(WGPURenderBundleEncoder encoder, WGPUStringView label)
{
    native_label(doe_resource_bundle_encoder_device((doe_resource_bundle_encoder *)encoder), encoder, label);
}
void wgpuRenderBundleEncoderSetPipeline(WGPURenderBundleEncoder handle, WGPURenderPipeline pipeline)
{
    doe_resource_bundle_encoder *encoder = (doe_resource_bundle_encoder *)handle;
    native_device *device = doe_resource_bundle_encoder_device(encoder);
    if (!device) return;
    native_lock(device);
    const bool ended = doe_resource_bundle_encoder_ended(encoder);
    doe_resource_status status = doe_resource_bundle_encoder_set_pipeline(encoder,(doe_resource_render_pipeline *)pipeline);
    native_unlock(device);
    native_encoding_error(device, ended, status, "bundle pipeline binding failed");
}
void wgpuRenderBundleEncoderSetBindGroup(WGPURenderBundleEncoder handle, uint32_t index,
    WGPUBindGroup group, size_t count, const uint32_t *offsets)
{
    doe_resource_bundle_encoder *encoder = (doe_resource_bundle_encoder *)handle;
    native_device *device = doe_resource_bundle_encoder_device(encoder);
    if (!device) return;
    doe_resource_dispatch_group binding = {(doe_resource_bind_group *)group,offsets,count};
    native_lock(device);
    const bool ended = doe_resource_bundle_encoder_ended(encoder);
    doe_resource_status status = doe_resource_bundle_encoder_set_group(encoder,index,&binding);
    native_unlock(device);
    native_encoding_error(device, ended, status, "bundle group binding failed");
}
void wgpuRenderBundleEncoderSetVertexBuffer(WGPURenderBundleEncoder handle, uint32_t slot,
    WGPUBuffer buffer, uint64_t offset, uint64_t size)
{
    doe_resource_bundle_encoder *encoder = (doe_resource_bundle_encoder *)handle;
    native_device *device = doe_resource_bundle_encoder_device(encoder);
    if (!device) return;
    native_lock(device);
    const bool ended = doe_resource_bundle_encoder_ended(encoder);
    doe_resource_status status = doe_resource_bundle_encoder_set_vertex(encoder,slot,(doe_resource_buffer *)buffer,offset,size);
    native_unlock(device);
    native_encoding_error(device, ended, status, "bundle vertex binding failed");
}
void wgpuRenderBundleEncoderSetIndexBuffer(WGPURenderBundleEncoder handle, WGPUBuffer buffer,
    WGPUIndexFormat format, uint64_t offset, uint64_t size)
{
    doe_resource_bundle_encoder *encoder = (doe_resource_bundle_encoder *)handle;
    native_device *device = doe_resource_bundle_encoder_device(encoder);
    if (!device) return;
    native_lock(device);
    const bool ended = doe_resource_bundle_encoder_ended(encoder);
    doe_resource_status status = doe_resource_bundle_encoder_set_index(encoder,(doe_resource_buffer *)buffer,format,offset,size);
    native_unlock(device);
    native_encoding_error(device, ended, status, "bundle index binding failed");
}
void wgpuRenderBundleEncoderDrawIndexed(WGPURenderBundleEncoder handle, uint32_t count,
    uint32_t instances, uint32_t first_index, int32_t base_vertex, uint32_t first_instance)
{
    doe_resource_bundle_encoder *encoder = (doe_resource_bundle_encoder *)handle;
    native_device *device = doe_resource_bundle_encoder_device(encoder);
    if (!device) return;
    native_lock(device);
    const bool ended = doe_resource_bundle_encoder_ended(encoder);
    doe_resource_status status = doe_resource_bundle_encoder_draw(encoder,true,count,instances,first_index,base_vertex,first_instance);
    native_unlock(device);
    native_encoding_error(device, ended, status, "bundle indexed draw failed");
}
void wgpuRenderBundleEncoderDraw(WGPURenderBundleEncoder handle, uint32_t count,
    uint32_t instances, uint32_t first, uint32_t first_instance)
{
    doe_resource_bundle_encoder *encoder = (doe_resource_bundle_encoder *)handle;
    native_device *device = doe_resource_bundle_encoder_device(encoder);
    if (!device) return;
    native_lock(device);
    const bool ended = doe_resource_bundle_encoder_ended(encoder);
    doe_resource_status status = doe_resource_bundle_encoder_draw(encoder,false,count,instances,first,0,first_instance);
    native_unlock(device);
    native_encoding_error(device, ended, status, "bundle draw failed");
}
WGPURenderBundle wgpuRenderBundleEncoderFinish(WGPURenderBundleEncoder handle, const WGPURenderBundleDescriptor *descriptor)
{
    doe_resource_bundle_encoder *encoder = (doe_resource_bundle_encoder *)handle;
    native_device *device = doe_resource_bundle_encoder_device(encoder);
    if (!device) return NULL;
    doe_resource_render_bundle *bundle = NULL;
    native_lock(device);
    if (descriptor && descriptor->nextInChain) (void)doe_resource_bundle_encoder_invalidate(encoder,DOE_RESOURCE_UNSUPPORTED);
    /* Finish always ends the encoder. A failed finish (including on an encoder
     * from a rejected descriptor: "this must be valid") reports its error and
     * returns a WebGPU invalid bundle; executing that bundle invalidates the
     * pass's command encoder, whose Finish reports it. */
    doe_resource_status status = doe_resource_bundle_encoder_finish(encoder,&bundle);
    if (status != DOE_RESOURCE_OK) bundle = doe_resource_render_bundle_rejected(encoder);
    native_unlock(device);
    if (status != DOE_RESOURCE_OK)
        native_error(device, native_resource_error(status), "render bundle encoder invalid: an encoded command or the finish was rejected");
    if (bundle && descriptor) native_label(device, bundle, descriptor->label);
    return (WGPURenderBundle)bundle;
}
void wgpuRenderBundleAddRef(WGPURenderBundle bundle) { doe_resource_render_bundle_retain((doe_resource_render_bundle *)bundle); }
void wgpuRenderBundleRelease(WGPURenderBundle bundle) { doe_resource_render_bundle_release((doe_resource_render_bundle *)bundle); }
void wgpuRenderBundleSetLabel(WGPURenderBundle bundle, WGPUStringView label)
{
    native_label(doe_resource_render_bundle_device((doe_resource_render_bundle *)bundle), bundle, label);
}
void wgpuRenderPassEncoderExecuteBundles(WGPURenderPassEncoder handle, size_t count, const WGPURenderBundle *bundles)
{
    doe_resource_render_pass *pass = (doe_resource_render_pass *)handle;
    native_device *device = doe_resource_render_pass_device(pass);
    if (!device) return;
    /* Opaque public handles alias the exact resource object type. Copy typed
     * pointers instead of type-punning pointer arrays across the public ABI. */
    doe_resource_render_bundle **converted = NULL;
    doe_resource_status status = DOE_RESOURCE_OK;
    if (count && (!bundles || count > SIZE_MAX / sizeof(*converted))) status = DOE_RESOURCE_INVALID_ARGUMENT;
    if (status == DOE_RESOURCE_OK && count) {
        converted = native_allocate(NULL,NULL,count*sizeof(*converted),_Alignof(doe_resource_render_bundle *));
        if (!converted) status = DOE_RESOURCE_OUT_OF_MEMORY;
        else for (size_t i = 0; i < count; ++i) converted[i] = (doe_resource_render_bundle *)bundles[i];
    }
    native_lock(device);
    const bool ended = doe_resource_render_pass_ended(pass);
    if (status == DOE_RESOURCE_OK) status = doe_resource_render_pass_execute_bundles(pass,converted,count);
    else status = doe_resource_render_pass_invalidate(pass,status);
    native_unlock(device);
    if (converted) native_deallocate(NULL,NULL,converted,count*sizeof(*converted),_Alignof(doe_resource_render_bundle *));
    native_encoding_error(device, ended, status, "bundle execution recording failed");
}
