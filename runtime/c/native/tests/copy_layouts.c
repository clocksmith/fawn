/* SPDX-License-Identifier: MIT
 * Public texel copy layouts with WGPU_COPY_STRIDE_UNDEFINED: rowsPerImage may be
 * undefined when one image is copied and bytesPerRow when one row is copied, for
 * texture-to-buffer, buffer-to-texture and queue writes. Exact readback, plus
 * the multi-image and multi-row controls that must reject. */
#include "consumer.h"
#include <stdlib.h>

enum { SIZE = 4, PITCH = 256 };

static uint32_t texel(uint32_t x, uint32_t y, uint32_t layer)
{
    return 0x01000000u * (layer + 1) + 0x00010000u * (y + 1) + 0x00000100u * (x + 1) + 0x5Au;
}
static WGPUTexture image(state *s, uint32_t layers)
{
    WGPUTextureDescriptor descriptor = WGPU_TEXTURE_DESCRIPTOR_INIT;
    descriptor.format = WGPUTextureFormat_RGBA8Unorm;
    descriptor.usage = WGPUTextureUsage_CopySrc | WGPUTextureUsage_CopyDst;
    descriptor.size = (WGPUExtent3D){SIZE,SIZE,layers};
    WGPUTexture result = wgpuDeviceCreateTexture(s->device,&descriptor);
    assert(result);
    return result;
}
/* Reads layer 0 with a fully specified layout and checks every texel. */
static void expect_texels(state *s, WGPUTexture texture, uint32_t rows, const char *name)
{
    WGPUBuffer readback = buffer(s,PITCH * SIZE,WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst);
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device,NULL);
    texture_to_buffer(encoder,texture,readback,0,SIZE,SIZE);
    submit(s,encoder);
    const unsigned char *bytes = map(s,readback,PITCH * SIZE);
    for (uint32_t y = 0; y < rows; ++y) for (uint32_t x = 0; x < SIZE; ++x) {
        uint32_t value;
        memcpy(&value,bytes + y * PITCH + x * 4,4);
        if (value != texel(x,y,0)) {
            fprintf(stderr,"%s texel (%u,%u): %08x\n",name,x,y,value);
            abort();
        }
    }
    wgpuBufferUnmap(readback);
    wgpuBufferRelease(readback);
}

/* Color copies need only texel-aligned buffer offsets: an r8unorm row moves
 * through buffer offset 3 in both directions with exact bytes. */
static void unaligned_offsets(state *s)
{
    WGPUTextureDescriptor descriptor = WGPU_TEXTURE_DESCRIPTOR_INIT;
    descriptor.format = WGPUTextureFormat_R8Unorm;
    descriptor.usage = WGPUTextureUsage_CopySrc | WGPUTextureUsage_CopyDst;
    descriptor.size = (WGPUExtent3D){SIZE,1,1};
    WGPUTexture bytes_texture = wgpuDeviceCreateTexture(s->device,&descriptor);
    assert(bytes_texture);
    const unsigned char pattern[8] = {0,0,0,11,22,33,44,0};
    WGPUBuffer source = buffer(s,8,WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst);
    wgpuQueueWriteBuffer(s->queue,source,0,pattern,sizeof(pattern));
    WGPUBuffer readback = buffer(s,8,WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst);
    const WGPUExtent3D size = {SIZE,1,1};
    WGPUTexelCopyTextureInfo texture = {.texture=bytes_texture,.aspect=WGPUTextureAspect_All};
    WGPUTexelCopyBufferInfo from = {.buffer=source,.layout={.offset=3,.bytesPerRow=WGPU_COPY_STRIDE_UNDEFINED,
        .rowsPerImage=WGPU_COPY_STRIDE_UNDEFINED}};
    WGPUTexelCopyBufferInfo to = {.buffer=readback,.layout={.offset=1,.bytesPerRow=WGPU_COPY_STRIDE_UNDEFINED,
        .rowsPerImage=WGPU_COPY_STRIDE_UNDEFINED}};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device,NULL);
    wgpuCommandEncoderCopyBufferToTexture(encoder,&from,&texture,&size);
    wgpuCommandEncoderCopyTextureToBuffer(encoder,&texture,&to,&size);
    submit(s,encoder);
    const unsigned char *bytes = map(s,readback,8);
    const unsigned char expected[5] = {0,11,22,33,44};
    assert(!memcmp(bytes,expected,sizeof(expected)));
    wgpuBufferUnmap(readback);
    wgpuBufferRelease(readback);
    wgpuBufferRelease(source);
    wgpuTextureRelease(bytes_texture);
}

int main(void)
{
    state s = {0};
    consumer_init(&s);
    uint32_t pitched[PITCH / 4 * SIZE] = {0}, packed[SIZE * SIZE];
    for (uint32_t y = 0; y < SIZE; ++y) for (uint32_t x = 0; x < SIZE; ++x)
        pitched[y * PITCH / 4 + x] = packed[y * SIZE + x] = texel(x,y,0);
    const WGPUExtent3D whole = {SIZE,SIZE,1}, row = {SIZE,1,1};

    /* Queue writes: one image with rowsPerImage undefined, then one row with
     * both strides undefined (rows 1..3 keep the first write). */
    WGPUTexture written = image(&s,1);
    WGPUTexelCopyTextureInfo target = {.texture=written,.aspect=WGPUTextureAspect_All};
    WGPUTexelCopyBufferLayout one_image = {.bytesPerRow=SIZE * 4,.rowsPerImage=WGPU_COPY_STRIDE_UNDEFINED};
    wgpuQueueWriteTexture(s.queue,&target,packed,sizeof(packed),&one_image,&whole);
    WGPUTexelCopyBufferLayout one_row = {.bytesPerRow=WGPU_COPY_STRIDE_UNDEFINED,.rowsPerImage=WGPU_COPY_STRIDE_UNDEFINED};
    wgpuQueueWriteTexture(s.queue,&target,packed,SIZE * 4,&one_row,&row);
    expect_texels(&s,written,SIZE,"queue write");

    /* Buffer to texture and texture to buffer, each with the undefined strides. */
    WGPUBuffer source = buffer(&s,sizeof(pitched),WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst);
    wgpuQueueWriteBuffer(s.queue,source,0,pitched,sizeof(pitched));
    WGPUTexture uploaded = image(&s,1);
    WGPUBuffer image_out = buffer(&s,PITCH * SIZE,WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst);
    WGPUBuffer row_out = buffer(&s,PITCH,WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst);
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s.device,NULL);
    WGPUTexelCopyBufferInfo from_buffer = {.buffer=source,.layout={.bytesPerRow=PITCH,.rowsPerImage=WGPU_COPY_STRIDE_UNDEFINED}};
    WGPUTexelCopyTextureInfo upload_target = {.texture=uploaded,.aspect=WGPUTextureAspect_All};
    wgpuCommandEncoderCopyBufferToTexture(encoder,&from_buffer,&upload_target,&whole);
    WGPUTexelCopyBufferInfo to_image = {.buffer=image_out,.layout={.bytesPerRow=PITCH,.rowsPerImage=WGPU_COPY_STRIDE_UNDEFINED}};
    wgpuCommandEncoderCopyTextureToBuffer(encoder,&upload_target,&to_image,&whole);
    WGPUTexelCopyBufferInfo to_row = {.buffer=row_out,
        .layout={.bytesPerRow=WGPU_COPY_STRIDE_UNDEFINED,.rowsPerImage=WGPU_COPY_STRIDE_UNDEFINED}};
    WGPUTexelCopyTextureInfo second_row = {.texture=uploaded,.origin={0,1,0},.aspect=WGPUTextureAspect_All};
    wgpuCommandEncoderCopyTextureToBuffer(encoder,&second_row,&to_row,&row);
    submit(&s,encoder);
    const unsigned char *bytes = map(&s,image_out,PITCH * SIZE);
    for (uint32_t y = 0; y < SIZE; ++y) assert(!memcmp(bytes + y * PITCH,packed + y * SIZE,SIZE * 4));
    wgpuBufferUnmap(image_out);
    bytes = map(&s,row_out,PITCH);
    assert(!memcmp(bytes,packed + SIZE,SIZE * 4));
    wgpuBufferUnmap(row_out);

    /* Undefined rowsPerImage with two images, undefined bytesPerRow with two rows
     * and an explicit rowsPerImage 0 below the copy height reject: one
     * validation error, reported at Finish and not at the copy call. */
    WGPUTexture layered = image(&s,2);
    WGPUBuffer large = buffer(&s,PITCH * SIZE * 2,WGPUBufferUsage_CopyDst);
    for (uint32_t which = 0; which < 3; ++which) {
        WGPUCommandEncoder rejected = wgpuDeviceCreateCommandEncoder(s.device,NULL);
        WGPUTexelCopyTextureInfo from = {.texture=which == 0 ? layered : uploaded,.aspect=WGPUTextureAspect_All};
        WGPUTexelCopyBufferInfo to = {.buffer=large,.layout={.bytesPerRow=which == 1 ? WGPU_COPY_STRIDE_UNDEFINED : PITCH,
            .rowsPerImage=which == 2 ? 0 : WGPU_COPY_STRIDE_UNDEFINED}};
        WGPUExtent3D size = {SIZE,which == 1 ? 2 : SIZE,which == 0 ? 2 : 1};
        unsigned before = s.errors;
        wgpuCommandEncoderCopyTextureToBuffer(rejected,&from,&to,&size);
        assert(s.errors == before); /* nothing reported at the encoding call */
        wgpuDevicePushErrorScope(s.device,WGPUErrorFilter_Validation);
        finish_invalid(rejected);
        expect_validation(&s);
        wgpuCommandEncoderRelease(rejected);
    }
    wgpuBufferRelease(large);
    wgpuTextureRelease(layered);
    wgpuBufferRelease(row_out);
    wgpuBufferRelease(image_out);
    wgpuBufferRelease(source);
    wgpuTextureRelease(uploaded);
    wgpuTextureRelease(written);
    unaligned_offsets(&s);
    consumer_finish(&s);
    printf("PASS public copy layouts: r8unorm copies through odd buffer offsets both ways; undefined rowsPerImage for one image and undefined bytesPerRow for one row "
        "through queue writes, buffer-to-texture and texture-to-buffer copies with exact texels; two-image and "
        "two-row undefined-stride and rowsPerImage-0 rejections reported once at Finish %s\n", adapter_class_scope());
    return 0;
}
