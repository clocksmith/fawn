/* SPDX-License-Identifier: MIT */
#include "consumer.h"
#include "doe_native_legacy.h"
#include "doe_native_external_texture.h"

static void legacy_error(uint32_t type, WGPUStringView message, void *one, void *two)
{
    assert(type == WGPUErrorType_Validation && message.length && one == two);
    ++*(unsigned *)one;
}
static void lost(WGPUDevice const *device, WGPUDeviceLostReason reason,
    WGPUStringView message, void *one, void *two)
{
    assert(device && *device && reason == WGPUDeviceLostReason_Destroyed && message.length && one == two);
    ++*(unsigned *)one;
}
static void rejected_timestamps(state *s)
{
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device,NULL);
    WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder,NULL);
    assert(pass);
    wgpuComputePassEncoderWriteTimestamp(pass,NULL,0);
    wgpuComputePassEncoderEnd(pass);
    wgpuComputePassEncoderRelease(pass);
    wgpuDevicePushErrorScope(s->device,WGPUErrorFilter_Validation);
    finish_invalid(encoder);
    expect_validation(s);
    wgpuCommandEncoderRelease(encoder);
    WGPUTexture image = texture(s,WGPUTextureFormat_RGBA8Unorm,WGPUTextureUsage_RenderAttachment,1);
    WGPUTextureView view = wgpuTextureCreateView(image,NULL);
    WGPURenderPassColorAttachment color = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    color.view=view; color.loadOp=WGPULoadOp_Clear; color.storeOp=WGPUStoreOp_Store;
    WGPURenderPassDescriptor descriptor = {.colorAttachmentCount=1,.colorAttachments=&color};
    encoder=wgpuDeviceCreateCommandEncoder(s->device,NULL);
    WGPURenderPassEncoder render=wgpuCommandEncoderBeginRenderPass(encoder,&descriptor);
    assert(render);
    wgpuRenderPassEncoderWriteTimestamp(render,NULL,0);
    wgpuRenderPassEncoderEnd(render);
    wgpuRenderPassEncoderRelease(render);
    wgpuDevicePushErrorScope(s->device,WGPUErrorFilter_Validation);
    finish_invalid(encoder);
    expect_validation(s);
    wgpuCommandEncoderRelease(encoder);
    wgpuTextureViewRelease(view); wgpuTextureRelease(image);
}
static void copies(state *s)
{
    WGPUTexture source=texture(s,WGPUTextureFormat_RGBA8Unorm,
        WGPUTextureUsage_CopySrc|WGPUTextureUsage_CopyDst|WGPUTextureUsage_TextureBinding,1);
    WGPUTexture destination=texture(s,WGPUTextureFormat_RGBA8Unorm,
        WGPUTextureUsage_CopySrc|WGPUTextureUsage_CopyDst,1);
    uint32_t pixels[4]={0x01020304,0x11223344,0x55667788,0x99aabbcc};
    WGPUTexelCopyTextureInfo src={.texture=source,.aspect=WGPUTextureAspect_All};
    WGPUTexelCopyTextureInfo dst={.texture=destination,.aspect=WGPUTextureAspect_All};
    WGPUTexelCopyBufferLayout layout={.bytesPerRow=8,.rowsPerImage=2};
    WGPUExtent3D size={2,2,1};
    wgpuQueueWriteTexture(s->queue,&src,pixels,sizeof(pixels),&layout,&size);
    WGPUTextureView view=wgpuTextureCreateView(source,NULL);
    DoeExternalTextureDescriptor descriptor={.plane0=view};
    WGPUExternalTexture external=wgpuDeviceCreateExternalTexture(s->device,&descriptor);
    assert(external);
    DoeLegacyImageCopyExternalTexture from={.externalTexture=external};
    DoeLegacyCopyTextureForBrowserOptions options={.flipY=WGPU_TRUE};
    wgpuQueueCopyExternalTextureForBrowser(s->queue,&from,&dst,&size,&options);
    wgpuTextureViewRelease(view);
    wgpuTextureRelease(source);
    wgpuExternalTextureExpire(external);
    wgpuDevicePushErrorScope(s->device,WGPUErrorFilter_Validation);
    wgpuQueueCopyExternalImageToTexture(s->queue,&from,&dst,&size);
    expect_validation(s);
    wgpuExternalTextureRefresh(external);
    options.needsColorSpaceConversion = WGPU_TRUE;
    wgpuDevicePushErrorScope(s->device,WGPUErrorFilter_Validation);
    wgpuQueueCopyExternalTextureForBrowser(s->queue,&from,&dst,&size,&options);
    expect_validation(s);
    options.needsColorSpaceConversion = WGPU_FALSE;
    WGPUExtent3D oversized={2,UINT32_MAX,1};
    wgpuDevicePushErrorScope(s->device,WGPUErrorFilter_Validation);
    wgpuQueueCopyExternalTextureForBrowser(s->queue,&from,&dst,&oversized,&options);
    expect_validation(s);
    wgpuExternalTextureDestroy(external);
    wgpuExternalTextureRelease(external);
    WGPUBuffer readback=buffer(s,512,WGPUBufferUsage_CopyDst|WGPUBufferUsage_MapRead);
    WGPUTexelCopyBufferInfo to={.buffer=readback,.layout={.bytesPerRow=256,.rowsPerImage=2}};
    WGPUCommandEncoder encoder=wgpuDeviceCreateCommandEncoder(s->device,NULL);
    wgpuCommandEncoderCopyTextureToBuffer(encoder,&dst,&to,&size);
    submit(s,encoder);
    unsigned before=s->mapped;
    WGPUBufferMapCallbackInfo callback={.mode=WGPUCallbackMode_AllowSpontaneous,.callback=mapped,.userdata1=s};
    assert(wgpuBufferMapAsync(readback,WGPUMapMode_Read,0,512,callback).id && s->mapped==before+1);
    const unsigned char *bytes=wgpuBufferGetConstMappedRange(readback,0,512);
    assert(bytes && !memcmp(bytes,pixels+2,8) && !memcmp(bytes+256,pixels,8));
    wgpuBufferUnmap(readback); wgpuBufferRelease(readback); wgpuTextureRelease(destination);
}
static void external_view_bounds(state *s)
{
    WGPUTextureDescriptor image = WGPU_TEXTURE_DESCRIPTOR_INIT;
    image.size=(WGPUExtent3D){2,2,2}; image.format=WGPUTextureFormat_RGBA8Unorm;
    image.usage=WGPUTextureUsage_CopySrc|WGPUTextureUsage_CopyDst|WGPUTextureUsage_TextureBinding;
    WGPUTexture source=wgpuDeviceCreateTexture(s->device,&image);
    WGPUTexture destination=wgpuDeviceCreateTexture(s->device,&image);
    assert(source && destination);
    WGPUTextureViewDescriptor desc=WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    desc.dimension=WGPUTextureViewDimension_2D; desc.arrayLayerCount=1;
    WGPUTextureView view=wgpuTextureCreateView(source,&desc);
    DoeExternalTextureDescriptor ext_desc={.plane0=view};
    WGPUExternalTexture external=wgpuDeviceCreateExternalTexture(s->device,&ext_desc);
    assert(external);
    DoeLegacyImageCopyExternalTexture from={.externalTexture=external};
    WGPUTexelCopyTextureInfo to={.texture=destination,.aspect=WGPUTextureAspect_All};
    WGPUExtent3D size={2,2,2};
    /* Fits the parent texture, but must never escape the declared single layer. */
    wgpuDevicePushErrorScope(s->device,WGPUErrorFilter_Validation);
    wgpuQueueCopyExternalImageToTexture(s->queue,&from,&to,&size);
    expect_validation(s);
    wgpuExternalTextureRelease(external); wgpuTextureViewRelease(view);
    wgpuTextureRelease(source); wgpuTextureRelease(destination);
}

int main(void)
{
    state s={0}; consumer_init(&s);
    WGPUInstance instance=wgpuAdapterGetInstance(s.adapter);
    WGPUAdapter adapter=wgpuDeviceGetAdapter(s.device);
    assert(instance==s.instance && adapter==s.adapter);
    wgpuInstanceRelease(instance);
    wgpuAdapterRelease(s.adapter);
    s.adapter=adapter; /* Device retains original adapter after caller release. */
    WGPUDeviceDescriptor descriptor=WGPU_DEVICE_DESCRIPTOR_INIT;
    descriptor.deviceLostCallbackInfo.mode=WGPUCallbackMode_AllowProcessEvents;
    WGPUDevice device=wgpuAdapterCreateDevice(s.adapter,&descriptor);
    assert(device);
    unsigned errors=0, lost_calls=0, wrong=0;
    wgpuDeviceSetUncapturedErrorCallback(device,legacy_error,&errors,&errors);
    wgpuDeviceValidateTextureDescriptor(device,NULL);
    assert(errors==1);
    wgpuDeviceSetUncapturedErrorCallback(device,NULL,NULL,NULL);
    wgpuDeviceValidateTextureDescriptor(device,NULL);
    assert(errors==1);
    wgpuDeviceSetDeviceLostCallback(device,lost,&lost_calls,&lost_calls);
    assert(!wgpuDeviceTick(device));
    wgpuDeviceDestroy(device);
    wgpuDeviceSetDeviceLostCallback(device,lost,&wrong,&wrong);
    wgpuInstanceProcessEvents(s.instance);
    assert(lost_calls==1 && !wrong);
    wgpuDeviceRelease(device);
    WGPUBufferDescriptor invalid={.size=16,.usage=WGPUBufferUsage_CopySrc};
    WGPUBuffer error_buffer=wgpuDeviceCreateErrorBuffer(s.device,&invalid);
    assert(error_buffer && wgpuBufferGetSize(error_buffer)==16);
    wgpuBufferDestroy(error_buffer); wgpuBufferRelease(error_buffer);
    WGPUTexture error_texture=wgpuDeviceCreateErrorTexture(s.device,NULL);
    assert(error_texture && wgpuTextureGetWidth(error_texture)==1);
    wgpuDevicePushErrorScope(s.device,WGPUErrorFilter_Validation);
    WGPUTextureView invalid_view=wgpuTextureCreateView(error_texture,NULL);
    assert(!invalid_view);
    expect_validation(&s);
    wgpuTextureDestroy(error_texture); wgpuTextureRelease(error_texture);
    WGPUBufferDescriptor mapping={.size=16,.usage=WGPUBufferUsage_CopySrc,.mappedAtCreation=WGPU_TRUE};
    WGPUBuffer mapped_buffer=wgpuDeviceCreateBuffer(s.device,&mapping);
    uint32_t word=0xdeadbeef;
    wgpuCommandEncoderWriteBuffer(NULL,mapped_buffer,0,(const uint8_t *)&word,sizeof(word));
    assert(!memcmp(wgpuBufferGetConstMappedRange(mapped_buffer,0,8),&word,sizeof(word)));
    wgpuBufferUnmap(mapped_buffer); wgpuBufferRelease(mapped_buffer);
    rejected_timestamps(&s); copies(&s); external_view_bounds(&s);
    consumer_finish(&s);
    printf("PASS legacy ABI identity, callbacks, invalid objects, rejection and exact browser flip copy (%s)\n",adapter_class_scope());
    return 0;
}
