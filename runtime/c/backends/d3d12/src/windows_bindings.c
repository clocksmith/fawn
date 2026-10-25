/* SPDX-License-Identifier: MIT */
#include "windows_internal.h"
#ifdef _WIN32

static void write_set(ID3D12Device *device,const doe_dx_resource_set *set,
    D3D12_CPU_DESCRIPTOR_HANDLE handle,D3D12_CPU_DESCRIPTOR_HANDLE sampler_handle) {
    const doe_dx_native_resources *resources=set->resources;
    UINT increment = ID3D12Device_GetDescriptorHandleIncrementSize(device,
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    for (size_t index = 0; index < resources->buffer_count; index++) {
        const doe_dx_binding_layout *layout = &set->buffers[index];
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
            view.Buffer.FirstElement = binding->offset / (layout->stride ? layout->stride : 4u);
            view.Buffer.NumElements = (UINT)(layout->stride ? binding->size / layout->stride : (binding->size+3)/4);
            view.Buffer.StructureByteStride = layout->stride;
            if (layout->view==DOE_DX_RAW) { view.Format=DXGI_FORMAT_R32_TYPELESS; view.Buffer.Flags=D3D12_BUFFER_SRV_FLAG_RAW; }
            ID3D12Device_CreateShaderResourceView(device, binding->buffer->resource, &view, handle);
        } else {
            D3D12_UNORDERED_ACCESS_VIEW_DESC view = {0};
            view.Format = DXGI_FORMAT_UNKNOWN;
            view.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
            view.Buffer.FirstElement = binding->offset / (layout->stride ? layout->stride : 4u);
            view.Buffer.NumElements = (UINT)(layout->stride ? binding->size / layout->stride : (binding->size+3)/4);
            view.Buffer.StructureByteStride = layout->stride;
            if (layout->view==DOE_DX_RAW) { view.Format=DXGI_FORMAT_R32_TYPELESS; view.Buffer.Flags=D3D12_BUFFER_UAV_FLAG_RAW; }
            ID3D12Device_CreateUnorderedAccessView(device, binding->buffer->resource,
                NULL, &view, handle);
        }
        handle.ptr += increment;
    }
    for (size_t index = 0; index < resources->texture_count; index++) {
        const doe_dx_texture_binding_layout *layout = &set->textures[index];
        doe_dx_native_texture *texture = resources->textures[index];
        if (layout->access == DOE_DX_READ_ONLY) {
            D3D12_SHADER_RESOURCE_VIEW_DESC view;
            doe_dx_texture_srv(texture,&resources->texture_views[index],&view);
            ID3D12Device_CreateShaderResourceView(device, texture->resource, &view, handle);
        } else {
            D3D12_UNORDERED_ACCESS_VIEW_DESC view;
            doe_dx_texture_uav(texture,&resources->texture_views[index],&view);
            ID3D12Device_CreateUnorderedAccessView(device, texture->resource, NULL, &view, handle);
        }
        handle.ptr += increment;
    }
    if (resources->sampler_count) {
        handle=sampler_handle;
        increment = ID3D12Device_GetDescriptorHandleIncrementSize(device,D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
        for (size_t index = 0; index < resources->sampler_count; ++index) {
            D3D12_SAMPLER_DESC sampler;
            doe_dx_sampler_native(&resources->samplers[index],&sampler);
            ID3D12Device_CreateSampler(device,&sampler,handle);
            handle.ptr += increment;
        }
    }
}

doe_dx_status doe_dx_prepare_sets(doe_dx_native_commands *commands,
    const doe_dx_resource_set *sets,size_t count,doe_dx_descriptor_use **out,doe_dx_error *error) {
    if (!sets || !count || count>DOE_DX_RENDER_STAGES)
        return doe_dx_fail(DOE_DX_INVALID_ARGUMENT,0,"descriptors.sets",error);
    size_t resource_count=0,sampler_count=0;
    for (size_t i=0;i<count;++i) {
        const doe_dx_native_resources *values=sets[i].resources;
        if (!values || values->texture_count>DOE_DX_MAX_BINDINGS ||
            values->buffer_count>DOE_DX_MAX_BINDINGS-values->texture_count ||
            values->sampler_count>DOE_DX_MAX_DYNAMIC_SAMPLERS)
            return doe_dx_fail(DOE_DX_RANGE,0,"descriptors.set_range",error);
        resource_count+=sets[i].resources->buffer_count+sets[i].resources->texture_count;
        sampler_count+=sets[i].resources->sampler_count;
    }
    doe_dx_descriptor_use *use = calloc(1, sizeof(*use));
    if (!use) return doe_dx_fail(DOE_DX_OUT_OF_MEMORY, 0, "descriptors.allocate", error);
    ID3D12Device *device = commands->context->device;
    D3D12_DESCRIPTOR_HEAP_DESC description = {0};
    description.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    description.NumDescriptors = (UINT)resource_count;
    if (description.NumDescriptors) {
        description.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        HRESULT result = ID3D12Device_CreateDescriptorHeap(device,&description,
            &IID_ID3D12DescriptorHeap,(void **)&use->heap);
        if (FAILED(result)) {
            doe_dx_release_descriptors(use);
            return doe_dx_hresult(result,"ID3D12Device.CreateDescriptorHeap.resources",error);
        }
    }
    if (sampler_count) {
        description.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
        description.NumDescriptors = (UINT)sampler_count;
        HRESULT result = ID3D12Device_CreateDescriptorHeap(device,&description,
            &IID_ID3D12DescriptorHeap,(void **)&use->sampler_heap);
        if (FAILED(result)) {
            doe_dx_release_descriptors(use);
            return doe_dx_hresult(result,"ID3D12Device.CreateDescriptorHeap.samplers",error);
        }
    }
    D3D12_CPU_DESCRIPTOR_HANDLE handle={0},samplers={0};
    if (use->heap) use->heap->lpVtbl->GetCPUDescriptorHandleForHeapStart(use->heap,&handle);
    if (use->sampler_heap) use->sampler_heap->lpVtbl->GetCPUDescriptorHandleForHeapStart(use->sampler_heap,&samplers);
    UINT resource_stride=ID3D12Device_GetDescriptorHandleIncrementSize(device,D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    UINT sampler_stride=ID3D12Device_GetDescriptorHandleIncrementSize(device,D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
    for (size_t i=0;i<count;++i) {
        const doe_dx_native_resources *values=sets[i].resources;
        doe_dx_status status=doe_dx_prepare_storage_sizes(commands,values,&use->storage_sizes[i],error);
        if (status!=DOE_DX_OK) { doe_dx_release_descriptors(use); return status; }
        write_set(device,&sets[i],handle,samplers);
        handle.ptr+=(values->buffer_count+values->texture_count)*resource_stride;
        samplers.ptr+=values->sampler_count*sampler_stride;
    }
    *out=use;
    return DOE_DX_OK;
}
doe_dx_status doe_dx_prepare_descriptors(doe_dx_native_commands *commands,
    doe_dx_native_pipeline *pipeline,const doe_dx_native_resources *resources,
    doe_dx_descriptor_use **out,doe_dx_error *error) {
    const doe_dx_resource_set set={pipeline->bindings,pipeline->textures,resources};
    return doe_dx_prepare_sets(commands,&set,1,out,error);
}
#endif
