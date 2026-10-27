/* SPDX-License-Identifier: MIT */
#include "consumer.h"
#include "doe_native_legacy.h"
#include "doe_native_external_texture.h"

static WGPUExternalTextureDescriptor external_descriptor(WGPUTextureView view)
{
    static const float transfer[7] = {1,1,0,1,0,0,0};
    static const float gamut[9] = {1,0,0,0,1,0,0,0,1};
    return (WGPUExternalTextureDescriptor){.plane0=view,.cropSize={2,2},.apparentSize={2,2},
        .srcTransferFunctionParameters=transfer,.dstTransferFunctionParameters=transfer,
        .gamutConversionMatrix=gamut,.rotation=WGPUExternalTextureRotation_Rotate0Degrees};
}

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

static void read_words(state *s, WGPUBuffer readback, const uint32_t *expected, size_t count)
{
    unsigned before = s->mapped;
    WGPUBufferMapCallbackInfo callback = {.mode=WGPUCallbackMode_AllowSpontaneous,
        .callback=mapped,.userdata1=s};
    size_t bytes = count * sizeof(*expected);
    assert(wgpuBufferMapAsync(readback, WGPUMapMode_Read, 0, bytes, callback).id);
    assert(s->mapped == before + 1);
    const void *actual = wgpuBufferGetConstMappedRange(readback, 0, bytes);
    assert(actual && !memcmp(actual, expected, bytes));
    wgpuBufferUnmap(readback);
}

static void recorded_writes(state *s)
{
    WGPUBuffer destination = buffer(s, 16, WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst);
    WGPUBuffer before = buffer(s, 16, WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst);
    WGPUBuffer readback = buffer(s, 48, WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst);
    uint32_t initial[4] = {23, 24, 25, 26};
    wgpuQueueWriteBuffer(s->queue, destination, 0, initial, sizeof(initial));
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device, NULL);
    wgpuCommandEncoderClearBuffer(encoder, destination, 0, 16);
    uint32_t first = 42;
    wgpuCommandEncoderWriteBuffer(encoder, destination, 0, &first, sizeof(first));
    first = 99; /* Caller data must already have been snapshotted. */
    wgpuCommandEncoderCopyBufferToBuffer(encoder, destination, 0, readback, 0, 16);
    uint32_t second[4] = {7, 8, 9, 10};
    wgpuCommandEncoderWriteBuffer(encoder, destination, 0, second, sizeof(second));
    memset(second, 0, sizeof(second));
    wgpuCommandEncoderCopyBufferToBuffer(encoder, destination, 0, readback, 16, 16);
    wgpuCommandEncoderClearBuffer(encoder, destination, 0, 16);
    wgpuCommandEncoderCopyBufferToBuffer(encoder, destination, 0, readback, 32, 16);
    WGPUCommandEncoder inspect = wgpuDeviceCreateCommandEncoder(s->device, NULL);
    wgpuCommandEncoderCopyBufferToBuffer(inspect, destination, 0, before, 0, 16);
    submit(s, inspect);
    read_words(s, before, initial, 4); /* No eager mutation of destination. */
    wgpuBufferRelease(destination); /* Recorded copies keep all ordinary leases. */
    submit(s, encoder);
    const uint32_t expected[12] = {42, 0, 0, 0, 7, 8, 9, 10, 0, 0, 0, 0};
    read_words(s, readback, expected, 12);
    wgpuBufferRelease(readback);
    wgpuBufferRelease(before);

    destination = buffer(s, 16, WGPUBufferUsage_CopyDst);
    for (unsigned invalid = 0; invalid < 4; ++invalid) {
        encoder = wgpuDeviceCreateCommandEncoder(s->device, NULL);
        uint64_t offset = invalid == 0 ? 1 : invalid == 1 ? 16 : 0;
        size_t size = invalid == 2 ? 3 : sizeof(first);
        const void *data = invalid == 3 ? NULL : &first;
        wgpuCommandEncoderWriteBuffer(encoder, destination, offset, data, size);
        wgpuDevicePushErrorScope(s->device, WGPUErrorFilter_Validation);
        finish_invalid(encoder);
        expect_validation(s);
        wgpuCommandEncoderRelease(encoder);
    }
    encoder = wgpuDeviceCreateCommandEncoder(s->device, NULL);
    wgpuCommandEncoderWriteBuffer(encoder, destination, 16, NULL, 0);
    submit(s, encoder);
    wgpuBufferRelease(destination);
}
static void mapped_recorded_write(state *s)
{
    WGPUBufferDescriptor desc = {.size=16,.usage=WGPUBufferUsage_CopyDst | WGPUBufferUsage_CopySrc,
        .mappedAtCreation=WGPU_TRUE};
    WGPUBuffer destination = wgpuDeviceCreateBuffer(s->device, &desc);
    assert(destination);
    uint32_t *mapped_words = wgpuBufferGetMappedRange(destination, 0, 16);
    assert(mapped_words);
    uint32_t initial[4] = {11,12,13,14};
    memcpy(mapped_words, initial, sizeof(initial));
    WGPUBuffer readback = buffer(s, 16, WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst);
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device, NULL);
    uint32_t written[4] = {31,32,33,34};
    wgpuCommandEncoderWriteBuffer(encoder, destination, 0, written, sizeof(written));
    assert(!memcmp(mapped_words, initial, sizeof(initial)));
    wgpuCommandEncoderCopyBufferToBuffer(encoder, destination, 0, readback, 0, 16);
    /* Mapping is a submission restriction, not an encoding restriction. */
    wgpuBufferUnmap(destination);
    submit(s, encoder);
    read_words(s, readback, written, 4);
    wgpuBufferRelease(readback);
    wgpuBufferRelease(destination);
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
    WGPUExternalTextureDescriptor descriptor=external_descriptor(view);
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
    WGPUExternalTextureDescriptor ext_desc=external_descriptor(view);
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
    wgpuDeviceTick(device);
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
    const uint32_t untouched[2] = {0, 0};
    assert(!memcmp(wgpuBufferGetConstMappedRange(mapped_buffer,0,8),untouched,sizeof(untouched)));
    wgpuBufferUnmap(mapped_buffer); wgpuBufferRelease(mapped_buffer);
    recorded_writes(&s);
    mapped_recorded_write(&s);
    rejected_timestamps(&s); copies(&s); external_view_bounds(&s);
    consumer_finish(&s);
    printf("PASS legacy ABI identity, callbacks, invalid objects, rejection and exact browser flip copy (%s)\n",adapter_class_scope());
    return 0;
}
