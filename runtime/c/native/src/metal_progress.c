/* SPDX-License-Identifier: MIT */
#include "surface.h"
#include "../../backends/metal/src/progress.h"
#include <stdlib.h>

/* Only the existing pending owner can authorize waiting/retrying a conflict.
 * An unsubmitted foreign owner is BUSY, never a reason to wait or dispatch. */
static bool accepted_owner(native_device *device, doe_mtl_commands *owner)
{
    for (native_pending *pending = device->pending; pending; pending = pending->next)
        if (pending->kind == NATIVE_PENDING_COMMANDS && pending->native.kind == WGPUBackendType_Metal &&
            pending->native.value.metal == owner && native_command_in_flight(&pending->native)) return true;
    return false;
}

native_failure native_metal_wait_owner(native_device *device, doe_mtl_commands *owner,
    const struct timespec *deadline)
{
    if (!accepted_owner(device,owner)) return native_failure_status(NATIVE_BUSY);
    doe_mtl_wait_lease lease = {0};
    doe_mtl_status held = doe_mtl_commands_hold(owner,&lease);
    if (held != DOE_MTL_OK) return native_failure_status(native_from_metal(held));
    native_failure failure = native_failure_status(NATIVE_OK);
    for (;;) {
        native_queue_settlement settled = native_poll_pending(device);
        if (settled.failure.status != NATIVE_OK) { failure = settled.failure; break; }
        bool linked = false;
        for (native_pending *pending = device->pending; pending; pending = pending->next)
            if (pending->kind == NATIVE_PENDING_COMMANDS && pending->native.kind == WGPUBackendType_Metal &&
                doe_mtl_commands_matches(pending->native.value.metal,&lease)) linked = true;
        if (!linked) break;
        struct timespec now;
        if (doe_monotonic_now(&now).status != DOE_HOST_OK) {
            failure = native_failure_status(NATIVE_FAILURE); break;
        }
        if (doe_time_reached(&now,deadline)) {
            failure = native_failure_status(NATIVE_TIMEOUT); break;
        }
        uint64_t remaining = (uint64_t)(deadline->tv_sec - now.tv_sec) * UINT64_C(1000000000);
        if (deadline->tv_nsec >= now.tv_nsec) remaining += (uint64_t)(deadline->tv_nsec - now.tv_nsec);
        else remaining -= (uint64_t)(now.tv_nsec - deadline->tv_nsec);
        const uint64_t slice = remaining < UINT64_C(100000000) ? remaining : UINT64_C(100000000);
        native_unlock(device);
        doe_mtl_status waited = doe_mtl_commands_block(&lease,slice);
        native_lock(device);
        if (waited != DOE_MTL_OK && waited != DOE_MTL_PENDING) {
            /* Read-only wakeup is never completion proof. Ordinary polling
             * above remains the sole state/error/resource retirement owner. */
            failure = native_poll_pending(device).failure;
            if (failure.status == NATIVE_OK) failure = native_failure_status(native_from_metal(waited));
            break;
        }
    }
    doe_mtl_commands_unhold(&lease);
    return failure;
}

native_failure native_metal_record_batch(native_device *device, native_command_handle *native,
    doe_resource_commands **resources, size_t count)
{
    struct timespec started, deadline;
    if (doe_monotonic_now(&started).status != DOE_HOST_OK ||
        doe_time_add(&started,UINT64_C(5000000000),&deadline).status != DOE_HOST_OK)
        return native_failure_status(NATIVE_FAILURE);
    for (;;) {
        for (size_t i = 0; i < count; ++i)
            if (doe_resource_commands_validate_submission(resources[i]) != DOE_RESOURCE_OK)
                return native_failure_status(NATIVE_INVALID_ARGUMENT);
        native_failure failure = native_command_create(&device->backend,native);
        for (size_t i = 0; failure.status == NATIVE_OK && i < count; ++i)
            failure = native_record_resource_commands(native,resources[i]);
        if (failure.status != NATIVE_BUSY || native->kind != WGPUBackendType_Metal) return failure;
        doe_mtl_commands *conflict = doe_mtl_commands_conflict(native->value.metal);
        /* Roll back ALL uncommitted native work and speculative leases before
         * waiting. Replaying resource records never re-dispatches that work. */
        if (native_command_release(native) != NATIVE_OK) abort();
        failure = native_metal_wait_owner(device,conflict,&deadline);
        if (failure.status != NATIVE_OK) return failure;
        failure = native_failure_status(native_device_result(device));
        if (failure.status != NATIVE_OK) return failure;
    }
}
