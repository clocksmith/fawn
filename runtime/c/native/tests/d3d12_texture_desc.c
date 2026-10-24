/* SPDX-License-Identifier: MIT */
#include "../src/d3d12_texture_desc.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void)
{
    doe_resource_texture_desc desc = {.dimension=WGPUTextureDimension_2D,.format=WGPUTextureFormat_RGBA8Unorm,
        .width=3,.height=2,.depth_or_layers=1,.mip_levels=1,.samples=1,
        .usage=WGPUTextureUsage_CopySrc|WGPUTextureUsage_TextureBinding};
    doe_dx_texture_desc native = {0};
    assert(native_d3d12_texture_descriptor(&desc,&native));
    assert(native.width == 3 && native.height == 2 && native.format == DOE_DX_RGBA8_UNORM &&
        native.usage == (DOE_DX_TEXTURE_COPY_SRC|DOE_DX_TEXTURE_COPY_DST|DOE_DX_TEXTURE_SAMPLED));
    doe_dx_texture_desc saved = native;
    const WGPUTextureFormat absent[] = {WGPUTextureFormat_RGBA8UnormSrgb,WGPUTextureFormat_Depth32Float,
        WGPUTextureFormat_RGBA16Float,WGPUTextureFormat_BC1RGBAUnorm};
    for (size_t i = 0; i < sizeof(absent)/sizeof(*absent); ++i) {
        desc.format = absent[i];
        assert(!native_d3d12_texture_descriptor(&desc,&native) && !memcmp(&native,&saved,sizeof(saved)));
    }
    const WGPUTextureFormat formats[] = {WGPUTextureFormat_RGBA8Uint,WGPUTextureFormat_R32Float,
        WGPUTextureFormat_R32Uint,WGPUTextureFormat_BGRA8Unorm};
    for (size_t i = 0; i < sizeof(formats)/sizeof(*formats); ++i) {
        desc.format = formats[i];
        assert(native_d3d12_texture_descriptor(&desc,&native));
    }
    desc.samples = 4;
    assert(!native_d3d12_texture_descriptor(&desc,&native));
    desc.samples = 1; desc.mip_levels = 2;
    assert(!native_d3d12_texture_descriptor(&desc,&native));
    desc.mip_levels = 1; desc.depth_or_layers = 2;
    assert(!native_d3d12_texture_descriptor(&desc,&native));
    desc.depth_or_layers = 1; desc.usage |= WGPUTextureUsage_RenderAttachment;
    assert(!native_d3d12_texture_descriptor(&desc,&native));
    doe_resource_texture_copy copy = {.width=2,.height=3,.depth_or_layers=1,.origin={4,5,0},.aspect=WGPUTextureAspect_All};
    doe_dx_texture_region region = {0};
    assert(native_d3d12_texture_region(&copy,&region) && region.x == 4 && region.y == 5 && region.height == 3);
    doe_dx_texture_region kept = region;
    copy.mip = 1;
    assert(!native_d3d12_texture_region(&copy,&region) && !memcmp(&region,&kept,sizeof(kept)));
    WGPUSamplerDescriptor sampler = WGPU_SAMPLER_DESCRIPTOR_INIT;
    sampler.minFilter = sampler.magFilter = WGPUFilterMode_Nearest;
    sampler.maxAnisotropy = 1;
    assert(native_d3d12_sampler_supported(&sampler));
    sampler.magFilter = WGPUFilterMode_Linear;
    assert(!native_d3d12_sampler_supported(&sampler));
    sampler.minFilter = WGPUFilterMode_Linear;
    assert(native_d3d12_sampler_supported(&sampler));
    sampler.compare = WGPUCompareFunction_Less;
    assert(!native_d3d12_sampler_supported(&sampler));
    sampler.compare = WGPUCompareFunction_Undefined; sampler.lodMinClamp = 1;
    assert(!native_d3d12_sampler_supported(&sampler));
    puts("PASS D3D12 texture descriptor/control: exact formats/uses, shape/region/immutable sampler rejection, untouched failures; no SDK/GPU");
}
