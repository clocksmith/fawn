/* SPDX-License-Identifier: MIT
 * Canonical public vertex conversion, no SDK/device or invented fetch. */
#include "../src/d3d12_graphics_internal.h"
#include "d3d12_vertex_cases.h"
#include <assert.h>
#include <stdio.h>

static const doe_dx_vertex_format expected[]={
    DOE_DX_UINT8,
    DOE_DX_UINT8X2,
    DOE_DX_UINT8X4,
    DOE_DX_SINT8,
    DOE_DX_SINT8X2,
    DOE_DX_SINT8X4,
    DOE_DX_UNORM8,
    DOE_DX_UNORM8X2,
    DOE_DX_UNORM8X4,
    DOE_DX_SNORM8,
    DOE_DX_SNORM8X2,
    DOE_DX_SNORM8X4,
    DOE_DX_UINT16,
    DOE_DX_UINT16X2,
    DOE_DX_UINT16X4,
    DOE_DX_SINT16,
    DOE_DX_SINT16X2,
    DOE_DX_SINT16X4,
    DOE_DX_UNORM16,
    DOE_DX_UNORM16X2,
    DOE_DX_UNORM16X4,
    DOE_DX_SNORM16,
    DOE_DX_SNORM16X2,
    DOE_DX_SNORM16X4,
    DOE_DX_FLOAT16,
    DOE_DX_FLOAT16X2,
    DOE_DX_FLOAT16X4,
    DOE_DX_FLOAT32,
    DOE_DX_FLOAT32X3,
    DOE_DX_UINT32X2,
    DOE_DX_SINT32X4,
    DOE_DX_UNORM10_10_10_2,
    DOE_DX_UNORM8X4_BGRA,
    DOE_DX_FLOAT32X2,
    DOE_DX_FLOAT32X4,
    DOE_DX_UINT32,
    DOE_DX_UINT32X3,
    DOE_DX_UINT32X4,
    DOE_DX_SINT32,
    DOE_DX_SINT32X2,
    DOE_DX_SINT32X3,
};
int main(void)
{
    native_device device={0};
    device.backend.limits=(WGPULimits){.maxVertexBuffers=8,.maxVertexAttributes=16,.maxVertexBufferArrayStride=2048};
    WGPUVertexAttribute attribute={.offset=8};
    WGPUVertexBufferLayout source_layouts[3]={{0},{.stepMode=WGPUVertexStepMode_Vertex},
        {.arrayStride=32,.stepMode=WGPUVertexStepMode_Instance,.attributeCount=1,.attributes=&attribute}};
    WGPUVertexState source={.bufferCount=3,.buffers=source_layouts};
    for (size_t i=0;i<sizeof(cases)/sizeof(*cases);++i) {
        attribute.format=cases[i].format;
        doe_dx_render_pipeline_desc native={0}; doe_resource_render_pipeline_desc resource={0};
        doe_dx_vertex_layout layouts[8]; doe_dx_vertex_attribute attributes[16];
        assert(native_d3d12_graphics_vertices(&source,&device,&native,&resource,layouts,attributes));
        assert(native.vertex_buffer_count==3 && native.attribute_count==1 && native.vertex_buffers==layouts &&
            native.attributes==attributes && attributes[0].format==expected[i] && attributes[0].buffer_slot==2 &&
            attributes[0].offset==8 && layouts[2].stride==32 && layouts[2].step==DOE_DX_PER_INSTANCE);
        assert(resource.vertices[0].step_mode==WGPUVertexStepMode_Undefined &&
            resource.vertices[1].step_mode==WGPUVertexStepMode_Vertex && !resource.vertices[1].last_attribute_end);
        assert(resource.vertices[2].last_attribute_end>8 && resource.vertices[2].last_attribute_end<=24);
    }
    doe_dx_render_pipeline_desc native={0}; doe_resource_render_pipeline_desc resource={0};
    doe_dx_vertex_layout layouts[8]; doe_dx_vertex_attribute attributes[16];
    attribute.format=WGPUVertexFormat_Unorm8; attribute.offset=3;
    source_layouts[2].arrayStride=0;
    assert(native_d3d12_graphics_vertices(&source,&device,&native,&resource,layouts,attributes));
    assert(layouts[2].stride==0 && attributes[0].offset==3 && resource.vertices[2].last_attribute_end==4);
    doe_dx_render_pipeline_desc prior=native;
    doe_resource_render_pipeline_desc old_shape=resource;
    doe_dx_vertex_layout old_layouts[8]; doe_dx_vertex_attribute old_attributes[16];
    memcpy(old_layouts,layouts,sizeof(layouts)); memcpy(old_attributes,attributes,sizeof(attributes));
    attribute.format=WGPUVertexFormat_Float16x4; attribute.offset=2;
    assert(!native_d3d12_graphics_vertices(&source,&device,&native,&resource,layouts,attributes));
    assert(!memcmp(&native,&prior,sizeof(native)) && !memcmp(&resource,&old_shape,sizeof(resource)) &&
        !memcmp(layouts,old_layouts,sizeof(layouts)) && !memcmp(attributes,old_attributes,sizeof(attributes)));
    native=(doe_dx_render_pipeline_desc){0}; attribute.offset=8; attribute.shaderLocation=16;
    assert(!native_d3d12_graphics_vertices(&source,&device,&native,&resource,layouts,attributes));
    native=(doe_dx_render_pipeline_desc){0}; attribute.shaderLocation=0; source_layouts[2].arrayStride=2049;
    assert(!native_d3d12_graphics_vertices(&source,&device,&native,&resource,layouts,attributes));
    native=(doe_dx_render_pipeline_desc){0}; source_layouts[2].arrayStride=32;
    device.backend.limits.maxVertexBuffers=2;
    assert(!native_d3d12_graphics_vertices(&source,&device,&native,&resource,layouts,attributes));
    puts("PASS canonical 41-format D3D12 vertex conversion, public holes/required empty slots, zero stride, byte alignment and actual device-limit admission; CPU owner only");
    return 0;
}
