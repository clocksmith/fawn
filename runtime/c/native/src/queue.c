/* SPDX-License-Identifier: MIT */
#include "surface.h"
#include "metal_frontend.h"
#include "d3d12_frontend.h"
#include "doe_native_queue.h"
#include "../../backends/metal/src/progress.h"
#include <stdlib.h>

static doe_vk_status record_dispatch(doe_vk_commands *commands, const doe_resource_command *record)
{
    native_pipeline *pipeline = doe_resource_pipeline_native(record->value.dispatch.pipeline);
    doe_vk_binding_resource bindings[NATIVE_MAX_PIPELINE_BINDINGS];
    doe_vk_status status = native_resolve_bindings(commands, pipeline,
        doe_resource_pipeline_get_layout(record->value.dispatch.pipeline), record->value.dispatch.groups,
        record->value.dispatch.dynamic_offsets, bindings);
    if (status != DOE_VK_OK) return status;
    status = doe_vk_commands_set_immediates(commands, pipeline->handle.vulkan,
        record->value.dispatch.immediates.words, pipeline->immediate_bytes);
    if (status != DOE_VK_OK) return status;
    if (record->kind == DOE_COMMAND_DISPATCH_INDIRECT) {
        status = native_dispatch_guard_initialize(pipeline->device);
        if (status != DOE_VK_OK) return status;
        native_buffer *arguments = doe_resource_buffer_native(record->value.dispatch.indirect);
        return doe_vk_commands_dispatch_indirect(commands,pipeline->device->dispatch_validator,
            pipeline->handle.vulkan,bindings,pipeline->binding_count,arguments->handle.vulkan,
            record->value.dispatch.indirect_offset);
    }
    return doe_vk_commands_dispatch_resources(commands, pipeline->handle.vulkan, bindings, pipeline->binding_count,
        record->value.dispatch.x, record->value.dispatch.y, record->value.dispatch.z);
}

static doe_vk_status record_vulkan_commands(doe_vk_commands *native, doe_resource_commands *commands)
{
    size_t count = 0;
    const doe_resource_command *records = doe_resource_commands_borrow(commands, &count);
    for (size_t i = 0; i < count; ++i) {
        const doe_resource_command *record = &records[i];
        doe_vk_status status;
        switch (record->kind) {
        case DOE_COMMAND_COPY_BUFFER: {
            native_buffer *source = doe_resource_buffer_native(record->value.copy.source);
            native_buffer *destination = doe_resource_buffer_native(record->value.copy.destination);
            if (!record->value.copy.size) continue;
            status = doe_vk_commands_copy(native, source->handle.vulkan, record->value.copy.source_offset,
                destination->handle.vulkan, record->value.copy.destination_offset, record->value.copy.size);
            break;
        }
        case DOE_COMMAND_CLEAR_BUFFER: {
            native_buffer *buffer = doe_resource_buffer_native(record->value.clear.buffer);
            if (!record->value.clear.size) continue;
            status = doe_vk_commands_fill(native, buffer->handle.vulkan, record->value.clear.offset,
                record->value.clear.size, 0);
            break;
        }
        case DOE_COMMAND_BUFFER_TEXTURE: {
            const doe_resource_texture_copy *copy = &record->value.texture.copy;
            if (!copy->width || !copy->height || !copy->depth_or_layers) continue;
            native_buffer *buffer = doe_resource_buffer_native(record->value.texture.buffer);
            native_texture *texture = doe_resource_texture_native(record->value.texture.texture);
            /* A discarded attachment reads (and partially updates) as zeros. */
            status = doe_vk_commands_initialize_texture(native, texture->texture);
            if (status != DOE_VK_OK) break;
            doe_vk_texture_buffer_layout layout = {copy->offset, record->value.texture.region.pitch,
                record->value.texture.region.image_rows};
            doe_vk_texture_region region = {.mip=copy->mip,
                .origin={copy->origin[0], copy->origin[1], copy->origin[2]},
                .extent={copy->width, copy->height, copy->depth_or_layers}};
            /* The resource region carries the admitted single aspect. */
            if (!native_texture_aspect(record->value.texture.region.aspect, &region.aspect)) {
                status = DOE_VK_INVALID_ARGUMENT;
                break;
            }
            status = record->value.texture.direction == DOE_RESOURCE_BUFFER_TO_TEXTURE ?
                doe_vk_commands_upload_texture(native, buffer->handle.vulkan, layout, texture->texture, region) :
                doe_vk_commands_read_texture(native, texture->texture, region, buffer->handle.vulkan, layout);
            break;
        }
        case DOE_COMMAND_COPY_TEXTURE: {
            const doe_resource_texture_blit *copy = &record->value.blit;
            if (!copy->width || !copy->height || !copy->depth_or_layers) continue;
            native_texture *source = doe_resource_texture_native(copy->source.texture);
            native_texture *destination = doe_resource_texture_native(copy->destination.texture);
            status = doe_vk_commands_initialize_texture(native, source->texture);
            if (status == DOE_VK_OK) status = doe_vk_commands_initialize_texture(native, destination->texture);
            if (status != DOE_VK_OK) break;
            doe_vk_texture_region from = {.mip=copy->source.mip,
                .origin={copy->source.origin[0],copy->source.origin[1],copy->source.origin[2]},
                .extent={copy->width,copy->height,copy->depth_or_layers}};
            doe_vk_texture_region to = {.mip=copy->destination.mip,
                .origin={copy->destination.origin[0],copy->destination.origin[1],copy->destination.origin[2]},
                .extent={copy->width,copy->height,copy->depth_or_layers}};
            /* Admitted texture copies use every aspect of both endpoints. */
            if (!native_texture_aspect(copy->source.aspect, &from.aspect) ||
                !native_texture_aspect(copy->destination.aspect, &to.aspect)) {
                status = DOE_VK_INVALID_ARGUMENT;
                break;
            }
            status = doe_vk_commands_copy_texture(native, source->texture, from, destination->texture, to);
            break;
        }
        case DOE_COMMAND_RENDER: {
            size_t consumed = 0;
            status = native_record_render(native, records + i, count - i, &consumed);
            if (status == DOE_VK_OK) i += consumed - 1;
            break;
        }
        case DOE_COMMAND_WRITE_TIMESTAMP:
        case DOE_COMMAND_RESOLVE_QUERY:
            status = native_record_query(native, record);
            break;
        case DOE_COMMAND_DISPATCH:
        case DOE_COMMAND_DISPATCH_INDIRECT:
            status = record_dispatch(native, record);
            break;
        default:
            return DOE_VK_UNSUPPORTED;
        }
        if (status != DOE_VK_OK) return status;
    }
    return DOE_VK_OK;
}

native_failure native_record_resource_commands(native_command_handle *native, doe_resource_commands *commands)
{
    if (native->kind == WGPUBackendType_Vulkan)
        return native_failure_status(native_from_vulkan(record_vulkan_commands(native->value.vulkan, commands)));
    if (native->kind == WGPUBackendType_Metal)
        return native_metal_record_commands(native->value.metal, commands);
    if (native->kind == WGPUBackendType_D3D12)
        return native_d3d12_record_commands(native, commands);
    return native_failure_status(NATIVE_UNSUPPORTED);
}

static void settle_resources(native_pending *pending, bool success)
{
    for (size_t i = 0; i < pending->count; ++i) {
        doe_resource_status terminal = success ? DOE_RESOURCE_OK : DOE_RESOURCE_EXECUTION_FAILED;
        doe_resource_status status = pending->replay ?
            doe_resource_commands_settle_replay(pending->resources[i], terminal) :
            doe_resource_commands_settle(pending->resources[i], terminal);
        if (status != DOE_RESOURCE_OK && status != DOE_RESOURCE_EXECUTION_FAILED) abort();
    }
    free(pending->resources);
    native_device *device = pending->device;
    free(pending);
    /* The foreground call or progress worker still owns a device lease while
     * this recursive drop returns to its enclosing device-mutex scope. */
    native_device_release(device);
}

/* Oldest first, so the first terminal failure is the earliest submission's.
 * poll observes completion without blocking; a record still running stays
 * linked with every lease and is not a failure. Otherwise recovery blocks
 * until each record is terminal (queue idle for uncertain submissions). */
static native_queue_settlement settle_pending(native_device *device, bool poll)
{
    if (!poll && device->backend.kind == WGPUBackendType_D3D12)
        return native_d3d12_drain_pending(device);
    native_queue_settlement result = {.complete = true};
    native_pending **cursor = &device->pending;
    while (*cursor) {
        native_pending *pending = *cursor;
        if (pending->kind == NATIVE_PENDING_SURFACE) {
            /* This also covers an acquisition/discard with no command object.
             * Zero timeout polls existing native fences; it never starts a frame. */
            if (!pending->ready)
                pending->ready = doe_vk_surface_retire(pending->frame, 0, NULL) == DOE_VK_OK;
            /* Cleanup-only WSI/frame leases do not describe unfinished queue
             * commands. Keep polling them, without delaying WorkDone or creating
             * a command error. Actual command records below retain their proof. */
            cursor = &pending->next;
            continue;
        }
        /* Blocking settlement waits each accepted submission's own fence and
         * recovers (queue idle) only a refused or uncertain one. */
        native_failure failure = poll ? native_command_poll(&pending->native) :
            native_command_in_flight(&pending->native) ? native_command_wait(&pending->native) :
            native_command_recover(&pending->native);
        if (poll && failure.status == NATIVE_PENDING) {
            result.complete = false;
            cursor = &pending->next;
            continue;
        }
        /* A terminal render fence does not retire presentation. Poll the same
         * backend frame before release, including discard after enqueue failure. */
        if (pending->frame) (void)doe_vk_surface_retire_frame(pending->frame, 0, NULL);
        native_result released = native_command_release(&pending->native);
        if (result.failure.status == NATIVE_OK && failure.status != NATIVE_OK) result.failure = failure;
        if (released == NATIVE_BUSY) {
            device->queue_failed = true;
            result.complete = false;
            if (result.failure.status == NATIVE_OK) result.failure = native_failure_status(NATIVE_PENDING);
            cursor = &pending->next;
            continue;
        }
        if (released != NATIVE_OK) abort();
        *cursor = pending->next;
        if (failure.status != NATIVE_OK) {
            if (device->retired_failure.status == NATIVE_OK) device->retired_failure = failure;
            device->queue_failed = true;
            /* Proven terminal failure may poison private helper scratch. Later
             * operations reconstruct helpers, never reuse failed storage. */
            doe_vk_timestamp_normalizer_release(device->timestamp_normalizer);
            device->timestamp_normalizer = NULL;
            native_indirect_guards_release(device);
        }
        settle_resources(pending, failure.status == NATIVE_OK);
    }
    return result;
}

static native_queue_settlement consume_settlement(native_device *device, bool poll)
{
    native_queue_settlement result = settle_pending(device, poll);
    if (device->retired_failure.status != NATIVE_OK) {
        result.failure = device->retired_failure;
        device->retired_failure = native_failure_status(NATIVE_OK);
    }
    return result;
}

native_queue_settlement native_settle_pending(native_device *device)
{
    return consume_settlement(device, false);
}

native_queue_settlement native_poll_pending(native_device *device)
{
    return consume_settlement(device, true);
}

native_queue_settlement native_settle_routine(native_device *device)
{
    return consume_settlement(device, device->backend.kind == WGPUBackendType_Vulkan ||
        device->backend.kind == WGPUBackendType_Metal || device->backend.kind == WGPUBackendType_D3D12);
}

/* Host access waits only while this buffer has an accepted native owner.
 * Vulkan visits older fences; Metal waits the exact conflicting native handle.
 * Unrelated running work is polled, never waited for. Errors are consumed once. */
native_failure native_settle_for_buffer(native_device *device, const native_buffer *buffer)
{
    if (device->backend.kind == WGPUBackendType_Metal) {
        native_failure failure = native_poll_pending(device).failure;
        doe_mtl_commands *owner = doe_mtl_buffer_owner(buffer->handle.metal);
        if (failure.status != NATIVE_OK || !owner) return failure;
        struct timespec now, deadline;
        if (doe_monotonic_now(&now).status != DOE_HOST_OK ||
            doe_time_add(&now,UINT64_C(5000000000),&deadline).status != DOE_HOST_OK)
            return native_failure_status(NATIVE_FAILURE);
        return native_metal_wait_owner(device,owner,&deadline);
    }
    if (device->backend.kind == WGPUBackendType_D3D12) {
#if defined(_WIN32)
        /* Open queue writes may target this buffer: submit them so the wait
         * below observes an accepted native owner. */
        native_failure failure = native_d3d12_flush_writes(device);
        if (failure.status == NATIVE_OK) failure = native_poll_pending(device).failure;
        if (failure.status != NATIVE_OK) return failure;
        if (!buffer->handle.d3d12) return native_failure_status(NATIVE_INVALID_STATE);
        doe_dx_commands *owner = doe_dx_buffer_owner(buffer->handle.d3d12->allocation);
        if (!owner) return failure;
        struct timespec now, deadline;
        if (doe_monotonic_now(&now).status != DOE_HOST_OK ||
            doe_time_add(&now, UINT64_C(30000000000), &deadline).status != DOE_HOST_OK)
            return native_failure_status(NATIVE_FAILURE);
        return native_d3d12_wait_owner(device, owner, &deadline);
#else
        return native_failure_status(NATIVE_UNAVAILABLE);
#endif
    }
    if (device->backend.kind != WGPUBackendType_Vulkan) return native_settle_pending(device).failure;
    native_failure first = native_poll_pending(device).failure;
    while (doe_vk_buffer_busy(buffer->handle.vulkan)) {
        native_pending *oldest = device->pending;
        while (oldest && oldest->kind == NATIVE_PENDING_SURFACE) oldest = oldest->next;
        if (!oldest) break;
        if (!native_command_in_flight(&oldest->native)) {
            native_failure recovered = native_settle_pending(device).failure;
            if (first.status == NATIVE_OK) first = recovered;
            break;
        }
        (void)native_command_wait(&oldest->native); /* Completion is observed by the settle below. */
        native_failure settled = native_poll_pending(device).failure;
        if (first.status == NATIVE_OK) first = settled;
        if (!doe_vk_buffer_busy(buffer->handle.vulkan)) break;
        for (native_pending *linked = device->pending; linked; linked = linked->next) {
            if (linked != oldest) continue;
            /* Its fence signalled but completion stays unknown. */
            device->queue_failed = true;
            return first.status == NATIVE_OK ? native_failure_status(NATIVE_PENDING) : first;
        }
    }
    return first;
}

/* Appends, keeping the list in submission order. */
static void link_pending(native_device *device, native_pending *pending)
{
    native_pending **cursor = &device->pending;
    while (*cursor) cursor = &(*cursor)->next;
    pending->next = NULL;
    *cursor = pending;
    native_pending_progress_wake(device);
}

void native_progress_pending(native_device *device)
{
    /* Preserve the first terminal failure until a public operation observes it.
     * Background retirement cannot silently turn its next write into success. */
    (void)settle_pending(device, true);
}

void native_drain_pending(native_device *device)
{
    (void)settle_pending(device, false);
}

void native_surface_retirement_arm(native_surface *surface)
{
    native_pending *pending = surface->retirement;
    if (pending->armed) return;
    pending->armed = true;
    native_retain_count(&surface->leases);
    native_device_retain(pending->device);
    pending->next = pending->device->pending;
    pending->device->pending = pending;
    native_pending_progress_wake(pending->device);
}

native_surface *native_surface_retirement_peek(native_device *device)
{
    for (native_pending *pending = device->pending; pending; pending = pending->next) {
        if (pending->kind == NATIVE_PENDING_SURFACE && pending->ready) {
            native_retain_count(&pending->surface->leases);
            return pending->surface;
        }
    }
    return NULL;
}

native_pending *native_surface_retirement_take(native_device *device, native_pending *only)
{
    native_pending **cursor = &device->pending;
    while (*cursor) {
        native_pending *pending = *cursor;
        if (pending->kind == NATIVE_PENDING_SURFACE && pending->ready && (!only || only == pending)) {
            *cursor = pending->next;
            pending->next = NULL;
            return pending;
        }
        cursor = &pending->next;
    }
    return NULL;
}

/* Admitted backend submissions return once the queue accepts them:
 * the record keeps its native and resource leases on the pending list until
 * the progress worker or a later public call observes it terminal. wait keeps
 * the explicit synchronous wait (programs and private initialization).
 * A refused submission recovers before returning, as before. */
static native_failure submit_pending(native_device *device, native_pending *pending, bool wait)
{
    link_pending(device, pending);
    native_failure failure = native_command_submit(&pending->native);
    if (failure.status == NATIVE_OK && wait) {
        if (pending->native.kind == WGPUBackendType_D3D12 && native_command_in_flight(&pending->native)) {
            struct timespec now, deadline;
            if (doe_monotonic_now(&now).status != DOE_HOST_OK ||
                doe_time_add(&now, UINT64_C(30000000000), &deadline).status != DOE_HOST_OK)
                failure = native_failure_status(NATIVE_FAILURE);
            else failure = native_d3d12_wait_owner(device, pending->native.value.d3d12.commands, &deadline);
        } else failure = native_command_wait(&pending->native);
    }
    if (failure.status != NATIVE_OK) {
        device->queue_failed = true;
        native_queue_settlement settled = native_settle_pending(device);
        (void)settled;
        return failure;
    }
    native_queue_settlement settled = native_poll_pending(device);
    if (wait && settled.failure.status == NATIVE_OK) {
        /* A waited submission still linked after its fence has unknown
         * completion: it keeps its leases and the queue reports failure. */
        for (native_pending *linked = device->pending; linked; linked = linked->next) {
            if (linked != pending) continue;
            device->queue_failed = true;
            return native_failure_status(NATIVE_PENDING);
        }
    }
    return settled.failure;
}

native_failure native_submit_program(native_device *device, native_command_handle native,
    doe_resource_commands *commands)
{
    native_pending *pending = calloc(1, sizeof(*pending));
    doe_resource_commands **resources = pending ? calloc(1, sizeof(*resources)) : NULL;
    if (!resources) {
        free(pending);
        if (native_command_release(&native) != NATIVE_OK) abort();
        if (doe_resource_commands_settle_replay(commands, DOE_RESOURCE_EXECUTION_FAILED) !=
                DOE_RESOURCE_EXECUTION_FAILED) abort();
        return native_failure_status(NATIVE_OUT_OF_MEMORY);
    }
    resources[0] = commands;
    *pending = (native_pending){.device=device,.native=native,.resources=resources,.count=1,.replay=true};
    native_device_retain(device);
    return submit_pending(device, pending, true);
}

static native_failure submit_backend_commands(native_device *device, native_command_handle commands, bool wait)
{
    native_pending *pending = calloc(1, sizeof(*pending));
    if (!pending) {
        if (native_command_release(&commands) != NATIVE_OK) abort();
        return native_failure_status(NATIVE_OUT_OF_MEMORY);
    }
    pending->device = device;
    native_device_retain(device);
    pending->native = commands;
    return submit_pending(device, pending, wait);
}

native_failure native_submit_backend_commands(native_device *device, native_command_handle commands)
{
    return submit_backend_commands(device, commands, true);
}

native_failure native_submit_backend_commands_async(native_device *device, native_command_handle commands)
{
    return submit_backend_commands(device, commands, false);
}

native_failure native_flush_initialization(native_device *device)
{
    if (device->backend.kind == WGPUBackendType_D3D12) return native_d3d12_prepare_recording(device);
    doe_vk_commands *batch = device->init_batch;
    if (!batch) return native_failure_status(NATIVE_OK);
    device->init_batch = NULL;
    device->init_batch_textures = 0;
    native_pending *pending = calloc(1, sizeof(*pending));
    if (!pending) {
        doe_vk_commands_abandon(batch, DOE_VK_OUT_OF_MEMORY);
        if (doe_vk_commands_release(batch) != DOE_VK_OK) abort();
        return native_failure_status(NATIVE_OUT_OF_MEMORY);
    }
    pending->device = device;
    native_device_retain(device);
    pending->native = (native_command_handle){.kind=WGPUBackendType_Vulkan,.value.vulkan=batch};
    /* Queue order puts it before the caller's recording; it does not wait. */
    return submit_pending(device, pending, false);
}

native_failure native_submit_commands(native_device *device, doe_vk_commands *commands)
{
    native_command_handle native = {.kind=WGPUBackendType_Vulkan,.value.vulkan=commands};
    return native_submit_backend_commands(device, native);
}

native_presentation native_submit_present(native_device *device, doe_vk_commands *commands,
    doe_vk_surface *surface)
{
    native_presentation result = {.presentation = {.status = NATIVE_INVALID_STATE}};
    native_pending *pending = calloc(1, sizeof(*pending));
    if (!pending) {
        /* Unsubmitted commands detach their present lease on release; the
         * acquired image is then discarded, never left owned. */
        if (doe_vk_commands_release(commands) != DOE_VK_OK) abort();
        (void)doe_vk_surface_retire_frame(surface, NATIVE_SURFACE_TIMEOUT_NS, NULL);
        result.submission = native_failure_status(NATIVE_OUT_OF_MEMORY);
        return result;
    }
    pending->device = device;
    native_device_retain(device);
    pending->native = (native_command_handle){.kind=WGPUBackendType_Vulkan,.value.vulkan=commands};
    pending->frame = surface;
    link_pending(device, pending);
    native_failure failure = native_command_submit(&pending->native);
    if (failure.status == NATIVE_OK) {
        bool suboptimal = false;
        doe_vk_error detail = {0};
        /* Waits the submit's render-finished semaphore on the same queue. */
        doe_vk_status status = doe_vk_surface_present(surface, commands, &suboptimal, &detail);
        result.presentation = native_failure_vulkan(status, &detail);
        failure = native_command_wait(&pending->native);
    }
    if (failure.status != NATIVE_OK) device->queue_failed = true;
    /* The presentation fence (or maintenance1 discard when present was never
     * enqueued) retires the frame; command completion never does. Settlement
     * must follow it: commands stay BUSY while their frame is unretired. */
    doe_vk_error detail = {0};
    doe_vk_status retired = doe_vk_surface_retire_frame(surface, NATIVE_SURFACE_TIMEOUT_NS, &detail);
    if (result.presentation.status == NATIVE_OK) result.presentation = native_failure_vulkan(retired, &detail);
    native_queue_settlement settled = native_settle_pending(device);
    /* A present that was never enqueued (refused submission or preparation)
     * could not discard its image while those commands were unsettled. Poll
     * once more without waiting: the image is discarded, not left owned. */
    (void)doe_vk_surface_retire_frame(surface, 0, NULL);
    result.submission = failure.status == NATIVE_OK ? settled.failure : failure;
    return result;
}

/* Caller holds the device mutex and a call lease; INVALID consumes only fresh
 * aliases, preserving existing submitted/program/replay ownership. */
static void invalidate_batch(native_device *device, size_t count, const WGPUCommandBuffer *handles)
{
    for (size_t i = 0; i < count; ++i)
        if (doe_resource_commands_device((doe_resource_commands *)handles[i]) == device)
            doe_resource_commands_invalidate((doe_resource_commands *)handles[i]);
}

void wgpuQueueSubmit(WGPUQueue handle, size_t count, const WGPUCommandBuffer *handles)
{
    native_device *device = (native_device *)handle;
    if (!device) return;
    if (!count) return;
    native_device_retain(device);
    if (!handles || count > SIZE_MAX / sizeof(doe_resource_commands *)) {
        native_error(device, WGPUErrorType_Validation, "invalid queue submit array");
        native_device_release(device);
        return;
    }
    native_pending *pending = calloc(1, sizeof(*pending));
    if (pending) pending->resources = calloc(count, sizeof(*pending->resources));
    if (!pending || !pending->resources) {
        free(pending);
        native_lock(device);
        invalidate_batch(device, count, handles);
        native_unlock(device);
        native_error(device, WGPUErrorType_OutOfMemory, "submission ownership allocation failed");
        native_device_release(device);
        return;
    }
    pending->device = device;
    native_device_retain(device);
    native_lock(device);
    native_failure failure = native_failure_status(native_device_result(device));
    if (failure.status == NATIVE_OK) failure = native_settle_routine(device).failure;
    for (size_t i = 0; failure.status == NATIVE_OK && i < count; ++i) {
        pending->resources[i] = (doe_resource_commands *)handles[i];
        if (doe_resource_commands_device(pending->resources[i]) != device)
            failure = native_failure_status(NATIVE_INVALID_ARGUMENT);
        for (size_t j = 0; j < i; ++j)
            if (handles[i] == handles[j]) failure = native_failure_status(NATIVE_INVALID_ARGUMENT);
    }
    /* Submission validity (including single use) is decided before any native
     * recording, so an invalid buffer never initializes or records backend work. */
    doe_resource_status resource_status = DOE_RESOURCE_OK;
    for (size_t i = 0; failure.status == NATIVE_OK && i < count; ++i) {
        resource_status = doe_resource_commands_begin_submit(pending->resources[i]);
        if (resource_status != DOE_RESOURCE_OK) failure = native_failure_status(NATIVE_INVALID_ARGUMENT);
        else ++pending->count;
    }
    /* D3D12 materializes zero fills itself and adopts, not flushes, open writes. */
    if (failure.status == NATIVE_OK && device->backend.kind != WGPUBackendType_D3D12)
        failure = native_flush_initialization(device);
    if (failure.status == NATIVE_OK && device->backend.kind == WGPUBackendType_Metal)
        failure = native_metal_record_batch(device,&pending->native,pending->resources,count);
    else if (failure.status == NATIVE_OK && device->backend.kind == WGPUBackendType_D3D12)
        failure = native_d3d12_record_batch(device,&pending->native,pending->resources,count);
    else {
        if (failure.status == NATIVE_OK) failure = native_command_create(&device->backend, &pending->native);
        for (size_t i = 0; failure.status == NATIVE_OK && i < count; ++i)
            failure = native_record_resource_commands(&pending->native, pending->resources[i]);
    }
    if (failure.status == NATIVE_OK) {
        /* Recovery preserves the original submission failure; successful release
         * is the backend's proof of terminal or never-dispatched ownership. */
        failure = submit_pending(device, pending, false);
    } else {
        if (native_command_release(&pending->native) != NATIVE_OK) abort();
        settle_resources(pending, false);
        /* A command buffer is single-use: any failed submit (validation or a
         * backend OOM/internal failure) consumes every command buffer it names
         * on this device, so a resubmission is a validation error. */
        invalidate_batch(device, count, handles);
    }
    native_unlock(device);
    if (failure.status != NATIVE_OK) {
        char message[NATIVE_FAILURE_MESSAGE_CAPACITY];
        native_failure_format(&failure, "queue submission failed; unresolved work stays retained", message);
        native_error(device, resource_status != DOE_RESOURCE_OK ?
            native_resource_error(resource_status) : native_result_error(failure.status), message);
    }
    native_device_release(device);
}

typedef struct queue_delivery {
    WGPUQueueWorkDoneCallbackInfo callback;
    WGPUQueueWorkDoneStatus status;
    char message[NATIVE_FAILURE_MESSAGE_CAPACITY];
} queue_delivery;
static void copy_queue(void *destination, const void *source)
{
    *(queue_delivery *)destination = *(const queue_delivery *)source;
}
static void deliver_queue(void *context)
{
    queue_delivery value = *(queue_delivery *)context;
    if (value.callback.callback)
        value.callback.callback(value.status, native_string(value.message),
            value.callback.userdata1, value.callback.userdata2);
}
WGPUFuture wgpuQueueOnSubmittedWorkDone(WGPUQueue handle, WGPUQueueWorkDoneCallbackInfo callback)
{
    native_device *device = (native_device *)handle;
    if (!device || callback.nextInChain) return (WGPUFuture){0};
    native_device_retain(device);
    native_lock(device);
    native_queue_settlement settled = native_settle_pending(device);
    queue_delivery delivery = {.callback = callback,
        .status = device->queue_failed ? WGPUQueueWorkDoneStatus_Error : WGPUQueueWorkDoneStatus_Success};
    native_unlock(device);
    WGPUFuture future = {0};
    native_failure failure = settled.failure;
    if (delivery.status == WGPUQueueWorkDoneStatus_Error && failure.status == NATIVE_OK)
        failure = native_failure_status(NATIVE_FAILURE);
    native_failure_format(&failure, "queue work failed", delivery.message);
    if (settled.complete) {
        doe_callback_status admitted;
        future = native_callback_admit(device->instance, callback.mode, &delivery,
            sizeof(delivery), _Alignof(queue_delivery), copy_queue, deliver_queue, &admitted);
        /* No callback was admitted. Preserve the consumed receipt through the
         * existing error channel while this call still owns its device lease. */
        if (!future.id && settled.failure.status != NATIVE_OK)
            native_error(device, native_result_error(settled.failure.status), delivery.message);
        else if (!future.id) native_callback_refused(device, admitted);
    } else {
        char message[NATIVE_FAILURE_MESSAGE_CAPACITY];
        native_failure_format(&settled.failure, "queue completion remains unknown; no completion future published", message);
        native_error(device, native_result_error(settled.failure.status), message);
    }
    native_device_release(device);
    return future;
}

uint32_t doeNativeQueueSyncInfo(WGPUQueue handle)
{
    native_device *device = (native_device *)handle;
    if (!device || (device->backend.kind != WGPUBackendType_Vulkan && device->backend.kind != WGPUBackendType_Metal &&
        device->backend.kind != WGPUBackendType_D3D12)) return 0;
    uint32_t bits;
    if (device->backend.kind == WGPUBackendType_Metal)
        bits = DOE_NATIVE_QUEUE_SYNC_BACKEND_METAL |
            (device->backend.value.metal.caps.timeline_enabled ? DOE_NATIVE_QUEUE_SYNC_TIMELINE_SEMAPHORE : 0);
    else if (device->backend.kind == WGPUBackendType_D3D12)
        bits = DOE_NATIVE_QUEUE_SYNC_BACKEND_D3D12 | DOE_NATIVE_QUEUE_SYNC_TIMELINE_SEMAPHORE;
    else bits = DOE_NATIVE_QUEUE_SYNC_BACKEND_VULKAN |
        (device->backend.value.vulkan.caps.timeline_enabled ? DOE_NATIVE_QUEUE_SYNC_TIMELINE_SEMAPHORE :
         DOE_NATIVE_QUEUE_SYNC_FENCE_POOL);
    native_lock(device);
    /* A command record leaves the pending list once its completion or failure
     * has been observed; surface records are presentation cleanup, not work. */
    for (const native_pending *pending = device->pending; pending; pending = pending->next) {
        if (pending->kind != NATIVE_PENDING_COMMANDS) continue;
        bits |= DOE_NATIVE_QUEUE_SYNC_DEFERRED_SUBMISSIONS;
        break;
    }
    native_unlock(device);
    return bits;
}
