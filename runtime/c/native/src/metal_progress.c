/* SPDX-License-Identifier: MIT */
#include "surface.h"
#include "../../backends/metal/src/progress.h"
#include <limits.h>
#include <stdlib.h>

/* Owner decision 2026-10-31, superseding the 2026-10-24 worker wake for Metal:
 * Metal reports completion through each command buffer's completed handler, as
 * Dawn does (QueueMTL.mm:234-240). Submission no longer wakes the progress
 * worker under the device mutex. The handler runs on a Metal-owned thread, never
 * takes the device mutex and never retires anything: public calls keep retiring
 * through routine settlement, and once the device is destroyed (no external
 * device reference remains) the handler wakes the worker, which retires what is
 * left under its ordinary device lease. Its own references (device plus one per
 * registered handler) let a late handler outlive the device. */
typedef struct native_metal_completion {
    atomic_uint references;
    atomic_bool armed;
    atomic_uint_fast64_t count; /* Handlers run so far. */
    doe_mutex mutex;            /* Orders a wake against device detach. */
    native_device *device;      /* Guarded by mutex; NULL once detached. */
} native_metal_completion;

static void completion_release(native_metal_completion *completion)
{
    if (atomic_fetch_sub_explicit(&completion->references, 1, memory_order_acq_rel) != 1) return;
    if (doe_mutex_destroy(&completion->mutex).status != DOE_HOST_OK) abort();
    free(completion);
}

static void completion_acquire(void *data)
{
    native_metal_completion *completion = data;
    if (atomic_fetch_add_explicit(&completion->references, 1, memory_order_relaxed) == UINT_MAX) abort();
}

/* Metal's completion thread, once per committed command (or synchronously when
 * registration failed). The device condition outlives every wake: detach takes
 * this mutex before the device destroys it. */
static void completion_notify(void *data)
{
    native_metal_completion *completion = data;
    atomic_fetch_add_explicit(&completion->count, 1, memory_order_release);
    if (atomic_load_explicit(&completion->armed, memory_order_acquire)) {
        if (doe_mutex_lock(&completion->mutex).status != DOE_HOST_OK) abort();
        if (completion->device &&
            doe_condition_signal(&completion->device->pending_changed).status != DOE_HOST_OK) abort();
        if (doe_mutex_unlock(&completion->mutex).status != DOE_HOST_OK) abort();
    }
    completion_release(completion);
}

bool native_metal_completion_attach(native_device *device)
{
    native_metal_completion *completion = calloc(1, sizeof(*completion));
    if (!completion) return false;
    if (doe_mutex_init(&completion->mutex, false).status != DOE_HOST_OK) {
        if (doe_mutex_destroy(&completion->mutex).status != DOE_HOST_OK) abort();
        free(completion);
        return false;
    }
    atomic_init(&completion->references, 1);
    atomic_init(&completion->armed, false);
    atomic_init(&completion->count, 0);
    completion->device = device;
    const doe_mtl_completion_sink sink = {completion, completion_acquire, completion_notify};
    if (doe_mtl_context_set_completion(device->backend.value.metal.context, sink) != DOE_MTL_OK) {
        completion_release(completion);
        return false;
    }
    device->metal_completion = completion;
    return true;
}

void native_metal_completion_detach(native_device *device)
{
    native_metal_completion *completion = device->metal_completion;
    if (!completion) return;
    if (doe_mutex_lock(&completion->mutex).status != DOE_HOST_OK) abort();
    completion->device = NULL;
    if (doe_mutex_unlock(&completion->mutex).status != DOE_HOST_OK) abort();
    device->metal_completion = NULL;
    completion_release(completion);
}

/* Device mutex held. From now on handlers wake the worker. */
void native_metal_completion_arm(native_device *device)
{
    if (device->metal_completion)
        atomic_store_explicit(&device->metal_completion->armed, true, memory_order_release);
}

bool native_metal_completion_armed(const native_device *device)
{
    return device->metal_completion &&
        atomic_load_explicit(&device->metal_completion->armed, memory_order_acquire);
}

uint64_t native_metal_completion_count(const native_device *device)
{
    return device->metal_completion ?
        (uint64_t)atomic_load_explicit(&device->metal_completion->count, memory_order_acquire) : 0;
}

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
