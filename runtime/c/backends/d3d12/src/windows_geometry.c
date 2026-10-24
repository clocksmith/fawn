/* SPDX-License-Identifier: MIT */
#include "windows_internal.h"
#ifdef _WIN32

static DXGI_FORMAT vertex_format(doe_dx_vertex_format format) {
    switch (format) {
        case DOE_DX_FLOAT32: return DXGI_FORMAT_R32_FLOAT;
        case DOE_DX_FLOAT32X2: return DXGI_FORMAT_R32G32_FLOAT;
        case DOE_DX_FLOAT32X3: return DXGI_FORMAT_R32G32B32_FLOAT;
        case DOE_DX_FLOAT32X4: return DXGI_FORMAT_R32G32B32A32_FLOAT;
        case DOE_DX_UINT32: return DXGI_FORMAT_R32_UINT;
        case DOE_DX_UINT32X2: return DXGI_FORMAT_R32G32_UINT;
        case DOE_DX_UINT32X3: return DXGI_FORMAT_R32G32B32_UINT;
        case DOE_DX_UINT32X4: return DXGI_FORMAT_R32G32B32A32_UINT;
        case DOE_DX_SINT32: return DXGI_FORMAT_R32_SINT;
        case DOE_DX_SINT32X2: return DXGI_FORMAT_R32G32_SINT;
        case DOE_DX_SINT32X3: return DXGI_FORMAT_R32G32B32_SINT;
        case DOE_DX_SINT32X4: return DXGI_FORMAT_R32G32B32A32_SINT;
        case DOE_DX_UINT8: return DXGI_FORMAT_R8_UINT;
        case DOE_DX_SINT8: return DXGI_FORMAT_R8_SINT;
        case DOE_DX_UNORM8: return DXGI_FORMAT_R8_UNORM;
        case DOE_DX_SNORM8: return DXGI_FORMAT_R8_SNORM;
        case DOE_DX_UINT8X2: return DXGI_FORMAT_R8G8_UINT;
        case DOE_DX_SINT8X2: return DXGI_FORMAT_R8G8_SINT;
        case DOE_DX_UNORM8X2: return DXGI_FORMAT_R8G8_UNORM;
        case DOE_DX_SNORM8X2: return DXGI_FORMAT_R8G8_SNORM;
        case DOE_DX_UINT8X4: return DXGI_FORMAT_R8G8B8A8_UINT;
        case DOE_DX_SINT8X4: return DXGI_FORMAT_R8G8B8A8_SINT;
        case DOE_DX_UNORM8X4: return DXGI_FORMAT_R8G8B8A8_UNORM;
        case DOE_DX_SNORM8X4: return DXGI_FORMAT_R8G8B8A8_SNORM;
        case DOE_DX_UINT16: return DXGI_FORMAT_R16_UINT;
        case DOE_DX_SINT16: return DXGI_FORMAT_R16_SINT;
        case DOE_DX_UNORM16: return DXGI_FORMAT_R16_UNORM;
        case DOE_DX_SNORM16: return DXGI_FORMAT_R16_SNORM;
        case DOE_DX_FLOAT16: return DXGI_FORMAT_R16_FLOAT;
        case DOE_DX_UINT16X2: return DXGI_FORMAT_R16G16_UINT;
        case DOE_DX_SINT16X2: return DXGI_FORMAT_R16G16_SINT;
        case DOE_DX_UNORM16X2: return DXGI_FORMAT_R16G16_UNORM;
        case DOE_DX_SNORM16X2: return DXGI_FORMAT_R16G16_SNORM;
        case DOE_DX_FLOAT16X2: return DXGI_FORMAT_R16G16_FLOAT;
        case DOE_DX_UINT16X4: return DXGI_FORMAT_R16G16B16A16_UINT;
        case DOE_DX_SINT16X4: return DXGI_FORMAT_R16G16B16A16_SINT;
        case DOE_DX_UNORM16X4: return DXGI_FORMAT_R16G16B16A16_UNORM;
        case DOE_DX_SNORM16X4: return DXGI_FORMAT_R16G16B16A16_SNORM;
        case DOE_DX_FLOAT16X4: return DXGI_FORMAT_R16G16B16A16_FLOAT;
        case DOE_DX_UNORM10_10_10_2: return DXGI_FORMAT_R10G10B10A2_UNORM;
        case DOE_DX_UNORM8X4_BGRA: return DXGI_FORMAT_B8G8R8A8_UNORM;
    }
    return DXGI_FORMAT_UNKNOWN;
}

D3D12_COMPARISON_FUNC doe_dx_depth_compare(doe_dx_compare compare) {
    switch (compare) {
        case DOE_DX_NEVER: return D3D12_COMPARISON_FUNC_NEVER;
        case DOE_DX_LESS: return D3D12_COMPARISON_FUNC_LESS;
        case DOE_DX_EQUAL: return D3D12_COMPARISON_FUNC_EQUAL;
        case DOE_DX_LESS_EQUAL: return D3D12_COMPARISON_FUNC_LESS_EQUAL;
        case DOE_DX_GREATER: return D3D12_COMPARISON_FUNC_GREATER;
        case DOE_DX_NOT_EQUAL: return D3D12_COMPARISON_FUNC_NOT_EQUAL;
        case DOE_DX_GREATER_EQUAL: return D3D12_COMPARISON_FUNC_GREATER_EQUAL;
        case DOE_DX_ALWAYS: return D3D12_COMPARISON_FUNC_ALWAYS;
    }
    return D3D12_COMPARISON_FUNC_ALWAYS; /* Owner validated. */
}

doe_dx_status doe_dx_input_layout_rows(doe_dx_native_context *context,
    const doe_dx_vertex_layout *layouts,size_t layout_count,const doe_dx_vertex_attribute *attributes,
    size_t attribute_count,doe_dx_native_pipeline *pipeline,D3D12_INPUT_ELEMENT_DESC *out,doe_dx_error *error)
{
    for (size_t i=0;i<layout_count;++i) pipeline->vertex_buffers[i]=layouts[i];
    for (size_t i=0;i<attribute_count;++i) {
        const doe_dx_vertex_attribute *attribute=&attributes[i];
        D3D12_FEATURE_DATA_FORMAT_SUPPORT support={0};
        support.Format=vertex_format(attribute->format);
        if (support.Format==DXGI_FORMAT_UNKNOWN) return doe_dx_fail(DOE_DX_UNSUPPORTED,0,"vertex.format",error);
        HRESULT result=ID3D12Device_CheckFeatureSupport(context->device,
            D3D12_FEATURE_FORMAT_SUPPORT,&support,sizeof(support));
        if (FAILED(result)) return doe_dx_hresult(result,"CheckFeatureSupport.vertex",error);
        if (!(support.Support1 & D3D12_FORMAT_SUPPORT1_IA_VERTEX_BUFFER))
            return doe_dx_fail(DOE_DX_UNSUPPORTED,0,"vertex.native_format",error);
        out[i].SemanticName="TEXCOORD";
        out[i].SemanticIndex=attribute->shader_location;
        out[i].Format=support.Format;
        out[i].InputSlot=attribute->buffer_slot;
        out[i].AlignedByteOffset=attribute->offset;
        bool instance=layouts[attribute->buffer_slot].step==DOE_DX_PER_INSTANCE;
        out[i].InputSlotClass=instance ? D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA : D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
        out[i].InstanceDataStepRate=instance ? 1 : 0;
    }
    return DOE_DX_OK;
}

doe_dx_status doe_dx_input_layout(doe_dx_native_context *context,const doe_dx_mesh_pipeline_desc *desc,
    doe_dx_native_pipeline *pipeline,D3D12_INPUT_ELEMENT_DESC *out,doe_dx_error *error)
{
    return doe_dx_input_layout_rows(context,desc->vertex_buffers,desc->vertex_buffer_count,
        desc->attributes,desc->attribute_count,pipeline,out,error);
}

void doe_dx_geometry_transition(doe_dx_native_commands *commands, const doe_dx_native_geometry *geometry,
    const doe_dx_mesh_draw_desc *draw, bool restore) {
    for (size_t index = 0; index < draw->vertex_count; index++)
        doe_dx_transition(commands->list, geometry->vertices[index].buffer->resource,
            restore ? D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER : D3D12_RESOURCE_STATE_COPY_DEST,
            restore ? D3D12_RESOURCE_STATE_COPY_DEST : D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
    if (draw->indices) doe_dx_transition(commands->list, geometry->indices.buffer->resource,
        restore ? D3D12_RESOURCE_STATE_INDEX_BUFFER : D3D12_RESOURCE_STATE_COPY_DEST,
        restore ? D3D12_RESOURCE_STATE_COPY_DEST : D3D12_RESOURCE_STATE_INDEX_BUFFER);
    if (geometry->depth) doe_dx_transition(commands->list, geometry->depth->resource,
        restore ? D3D12_RESOURCE_STATE_DEPTH_WRITE : D3D12_RESOURCE_STATE_COPY_DEST,
        restore ? D3D12_RESOURCE_STATE_COPY_DEST : D3D12_RESOURCE_STATE_DEPTH_WRITE);
}

void doe_dx_bind_vertex_views(doe_dx_native_commands *commands,doe_dx_native_pipeline *pipeline,
    const doe_dx_native_binding *bindings,size_t count)
{
    D3D12_VERTEX_BUFFER_VIEW vertices[DOE_DX_MAX_VERTEX_BUFFERS]={0};
    for (size_t i=0;i<count;++i) {
        const doe_dx_native_binding *binding=&bindings[i];
        if (!binding->buffer) continue;
        vertices[i].BufferLocation=ID3D12Resource_GetGPUVirtualAddress(binding->buffer->resource)+binding->offset;
        vertices[i].SizeInBytes=(UINT)binding->size;
        vertices[i].StrideInBytes=pipeline->vertex_buffers[i].stride;
    }
    if (count) ID3D12GraphicsCommandList_IASetVertexBuffers(commands->list,0,(UINT)count,vertices);
}

void doe_dx_bind_geometry(doe_dx_native_commands *commands,doe_dx_native_pipeline *pipeline,
    const doe_dx_native_geometry *geometry,const doe_dx_mesh_draw_desc *draw)
{
    doe_dx_bind_vertex_views(commands,pipeline,geometry->vertices,draw->vertex_count);
    if (draw->indices) {
        D3D12_INDEX_BUFFER_VIEW indices={0};
        indices.BufferLocation=ID3D12Resource_GetGPUVirtualAddress(geometry->indices.buffer->resource)+geometry->indices.offset;
        indices.SizeInBytes=(UINT)geometry->indices.size;
        indices.Format=draw->indices->format==DOE_DX_INDEX_UINT16 ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT;
        ID3D12GraphicsCommandList_IASetIndexBuffer(commands->list,&indices);
    }
}
#endif
