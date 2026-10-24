/* SPDX-License-Identifier: MIT */
#include "d3d12_graphics_internal.h"

/* Public scalar/byte/alignment admission remains the common typed owner.
 * These names select D3D12 input formats, never enum casts or shader rewrites. */
static bool vertex_format(WGPUVertexFormat source,doe_dx_vertex_format *out)
{
    switch (source) {
    case WGPUVertexFormat_Uint8: *out=DOE_DX_UINT8; return true;
    case WGPUVertexFormat_Sint8: *out=DOE_DX_SINT8; return true;
    case WGPUVertexFormat_Unorm8: *out=DOE_DX_UNORM8; return true;
    case WGPUVertexFormat_Snorm8: *out=DOE_DX_SNORM8; return true;
    case WGPUVertexFormat_Uint8x2: *out=DOE_DX_UINT8X2; return true;
    case WGPUVertexFormat_Sint8x2: *out=DOE_DX_SINT8X2; return true;
    case WGPUVertexFormat_Unorm8x2: *out=DOE_DX_UNORM8X2; return true;
    case WGPUVertexFormat_Snorm8x2: *out=DOE_DX_SNORM8X2; return true;
    case WGPUVertexFormat_Uint8x4: *out=DOE_DX_UINT8X4; return true;
    case WGPUVertexFormat_Sint8x4: *out=DOE_DX_SINT8X4; return true;
    case WGPUVertexFormat_Unorm8x4: *out=DOE_DX_UNORM8X4; return true;
    case WGPUVertexFormat_Snorm8x4: *out=DOE_DX_SNORM8X4; return true;
    case WGPUVertexFormat_Uint16: *out=DOE_DX_UINT16; return true;
    case WGPUVertexFormat_Sint16: *out=DOE_DX_SINT16; return true;
    case WGPUVertexFormat_Unorm16: *out=DOE_DX_UNORM16; return true;
    case WGPUVertexFormat_Snorm16: *out=DOE_DX_SNORM16; return true;
    case WGPUVertexFormat_Float16: *out=DOE_DX_FLOAT16; return true;
    case WGPUVertexFormat_Uint16x2: *out=DOE_DX_UINT16X2; return true;
    case WGPUVertexFormat_Sint16x2: *out=DOE_DX_SINT16X2; return true;
    case WGPUVertexFormat_Unorm16x2: *out=DOE_DX_UNORM16X2; return true;
    case WGPUVertexFormat_Snorm16x2: *out=DOE_DX_SNORM16X2; return true;
    case WGPUVertexFormat_Float16x2: *out=DOE_DX_FLOAT16X2; return true;
    case WGPUVertexFormat_Uint16x4: *out=DOE_DX_UINT16X4; return true;
    case WGPUVertexFormat_Sint16x4: *out=DOE_DX_SINT16X4; return true;
    case WGPUVertexFormat_Unorm16x4: *out=DOE_DX_UNORM16X4; return true;
    case WGPUVertexFormat_Snorm16x4: *out=DOE_DX_SNORM16X4; return true;
    case WGPUVertexFormat_Float16x4: *out=DOE_DX_FLOAT16X4; return true;
    case WGPUVertexFormat_Float32: *out=DOE_DX_FLOAT32; return true;
    case WGPUVertexFormat_Uint32: *out=DOE_DX_UINT32; return true;
    case WGPUVertexFormat_Sint32: *out=DOE_DX_SINT32; return true;
    case WGPUVertexFormat_Float32x2: *out=DOE_DX_FLOAT32X2; return true;
    case WGPUVertexFormat_Uint32x2: *out=DOE_DX_UINT32X2; return true;
    case WGPUVertexFormat_Sint32x2: *out=DOE_DX_SINT32X2; return true;
    case WGPUVertexFormat_Float32x3: *out=DOE_DX_FLOAT32X3; return true;
    case WGPUVertexFormat_Uint32x3: *out=DOE_DX_UINT32X3; return true;
    case WGPUVertexFormat_Sint32x3: *out=DOE_DX_SINT32X3; return true;
    case WGPUVertexFormat_Float32x4: *out=DOE_DX_FLOAT32X4; return true;
    case WGPUVertexFormat_Uint32x4: *out=DOE_DX_UINT32X4; return true;
    case WGPUVertexFormat_Sint32x4: *out=DOE_DX_SINT32X4; return true;
    case WGPUVertexFormat_Unorm10_10_10_2: *out=DOE_DX_UNORM10_10_10_2; return true;
    case WGPUVertexFormat_Unorm8x4BGRA: *out=DOE_DX_UNORM8X4_BGRA; return true;
    default: return false;
    }
}

static bool prepare_vertices(const WGPUVertexState *source,const native_device *device,
    doe_dx_render_pipeline_desc *native,doe_resource_render_pipeline_desc *resource,
    doe_dx_vertex_layout layouts[DOE_DX_MAX_VERTEX_BUFFERS],doe_dx_vertex_attribute attributes[DOE_DX_MAX_VERTEX_ATTRIBUTES])
{
    const WGPULimits *limits=&device->backend.limits;
    if (source->bufferCount>DOE_DX_MAX_VERTEX_BUFFERS || source->bufferCount>limits->maxVertexBuffers ||
        (source->bufferCount && !source->buffers)) return false;
    resource->vertex_count=native->vertex_buffer_count=source->bufferCount;
    native->vertex_buffers=layouts;
    native->attributes=attributes;
    for (size_t slot=0;slot<source->bufferCount;++slot) {
        const WGPUVertexBufferLayout *buffer=&source->buffers[slot];
        bool hole=!buffer->attributeCount && buffer->stepMode==WGPUVertexStepMode_Undefined;
        WGPUVertexStepMode step=buffer->stepMode;
        if (!step && buffer->attributeCount) step=WGPUVertexStepMode_Vertex;
        if (buffer->nextInChain || (!hole && buffer->arrayStride>limits->maxVertexBufferArrayStride) ||
            (step!=WGPUVertexStepMode_Undefined && step!=WGPUVertexStepMode_Vertex && step!=WGPUVertexStepMode_Instance) ||
            buffer->attributeCount>DOE_DX_MAX_VERTEX_ATTRIBUTES-native->attribute_count ||
            buffer->attributeCount>limits->maxVertexAttributes-native->attribute_count ||
            (buffer->attributeCount && !buffer->attributes)) return false;
        layouts[slot]=(doe_dx_vertex_layout){hole ? 0 : (uint32_t)buffer->arrayStride,
            step==WGPUVertexStepMode_Instance ? DOE_DX_PER_INSTANCE : DOE_DX_PER_VERTEX};
        resource->vertices[slot]=(doe_resource_vertex_layout){hole ? 0 : buffer->arrayStride,0,step};
        for (size_t i=0;i<buffer->attributeCount;++i) {
            const WGPUVertexAttribute *attribute=&buffer->attributes[i];
            doe_vk_vertex_format canonical;
            doe_dx_vertex_format format;
            uint32_t scalar,bytes;
            if (attribute->nextInChain || attribute->shaderLocation>=limits->maxVertexAttributes ||
                !native_vertex_format(attribute->format,&canonical,&scalar,&bytes) || !bytes ||
                attribute->offset%(bytes<4 ? bytes : 4) || attribute->offset>UINT32_MAX ||
                attribute->offset>UINT32_MAX-bytes || !vertex_format(attribute->format,&format) ||
                attribute->offset+bytes>(buffer->arrayStride ? buffer->arrayStride : limits->maxVertexBufferArrayStride)) return false;
            for (size_t j=0;j<native->attribute_count;++j)
                if (attributes[j].shader_location==attribute->shaderLocation) return false;
            uint64_t end=attribute->offset+bytes;
            if (resource->vertices[slot].last_attribute_end<end) resource->vertices[slot].last_attribute_end=end;
            attributes[native->attribute_count++]=(doe_dx_vertex_attribute){attribute->shaderLocation,
                (uint32_t)slot,(uint32_t)attribute->offset,format};
        }
    }
    return true;
}

bool native_d3d12_graphics_vertices(const WGPUVertexState *source,const native_device *device,
    doe_dx_render_pipeline_desc *native,doe_resource_render_pipeline_desc *resource,
    doe_dx_vertex_layout layouts[DOE_DX_MAX_VERTEX_BUFFERS],doe_dx_vertex_attribute attributes[DOE_DX_MAX_VERTEX_ATTRIBUTES])
{
    if (!source || !device || !native || !resource || !layouts || !attributes) return false;
    doe_dx_render_pipeline_desc result=*native;
    doe_resource_render_pipeline_desc shape=*resource;
    doe_dx_vertex_layout prepared_layouts[DOE_DX_MAX_VERTEX_BUFFERS]={0};
    doe_dx_vertex_attribute prepared_attributes[DOE_DX_MAX_VERTEX_ATTRIBUTES]={0};
    result.attribute_count=0;
    if (!prepare_vertices(source,device,&result,&shape,prepared_layouts,prepared_attributes)) return false;
    memcpy(layouts,prepared_layouts,sizeof(prepared_layouts));
    memcpy(attributes,prepared_attributes,sizeof(prepared_attributes));
    result.vertex_buffers=layouts; result.attributes=attributes;
    *native=result; *resource=shape;
    return true;
}
