/* SPDX-License-Identifier: MIT */
#include "windows_internal.h"
#ifdef _WIN32

void doe_dx_native_commands_destroy(doe_dx_native_commands *commands) {
    if (!commands) return;
    doe_dx_shared_waits_close(commands);
    if (commands->list) ID3D12GraphicsCommandList_Release(commands->list);
    if (commands->allocator) ID3D12CommandAllocator_Release(commands->allocator);
    doe_dx_descriptor_use *use = commands->descriptors;
    while (use) {
        doe_dx_descriptor_use *next = use->next;
        doe_dx_release_descriptors(use);
        use = next;
    }
    free(commands->held);
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
    commands->serial = ++context->recording_serial;
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
    if (before==after) return;
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
    /* The destination returns to COPY_DEST; a DEFAULT source is tracked at
     * COPY_SOURCE, so consecutive copies out of one buffer record no barrier. */
    doe_dx_native_buffer *written = destination->memory == DOE_DX_DEFAULT ? destination : NULL;
    doe_dx_settle_tracked(commands, &written, 1);
    bool round_trip = false;
    if (source->memory == DOE_DX_DEFAULT) {
        const doe_dx_buffer_state read = {source, D3D12_RESOURCE_STATE_COPY_SOURCE};
        if (!doe_dx_hold_buffers(commands, &read, 1)) {
            doe_dx_settle_buffers(commands);
            doe_dx_transition(commands->list, source->resource,
                D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE);
            round_trip = true;
        }
    }
    ID3D12GraphicsCommandList_CopyBufferRegion(commands->list, destination->resource,
        destination_offset, source->resource, source_offset, size);
    if (round_trip)
        doe_dx_transition(commands->list, source->resource,
            D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
}

void doe_dx_native_order_buffers(doe_dx_native_commands *commands,
    doe_dx_native_buffer *const *buffers, size_t count) {
    /* A transition out of COPY_DEST waits for every earlier write in that state;
     * returning to COPY_DEST keeps the per-operation state contract. */
    doe_dx_settle_tracked(commands, buffers, count);
    D3D12_RESOURCE_BARRIER barriers[16];
    for (size_t first = 0; first < count; first += 16) {
        const size_t chunk = count - first < 16 ? count - first : 16;
        for (size_t index = 0; index < chunk; ++index) {
            barriers[index] = (D3D12_RESOURCE_BARRIER){.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION};
            barriers[index].Transition.pResource = buffers[first + index]->resource;
            barriers[index].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            barriers[index].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
            barriers[index].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        }
        ID3D12GraphicsCommandList_ResourceBarrier(commands->list, (UINT)chunk, barriers);
        for (size_t index = 0; index < chunk; ++index) {
            barriers[index].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
            barriers[index].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
        }
        ID3D12GraphicsCommandList_ResourceBarrier(commands->list, (UINT)chunk, barriers);
    }
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

/* An identical consecutive dispatch borrows the previous descriptor range: the
 * range is already written, owned by this command and unchanged until retire. */
static doe_dx_status dispatch_descriptors(doe_dx_native_commands *commands,
    doe_dx_native_pipeline *pipeline, const doe_dx_native_resources *resources,
    doe_dx_descriptor_use **out, doe_dx_error *error) {
    doe_dx_compute_reuse *reuse = &commands->reuse;
    if (reuse->use && reuse->pipeline == pipeline && doe_dx_same_resources(&reuse->resources, resources)) {
        *out = reuse->use;
        return DOE_DX_OK;
    }
    doe_dx_descriptor_use *use = NULL;
    doe_dx_status status = doe_dx_prepare_descriptors(commands, pipeline, resources, &use, error);
    if (status != DOE_DX_OK) return status;
    use->next = commands->descriptors;
    commands->descriptors = use;
    reuse->pipeline = pipeline;
    reuse->use = use;
    doe_dx_copy_resources(&reuse->resources, resources);
    *out = use;
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
        doe_dx_status status = dispatch_descriptors(commands, pipeline, resources, &use, error);
        if (status != DOE_DX_OK) return status;
    }
    ID3D12GraphicsCommandList *list = commands->list;
    doe_dx_bind_compute_root(commands, pipeline->root);
    doe_dx_bind_state(commands, pipeline->state);
    if (pipeline->immediate_words)
        ID3D12GraphicsCommandList_SetComputeRoot32BitConstants(list,pipeline->immediate_root,
            pipeline->immediate_words,resources->immediate_words,0);
    if (use && use != commands->bound.compute_use) {
        doe_dx_bind_descriptors(commands,pipeline,use,false);
        if (use->storage_sizes[0]) ID3D12GraphicsCommandList_SetComputeRootConstantBufferView(list,
            pipeline->storage_sizes_root,use->storage_sizes[0]);
        commands->bound.compute_use = use;
    }
    /* Buffers stay in their dispatch state for the next dispatch (windows_states.c);
     * the portable owner rejects an argument buffer that is also bound. */
    const doe_dx_resource_set set = {pipeline->bindings, pipeline->textures, resources};
    doe_dx_buffer_state states[DOE_DX_MAX_BINDINGS + 1];
    size_t count = doe_dx_set_buffer_states(&set, 1, false, states);
    if (arguments)
        count = doe_dx_add_buffer_state(states, count, arguments, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
    const bool held = doe_dx_hold_buffers(commands, states, count);
    if (!held) {
        /* No room to remember states: the original per-dispatch round trip,
         * from the explicit COPY_DEST contract. */
        doe_dx_settle_buffers(commands);
        doe_dx_apply_buffer_states(list, states, count, false);
    }
    doe_dx_transition_set_textures(commands, &set, 1, false, false);
    if (arguments) {
        ID3D12GraphicsCommandList_ExecuteIndirect(list, commands->context->dispatch_signature,
            1, arguments->resource, offset, NULL, 0);
    } else {
        ID3D12GraphicsCommandList_Dispatch(list, x, y, z);
    }
    doe_dx_transition_set_textures(commands, &set, 1, false, true);
    if (!held) doe_dx_apply_buffer_states(list, states, count, true);
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
    doe_dx_forget_buffers(commands);
    HRESULT result = ID3D12GraphicsCommandList_Close(commands->list);
    return doe_dx_hresult(result, "ID3D12GraphicsCommandList.Close", error);
}

doe_dx_status doe_dx_native_execute(doe_dx_native_context *context,doe_dx_native_commands *commands,doe_dx_error *error) {
    doe_dx_status status=doe_dx_shared_waits_execute(context,commands,error);
    if (status!=DOE_DX_OK) return status;
    ID3D12CommandList *list = (ID3D12CommandList *)commands->list;
    ID3D12CommandQueue_ExecuteCommandLists(context->queue, 1, &list);
    return DOE_DX_OK;
}
#endif
