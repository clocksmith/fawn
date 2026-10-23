/* SPDX-License-Identifier: MIT */
#include "internal.h"
#include "surface_internal.h"

doe_vk_status doe_vk_poison(doe_vk_commands *commands, doe_vk_status status) {
    if (commands != NULL && commands->state == DOE_VK_RECORDING) {
        commands->state = DOE_VK_POISONED;
        commands->failure = (doe_vk_error){status, 0, "recording failure"};
    }
    return status;
}

doe_vk_status doe_vk_recording(doe_vk_commands *commands) {
    if (commands == NULL) return DOE_VK_INVALID_ARGUMENT;
    if (commands->state == DOE_VK_POISONED) return commands->failure.status;
    if (commands->state != DOE_VK_RECORDING) return DOE_VK_INVALID_STATE;
    if (commands->context->lost) return doe_vk_poison(commands, DOE_VK_DEVICE_LOST);
    return DOE_VK_OK;
}

doe_vk_status doe_vk_use_buffer(doe_vk_commands *commands, doe_vk_buffer *buffer) {
    if (buffer == NULL || buffer->context != commands->context) return DOE_VK_INVALID_ARGUMENT;
    if (buffer->content_status != DOE_VK_OK) return buffer->content_status;
    for (doe_vk_buffer_use *use = commands->buffers; use != NULL; use = use->next) {
        if (use->buffer == buffer) return DOE_VK_OK;
    }
    if (buffer->recorded_uses == UINT32_MAX) return DOE_VK_OUT_OF_MEMORY;
    doe_vk_buffer_use *use = malloc(sizeof(*use));
    if (use == NULL) return DOE_VK_OUT_OF_MEMORY;
    doe_vk_status status = doe_vk_buffer_retain(buffer);
    if (status != DOE_VK_OK) {
        free(use);
        return status;
    }
    ++buffer->recorded_uses;
    *use = (doe_vk_buffer_use){ buffer, commands->buffers };
    commands->buffers = use;
    return DOE_VK_OK;
}

/* Deliberately conservative synchronization. The production optimization and
 * hazard tracker are not claimed migrated by this correctness-first owner. */
void doe_vk_barrier(doe_vk_commands *commands) {
    const VkMemoryBarrier barrier = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_HOST_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_HOST_READ_BIT
    };
    const VkFlags stages = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT | VK_PIPELINE_STAGE_HOST_BIT;
    commands->context->vk.vkCmdPipelineBarrier(commands->command, stages, stages, 0,
        1, &barrier, 0, NULL, 0, NULL);
}

static doe_vk_status create_commands(doe_vk_context *context, bool reusable,
    doe_vk_commands **out, doe_vk_error *error) {
    if (context == NULL || out == NULL) return doe_vk_fail(DOE_VK_INVALID_ARGUMENT, "command descriptor", error);
    if (context->lost) return doe_vk_fail(DOE_VK_DEVICE_LOST, "command context", error);
    doe_vk_commands *commands = calloc(1, sizeof(*commands));
    if (commands == NULL) return doe_vk_fail(DOE_VK_OUT_OF_MEMORY, "command owner", error);
    doe_vk_status status = doe_vk_context_retain(context);
    if (status != DOE_VK_OK) {
        free(commands);
        return doe_vk_fail(status, "command context retain", error);
    }
    commands->context = context;
    commands->state = DOE_VK_RECORDING;
    commands->uses_active = true;
    commands->reusable = reusable;
    commands->references = 1;
    const VkCommandPoolCreateInfo pool = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
        .queueFamilyIndex = context->caps.queue_family
    };
    status = doe_vk_result(context, context->vk.vkCreateCommandPool(context->device, &pool, NULL, &commands->pool), "vkCreateCommandPool", error);
    if (status != DOE_VK_OK) goto failed;
    const VkCommandBufferAllocateInfo allocate = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = commands->pool,
        .commandBufferCount = 1
    };
    status = doe_vk_result(context, context->vk.vkAllocateCommandBuffers(context->device, &allocate, &commands->command), "vkAllocateCommandBuffers", error);
    if (status != DOE_VK_OK) goto failed;
    const VkFenceCreateInfo fence = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    status = doe_vk_result(context, context->vk.vkCreateFence(context->device, &fence, NULL, &commands->fence), "vkCreateFence", error);
    if (status != DOE_VK_OK) goto failed;
    const VkCommandBufferBeginInfo begin = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = reusable ? 0 : VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
    };
    status = doe_vk_result(context, context->vk.vkBeginCommandBuffer(commands->command, &begin), "vkBeginCommandBuffer", error);
    if (status != DOE_VK_OK) goto failed;
    *out = commands;
    return doe_vk_fail(DOE_VK_OK, "commands recording", error);
failed:
    (void)doe_vk_commands_release(commands);
    return status;
}

doe_vk_status doe_vk_commands_create(doe_vk_context *context,
    doe_vk_commands **out, doe_vk_error *error) {
    return create_commands(context, false, out, error);
}

doe_vk_status doe_vk_commands_create_reusable(doe_vk_context *context,
    doe_vk_commands **out, doe_vk_error *error) {
    return create_commands(context, true, out, error);
}

doe_vk_status doe_vk_commands_retain(doe_vk_commands *commands) {
    if (commands == NULL || !commands->reusable) return DOE_VK_INVALID_ARGUMENT;
    if (commands->references == UINT32_MAX) return DOE_VK_OUT_OF_MEMORY;
    ++commands->references;
    return DOE_VK_OK;
}

doe_vk_status doe_vk_commands_copy(doe_vk_commands *commands,
    doe_vk_buffer *source, uint64_t source_offset, doe_vk_buffer *destination,
    uint64_t destination_offset, uint64_t size) {
    doe_vk_status status = doe_vk_recording(commands);
    if (status != DOE_VK_OK) return status;
    if (source == NULL || destination == NULL || source->context != commands->context ||
        destination->context != commands->context || !(source->usage & DOE_VK_BUFFER_COPY_SRC) ||
        !(destination->usage & DOE_VK_BUFFER_COPY_DST) || size == 0 ||
        ((source_offset | destination_offset | size) & 3) != 0 ||
        !doe_vk_range(source->size, source_offset, size) ||
        !doe_vk_range(destination->size, destination_offset, size))
        return doe_vk_poison(commands, DOE_VK_INVALID_ARGUMENT);
    if (source == destination && source_offset < destination_offset + size && destination_offset < source_offset + size)
        return doe_vk_poison(commands, DOE_VK_INVALID_ARGUMENT);
    status = doe_vk_use_buffer(commands, source);
    if (status == DOE_VK_OK) status = doe_vk_use_buffer(commands, destination);
    if (status != DOE_VK_OK) return doe_vk_poison(commands, status);
    const VkBufferCopy copy = { source_offset, destination_offset, size };
    doe_vk_barrier(commands);
    commands->context->vk.vkCmdCopyBuffer(commands->command, source->buffer, destination->buffer, 1, &copy);
    return DOE_VK_OK;
}

doe_vk_status doe_vk_commands_fill(doe_vk_commands *commands,
    doe_vk_buffer *destination, uint64_t offset, uint64_t size, uint32_t value) {
    doe_vk_status status = doe_vk_recording(commands);
    if (status != DOE_VK_OK) return status;
    if (destination == NULL || destination->context != commands->context ||
        !(destination->usage & DOE_VK_BUFFER_COPY_DST) || size == 0 ||
        ((offset | size) & 3) != 0 || !doe_vk_range(destination->size, offset, size))
        return doe_vk_poison(commands, DOE_VK_INVALID_ARGUMENT);
    status = doe_vk_use_buffer(commands, destination);
    if (status != DOE_VK_OK) return doe_vk_poison(commands, status);
    doe_vk_barrier(commands);
    commands->context->vk.vkCmdFillBuffer(commands->command, destination->buffer, offset, size, value);
    return DOE_VK_OK;
}

doe_vk_status doe_vk_commands_dispatch(doe_vk_commands *commands,
    doe_vk_pipeline *pipeline, const doe_vk_buffer_binding *bindings,
    size_t binding_count, uint32_t x, uint32_t y, uint32_t z) {
    doe_vk_status status = doe_vk_recording(commands);
    if (status != DOE_VK_OK) return status;
    const uint32_t *limit = commands->context->caps.max_workgroup_count;
    if (x > limit[0] || y > limit[1] || z > limit[2])
        return doe_vk_poison(commands, DOE_VK_INVALID_ARGUMENT);
    status = doe_vk_bind_dispatch(commands, pipeline, bindings, binding_count);
    if (status != DOE_VK_OK) return status;
    commands->context->vk.vkCmdDispatch(commands->command, x, y, z);
    return DOE_VK_OK;
}

static void complete(doe_vk_commands *commands, doe_vk_status status, const doe_vk_error *error) {
    if (status != DOE_VK_OK) {
        if (commands->failure.status == DOE_VK_OK)
            commands->failure = error && error->status == status ? *error :
                (doe_vk_error){status, 0, "terminal failure"};
        status = commands->failure.status;
    }
    commands->state = status != DOE_VK_OK ? DOE_VK_FAILED :
        commands->reusable ? DOE_VK_READY : DOE_VK_COMPLETE;
    doe_vk_textures_complete(commands, status);
    doe_vk_queries_complete(commands, status);
    if (!commands->uses_active) return;
    for (doe_vk_buffer_use *use = commands->buffers; use != NULL; use = use->next) {
        if (status != DOE_VK_OK && use->buffer->content_status == DOE_VK_OK)
            use->buffer->content_status = status;
        --use->buffer->recorded_uses;
    }
    commands->uses_active = false;
}

/* A reusable recording keeps only state whose meaning does not change between
 * submissions: buffer uses and descriptor-bound dispatches. */
static bool reusable_admissible(const doe_vk_commands *commands) {
    return commands->textures == NULL && commands->queries == NULL &&
        commands->renders == NULL && commands->surface == NULL;
}

static doe_vk_status queue_submit(doe_vk_commands *commands, doe_vk_error *error) {
    doe_vk_context *context = commands->context;
    const VkSubmitInfo submit = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &commands->command,
        .signalSemaphoreCount = commands->surface != NULL ? 1 : 0,
        .pSignalSemaphores = commands->surface != NULL ? &commands->surface->render_finished : NULL
    };
    commands->state = DOE_VK_SUBMITTED;
    doe_vk_status status = doe_vk_result(context, context->vk.vkQueueSubmit(context->queue, 1, &submit, commands->fence), "vkQueueSubmit", error);
    if (status == DOE_VK_DEVICE_LOST) complete(commands, status, error);
    else if (status != DOE_VK_OK) {
        commands->state = DOE_VK_UNCERTAIN;
        commands->failure = *error;
    }
    return status;
}

/* READY recording: buffer uses were dropped at completion so buffers could be
 * mapped; re-acquire them, then re-arm this recording's own fence. */
static doe_vk_status resubmit(doe_vk_commands *commands, doe_vk_error *error) {
    doe_vk_context *context = commands->context;
    if (context->lost) return doe_vk_fail(DOE_VK_DEVICE_LOST, "resubmit device lost", error);
    for (doe_vk_buffer_use *use = commands->buffers; use != NULL; use = use->next) {
        if (use->buffer->content_status != DOE_VK_OK)
            return doe_vk_fail(use->buffer->content_status, "resource content from failed submission", error);
        if (use->buffer->recorded_uses == UINT32_MAX) return doe_vk_fail(DOE_VK_OUT_OF_MEMORY, "buffer use count", error);
    }
    for (doe_vk_buffer_use *use = commands->buffers; use != NULL; use = use->next) ++use->buffer->recorded_uses;
    commands->uses_active = true;
    doe_vk_status status = doe_vk_result(context, context->vk.vkResetFences(context->device, 1, &commands->fence),
        "vkResetFences", error);
    if (status != DOE_VK_OK) {
        /* Never dispatched: drop the uses without poisoning buffer contents, but
         * the recording itself is terminal. */
        for (doe_vk_buffer_use *use = commands->buffers; use != NULL; use = use->next) --use->buffer->recorded_uses;
        commands->uses_active = false;
        commands->state = DOE_VK_FAILED;
        commands->failure = *error;
        return status;
    }
    return queue_submit(commands, error);
}

doe_vk_status doe_vk_commands_seal(doe_vk_commands *commands, doe_vk_error *error) {
    doe_vk_error local_error = {0};
    if (error == NULL) error = &local_error;
    if (commands == NULL || !commands->reusable) return doe_vk_fail(DOE_VK_INVALID_ARGUMENT, "seal reusable commands", error);
    doe_vk_status status = doe_vk_recording(commands);
    if (status != DOE_VK_OK) return doe_vk_fail(status, "seal recording state", error);
    if (!reusable_admissible(commands)) {
        status = doe_vk_poison(commands, DOE_VK_UNSUPPORTED);
        return doe_vk_fail(status, "reusable recording state", error);
    }
    doe_vk_barrier(commands);
    status = doe_vk_result(commands->context, commands->context->vk.vkEndCommandBuffer(commands->command),
        "vkEndCommandBuffer", error);
    if (status != DOE_VK_OK) {
        (void)doe_vk_poison(commands, status);
        commands->failure = *error;
        return status;
    }
    for (doe_vk_buffer_use *use = commands->buffers; use != NULL; use = use->next) --use->buffer->recorded_uses;
    commands->uses_active = false;
    commands->state = DOE_VK_READY; /* The fence is unsignaled; resubmit re-arms it harmlessly. */
    return doe_vk_fail(DOE_VK_OK, "reusable recording sealed", error);
}

doe_vk_status doe_vk_commands_submit(doe_vk_commands *commands, doe_vk_error *error) {
    doe_vk_error local_error = {0};
    if (error == NULL) error = &local_error;
    if (commands != NULL && commands->reusable && commands->state == DOE_VK_READY)
        return resubmit(commands, error);
    doe_vk_status status = doe_vk_recording(commands);
    if (status != DOE_VK_OK) return doe_vk_fail(status, "submit recording state", error);
    if (commands->reusable && !reusable_admissible(commands)) {
        status = doe_vk_poison(commands, DOE_VK_UNSUPPORTED);
        return doe_vk_fail(status, "reusable recording state", error);
    }
    doe_vk_context *context = commands->context;
    for (doe_vk_buffer_use *use = commands->buffers; use != NULL; use = use->next) {
        if (use->buffer->content_status != DOE_VK_OK) {
            status = doe_vk_poison(commands, use->buffer->content_status);
            return doe_vk_fail(status, "resource content from failed submission", error);
        }
    }
    status = doe_vk_surface_submit_prepare(commands, error);
    if (status != DOE_VK_OK) {
        (void)doe_vk_poison(commands, status);
        commands->failure = *error;
        return status;
    }
    doe_vk_barrier(commands);
    status = doe_vk_result(context, context->vk.vkEndCommandBuffer(commands->command), "vkEndCommandBuffer", error);
    if (status != DOE_VK_OK) {
        (void)doe_vk_poison(commands, status);
        commands->failure = *error;
        return status;
    }
    return queue_submit(commands, error);
}

static doe_vk_status completion_state(doe_vk_commands *commands, doe_vk_error *error) {
    if (commands == NULL) return doe_vk_fail(DOE_VK_INVALID_ARGUMENT, "completion command", error);
    if (commands->state == DOE_VK_COMPLETE || commands->state == DOE_VK_READY)
        return doe_vk_fail(DOE_VK_OK, "completed", error);
    if (commands->state == DOE_VK_FAILED) {
        if (error != NULL) *error = commands->failure;
        return commands->failure.status;
    }
    if (commands->state != DOE_VK_SUBMITTED && commands->state != DOE_VK_UNCERTAIN)
        return doe_vk_fail(DOE_VK_INVALID_STATE, "command not submitted", error);
    if (commands->context->lost) {
        doe_vk_error lost = {DOE_VK_DEVICE_LOST, 0, "device lost"};
        complete(commands, DOE_VK_DEVICE_LOST, &lost);
        if (error != NULL) *error = commands->failure;
        return commands->failure.status;
    }
    return DOE_VK_PENDING;
}

doe_vk_status doe_vk_commands_wait(doe_vk_commands *commands,
    uint64_t timeout_ns, doe_vk_error *error) {
    doe_vk_error local_error = {0};
    if (error == NULL) error = &local_error;
    doe_vk_status status = completion_state(commands, error);
    if (status != DOE_VK_PENDING) return status;
    doe_vk_context *context = commands->context;
    status = doe_vk_result(context, context->vk.vkWaitForFences(context->device, 1,
        &commands->fence, 1, timeout_ns), "vkWaitForFences", error);
    if (status == DOE_VK_OK) {
        if (commands->state == DOE_VK_UNCERTAIN) status = commands->failure.status;
        complete(commands, status, error);
        if (status != DOE_VK_OK) *error = commands->failure;
    } else if (status == DOE_VK_DEVICE_LOST) {
        complete(commands, status, error);
        *error = commands->failure;
        status = commands->failure.status;
    }
    return status;
}

doe_vk_status doe_vk_commands_poll(doe_vk_commands *commands, doe_vk_error *error) {
    doe_vk_error local_error = {0};
    if (error == NULL) error = &local_error;
    doe_vk_status status = completion_state(commands, error);
    if (status != DOE_VK_PENDING) return status;
    doe_vk_context *context = commands->context;
    status = doe_vk_result(context, context->vk.vkGetFenceStatus(context->device, commands->fence), "vkGetFenceStatus", error);
    if (status == DOE_VK_OK) {
        if (commands->state == DOE_VK_UNCERTAIN) status = commands->failure.status;
        complete(commands, status, error);
        if (status != DOE_VK_OK) *error = commands->failure;
    } else if (status == DOE_VK_DEVICE_LOST) {
        complete(commands, status, error);
        *error = commands->failure;
        status = commands->failure.status;
    }
    return status;
}

doe_vk_status doe_vk_commands_recover(doe_vk_commands *commands, doe_vk_error *error) {
    doe_vk_error local_error = {0};
    if (error == NULL) error = &local_error;
    doe_vk_status status = completion_state(commands, error);
    if (status != DOE_VK_PENDING) return status;
    doe_vk_context *context = commands->context;
    status = doe_vk_result(context, context->vk.vkQueueWaitIdle(context->queue), "vkQueueWaitIdle", error);
    if (status == DOE_VK_OK) {
        if (commands->state == DOE_VK_UNCERTAIN) status = commands->failure.status;
        complete(commands, status, error);
        if (status != DOE_VK_OK) *error = commands->failure;
    } else if (status == DOE_VK_DEVICE_LOST) {
        complete(commands, status, error);
        *error = commands->failure;
        status = commands->failure.status;
    }
    return status;
}

doe_vk_status doe_vk_commands_release(doe_vk_commands *commands) {
    if (commands == NULL) return DOE_VK_OK;
    if (commands->state == DOE_VK_SUBMITTED || commands->state == DOE_VK_UNCERTAIN) {
        if (!commands->context->lost) return DOE_VK_BUSY;
        complete(commands, DOE_VK_DEVICE_LOST, NULL);
    }
    if (commands->references > 1) {
        --commands->references;
        return DOE_VK_OK;
    }
    doe_vk_status surface_status = doe_vk_surface_commands_release(commands);
    if (surface_status != DOE_VK_OK) return surface_status;
    doe_vk_context *context = commands->context;
    if (commands->pool != 0) context->vk.vkDestroyCommandPool(context->device, commands->pool, NULL);
    if (commands->fence != 0) context->vk.vkDestroyFence(context->device, commands->fence, NULL);
    while (commands->dispatches != NULL) {
        doe_vk_dispatch_use *use = commands->dispatches;
        commands->dispatches = use->next;
        doe_vk_dispatch_release(context, use);
    }
    doe_vk_render_release(commands);
    doe_vk_queries_release(commands);
    doe_vk_textures_release(commands);
    while (commands->buffers != NULL) {
        doe_vk_buffer_use *use = commands->buffers;
        commands->buffers = use->next;
        if (commands->uses_active) --use->buffer->recorded_uses;
        doe_vk_buffer_release(use->buffer);
        free(use);
    }
    free(commands);
    doe_vk_context_release(context);
    return DOE_VK_OK;
}

doe_vk_status doe_vk_commands_drop(doe_vk_commands *commands) {
    if (commands == NULL) return DOE_VK_OK;
    if (commands->references > 1) {
        /* Another holder (a pending submission) owns completion and release. */
        --commands->references;
        return DOE_VK_OK;
    }
    return doe_vk_commands_release(commands);
}
