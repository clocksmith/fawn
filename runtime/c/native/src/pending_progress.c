/* SPDX-License-Identifier: MIT */
#include "surface.h"
#include "provider_pin.h"
#include <stdlib.h>

/* The device mutex owns the pending list. This thread neither copies that list
 * nor creates resource references: it owns one existing device lease, allowing
 * the recorded submissions to retire even after every public handle is gone. */
static void pending_progress(void *identity)
{
    native_device *device = identity;
    native_lock(device);
    for (;;) {
        while (!device->pending && !device->pending_progress_join &&
                atomic_load_explicit(&device->references, memory_order_acquire) > 1) {
            if (doe_condition_wait(&device->pending_changed, &device->mutex).status != DOE_HOST_OK) abort();
        }
        if (!device->pending) {
            device->pending_progress_ready = false;
            bool join = device->pending_progress_join;
            native_unlock(device);
            /* The worker's ordinary lease protects the actual stored host
             * handle through self-detach; no pseudo-handle is substituted. */
            if (!join && doe_thread_detach(&device->pending_thread).status != DOE_HOST_OK) abort();
            native_device_release(device);
            return;
        }
        native_progress_pending(device);
        native_surface *surface = native_surface_retirement_peek(device);
        if (surface) {
            /* A temporary existing surface lease permits lock-order reversal.
             * The record stays linked until surface->mutex is held, so a public
             * reconfigure can complete it synchronously instead of racing an
             * already-transferred cleanup. Neither path owns a copied record. */
            native_unlock(device);
            if (doe_mutex_lock(&surface->mutex).status != DOE_HOST_OK) abort();
            native_lock(device);
            native_pending *retired = surface->retirement ?
                native_surface_retirement_take(device, surface->retirement) : NULL;
            if (retired) native_surface_retirement_finish_locked(retired);
            native_unlock(device);
            if (doe_mutex_unlock(&surface->mutex).status != DOE_HOST_OK) abort();
            native_surface_retirement_release(retired);
            native_surface_release_lease(surface);
            native_lock(device);
            continue;
        }
        if (device->pending) {
            struct timespec now, deadline;
            doe_host_result clock = doe_monotonic_now(&now);
            if (clock.status == DOE_HOST_OK) clock = doe_time_add(&now, UINT64_C(10000000), &deadline);
            if (clock.status != DOE_HOST_OK) {
                /* Clock failure cannot discard live work or spin with its
                 * mutex held. Poll again after a real bounded host sleep. */
                native_unlock(device);
                if (doe_sleep_for(UINT64_C(10000000)).status != DOE_HOST_OK) abort();
                native_lock(device);
                continue;
            }
            /* Only this worker waits: its recursive device mutex is held at
             * depth1 here; all recursive helpers above returned balanced. */
            doe_host_result waited = doe_condition_wait_until_at(&device->pending_changed,
                &device->mutex, &deadline, &now);
            if (waited.status != DOE_HOST_OK && waited.status != DOE_HOST_TIMED_OUT) abort();
        }
    }
}

bool native_pending_progress_start(native_device *device)
{
    if (!native_provider_pin()) return false;
    if (doe_condition_init(&device->pending_changed).status != DOE_HOST_OK) {
        if (doe_condition_destroy(&device->pending_changed).status != DOE_HOST_OK) abort();
        return false;
    }
    native_device_retain(device);
    /* The worker begins by taking this mutex. Exclude execution until the host
     * thread control and its real handle have been completely published. */
    native_lock(device);
    device->pending_progress_ready = true;
    doe_host_result created = doe_thread_create(&device->pending_thread, pending_progress, device);
    if (created.status != DOE_HOST_OK) {
        device->pending_progress_ready = false;
        if (native_release_count(&device->references)) abort();
    }
    native_unlock(device);
    if (created.status != DOE_HOST_OK && doe_condition_destroy(&device->pending_changed).status != DOE_HOST_OK) abort();
    return created.status == DOE_HOST_OK;
}

void native_pending_progress_wake(native_device *device)
{
    /* Caller holds device mutex and a live lease. A worker-less device is never
     * published, so no native submission can create an unserviceable obligation. */
    if (device->pending_progress_ready && doe_condition_signal(&device->pending_changed).status != DOE_HOST_OK) abort();
}

void native_pending_progress_destroy(native_device *device)
{
    if (device->pending_progress_ready) abort();
    if (doe_condition_destroy(&device->pending_changed).status != DOE_HOST_OK) abort();
}
