/* SPDX-License-Identifier: MIT
 * Portable public color texture lifecycle/copy consumer; no sampling claim. */
#include "consumer.h"

static WGPUTexture image(state *s)
{
    WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    desc.size = (WGPUExtent3D){3,3,1};
    desc.format = WGPUTextureFormat_RGBA8Uint;
    desc.usage = WGPUTextureUsage_CopySrc|WGPUTextureUsage_CopyDst|WGPUTextureUsage_TextureBinding;
    WGPUTexture result = wgpuDeviceCreateTexture(s->device,&desc);
    assert(result);
    return result;
}
static void download(WGPUCommandEncoder encoder,WGPUTexture source,WGPUBuffer destination)
{
    WGPUTexelCopyTextureInfo from = {.texture=source};
    WGPUTexelCopyBufferInfo to = {.buffer=destination,.layout={.offset=4,.bytesPerRow=256,.rowsPerImage=3}};
    WGPUExtent3D size = {3,3,1};
    wgpuCommandEncoderCopyTextureToBuffer(encoder,&from,&to,&size);
}
static void verify(state *s,WGPUBuffer buffer,const unsigned char pixels[36],unsigned char padding)
{
    const unsigned char *bytes = map(s,buffer,532);
    for (size_t i = 0; i < 532; ++i) {
        unsigned char expected = padding;
        for (size_t row = 0; row < 3; ++row)
            if (i >= 4 + row * 256 && i < 16 + row * 256) expected = pixels[row * 12 + i - 4 - row * 256];
        assert(bytes[i] == expected);
    }
    wgpuBufferUnmap(buffer);
}
static void empty_copy_lifetimes(state *s)
{
    for (unsigned axis = 0; axis < 3; ++axis) for (unsigned destroyed = 0; destroyed < 3; ++destroyed) {
        WGPUTextureDescriptor descriptor = WGPU_TEXTURE_DESCRIPTOR_INIT;
        descriptor.dimension = WGPUTextureDimension_2D;
        descriptor.size = (WGPUExtent3D){3,3,1};
        descriptor.format = WGPUTextureFormat_RGBA8Unorm;
        descriptor.usage = WGPUTextureUsage_CopySrc | WGPUTextureUsage_CopyDst;
        WGPUTexture textures[2] = {wgpuDeviceCreateTexture(s->device, &descriptor),
            wgpuDeviceCreateTexture(s->device, &descriptor)};
        assert(textures[0] && textures[1]);
        WGPUTexelCopyTextureInfo from = {.texture=textures[0]}, to = {.texture=textures[1]};
        WGPUExtent3D size = {1,1,1};
        if (axis == 0) size.width = 0;
        else if (axis == 1) size.height = 0;
        else size.depthOrArrayLayers = 0;
        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device, NULL);
        wgpuCommandEncoderCopyTextureToTexture(encoder, &from, &to, &size);
        WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, NULL);
        assert(commands);
        wgpuCommandEncoderRelease(encoder);
        if (destroyed) wgpuTextureDestroy(textures[destroyed - 1]);
        wgpuTextureRelease(textures[0]);
        wgpuTextureRelease(textures[1]);
        if (destroyed) wgpuDevicePushErrorScope(s->device, WGPUErrorFilter_Validation);
        wgpuQueueSubmit(s->queue, 1, &commands);
        if (destroyed) expect_validation(s);
        wgpuCommandBufferRelease(commands);
    }
}

static void empty_buffer_texture_lifetimes(state *s)
{
    for (unsigned direction = 0; direction < 2; ++direction)
        for (unsigned axis = 0; axis < 3; ++axis) for (unsigned changed = 0; changed < 4; ++changed) {
            WGPUTextureDescriptor descriptor = WGPU_TEXTURE_DESCRIPTOR_INIT;
            descriptor.dimension = WGPUTextureDimension_2D;
            descriptor.size = (WGPUExtent3D){1,1,1};
            descriptor.format = WGPUTextureFormat_RGBA8Unorm;
            descriptor.usage = WGPUTextureUsage_CopySrc | WGPUTextureUsage_CopyDst;
            WGPUTexture texture = wgpuDeviceCreateTexture(s->device, &descriptor);
            assert(texture);
            WGPUBuffer bytes = buffer(s, 256, direction ? WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst :
                WGPUBufferUsage_MapWrite | WGPUBufferUsage_CopySrc);
            WGPUTexelCopyTextureInfo image_copy = {.texture=texture};
            WGPUTexelCopyBufferInfo buffer_copy = {.buffer=bytes, .layout={.bytesPerRow=256, .rowsPerImage=1}};
            WGPUExtent3D size = {1,1,1};
            if (axis == 0) size.width = 0;
            else if (axis == 1) size.height = 0;
            else size.depthOrArrayLayers = 0;
            WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device, NULL);
            if (direction) wgpuCommandEncoderCopyTextureToBuffer(encoder, &image_copy, &buffer_copy, &size);
            else wgpuCommandEncoderCopyBufferToTexture(encoder, &buffer_copy, &image_copy, &size);
            WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, NULL);
            assert(commands);
            wgpuCommandEncoderRelease(encoder);
            if (changed == 1) wgpuBufferDestroy(bytes);
            if (changed == 2) wgpuTextureDestroy(texture);
            if (changed == 3) {
                unsigned before = s->mapped;
                WGPUBufferMapCallbackInfo callback = {.mode=WGPUCallbackMode_AllowSpontaneous,
                    .callback=mapped, .userdata1=s};
                assert(wgpuBufferMapAsync(bytes, direction ? WGPUMapMode_Read : WGPUMapMode_Write,
                    0, 256, callback).id && s->mapped == before + 1);
            }
            wgpuBufferRelease(bytes);
            wgpuTextureRelease(texture);
            if (changed) wgpuDevicePushErrorScope(s->device, WGPUErrorFilter_Validation);
            wgpuQueueSubmit(s->queue, 1, &commands);
            if (changed) expect_validation(s);
            wgpuCommandBufferRelease(commands);
        }
}

int main(void)
{
    state s = {0};
    consumer_init(&s);
    empty_copy_lifetimes(&s);
    empty_buffer_texture_lifetimes(&s);
    WGPUTexture source = image(&s), destination = image(&s);
    WGPUTextureView view = wgpuTextureCreateView(source,NULL);
    assert(view);
    WGPUSampler sampler = wgpuDeviceCreateSampler(s.device,NULL);
    assert(sampler);
    wgpuTextureViewRelease(view);
    wgpuSamplerRelease(sampler);
    WGPUBuffer first = buffer(&s,532,WGPUBufferUsage_MapRead|WGPUBufferUsage_CopyDst);
    WGPUBuffer second = buffer(&s,532,WGPUBufferUsage_MapRead|WGPUBufferUsage_CopyDst);
    unsigned char padding[532];
    memset(padding,0xa7,sizeof(padding));
    wgpuQueueWriteBuffer(s.queue,first,0,padding,sizeof(padding));
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s.device,NULL);
    download(encoder,source,first);
    submit(&s,encoder);
    unsigned char expected[36] = {0};
    verify(&s,first,expected,0xa7); /* Real allocation zeroing before publication. */
    unsigned char data[52];
    memset(data,0xde,sizeof(data));
    for (size_t row = 0; row < 3; ++row)
        for (size_t x = 0; x < 12; ++x) data[3 + row * 17 + x] = expected[row * 12 + x] = (unsigned char)(row * 12 + x + 1);
    WGPUTexelCopyTextureInfo target = {.texture=source};
    WGPUTexelCopyBufferLayout layout = {.offset=3,.bytesPerRow=17,.rowsPerImage=3};
    WGPUExtent3D extent = {3,3,1};
    wgpuQueueWriteTexture(s.queue,&target,data,sizeof(data),&layout,&extent);
    WGPUExtent3D zero = {3,3,0};
    WGPUTexelCopyBufferLayout empty_layout = {.bytesPerRow=17,.rowsPerImage=3};
    wgpuQueueWriteTexture(s.queue,&target,NULL,0,&empty_layout,&zero);
    unsigned char patch[5] = {0xff,91,92,93,94};
    target.origin = (WGPUOrigin3D){1,1,0};
    layout = (WGPUTexelCopyBufferLayout){.offset=1,.bytesPerRow=WGPU_COPY_STRIDE_UNDEFINED,
        .rowsPerImage=WGPU_COPY_STRIDE_UNDEFINED};
    extent = (WGPUExtent3D){1,1,1};
    wgpuQueueWriteTexture(s.queue,&target,patch,sizeof(patch),&layout,&extent);
    memcpy(expected+16,patch+1,4);
    unsigned char input[272] = {0};
    const unsigned char block[16] = {101,102,103,104,105,106,107,108,109,110,111,112,113,114,115,116};
    memcpy(input+4,block,8);
    memcpy(input+260,block+8,8);
    WGPUBuffer upload = buffer(&s,sizeof(input),WGPUBufferUsage_CopySrc|WGPUBufferUsage_CopyDst);
    wgpuQueueWriteBuffer(s.queue,upload,0,input,sizeof(input));
    wgpuQueueWriteBuffer(s.queue,first,0,padding,sizeof(padding));
    wgpuQueueWriteBuffer(s.queue,second,0,padding,sizeof(padding));
    encoder = wgpuDeviceCreateCommandEncoder(s.device,NULL);
    WGPUTexelCopyBufferInfo from = {.buffer=upload,.layout={.offset=4,.bytesPerRow=256,.rowsPerImage=2}};
    target.origin = (WGPUOrigin3D){0,0,0};
    extent = (WGPUExtent3D){2,2,1};
    wgpuCommandEncoderCopyBufferToTexture(encoder,&from,&target,&extent);
    memcpy(expected,block,8);
    memcpy(expected+12,block+8,8);
    WGPUTexelCopyTextureInfo src = {.texture=source,.origin={1,0,0}};
    WGPUTexelCopyTextureInfo dst = {.texture=destination,.origin={0,1,0}};
    wgpuCommandEncoderCopyTextureToTexture(encoder,&src,&dst,&extent);
    unsigned char expected_destination[36] = {0};
    memcpy(expected_destination+12,expected+4,8);
    memcpy(expected_destination+24,expected+16,8);
    download(encoder,source,first);
    download(encoder,destination,second);
    WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder,NULL);
    assert(commands);
    wgpuCommandEncoderRelease(encoder);
    wgpuBufferRelease(upload);
    wgpuTextureRelease(source);
    wgpuTextureRelease(destination);
    wgpuQueueSubmit(s.queue,1,&commands);
    wgpuCommandBufferRelease(commands);
    verify(&s,first,expected,0xa7);
    verify(&s,second,expected_destination,0xa7);
    wgpuBufferRelease(first);
    wgpuBufferRelease(second);
    consumer_finish(&s);
    printf("PASS public texture transfers: zero publication, odd queue layouts, partial GPU buffer/texture/blit copies, preserved padding, immutable view/sampler and early releases %s\n",adapter_class_scope());
}
