/* SPDX-License-Identifier: MIT */
#include "surface.h"
#include "provider_pin.h"
#include "d3d12_frontend.h"
#include "../../backends/metal/src/progress.h"
#include <stdlib.h>

/* The device mutex owns the pending list. This thread neither copies that list
 * nor creates resource references: it owns one existing device lease, allowing
 * the recorded submissions to retire even after every public handle is gone. */
/* The oldest command record the queue accepted and nobody has yet observed
 * terminal, or NULL. A refused submission (uncertain, its fence never
 * signals) is skipped: only blocking recovery resolves it, and waiting for it
 * would strand later work. Caller holds the device mutex. */
static native_command_handle *oldest_in_flight(native_device *device)
{
    for (native_pending *pending = device->pending; pending; pending = pending->next) {
        if (pending->kind == NATIVE_PENDING_SURFACE) continue;
        if (native_command_in_flight(&pending->native)) return &pending->native;
    }
    return NULL;
}

/* Bounds one unlocked fence wait so join requests and new records are seen. */
#define PROGRESS_FENCE_WAIT_NS UINT64_C(100000000)

/* Live Metal devices retire through public calls; their completed handlers
 * report completion without this thread (metal_progress.c). */
static bool progress_needed(const native_device *device)
{
    return device->pending && (device->backend.kind != WGPUBackendType_Metal ||
        native_metal_completion_armed(device));
}

static void pending_progress(void *identity)
{
    native_device *device = identity;
    /* A fence that signalled while its record still reports running (a
     * backend boundary that obscures completion) falls back to timed polls. */
    uintptr_t signalled = 0;
    native_lock(device);
    for (;;) {
        while (!progress_needed(device) && !device->pending_progress_join &&
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
        const uint64_t completions = native_metal_completion_count(device);
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
            native_pending *retired = NULL;
            for (native_pending *candidate = device->pending; candidate; candidate = candidate->next) {
                if (candidate->kind == NATIVE_PENDING_SURFACE && candidate->ready && candidate->surface == surface) {
                    retired = native_surface_retirement_take(device, candidate);
                    break;
                }
            }
            if (retired) native_surface_retirement_finish_locked(retired);
            native_unlock(device);
            if (doe_mutex_unlock(&surface->mutex).status != DOE_HOST_OK) abort();
            native_surface_retirement_release(retired);
            native_surface_release_lease(surface);
            native_lock(device);
            continue;
        }
        /* Wait for the oldest accepted submission's fence outside the device
         * mutex (owner authorized 2026-10-24: async submit A+B, D2,
         * fence-wait completion; futures later; reconfirmed after the section 7 risk review). The hold keeps its fence alive
         * through a concurrent settle; completion is observed under the
         * mutex on the next iteration. Metal no longer waits here (owner
         * decision 2026-10-31): its completed handler signals pending_changed. */
        native_command_handle *oldest = oldest_in_flight(device);
        if (oldest && oldest->kind == WGPUBackendType_Vulkan &&
            (uintptr_t)oldest->value.vulkan != signalled && doe_vk_commands_hold(oldest->value.vulkan) == DOE_VK_OK) {
            doe_vk_commands *command = oldest->value.vulkan;
            const uintptr_t command_identity = (uintptr_t)command;
            native_unlock(device);
            doe_vk_status waited = doe_vk_commands_block(command, PROGRESS_FENCE_WAIT_NS);
            native_lock(device);
            signalled = waited == DOE_VK_OK ? command_identity : 0;
            doe_vk_commands_unhold(command);
            continue;
        }
        uint64_t wait_ns = UINT64_C(10000000);
        if (oldest && oldest->kind == WGPUBackendType_Metal) {
            /* A handler that ran since the poll above is not lost; one that
             * signals between this check and the wait is bounded by wait_ns. */
            if (native_metal_completion_count(device) != completions) continue;
            wait_ns = PROGRESS_FENCE_WAIT_NS;
        }
#if defined(_WIN32)
        if (oldest && oldest->kind == WGPUBackendType_D3D12 &&
                (uintptr_t)oldest->value.d3d12.commands != signalled) {
            const uintptr_t command_identity = (uintptr_t)oldest->value.d3d12.commands;
            doe_dx_wait_lease lease = {0};
            doe_dx_status held = doe_dx_commands_hold(oldest->value.d3d12.commands, &lease, NULL);
            if (held == DOE_DX_OK) {
                native_unlock(device);
                doe_dx_status waited = doe_dx_commands_block(&lease, 100, NULL);
                native_lock(device);
                signalled = waited == DOE_DX_PENDING ? 0 : command_identity;
                /* The ordinary worker device lease prevents final context
                 * destruction while this event obligation is transferred. */
                doe_dx_status released = doe_dx_commands_unhold(&lease, NULL);
                if (released != DOE_DX_OK && device->retired_failure.status == NATIVE_OK)
                    device->retired_failure = native_failure_d3d12(released, NULL);
                continue;
            }
        }
#endif
        if (!oldest) signalled = 0;
        if (device->pending) {
            struct timespec now, deadline;
            doe_host_result clock = doe_monotonic_now(&now);
            if (clock.status == DOE_HOST_OK) clock = doe_time_add(&now, wait_ns, &deadline);
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
