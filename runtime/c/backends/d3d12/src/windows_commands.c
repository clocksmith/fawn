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
        doe_dx_release_descriptors(use);
        use = next;
    }
    free(commands);
}

void doe_dx_native_commands_cache_close(doe_dx_native_context *context) {
    while (context->idle_pairs) {
        const uint32_t top = --context->idle_pairs;
        ID3D12GraphicsCommandList_Release(context->idle_lists[top]);
        ID3D12CommandAllocator_Release(context->idle_allocators[top]);
        context->idle_lists[top] = NULL;
        context->idle_allocators[top] = NULL;
    }
}

void doe_dx_native_commands_recycle(doe_dx_native_commands *commands) {
    if (!commands) return;
    doe_dx_native_context *context = commands->context;
    /* Called only after a successfully observed terminal fence. Unknown/lost
     * work and discarded recordings use destroy, never this transfer. Up to
     * DOE_DX_IDLE_COMMAND_PAIRS pairs are kept; any further pair releases. */
    if (context->idle_pairs < DOE_DX_IDLE_COMMAND_PAIRS) {
        context->idle_allocators[context->idle_pairs] = commands->allocator;
        context->idle_lists[context->idle_pairs] = commands->list;
        context->idle_pairs++;
        commands->allocator = NULL;
        commands->list = NULL;
    }
    doe_dx_native_commands_destroy(commands);
}

doe_dx_status doe_dx_native_commands_create(doe_dx_native_context *context,
    doe_dx_native_commands **out, doe_dx_error *error) {
    doe_dx_native_commands *commands = calloc(1, sizeof(*commands));
    if (!commands) return doe_dx_fail(DOE_DX_OUT_OF_MEMORY, 0, "native.commands.allocate", error);
    commands->context = context;
    if (context->idle_pairs) {
        /* Oldest first: the earliest retired pair is reused, as with one slot. */
        commands->allocator = context->idle_allocators[0];
        commands->list = context->idle_lists[0];
        const uint32_t remaining = --context->idle_pairs;
        memmove(context->idle_allocators, context->idle_allocators + 1, remaining * sizeof(*context->idle_allocators));
        memmove(context->idle_lists, context->idle_lists + 1, remaining * sizeof(*context->idle_lists));
        context->idle_allocators[remaining] = NULL;
        context->idle_lists[remaining] = NULL;
        HRESULT reset = ID3D12CommandAllocator_Reset(commands->allocator);
        const char *operation = "ID3D12CommandAllocator.Reset";
        if (SUCCEEDED(reset)) {
            operation = "ID3D12GraphicsCommandList.Reset";
            reset = ID3D12GraphicsCommandList_Reset(commands->list,commands->allocator,NULL);
        }
        if (FAILED(reset)) {
            doe_dx_native_commands_destroy(commands);
            return doe_dx_hresult(reset,operation,error);
        }
        *out = commands;
        return doe_dx_fail(DOE_DX_OK,0,"native.commands.reuse",error);
    }
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

static doe_dx_status prepare_dispatch_signature(doe_dx_native_context *context,
    doe_dx_error *error) {
    if (context->dispatch_signature) return DOE_DX_OK;
    _Static_assert(sizeof(D3D12_DISPATCH_ARGUMENTS) == 3u * sizeof(uint32_t),
        "Native indirect dispatch arguments must match the typed record");
    D3D12_INDIRECT_ARGUMENT_DESC argument = {0};
    argument.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH;
    D3D12_COMMAND_SIGNATURE_DESC description = {0};
    description.ByteStride = (UINT)sizeof(D3D12_DISPATCH_ARGUMENTS);
    description.NumArgumentDescs = 1;
    description.pArgumentDescs = &argument;
    ID3D12CommandSignature *signature = NULL;
    HRESULT result = ID3D12Device_CreateCommandSignature(context->device, &description,
        NULL, &IID_ID3D12CommandSignature, (void **)&signature);
    if (FAILED(result)) return doe_dx_hresult(result, "ID3D12Device.CreateCommandSignature", error);
    context->dispatch_signature = signature;
    return DOE_DX_OK;
}

static doe_dx_status record_dispatch(doe_dx_native_commands *commands,
    doe_dx_native_pipeline *pipeline, const doe_dx_native_resources *resources,
    doe_dx_native_buffer *arguments, uint64_t offset, uint32_t x, uint32_t y,
    uint32_t z, doe_dx_error *error) {
    if (arguments) {
        doe_dx_status status = prepare_dispatch_signature(commands->context, error);
        if (status != DOE_DX_OK) return status;
    }
    doe_dx_descriptor_use *use = NULL;
    if (resources->buffer_count + resources->texture_count + resources->sampler_count) {
        doe_dx_status status = doe_dx_prepare_descriptors(commands, pipeline, resources, &use, error);
        if (status != DOE_DX_OK) return status;
        use->next = commands->descriptors;
        commands->descriptors = use;
    }
    ID3D12GraphicsCommandList *list = commands->list;
    ID3D12GraphicsCommandList_SetComputeRootSignature(list, pipeline->root);
    ID3D12GraphicsCommandList_SetPipelineState(list, pipeline->state);
    if (pipeline->immediate_words)
        ID3D12GraphicsCommandList_SetComputeRoot32BitConstants(list,pipeline->immediate_root,
            pipeline->immediate_words,resources->immediate_words,0);
    if (use) doe_dx_bind_descriptors(commands,pipeline,use,false);
    if (use && use->storage_sizes[0]) ID3D12GraphicsCommandList_SetComputeRootConstantBufferView(list,
        pipeline->storage_sizes_root,use->storage_sizes[0]);
    doe_dx_transition_resources(commands, pipeline, resources, false, false);
    if (arguments) {
        doe_dx_transition(list, arguments->resource,
            D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
        ID3D12GraphicsCommandList_ExecuteIndirect(list, commands->context->dispatch_signature,
            1, arguments->resource, offset, NULL, 0);
        doe_dx_transition(list, arguments->resource,
            D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT, D3D12_RESOURCE_STATE_COPY_DEST);
    } else {
        ID3D12GraphicsCommandList_Dispatch(list, x, y, z);
    }
    doe_dx_transition_resources(commands, pipeline, resources, false, true);
    return doe_dx_fail(DOE_DX_OK, 0, "native.dispatch", error);
}

doe_dx_status doe_dx_native_dispatch(doe_dx_native_commands *commands,
    doe_dx_native_pipeline *pipeline, const doe_dx_native_resources *resources,
    uint32_t x, uint32_t y, uint32_t z, doe_dx_error *error) {
    return record_dispatch(commands, pipeline, resources, NULL, 0, x, y, z, error);
}

doe_dx_status doe_dx_native_dispatch_indirect(doe_dx_native_commands *commands,
    doe_dx_native_pipeline *pipeline, const doe_dx_native_resources *resources,
    doe_dx_native_buffer *arguments, uint64_t offset, doe_dx_error *error) {
    return record_dispatch(commands, pipeline, resources, arguments, offset, 0, 0, 0, error);
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
