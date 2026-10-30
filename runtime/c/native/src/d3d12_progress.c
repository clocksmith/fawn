/* SPDX-License-Identifier: MIT */
#include "surface.h"
#include "d3d12_frontend.h"
#include <stdlib.h>

#if defined(_WIN32)
static native_pending *accepted_owner(native_device *device, doe_dx_commands *owner)
{
    for (native_pending *pending = device->pending; pending; pending = pending->next)
        if (pending->kind == NATIVE_PENDING_COMMANDS && pending->native.kind == WGPUBackendType_D3D12 &&
            pending->native.value.d3d12.commands == owner) return pending;
    return NULL;
}

static native_failure wait_owner(native_device *device, doe_dx_commands *owner,
    const struct timespec *deadline, bool require_alive)
{
    native_pending *pending = accepted_owner(device, owner);
    if (!pending || !native_command_in_flight(&pending->native))
        return native_failure_status(NATIVE_BUSY);
    doe_dx_wait_lease lease = {0};
    doe_dx_error detail = {0};
    doe_dx_status status = doe_dx_commands_hold(owner, &lease, &detail);
    if (status != DOE_DX_OK) return native_failure_d3d12(status, &detail);
    native_failure failure = native_failure_status(NATIVE_OK);
    for (;;) {
        native_queue_settlement settled = native_poll_pending(device);
        if (settled.failure.status != NATIVE_OK) { failure = settled.failure; break; }
        if (require_alive) {
            failure = native_failure_status(native_device_result(device));
            if (failure.status != NATIVE_OK) break;
        }
        /* The shell can retire while unlocked. Its address is compared only;
         * the held context/event lease never dereferences that old command. */
        if (!accepted_owner(device, owner)) break;
        struct timespec now;
        if (doe_monotonic_now(&now).status != DOE_HOST_OK) {
            failure = native_failure_status(NATIVE_FAILURE); break;
        }
        if (doe_time_reached(&now, deadline)) {
            failure = native_failure_status(NATIVE_TIMEOUT); break;
        }
        uint64_t remaining = (uint64_t)(deadline->tv_sec - now.tv_sec) * UINT64_C(1000000000);
        if (deadline->tv_nsec >= now.tv_nsec) remaining += (uint64_t)(deadline->tv_nsec - now.tv_nsec);
        else remaining -= (uint64_t)(now.tv_nsec - deadline->tv_nsec);
        uint32_t milliseconds = remaining < UINT64_C(100000000) ?
            (uint32_t)((remaining + UINT64_C(999999)) / UINT64_C(1000000)) : 100;
        native_unlock(device);
        status = doe_dx_commands_block(&lease, milliseconds, &detail);
        native_lock(device);
        if (status != DOE_DX_OK && status != DOE_DX_PENDING) {
            failure = native_poll_pending(device).failure;
            if (failure.status == NATIVE_OK) failure = native_failure_d3d12(status, &detail);
            break;
        }
    }
    status = doe_dx_commands_unhold(&lease, &detail);
    if (failure.status == NATIVE_OK && status != DOE_DX_OK)
        failure = native_failure_d3d12(status, &detail);
    return failure;
}

static bool deadline_from_now(struct timespec *deadline)
{
    struct timespec now;
    return doe_monotonic_now(&now).status == DOE_HOST_OK &&
        doe_time_add(&now, UINT64_C(30000000000), deadline).status == DOE_HOST_OK;
}
#endif

native_failure native_d3d12_wait_owner(native_device *device, doe_dx_commands *owner,
    const struct timespec *deadline)
{
#if defined(_WIN32)
    return wait_owner(device, owner, deadline, true);
#else
    (void)device; (void)owner; (void)deadline;
    return native_failure_status(NATIVE_UNAVAILABLE);
#endif
}

native_queue_settlement native_d3d12_drain_pending(native_device *device)
{
#if defined(_WIN32)
    struct timespec deadline;
    if (!deadline_from_now(&deadline))
        return (native_queue_settlement){.failure={.status=NATIVE_FAILURE}};
    for (;;) {
        native_queue_settlement result = native_poll_pending(device);
        if (result.complete || result.failure.status != NATIVE_OK) return result;
        native_pending *pending = device->pending;
        while (pending && (pending->kind != NATIVE_PENDING_COMMANDS ||
                pending->native.kind != WGPUBackendType_D3D12)) pending = pending->next;
        if (!pending) return result;
        native_failure failure = wait_owner(device, pending->native.value.d3d12.commands,
            &deadline, false);
        if (failure.status != NATIVE_OK)
            return (native_queue_settlement){.failure=failure};
    }
#else
    (void)device;
    return (native_queue_settlement){.failure={.status=NATIVE_UNAVAILABLE}};
#endif
}

native_failure native_d3d12_record_batch(native_device *device, native_command_handle *native,
    doe_resource_commands **resources, size_t count)
{
#if defined(_WIN32)
    struct timespec deadline;
    if (!deadline_from_now(&deadline)) return native_failure_status(NATIVE_FAILURE);
    /* Deferred zero fills precede any recording that may read those buffers. */
    native_failure materialized = native_d3d12_materialize_zero(device);
    if (materialized.status != NATIVE_OK) return materialized;
    for (;;) {
        for (size_t i = 0; i < count; ++i)
            if (doe_resource_commands_validate_submission(resources[i]) != DOE_RESOURCE_OK)
                return native_failure_status(NATIVE_INVALID_ARGUMENT);
        native_failure failure = native_command_create(&device->backend, native);
        for (size_t i = 0; failure.status == NATIVE_OK && i < count; ++i)
            failure = native_record_resource_commands(native, resources[i]);
        if (failure.status != NATIVE_BUSY || native->kind != WGPUBackendType_D3D12) return failure;
        doe_dx_commands *conflict = doe_dx_commands_conflict(native->value.d3d12.commands);
        /* Discard every unsubmitted native record before waiting. Retrying the
         * immutable resource records cannot replay GPU work or preserve a new
         * speculative lease across an accepted predecessor's retirement. */
        if (native_command_release(native) != NATIVE_OK) abort();
        failure = wait_owner(device, conflict, &deadline, true);
        if (failure.status != NATIVE_OK) return failure;
    }
#else
    (void)device; (void)native; (void)resources; (void)count;
    return native_failure_status(NATIVE_UNAVAILABLE);
#endif
}
