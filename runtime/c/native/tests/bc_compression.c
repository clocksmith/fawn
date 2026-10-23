/* SPDX-License-Identifier: MIT
 * Public texture-compression-bc and texture-compression-bc-sliced-3d: solid BC1
 * and BC4 blocks uploaded through queue writes and buffer copies decode to
 * exact texels in compute textureLoad, a 6x6 mip takes its block-rounded 8x8
 * copy, texture-to-texture and texture-to-buffer copies return the exact
 * blocks, and a 3D BC1 texture uploaded with rowsPerImage 2 decodes per slice. Without the features, and
 * for non-block-aligned shapes or attachment/storage usage, creation and
 * copies reject. */
#include "consumer.h"
#include <stdlib.h>

enum { PITCH = 256 };

/* A solid BC1 block: equal RGB565 endpoints, all indices 0 (opaque endpoint). */
static void bc1(uint8_t out[8], uint16_t color)
{
    memset(out,0,8);
    out[0] = out[2] = (uint8_t)color;
    out[1] = out[3] = (uint8_t)(color >> 8);
}
static const uint16_t colors[4] = {0xF800,0x07E0,0x001F,0xFFFF}; /* red, green, blue, white */
static const float decoded[4][4] = {{1,0,0,1},{0,1,0,1},{0,0,1,1},{1,1,1,1}};

static WGPUTexture image(state *s, WGPUTextureFormat format, WGPUTextureDimension dimension,
    WGPUExtent3D size, uint32_t mips, WGPUTextureUsage usage)
{
    WGPUTextureDescriptor descriptor = WGPU_TEXTURE_DESCRIPTOR_INIT;
    descriptor.format = format;
    descriptor.dimension = dimension;
    descriptor.size = size;
    descriptor.mipLevelCount = mips;
    descriptor.usage = usage;
    return wgpuDeviceCreateTexture(s->device,&descriptor);
}
static void write_blocks(state *s, WGPUTexture texture, uint32_t mip, const void *data, size_t size,
    uint32_t row_bytes, uint32_t rows, WGPUExtent3D extent)
{
    WGPUTexelCopyTextureInfo destination = {.texture=texture,.mipLevel=mip,.aspect=WGPUTextureAspect_All};
    WGPUTexelCopyBufferLayout layout = {.bytesPerRow=row_bytes,.rowsPerImage=rows};
    wgpuQueueWriteTexture(s->queue,&destination,data,size,&layout,&extent);
}
/* Runs one compute shader that writes vec4f texels into o[], returns a copy. */
static float *load_texels(state *s, const char *code, WGPUTextureView view, size_t count)
{
    WGPUComputePipeline reader = pipeline(s,code);
    size_t bytes = count * 16;
    WGPUBuffer output = buffer(s,bytes,WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc);
    WGPUBuffer readback = buffer(s,bytes,WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst);
    WGPUBindGroupEntry entries[2] = {{.binding=0,.textureView=view},{.binding=1,.buffer=output,.size=bytes}};
    WGPUBindGroup bound = group(s,reader,entries,2);
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device,NULL);
    WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder,NULL);
    wgpuComputePassEncoderSetPipeline(pass,reader);
    wgpuComputePassEncoderSetBindGroup(pass,0,bound,0,NULL);
    wgpuComputePassEncoderDispatchWorkgroups(pass,1,1,1);
    wgpuComputePassEncoderEnd(pass);
    wgpuComputePassEncoderRelease(pass);
    wgpuCommandEncoderCopyBufferToBuffer(encoder,output,0,readback,0,bytes);
    submit(s,encoder);
    float *values = malloc(bytes);
    assert(values);
    memcpy(values,map(s,readback,bytes),bytes);
    wgpuBufferUnmap(readback);
    wgpuBindGroupRelease(bound);
    wgpuBufferRelease(readback);
    wgpuBufferRelease(output);
    wgpuComputePipelineRelease(reader);
    return values;
}
static void expect_texel(const float *values, size_t index, const float expected[4], const char *name)
{
    if (memcmp(values + index * 4,expected,16) != 0) {
        fprintf(stderr,"%s texel %zu: %g %g %g %g\n",name,index,values[index * 4],values[index * 4 + 1],
            values[index * 4 + 2],values[index * 4 + 3]);
        abort();
    }
}
static void expect_rejected_texture(state *s, WGPUTextureFormat format, WGPUTextureDimension dimension,
    WGPUExtent3D size, WGPUTextureUsage usage)
{
    wgpuDevicePushErrorScope(s->device,WGPUErrorFilter_Validation);
    assert(!image(s,format,dimension,size,1,usage));
    expect_validation(s);
}
static const WGPUTextureUsage sampled_copy = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopySrc |
    WGPUTextureUsage_CopyDst;

/* BC1 8x8 (four solid blocks) and a 12x12 two-mip BC1 whose 6x6 mip takes the
 * physical 8x8 copy; BC4 8x4 red 1.0/0.0. */
static void blocks_2d(state *s)
{
    uint8_t grid[2 * 16];
    for (size_t i = 0; i < 4; ++i) bc1(grid + i * 8,colors[i]);
    WGPUTexture source = image(s,WGPUTextureFormat_BC1RGBAUnorm,WGPUTextureDimension_2D,(WGPUExtent3D){8,8,1},1,sampled_copy);
    WGPUTexture copied = image(s,WGPUTextureFormat_BC1RGBAUnorm,WGPUTextureDimension_2D,(WGPUExtent3D){8,8,1},1,sampled_copy);
    WGPUTexture mipped = image(s,WGPUTextureFormat_BC1RGBAUnormSrgb,WGPUTextureDimension_2D,(WGPUExtent3D){12,12,1},2,sampled_copy);
    WGPUTexture red = image(s,WGPUTextureFormat_BC4RUnorm,WGPUTextureDimension_2D,(WGPUExtent3D){8,4,1},1,sampled_copy);
    assert(source && copied && mipped && red);
    write_blocks(s,source,0,grid,sizeof(grid),16,2,(WGPUExtent3D){8,8,1});
    /* The 6x6 mip is 2x2 blocks; its WebGPU copy names the physical 8x8 extent,
     * uploaded here from a 256-byte-pitch buffer. */
    uint8_t pitched[PITCH + 16] = {0};
    memcpy(pitched,grid,16);
    memcpy(pitched + PITCH,grid + 16,16);
    WGPUBuffer staging = buffer(s,sizeof(pitched),WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst);
    wgpuQueueWriteBuffer(s->queue,staging,0,pitched,sizeof(pitched));
    const uint8_t bc4[16] = {255,0,0,0,0,0,0,0, 0,255,0,0,0,0,0,0}; /* red0 1.0; red0 0.0 */
    write_blocks(s,red,0,bc4,sizeof(bc4),16,1,(WGPUExtent3D){8,4,1});
    WGPUBuffer readback = buffer(s,PITCH * 2,WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst);
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device,NULL);
    WGPUTexelCopyBufferInfo from_buffer = {.buffer=staging,.layout={.bytesPerRow=PITCH,.rowsPerImage=2}};
    WGPUTexelCopyTextureInfo mip_one = {.texture=mipped,.mipLevel=1,.aspect=WGPUTextureAspect_All};
    wgpuCommandEncoderCopyBufferToTexture(encoder,&from_buffer,&mip_one,&(WGPUExtent3D){8,8,1});
    wgpuCommandEncoderCopyTextureToTexture(encoder,&(WGPUTexelCopyTextureInfo){.texture=source},
        &(WGPUTexelCopyTextureInfo){.texture=copied},&(WGPUExtent3D){8,8,1});
    WGPUTexelCopyBufferInfo to_buffer = {.buffer=readback,.layout={.bytesPerRow=PITCH,.rowsPerImage=2}};
    wgpuCommandEncoderCopyTextureToBuffer(encoder,&(WGPUTexelCopyTextureInfo){.texture=copied},&to_buffer,
        &(WGPUExtent3D){8,8,1});
    submit(s,encoder);
    const uint8_t *bytes = map(s,readback,PITCH * 2);
    assert(!memcmp(bytes,grid,16) && !memcmp(bytes + PITCH,grid + 16,16));
    wgpuBufferUnmap(readback);

    WGPUTextureView source_view = wgpuTextureCreateView(source,NULL);
    float *values = load_texels(s,"@group(0) @binding(0) var t:texture_2d<f32>;"
        "@group(0) @binding(1) var<storage,read_write> o:array<vec4f>;"
        "@compute @workgroup_size(1) fn main(){for(var i=0u;i<64u;i++){o[i]=textureLoad(t,vec2i(i32(i%8u),i32(i/8u)),0);}}",
        source_view,64);
    for (size_t i = 0; i < 64; ++i) expect_texel(values,i,decoded[(i / 8 / 4) * 2 + (i % 8) / 4],"bc1 8x8");
    free(values);
    WGPUTextureView mip_view = wgpuTextureCreateView(mipped,NULL);
    values = load_texels(s,"@group(0) @binding(0) var t:texture_2d<f32>;"
        "@group(0) @binding(1) var<storage,read_write> o:array<vec4f>;"
        "@compute @workgroup_size(1) fn main(){for(var i=0u;i<36u;i++){o[i]=textureLoad(t,vec2i(i32(i%6u),i32(i/6u)),1);}"
        "o[36]=vec4f(vec2f(textureDimensions(t,1)),0.0,0.0);}",mip_view,37);
    /* sRGB endpoints 0 and 255 decode to exactly 0.0 and 1.0. */
    for (size_t i = 0; i < 36; ++i) expect_texel(values,i,decoded[(i / 6 / 4) * 2 + (i % 6) / 4],"bc1 srgb mip 1");
    const float mip_size[4] = {6,6,0,0};
    expect_texel(values,36,mip_size,"mip 1 dimensions");
    free(values);
    WGPUTextureView red_view = wgpuTextureCreateView(red,NULL);
    values = load_texels(s,"@group(0) @binding(0) var t:texture_2d<f32>;"
        "@group(0) @binding(1) var<storage,read_write> o:array<vec4f>;"
        "@compute @workgroup_size(1) fn main(){for(var i=0u;i<32u;i++){o[i]=textureLoad(t,vec2i(i32(i%8u),i32(i/8u)),0);}}",
        red_view,32);
    const float one[4] = {1,0,0,1}, zero[4] = {0,0,0,1};
    for (size_t i = 0; i < 32; ++i) expect_texel(values,i,i % 8 < 4 ? one : zero,"bc4");
    free(values);
    wgpuTextureViewRelease(red_view);
    wgpuTextureViewRelease(mip_view);
    wgpuTextureViewRelease(source_view);
    wgpuBufferRelease(readback);
    wgpuBufferRelease(staging);
    wgpuTextureRelease(red);
    wgpuTextureRelease(mipped);
    wgpuTextureRelease(copied);
    wgpuTextureRelease(source);
}

/* Shape, usage and copy-alignment rejections on a BC device; 3D needs sliced 3D. */
static void block_controls(state *s, bool sliced)
{
    expect_rejected_texture(s,WGPUTextureFormat_BC1RGBAUnorm,WGPUTextureDimension_2D,(WGPUExtent3D){6,8,1},sampled_copy);
    expect_rejected_texture(s,WGPUTextureFormat_BC1RGBAUnorm,WGPUTextureDimension_2D,(WGPUExtent3D){8,8,1},
        WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding);
    expect_rejected_texture(s,WGPUTextureFormat_BC7RGBAUnorm,WGPUTextureDimension_2D,(WGPUExtent3D){8,8,1},
        WGPUTextureUsage_StorageBinding);
    if (!sliced) expect_rejected_texture(s,WGPUTextureFormat_BC1RGBAUnorm,WGPUTextureDimension_3D,(WGPUExtent3D){4,4,2},sampled_copy);
    WGPUTexture texture = image(s,WGPUTextureFormat_BC1RGBAUnorm,WGPUTextureDimension_2D,(WGPUExtent3D){8,8,1},1,sampled_copy);
    WGPUBuffer bytes = buffer(s,PITCH * 2,WGPUBufferUsage_CopyDst);
    const struct { uint32_t x, width; } copies[2] = {{2,4},{0,6}}; /* unaligned origin; partial block */
    for (size_t i = 0; i < 2; ++i) {
        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device,NULL);
        WGPUTexelCopyTextureInfo source = {.texture=texture,.origin={copies[i].x,0,0},.aspect=WGPUTextureAspect_All};
        WGPUTexelCopyBufferInfo destination = {.buffer=bytes,.layout={.bytesPerRow=PITCH,.rowsPerImage=1}};
        wgpuCommandEncoderCopyTextureToBuffer(encoder,&source,&destination,&(WGPUExtent3D){copies[i].width,4,1});
        wgpuDevicePushErrorScope(s->device,WGPUErrorFilter_Validation);
        assert(!wgpuCommandEncoderFinish(encoder,NULL));
        expect_validation(s);
        wgpuCommandEncoderRelease(encoder);
    }
    wgpuBufferRelease(bytes);
    wgpuTextureRelease(texture);
}

/* Reads whole blocks of mip `mip` (blocks_wide x blocks_high) into out. */
static void read_blocks(state *s, WGPUTexture texture, uint32_t mip, uint32_t layer, uint32_t blocks_wide,
    uint32_t blocks_high, uint8_t *out)
{
    WGPUBuffer readback = buffer(s,PITCH * blocks_high,WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst);
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device,NULL);
    WGPUTexelCopyTextureInfo source = {.texture=texture,.mipLevel=mip,.origin={0,0,layer},.aspect=WGPUTextureAspect_All};
    WGPUTexelCopyBufferInfo destination = {.buffer=readback,.layout={.bytesPerRow=PITCH,.rowsPerImage=blocks_high}};
    wgpuCommandEncoderCopyTextureToBuffer(encoder,&source,&destination,&(WGPUExtent3D){blocks_wide * 4,blocks_high * 4,1});
    submit(s,encoder);
    const uint8_t *bytes = map(s,readback,PITCH * blocks_high);
    for (uint32_t row = 0; row < blocks_high; ++row) memcpy(out + row * blocks_wide * 8,bytes + row * PITCH,blocks_wide * 8);
    wgpuBufferUnmap(readback);
    wgpuBufferRelease(readback);
}

/* Copies whose block extent reaches one side's edge but not the other's,
 * whole-block queue writes, srgb view formats and a large zero-initialized texture. */
static void blocks_edges(state *s)
{
    uint8_t grid[4 * 8];
    for (size_t i = 0; i < 4; ++i) bc1(grid + i * 8,colors[i]);
    uint8_t gray[8];
    bc1(gray,0x7BEF);
    WGPUTexture edge = image(s,WGPUTextureFormat_BC1RGBAUnorm,WGPUTextureDimension_2D,(WGPUExtent3D){12,12,1},2,sampled_copy);
    WGPUTexture wide = image(s,WGPUTextureFormat_BC1RGBAUnorm,WGPUTextureDimension_2D,(WGPUExtent3D){16,16,1},1,sampled_copy);
    assert(edge && wide);
    write_blocks(s,edge,1,grid,sizeof(grid),16,2,(WGPUExtent3D){8,8,1}); /* 6x6 mip: physical 8x8 */
    WGPUTexelCopyTextureInfo middle = {.texture=wide,.origin={8,8,0},.aspect=WGPUTextureAspect_All};
    wgpuQueueWriteTexture(s->queue,&middle,gray,8,&(WGPUTexelCopyBufferLayout){.bytesPerRow=8,.rowsPerImage=1},
        &(WGPUExtent3D){4,4,1});
    /* Edge to middle: mip 1 block (1,0) (2 texels wide there) into wide block (1,0);
     * middle to edge: wide block (2,2) into mip 1 block (1,1). */
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device,NULL);
    wgpuCommandEncoderCopyTextureToTexture(encoder,&(WGPUTexelCopyTextureInfo){.texture=edge,.mipLevel=1,.origin={4,0,0}},
        &(WGPUTexelCopyTextureInfo){.texture=wide,.origin={4,0,0}},&(WGPUExtent3D){4,4,1});
    wgpuCommandEncoderCopyTextureToTexture(encoder,&middle,
        &(WGPUTexelCopyTextureInfo){.texture=edge,.mipLevel=1,.origin={4,4,0}},&(WGPUExtent3D){4,4,1});
    submit(s,encoder);
    uint8_t wide_blocks[16 * 8], edge_blocks[4 * 8], expected_wide[16 * 8] = {0};
    read_blocks(s,wide,0,0,4,4,wide_blocks);
    memcpy(expected_wide + 1 * 8,grid + 1 * 8,8);     /* green */
    memcpy(expected_wide + 10 * 8,gray,8);            /* block (2,2) */
    assert(!memcmp(wide_blocks,expected_wide,sizeof(wide_blocks)));
    read_blocks(s,edge,1,0,2,2,edge_blocks);
    assert(!memcmp(edge_blocks,grid,24) && !memcmp(edge_blocks + 24,gray,8));

    /* writeTexture needs whole blocks: 2x2 into a 2x2 mip rejects, 4x4 passes. */
    WGPUTexture small = image(s,WGPUTextureFormat_BC1RGBAUnorm,WGPUTextureDimension_2D,(WGPUExtent3D){8,8,1},3,sampled_copy);
    WGPUTexelCopyTextureInfo tiny = {.texture=small,.mipLevel=2,.aspect=WGPUTextureAspect_All};
    const WGPUTexelCopyBufferLayout one_block = {.bytesPerRow=8,.rowsPerImage=1};
    wgpuDevicePushErrorScope(s->device,WGPUErrorFilter_Validation);
    wgpuQueueWriteTexture(s->queue,&tiny,gray,8,&one_block,&(WGPUExtent3D){2,2,1});
    expect_validation(s);
    wgpuQueueWriteTexture(s->queue,&tiny,gray,8,&one_block,&(WGPUExtent3D){4,4,1});
    uint8_t written[8];
    read_blocks(s,small,2,0,1,1,written);
    assert(!memcmp(written,gray,8));

    /* writeTexture data needs no block alignment: odd offsets and a bytesPerRow
     * of 8*w+3 into a two-layer BC1 array with rowsPerImage 3, read back exactly. */
    WGPUTextureDescriptor layered_desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    layered_desc.format = WGPUTextureFormat_BC1RGBAUnorm;
    layered_desc.usage = sampled_copy;
    layered_desc.size = (WGPUExtent3D){8,8,2};
    WGPUTexture layered = wgpuDeviceCreateTexture(s->device,&layered_desc);
    assert(layered);
    enum { ODD_OFFSET = 7, ODD_PITCH = 8 * 2 + 3, ODD_ROWS = 3 };
    uint8_t odd[ODD_OFFSET + ODD_PITCH * ODD_ROWS * 2];
    for (size_t i = 0; i < sizeof(odd); ++i) odd[i] = (uint8_t)(0xA5 ^ i); /* filler outside the blocks */
    uint8_t layer_blocks[2][4 * 8];
    for (size_t layer = 0; layer < 2; ++layer)
        for (size_t block = 0; block < 4; ++block) {
            bc1(layer_blocks[layer] + block * 8,layer ? colors[3 - block] : colors[block]);
            memcpy(odd + ODD_OFFSET + layer * ODD_PITCH * ODD_ROWS + (block / 2) * ODD_PITCH + (block % 2) * 8,
                layer_blocks[layer] + block * 8,8);
        }
    WGPUTexelCopyTextureInfo layered_target = {.texture=layered,.aspect=WGPUTextureAspect_All};
    wgpuQueueWriteTexture(s->queue,&layered_target,odd,sizeof(odd),
        &(WGPUTexelCopyBufferLayout){.offset=ODD_OFFSET,.bytesPerRow=ODD_PITCH,.rowsPerImage=ODD_ROWS},
        &(WGPUExtent3D){8,8,2});
    for (uint32_t layer = 0; layer < 2; ++layer) {
        uint8_t back[4 * 8];
        read_blocks(s,layered,0,layer,2,2,back);
        assert(!memcmp(back,layer_blocks[layer],sizeof(back)));
    }
    /* A single odd-offset row with an odd pitch into one block row. */
    uint8_t row[3 + 16];
    memcpy(row + 3,gray,8);
    memcpy(row + 11,gray,8);
    WGPUTexelCopyTextureInfo second_row = {.texture=layered,.origin={0,4,1},.aspect=WGPUTextureAspect_All};
    wgpuQueueWriteTexture(s->queue,&second_row,row,sizeof(row),
        &(WGPUTexelCopyBufferLayout){.offset=3,.bytesPerRow=17,.rowsPerImage=1},&(WGPUExtent3D){8,4,1});
    uint8_t back[4 * 8];
    read_blocks(s,layered,0,1,2,2,back);
    assert(!memcmp(back,layer_blocks[1],16) && !memcmp(back + 16,gray,8) && !memcmp(back + 24,gray,8));
    wgpuTextureRelease(layered);

    /* srgb view formats on BC: the same bytes decode darker through the srgb view. */
    const WGPUTextureFormat bc7_srgb = WGPUTextureFormat_BC7RGBAUnormSrgb, bc1_srgb = WGPUTextureFormat_BC1RGBAUnormSrgb;
    WGPUTextureDescriptor described = WGPU_TEXTURE_DESCRIPTOR_INIT;
    described.format = WGPUTextureFormat_BC7RGBAUnorm;
    described.usage = sampled_copy;
    described.size = (WGPUExtent3D){8,8,1};
    described.viewFormatCount = 1;
    described.viewFormats = &bc7_srgb;
    WGPUTexture bc7 = wgpuDeviceCreateTexture(s->device,&described);
    assert(bc7);
    WGPUTextureViewDescriptor srgb_view_desc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    srgb_view_desc.format = bc7_srgb;
    WGPUTextureView bc7_view = wgpuTextureCreateView(bc7,&srgb_view_desc);
    assert(bc7_view);
    wgpuTextureViewRelease(bc7_view);
    wgpuTextureRelease(bc7);
    described.format = WGPUTextureFormat_BC1RGBAUnorm;
    described.size = (WGPUExtent3D){4,4,1};
    described.viewFormats = &bc1_srgb;
    WGPUTexture bc1_texture = wgpuDeviceCreateTexture(s->device,&described);
    assert(bc1_texture);
    write_blocks(s,bc1_texture,0,gray,8,8,1,(WGPUExtent3D){4,4,1});
    srgb_view_desc.format = bc1_srgb;
    WGPUTextureView views[2] = {wgpuTextureCreateView(bc1_texture,NULL),wgpuTextureCreateView(bc1_texture,&srgb_view_desc)};
    assert(views[0] && views[1]);
    const char *const load = "@group(0) @binding(0) var t:texture_2d<f32>;"
        "@group(0) @binding(1) var<storage,read_write> o:array<vec4f>;"
        "@compute @workgroup_size(1) fn main(){o[0]=textureLoad(t,vec2i(0,0),0);}";
    float *linear = load_texels(s,load,views[0],1), *srgb = load_texels(s,load,views[1],1);
    assert(linear[3] == 1.0f && srgb[3] == 1.0f && srgb[0] < linear[0] - 0.2f && srgb[0] > 0.0f);
    free(linear);
    free(srgb);
    wgpuTextureViewRelease(views[0]);
    wgpuTextureViewRelease(views[1]);
    /* Without the listed view format, the srgb view rejects. */
    WGPUTextureView unlisted = NULL;
    wgpuDevicePushErrorScope(s->device,WGPUErrorFilter_Validation);
    unlisted = wgpuTextureCreateView(small,&srgb_view_desc);
    assert(!unlisted);
    expect_validation(s);
    wgpuTextureRelease(bc1_texture);

    /* Zero staging is block-sized: 8192x8192x8 BC1 is 256 MiB of blocks. */
    WGPUTexture large = image(s,WGPUTextureFormat_BC1RGBAUnorm,WGPUTextureDimension_2D,(WGPUExtent3D){8192,8192,8},1,sampled_copy);
    assert(large);
    uint8_t zero_block[8], zeros[8] = {0};
    read_blocks(s,large,0,7,1,1,zero_block);
    assert(!memcmp(zero_block,zeros,8));
    wgpuTextureRelease(large);
    wgpuTextureRelease(small);
    wgpuTextureRelease(wide);
    wgpuTextureRelease(edge);
}

/* 3D BC1 4x4x2: slice 0 red, slice 1 blue. */
static void blocks_3d(state *s)
{
    uint8_t slices[16];
    bc1(slices,colors[0]);
    bc1(slices + 8,colors[2]);
    WGPUTexture volume = image(s,WGPUTextureFormat_BC1RGBAUnorm,WGPUTextureDimension_3D,(WGPUExtent3D){4,4,2},1,sampled_copy);
    assert(volume);
    /* Slices sit two block rows apart (rowsPerImage 2 > the one block row). */
    uint8_t pitched[3 * PITCH] = {0};
    memcpy(pitched,slices,8);
    memcpy(pitched + 2 * PITCH,slices + 8,8);
    WGPUBuffer staging = buffer(s,sizeof(pitched),WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst);
    wgpuQueueWriteBuffer(s->queue,staging,0,pitched,sizeof(pitched));
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device,NULL);
    WGPUTexelCopyBufferInfo from = {.buffer=staging,.layout={.bytesPerRow=PITCH,.rowsPerImage=2}};
    wgpuCommandEncoderCopyBufferToTexture(encoder,&from,&(WGPUTexelCopyTextureInfo){.texture=volume},
        &(WGPUExtent3D){4,4,2});
    submit(s,encoder);
    wgpuBufferRelease(staging);
    WGPUTextureView view = wgpuTextureCreateView(volume,NULL);
    float *values = load_texels(s,"@group(0) @binding(0) var t:texture_3d<f32>;"
        "@group(0) @binding(1) var<storage,read_write> o:array<vec4f>;"
        "@compute @workgroup_size(1) fn main(){for(var i=0u;i<32u;i++){"
        "o[i]=textureLoad(t,vec3i(i32(i%4u),i32((i/4u)%4u),i32(i/16u)),0);}}",view,32);
    for (size_t i = 0; i < 32; ++i) expect_texel(values,i,decoded[i < 16 ? 0 : 2],"bc1 3d");
    free(values);
    wgpuTextureViewRelease(view);
    wgpuTextureRelease(volume);
}

int main(void)
{
    state s = {0};
    consumer_init(&s);
    bool bc = wgpuAdapterHasFeature(s.adapter,WGPUFeatureName_TextureCompressionBC);
    bool sliced = wgpuAdapterHasFeature(s.adapter,WGPUFeatureName_TextureCompressionBCSliced3D);
    assert(bc || !sliced);
    expect_rejected_texture(&s,WGPUTextureFormat_BC1RGBAUnorm,WGPUTextureDimension_2D,(WGPUExtent3D){8,8,1},sampled_copy);
    consumer_finish(&s);
    if (!bc) {
        printf("SKIP texture-compression-bc: the Vulkan device does not offer textureCompressionBC with sampled, "
            "filterable, copyable BC formats %s\n",adapter_class_scope());
        return 0;
    }
    state enabled = {0};
    const WGPUFeatureName only_bc = WGPUFeatureName_TextureCompressionBC;
    consumer_init_features(&enabled,&only_bc,1);
    blocks_2d(&enabled);
    blocks_edges(&enabled);
    block_controls(&enabled,false);
    consumer_finish(&enabled);
    if (!sliced) {
        printf("SKIP texture-compression-bc-sliced-3d: the Vulkan device lacks 3D images of every BC format %s\n",
            adapter_class_scope());
    } else {
        state volume = {0};
        const WGPUFeatureName both[2] = {WGPUFeatureName_TextureCompressionBC,WGPUFeatureName_TextureCompressionBCSliced3D};
        consumer_init_features(&volume,both,2);
        blocks_3d(&volume);
        block_controls(&volume,true);
        consumer_finish(&volume);
    }
    printf("PASS public BC compression: solid BC1/BC1-srgb/BC4 blocks decode to exact texels through queue writes "
        "and buffer copies, a 6x6 mip takes its physical 8x8 copy, texture-to-texture and texture-to-buffer "
        "copies return exact blocks, copies whose block reaches only one side's edge (both directions) land "
        "exactly, writeTexture of whole blocks into a 2x2 mip and with odd offsets and bytesPerRow 8*w+3 "
        "(two layers, rowsPerImage 3), bc1/bc7 srgb view formats, a zeroed "
        "8192x8192x8 BC1 texture%s; without the feature, a partial-block writeTexture, non-block sizes, attachment/storage usage, 3D without "
        "sliced 3D and unaligned copies reject %s\n",
        sliced ? ", a 3D BC1 texture uploaded with rowsPerImage 2 decodes per slice (sliced 3D)" : "", adapter_class_scope());
    return 0;
}
