/* SPDX-License-Identifier: MIT
 * Compose D3D12's reported limits with actual shared render-layout admission.
 * Pure host control: native tokens exercise ownership, never a GPU or SDK. */
#include "../src/d3d12_frontend.h"
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
    doe_dx_caps caps={.limits={.max_buffer_size=UINT64_C(268435456),
        .uniform_offset_alignment=256,.max_descriptors=64}};
    WGPULimits limits=WGPU_LIMITS_INIT;
    assert(native_d3d12_limits(&caps,&limits)==WGPUStatus_Success);
    fixture f=fresh();
    doe_resource_config c=config(&f);
    c.max_color_attachments=limits.maxColorAttachments;
    c.max_color_attachment_bytes_per_sample=limits.maxColorAttachmentBytesPerSample;
    const WGPUTextureFormat formats[]={WGPUTextureFormat_RGBA8Unorm,WGPUTextureFormat_BGRA8Unorm};
    for (size_t i=0;i<sizeof(formats)/sizeof(*formats);++i)
        admission(&f,&c,&formats[i],1,DOE_RESOURCE_OK);
    const WGPUTextureFormat large=WGPUTextureFormat_RGBA32Uint;
    admission(&f,&c,&large,1,DOE_RESOURCE_RANGE);
    const WGPUTextureFormat extra_slot[]={WGPUTextureFormat_RGBA8Unorm,WGPUTextureFormat_Undefined};
    admission(&f,&c,extra_slot,2,DOE_RESOURCE_RANGE);
    /* Reproduce the former mistake without changing the canonical format cost:
     * four storage bytes do not satisfy UNORM's eight render-budget bytes. */
    c.max_color_attachment_bytes_per_sample=4;
    for (size_t i=0;i<sizeof(formats)/sizeof(*formats);++i)
        admission(&f,&c,&formats[i],1,DOE_RESOURCE_RANGE);
    puts("PASS D3D12 limits compose with shared RGBA/BGRA render admission; storage-byte budget, oversized format and extra-slot rejection, exact cleanup; host only");
    return 0;
}
