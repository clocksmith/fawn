/* SPDX-License-Identifier: MIT
 * Compose D3D12's reported limits with actual shared render-layout admission.
 * Pure host control: native tokens exercise ownership, never a GPU or SDK. */
#include "../src/d3d12_frontend.h"
#include "../../backends/d3d12/include/doe_d3d12_render.h"
#include "../../backends/d3d12/include/doe_d3d12_texture.h"
#include "../../resources/tests/fixture.h"

static void admission(fixture *f,const doe_resource_config *c,const WGPUTextureFormat *formats,
    uint32_t count,doe_resource_status expected)
{
    doe_resource_pipeline_layout *layout=NULL;
    assert(doe_resource_pipeline_layout_create(c,NULL,0,0,&layout)==DOE_RESOURCE_OK);
    doe_resource_render_pipeline_desc desc={.samples=1,.color_count=count};
    memcpy(desc.color_formats,formats,count*sizeof(*formats));
    native_token token={0};
    doe_resource_render_pipeline *pipeline=NULL;
    assert(doe_resource_render_pipeline_create(c,layout,&desc,native(f,&token),&pipeline)==expected);
    if (expected==DOE_RESOURCE_OK) {
        assert(pipeline);
        doe_resource_render_pipeline_release(pipeline);
        assert(token.releases==1);
    } else assert(!pipeline && !token.releases);
    doe_resource_pipeline_layout_release(layout);
    empty(f);
}

int main(void)
{
    doe_dx_caps caps={.feature_level=0xb000,.limits={.max_buffer_size=UINT64_C(268435456),
        .uniform_offset_alignment=256,.max_descriptors=64}};
    WGPULimits limits=WGPU_LIMITS_INIT;
    assert(native_d3d12_limits(&caps,&limits)==WGPUStatus_Success);
    assert(limits.maxTextureDimension1D==DOE_DX_MAX_TEXTURE_DIMENSION_1D);
    assert(limits.maxTextureDimension3D==DOE_DX_MAX_TEXTURE_DIMENSION_3D);
    assert(limits.maxTextureArrayLayers==DOE_DX_MAX_TEXTURE_ARRAY_LAYERS);
    assert(limits.maxColorAttachments==DOE_DX_MAX_COLOR_TARGETS);
    assert(limits.maxColorAttachmentBytesPerSample==32);
    WGPULimits unchanged=limits;
    caps.feature_level=0;
    assert(native_d3d12_limits(&caps,&limits)==WGPUStatus_Error);
    assert(!memcmp(&limits,&unchanged,sizeof(limits)));
    fixture f=fresh();
    doe_resource_config c=config(&f);
    c.max_color_attachments=limits.maxColorAttachments;
    c.max_color_attachment_bytes_per_sample=limits.maxColorAttachmentBytesPerSample;
    const WGPUTextureFormat formats[]={WGPUTextureFormat_RGBA8Unorm,WGPUTextureFormat_BGRA8Unorm};
    for (size_t i=0;i<sizeof(formats)/sizeof(*formats);++i)
        admission(&f,&c,&formats[i],1,DOE_RESOURCE_OK);
    const WGPUTextureFormat large[]={WGPUTextureFormat_RGBA32Uint,WGPUTextureFormat_RGBA32Uint,
        WGPUTextureFormat_RGBA32Uint};
    admission(&f,&c,large,2,DOE_RESOURCE_OK);
    admission(&f,&c,large,3,DOE_RESOURCE_RANGE);
    const WGPUTextureFormat slots[DOE_DX_MAX_COLOR_TARGETS]={[DOE_DX_MAX_COLOR_TARGETS-1]=WGPUTextureFormat_RGBA8Unorm};
    admission(&f,&c,slots,DOE_DX_MAX_COLOR_TARGETS,DOE_RESOURCE_OK);
    c.max_color_attachments=DOE_DX_MAX_COLOR_TARGETS-1;
    admission(&f,&c,slots,DOE_DX_MAX_COLOR_TARGETS,DOE_RESOURCE_RANGE);
    c.max_color_attachments=limits.maxColorAttachments;
    /* Reproduce the former mistake without changing the canonical format cost:
     * four storage bytes do not satisfy UNORM's eight render-budget bytes. */
    c.max_color_attachment_bytes_per_sample=4;
    for (size_t i=0;i<sizeof(formats)/sizeof(*formats);++i)
        admission(&f,&c,&formats[i],1,DOE_RESOURCE_RANGE);
    puts("PASS D3D12 limits compose with shared RGBA/BGRA render admission; MRT slot/byte budgets, queried-feature-level rejection, exact cleanup; host only");
    return 0;
}
