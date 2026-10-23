/* SPDX-License-Identifier: MIT */
#include "windows_internal.h"
#ifdef _WIN32

doe_dx_status doe_dx_prepare_descriptors(doe_dx_native_commands *commands,
    doe_dx_native_pipeline *pipeline, const doe_dx_native_resources *resources,
    doe_dx_descriptor_use **out, doe_dx_error *error) {
    doe_dx_descriptor_use *use = calloc(1, sizeof(*use));
    if (!use) return doe_dx_fail(DOE_DX_OUT_OF_MEMORY, 0, "descriptors.allocate", error);
    ID3D12Device *device = commands->context->device;
    D3D12_DESCRIPTOR_HEAP_DESC description = {0};
    description.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    description.NumDescriptors = (UINT)(resources->buffer_count + resources->texture_count);
    description.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    HRESULT result = ID3D12Device_CreateDescriptorHeap(device, &description,
        &IID_ID3D12DescriptorHeap, (void **)&use->heap);
    if (FAILED(result)) {
        free(use);
        return doe_dx_hresult(result, "ID3D12Device.CreateDescriptorHeap", error);
    }
    D3D12_CPU_DESCRIPTOR_HANDLE handle;
    use->heap->lpVtbl->GetCPUDescriptorHandleForHeapStart(use->heap, &handle);
    UINT increment = ID3D12Device_GetDescriptorHandleIncrementSize(device,
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    for (size_t index = 0; index < resources->buffer_count; index++) {
        const doe_dx_binding_layout *layout = &pipeline->bindings[index];
        const doe_dx_native_binding *binding = &resources->buffers[index];
        doe_dx_descriptor_kind kind = doe_dx_buffer_descriptor_kind(layout);
        if (kind == DOE_DX_DESCRIPTOR_CBV) {
            D3D12_CONSTANT_BUFFER_VIEW_DESC view = {0};
            view.BufferLocation = ID3D12Resource_GetGPUVirtualAddress(binding->buffer->resource) + binding->offset;
            view.SizeInBytes = (UINT)binding->size;
            ID3D12Device_CreateConstantBufferView(device, &view, handle);
        } else if (kind == DOE_DX_DESCRIPTOR_SRV) {
            D3D12_SHADER_RESOURCE_VIEW_DESC view = {0};
            view.Format = DXGI_FORMAT_UNKNOWN;
            view.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
            view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            view.Buffer.FirstElement = binding->offset / layout->stride;
            view.Buffer.NumElements = (UINT)(binding->size / layout->stride);
            view.Buffer.StructureByteStride = layout->stride;
            ID3D12Device_CreateShaderResourceView(device, binding->buffer->resource, &view, handle);
        } else {
            D3D12_UNORDERED_ACCESS_VIEW_DESC view = {0};
            view.Format = DXGI_FORMAT_UNKNOWN;
            view.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
            view.Buffer.FirstElement = binding->offset / layout->stride;
            view.Buffer.NumElements = (UINT)(binding->size / layout->stride);
            view.Buffer.StructureByteStride = layout->stride;
            ID3D12Device_CreateUnorderedAccessView(device, binding->buffer->resource,
                NULL, &view, handle);
        }
        handle.ptr += increment;
    }
    for (size_t index = 0; index < resources->texture_count; index++) {
        const doe_dx_texture_binding_layout *layout = &pipeline->textures[index];
        doe_dx_native_texture *texture = resources->textures[index];
        if (layout->access == DOE_DX_READ_ONLY) {
            D3D12_SHADER_RESOURCE_VIEW_DESC view = {0};
            view.Format = texture->description.Format;
            view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            view.Texture2D.MipLevels = 1;
            ID3D12Device_CreateShaderResourceView(device, texture->resource, &view, handle);
        } else {
            D3D12_UNORDERED_ACCESS_VIEW_DESC view = {0};
            view.Format = texture->description.Format;
            view.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
            ID3D12Device_CreateUnorderedAccessView(device, texture->resource, NULL, &view, handle);
        }
        handle.ptr += increment;
    }
    *out = use;
    return DOE_DX_OK;
}

void doe_dx_transition_resources(doe_dx_native_commands *commands, doe_dx_native_pipeline *pipeline,
    const doe_dx_native_resources *resources, bool graphics, bool restore) {
    D3D12_RESOURCE_STATES read = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    if (graphics) read |= D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    for (size_t index = 0; index < resources->buffer_count; index++) {
        doe_dx_descriptor_kind kind = doe_dx_buffer_descriptor_kind(&pipeline->bindings[index]);
        D3D12_RESOURCE_STATES state = kind == DOE_DX_DESCRIPTOR_CBV ? D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER :
            kind == DOE_DX_DESCRIPTOR_SRV ? read : D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        doe_dx_transition(commands->list, resources->buffers[index].buffer->resource,
            restore ? state : D3D12_RESOURCE_STATE_COPY_DEST,
            restore ? D3D12_RESOURCE_STATE_COPY_DEST : state);
    }
    for (size_t index = 0; index < resources->texture_count; index++) {
        D3D12_RESOURCE_STATES state = pipeline->textures[index].access == DOE_DX_READ_ONLY ?
            read : D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        doe_dx_transition(commands->list, resources->textures[index]->resource,
            restore ? state : D3D12_RESOURCE_STATE_COPY_DEST,
            restore ? D3D12_RESOURCE_STATE_COPY_DEST : state);
    }
}
#endif
