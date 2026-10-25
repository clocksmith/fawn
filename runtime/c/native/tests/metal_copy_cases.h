/* SPDX-License-Identifier: MIT */
#ifndef DOE_NATIVE_TEST_METAL_COPY_CASES_H
#define DOE_NATIVE_TEST_METAL_COPY_CASES_H

static void metal_compressed_copy_ledger(state *s, mock_native *mock)
{
    WGPUTextureDescriptor descriptor = WGPU_TEXTURE_DESCRIPTOR_INIT;
    descriptor.format = WGPUTextureFormat_BC1RGBAUnorm;
    descriptor.size = (WGPUExtent3D){4,4,1};
    descriptor.usage = WGPUTextureUsage_CopySrc | WGPUTextureUsage_CopyDst;
    WGPUTexture texture = wgpuDeviceCreateTexture(s->device, &descriptor);
    assert(texture);
    WGPUBuffer source = buffer(s, 8, WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst);
    WGPUBuffer destination = buffer(s, 8, WGPUBufferUsage_CopyDst);
    const uint32_t words[2] = {0x10203040,0x50607080};
    wgpuQueueWriteBuffer(s->queue, source, 0, words, sizeof(words));
    WGPUTexelCopyBufferInfo input = WGPU_TEXEL_COPY_BUFFER_INFO_INIT;
    input.buffer = source;
    WGPUTexelCopyBufferInfo output = WGPU_TEXEL_COPY_BUFFER_INFO_INIT;
    output.buffer = destination;
    WGPUTexelCopyTextureInfo image = {.texture=texture};
    WGPUExtent3D extent = {4,4,1};
    unsigned uploads = mock->uploads, reads = mock->reads;
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device, NULL);
    wgpuCommandEncoderCopyBufferToTexture(encoder, &input, &image, &extent);
    wgpuCommandEncoderCopyTextureToBuffer(encoder, &image, &output, &extent);
    /* One compressed block row uses exactly eight bytes without trailing
     * native padding, although its texel height is four. */
    wgpuBufferRelease(source);
    wgpuBufferRelease(destination);
    wgpuTextureRelease(texture);
    submit(s, encoder);
    assert(!s->errors && mock->uploads == uploads + 1 && mock->reads == reads + 1);
    assert(mock->image_to.region.height == 4 && mock->image_to.bytes_per_row == 256);
    assert(mock->image_from.region.height == 4 && mock->image_from.bytes_per_row == 256);
}

static void metal_extended_copy_disabled(state *s)
{
    const WGPUTextureFormat formats[] = {WGPUTextureFormat_ETC2RGB8Unorm, WGPUTextureFormat_ASTC4x4Unorm};
    for (size_t i = 0; i < 2; ++i) {
        WGPUTextureDescriptor descriptor = WGPU_TEXTURE_DESCRIPTOR_INIT;
        descriptor.format = formats[i];
        descriptor.size = (WGPUExtent3D){4,4,1};
        descriptor.usage = WGPUTextureUsage_CopyDst;
        wgpuDevicePushErrorScope(s->device, WGPUErrorFilter_Validation);
        assert(!wgpuDeviceCreateTexture(s->device, &descriptor));
        expect_validation(s);
    }
}

static void metal_extended_copy_ledger(state *s, mock_native *mock)
{
    static const struct { WGPUTextureFormat format; uint32_t width, height, bytes; } formats[] = {
        {WGPUTextureFormat_ETC2RGB8Unorm,4,4,8},
        {WGPUTextureFormat_ETC2RGB8UnormSrgb,4,4,8},
        {WGPUTextureFormat_ETC2RGB8A1Unorm,4,4,8},
        {WGPUTextureFormat_ETC2RGB8A1UnormSrgb,4,4,8},
        {WGPUTextureFormat_ETC2RGBA8Unorm,4,4,16},
        {WGPUTextureFormat_ETC2RGBA8UnormSrgb,4,4,16},
        {WGPUTextureFormat_EACR11Unorm,4,4,8},
        {WGPUTextureFormat_EACR11Snorm,4,4,8},
        {WGPUTextureFormat_EACRG11Unorm,4,4,16},
        {WGPUTextureFormat_EACRG11Snorm,4,4,16},
        {WGPUTextureFormat_ASTC4x4Unorm,4,4,16},
        {WGPUTextureFormat_ASTC4x4UnormSrgb,4,4,16},
        {WGPUTextureFormat_ASTC5x4Unorm,5,4,16},
        {WGPUTextureFormat_ASTC5x4UnormSrgb,5,4,16},
        {WGPUTextureFormat_ASTC5x5Unorm,5,5,16},
        {WGPUTextureFormat_ASTC5x5UnormSrgb,5,5,16},
        {WGPUTextureFormat_ASTC6x5Unorm,6,5,16},
        {WGPUTextureFormat_ASTC6x5UnormSrgb,6,5,16},
        {WGPUTextureFormat_ASTC6x6Unorm,6,6,16},
        {WGPUTextureFormat_ASTC6x6UnormSrgb,6,6,16},
        {WGPUTextureFormat_ASTC8x5Unorm,8,5,16},
        {WGPUTextureFormat_ASTC8x5UnormSrgb,8,5,16},
        {WGPUTextureFormat_ASTC8x6Unorm,8,6,16},
        {WGPUTextureFormat_ASTC8x6UnormSrgb,8,6,16},
        {WGPUTextureFormat_ASTC8x8Unorm,8,8,16},
        {WGPUTextureFormat_ASTC8x8UnormSrgb,8,8,16},
        {WGPUTextureFormat_ASTC10x5Unorm,10,5,16},
        {WGPUTextureFormat_ASTC10x5UnormSrgb,10,5,16},
        {WGPUTextureFormat_ASTC10x6Unorm,10,6,16},
        {WGPUTextureFormat_ASTC10x6UnormSrgb,10,6,16},
        {WGPUTextureFormat_ASTC10x8Unorm,10,8,16},
        {WGPUTextureFormat_ASTC10x8UnormSrgb,10,8,16},
        {WGPUTextureFormat_ASTC10x10Unorm,10,10,16},
        {WGPUTextureFormat_ASTC10x10UnormSrgb,10,10,16},
        {WGPUTextureFormat_ASTC12x10Unorm,12,10,16},
        {WGPUTextureFormat_ASTC12x10UnormSrgb,12,10,16},
        {WGPUTextureFormat_ASTC12x12Unorm,12,12,16},
        {WGPUTextureFormat_ASTC12x12UnormSrgb,12,12,16}
    };
    for (size_t i = 0; i < sizeof(formats) / sizeof(formats[0]); ++i) {
        WGPUTextureDescriptor descriptor = WGPU_TEXTURE_DESCRIPTOR_INIT;
        descriptor.format = formats[i].format;
        descriptor.size = (WGPUExtent3D){formats[i].width,formats[i].height,1};
        descriptor.usage = WGPUTextureUsage_CopySrc | WGPUTextureUsage_CopyDst;
        WGPUTexture texture = wgpuDeviceCreateTexture(s->device, &descriptor);
        assert(texture);
        WGPUBuffer target = buffer(s, formats[i].bytes, WGPUBufferUsage_CopyDst);
        WGPUTexelCopyBufferInfo destination = WGPU_TEXEL_COPY_BUFFER_INFO_INIT;
        destination.buffer = target;
        WGPUTexelCopyTextureInfo source = {.texture=texture};
        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device, NULL);
        unsigned reads = mock->reads;
        wgpuCommandEncoderCopyTextureToBuffer(encoder, &source, &destination, &descriptor.size);
        wgpuBufferRelease(target);
        wgpuTextureRelease(texture);
        submit(s, encoder);
        assert(!s->errors && mock->reads == reads + 1);
        assert(mock->image_from.region.width == formats[i].width &&
            mock->image_from.region.height == formats[i].height);
    }
    puts("PASS 38 ETC2/EAC/ASTC public Metal format/initialization/copy/early-release cases; host ledger only");
}
#endif
