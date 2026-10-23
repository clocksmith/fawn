/* SPDX-License-Identifier: MIT */
#include "windows_internal.h"
#ifdef _WIN32

void doe_dx_native_commands_destroy(doe_dx_native_commands *commands) {
    if (!commands) return;
    if (commands->list) ID3D12GraphicsCommandList_Release(commands->list);
    if (commands->allocator) ID3D12CommandAllocator_Release(commands->allocator);
    doe_dx_descriptor_use *use = commands->descriptors;
    while (use) {
        doe_dx_descriptor_use *next = use->next;
        ID3D12DescriptorHeap_Release(use->heap);
        free(use);
        use = next;
    }
    free(commands);
}

doe_dx_status doe_dx_native_commands_create(doe_dx_native_context *context,
    doe_dx_native_commands **out, doe_dx_error *error) {
    doe_dx_native_commands *commands = calloc(1, sizeof(*commands));
    if (!commands) return doe_dx_fail(DOE_DX_OUT_OF_MEMORY, 0, "native.commands.allocate", error);
    commands->context = context;
    HRESULT result = ID3D12Device_CreateCommandAllocator(context->device,
        D3D12_COMMAND_LIST_TYPE_DIRECT, &IID_ID3D12CommandAllocator, (void **)&commands->allocator);
    const char *operation = "ID3D12Device.CreateCommandAllocator";
    if (FAILED(result)) goto fail;
    operation = "ID3D12Device.CreateCommandList";
    result = ID3D12Device_CreateCommandList(context->device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT,
        commands->allocator, NULL, &IID_ID3D12GraphicsCommandList, (void **)&commands->list);
    if (FAILED(result)) goto fail;
    *out = commands;
    return doe_dx_fail(DOE_DX_OK, 0, "native.commands.create", error);
fail:
    doe_dx_native_commands_destroy(commands);
    return doe_dx_hresult(result, operation, error);
}

void doe_dx_transition(ID3D12GraphicsCommandList *list, ID3D12Resource *resource,
    D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
    D3D12_RESOURCE_BARRIER barrier = {0};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = before;
    barrier.Transition.StateAfter = after;
    ID3D12GraphicsCommandList_ResourceBarrier(list, 1, &barrier);
}

void doe_dx_native_copy(doe_dx_native_commands *commands, doe_dx_native_buffer *source,
    uint64_t source_offset, doe_dx_native_buffer *destination,
    uint64_t destination_offset, uint64_t size) {
    if (source->memory == DOE_DX_DEFAULT)
        doe_dx_transition(commands->list, source->resource,
            D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE);
    ID3D12GraphicsCommandList_CopyBufferRegion(commands->list, destination->resource,
        destination_offset, source->resource, source_offset, size);
    if (source->memory == DOE_DX_DEFAULT)
        doe_dx_transition(commands->list, source->resource,
            D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
}

static doe_dx_status prepare_descriptors(doe_dx_native_commands *commands,
    doe_dx_native_pipeline *pipeline, const doe_dx_native_binding *bindings,
    size_t count, doe_dx_descriptor_use **out, doe_dx_error *error) {
    doe_dx_descriptor_use *use = calloc(1, sizeof(*use));
    if (!use) return doe_dx_fail(DOE_DX_OUT_OF_MEMORY, 0, "descriptors.allocate", error);
    ID3D12Device *device = commands->context->device;
    D3D12_DESCRIPTOR_HEAP_DESC description = {0};
    description.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    description.NumDescriptors = (UINT)count;
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
    for (size_t index = 0; index < count; index++) {
        const doe_dx_binding_layout *layout = &pipeline->bindings[index];
        const doe_dx_native_binding *binding = &bindings[index];
        if (layout->access == DOE_DX_READ_ONLY) {
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
    *out = use;
    return DOE_DX_OK;
}

doe_dx_status doe_dx_native_dispatch(doe_dx_native_commands *commands,
    doe_dx_native_pipeline *pipeline, const doe_dx_native_binding *bindings,
    size_t count, uint32_t x, uint32_t y, uint32_t z, doe_dx_error *error) {
    doe_dx_descriptor_use *use = NULL;
    if (count) {
        doe_dx_status status = prepare_descriptors(commands, pipeline, bindings, count, &use, error);
        if (status != DOE_DX_OK) return status;
        use->next = commands->descriptors;
        commands->descriptors = use;
    }
    ID3D12GraphicsCommandList *list = commands->list;
    ID3D12GraphicsCommandList_SetComputeRootSignature(list, pipeline->root);
    ID3D12GraphicsCommandList_SetPipelineState(list, pipeline->state);
    if (use) {
        ID3D12GraphicsCommandList_SetDescriptorHeaps(list, 1, &use->heap);
        D3D12_GPU_DESCRIPTOR_HANDLE handle;
        use->heap->lpVtbl->GetGPUDescriptorHandleForHeapStart(use->heap, &handle);
        ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(list, 0, handle);
    }
    for (size_t index = 0; index < count; index++) {
        D3D12_RESOURCE_STATES state = pipeline->bindings[index].access == DOE_DX_READ_ONLY ?
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE : D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        doe_dx_transition(list, bindings[index].buffer->resource, D3D12_RESOURCE_STATE_COPY_DEST, state);
    }
    ID3D12GraphicsCommandList_Dispatch(list, x, y, z);
    for (size_t index = 0; index < count; index++) {
        D3D12_RESOURCE_STATES state = pipeline->bindings[index].access == DOE_DX_READ_ONLY ?
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE : D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        doe_dx_transition(list, bindings[index].buffer->resource, state, D3D12_RESOURCE_STATE_COPY_DEST);
    }
    return doe_dx_fail(DOE_DX_OK, 0, "native.dispatch", error);
}

doe_dx_status doe_dx_native_finish(doe_dx_native_commands *commands, doe_dx_error *error) {
    HRESULT result = ID3D12GraphicsCommandList_Close(commands->list);
    return doe_dx_hresult(result, "ID3D12GraphicsCommandList.Close", error);
}

void doe_dx_native_execute(doe_dx_native_context *context, doe_dx_native_commands *commands) {
    ID3D12CommandList *list = (ID3D12CommandList *)commands->list;
    ID3D12CommandQueue_ExecuteCommandLists(context->queue, 1, &list);
}
#endif
