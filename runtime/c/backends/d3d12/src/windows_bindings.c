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
    description.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    description.NumDescriptors = (UINT)(resources->buffer_count + resources->texture_count);
    if (description.NumDescriptors) {
        description.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        HRESULT result = ID3D12Device_CreateDescriptorHeap(device,&description,
            &IID_ID3D12DescriptorHeap,(void **)&use->heap);
        if (FAILED(result)) {
            doe_dx_release_descriptors(use);
            return doe_dx_hresult(result,"ID3D12Device.CreateDescriptorHeap.resources",error);
        }
    }
    if (resources->sampler_count) {
        description.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
        description.NumDescriptors = (UINT)resources->sampler_count;
        HRESULT result = ID3D12Device_CreateDescriptorHeap(device,&description,
            &IID_ID3D12DescriptorHeap,(void **)&use->sampler_heap);
        if (FAILED(result)) {
            doe_dx_release_descriptors(use);
            return doe_dx_hresult(result,"ID3D12Device.CreateDescriptorHeap.samplers",error);
        }
    }
    D3D12_CPU_DESCRIPTOR_HANDLE handle = {0};
    if (use->heap) use->heap->lpVtbl->GetCPUDescriptorHandleForHeapStart(use->heap, &handle);
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
    if (use->sampler_heap) {
        use->sampler_heap->lpVtbl->GetCPUDescriptorHandleForHeapStart(use->sampler_heap,&handle);
        increment = ID3D12Device_GetDescriptorHandleIncrementSize(device,D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
        for (size_t index = 0; index < resources->sampler_count; ++index) {
            D3D12_SAMPLER_DESC sampler;
            doe_dx_sampler_native(&resources->samplers[index],&sampler);
            ID3D12Device_CreateSampler(device,&sampler,handle);
            handle.ptr += increment;
        }
    }
    *out = use;
    return DOE_DX_OK;
}

void doe_dx_transition_resources(doe_dx_native_commands *commands, doe_dx_native_pipeline *pipeline,
    const doe_dx_native_resources *resources, bool graphics, bool restore) {
    D3D12_RESOURCE_STATES read = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    if (graphics) read |= D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    for (size_t index = 0; index < resources->buffer_count; index++) {
        doe_dx_native_buffer *buffer = resources->buffers[index].buffer;
        bool repeated = false;
        for (size_t prior = 0; prior < index; ++prior)
            if (resources->buffers[prior].buffer == buffer) repeated = true;
        if (repeated) continue;
        /* The portable owner rejects mixed read/write classes and overlapping
         * UAV views before native mutation. Read aliases combine native state
         * bits; every allocation transitions once regardless of view count. */
        D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
        for (size_t alias = index; alias < resources->buffer_count; ++alias) {
            if (resources->buffers[alias].buffer != buffer) continue;
            doe_dx_descriptor_kind kind = doe_dx_buffer_descriptor_kind(&pipeline->bindings[alias]);
            switch (kind) {
            case DOE_DX_DESCRIPTOR_CBV: state |= D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER; break;
            case DOE_DX_DESCRIPTOR_SRV: state |= read; break;
            case DOE_DX_DESCRIPTOR_UAV: state |= D3D12_RESOURCE_STATE_UNORDERED_ACCESS; break;
            case DOE_DX_DESCRIPTOR_INVALID: abort();
            }
        }
        doe_dx_transition(commands->list, buffer->resource,
            restore ? state : D3D12_RESOURCE_STATE_COPY_DEST,
            restore ? D3D12_RESOURCE_STATE_COPY_DEST : state);
    }
    for (size_t index = 0; index < resources->texture_count; index++) {
        bool repeated = false;
        for (size_t prior = 0; prior < index; ++prior)
            if (resources->textures[prior] == resources->textures[index]) repeated = true;
        if (repeated) continue;
        D3D12_RESOURCE_STATES state = pipeline->textures[index].access == DOE_DX_READ_ONLY ?
            read : D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        doe_dx_transition(commands->list, resources->textures[index]->resource,
            restore ? state : D3D12_RESOURCE_STATE_COPY_DEST,
            restore ? D3D12_RESOURCE_STATE_COPY_DEST : state);
    }
}
#endif
