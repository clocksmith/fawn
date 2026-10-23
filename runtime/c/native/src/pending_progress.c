/* SPDX-License-Identifier: MIT */
#include "internal.h"
#include "provider_pin.h"
#include <errno.h>
#include <stdlib.h>
#include <time.h>

/* The device mutex owns the pending list. This thread neither copies that list
 * nor creates resource references: it owns one existing device lease, allowing
 * the recorded submissions to retire even after every public handle is gone. */
static void *pending_progress(void *identity)
{
    native_device *device = identity;
    native_lock(device);
    for (;;) {
        while (!device->pending && !device->pending_progress_join &&
                atomic_load_explicit(&device->references, memory_order_acquire) > 1) {
            if (pthread_cond_wait(&device->pending_changed, &device->mutex)) abort();
        }
        if (!device->pending) {
            device->pending_progress_ready = false;
            bool join = device->pending_progress_join;
            native_unlock(device);
            if (!join && pthread_detach(pthread_self())) abort();
            native_device_release(device);
            return NULL;
        }
        native_progress_pending(device);
        if (device->pending) {
            struct timespec deadline;
            if (timespec_get(&deadline, TIME_UTC) != TIME_UTC) {
                /* A host-clock failure cannot discard live work. Retry without
                 * holding the device mutex or spinning on the failed clock. */
                native_unlock(device);
                struct timespec delay = {.tv_sec = 0, .tv_nsec = 10000000};
                while (nanosleep(&delay, &delay) && errno == EINTR) {}
                native_lock(device);
                continue;
            }
            deadline.tv_nsec += 10000000;
            if (deadline.tv_nsec >= 1000000000) {
                deadline.tv_nsec -= 1000000000;
                ++deadline.tv_sec;
            }
            int status = pthread_cond_timedwait(&device->pending_changed,
                &device->mutex, &deadline);
            if (status && status != ETIMEDOUT) abort();
        }
    }
}

bool native_pending_progress_start(native_device *device)
{
    if (!native_provider_pin()) return false;
    if (pthread_cond_init(&device->pending_changed, NULL)) return false;
    pthread_attr_t attributes;
    int status = pthread_attr_init(&attributes);
    if (status) {
        if (pthread_cond_destroy(&device->pending_changed)) abort();
        return false;
    }
    native_device_retain(device);
    device->pending_progress_ready = true;
    status = pthread_create(&device->pending_thread, &attributes, pending_progress, device);
    if (status) {
        device->pending_progress_ready = false;
        if (native_release_count(&device->references)) abort();
    }
    if (pthread_attr_destroy(&attributes)) abort();
    if (status && pthread_cond_destroy(&device->pending_changed)) abort();
    return status == 0;
}

void native_pending_progress_wake(native_device *device)
{
    /* Caller holds device mutex and a live lease. A worker-less device is never
     * published, so no native submission can create an unserviceable obligation. */
    if (device->pending_progress_ready && pthread_cond_signal(&device->pending_changed)) abort();
}

void native_pending_progress_destroy(native_device *device)
{
    if (device->pending_progress_ready) abort();
    if (pthread_cond_destroy(&device->pending_changed)) abort();
}
