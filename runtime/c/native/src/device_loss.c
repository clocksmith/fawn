/* SPDX-License-Identifier: MIT
 * Device-loss obligation translated from native/support/doe_device_loss.zig.
 * Uses the accepted callback owner; this file never owns a future queue. */
#include "internal.h"
#include <limits.h>
#include <stdlib.h>

struct native_loss {
    atomic_uint references; /* Device/creator lease plus pending callback lease. */
    doe_mutex mutex;
    doe_callback_event *event;
    WGPUFuture future;
    WGPUDeviceLostCallbackInfo callback;
    native_device *device; /* Weak, cleared before last external reference drops. */
    WGPUDeviceLostReason reason;
    const char *message;
};
typedef struct loss_delivery { native_loss *loss; } loss_delivery;

static void lock_loss(native_loss *loss)
{
    if (doe_mutex_lock(&loss->mutex).status != DOE_HOST_OK) abort();
}
static void unlock_loss(native_loss *loss)
{
    if (doe_mutex_unlock(&loss->mutex).status != DOE_HOST_OK) abort();
}
void native_loss_release(native_loss *loss)
{
    if (!loss || !native_release_count(&loss->references)) return;
    if (doe_mutex_destroy(&loss->mutex).status != DOE_HOST_OK) abort();
    free(loss);
}
static void copy_loss(void *destination, const void *source)
{
    *(loss_delivery *)destination = *(const loss_delivery *)source;
}
static void deliver_loss(void *context)
{
    native_loss *loss = ((loss_delivery *)context)->loss;
    lock_loss(loss);
    WGPUDeviceLostCallbackInfo callback = loss->callback;
    native_device *device = loss->device;
    native_device_retain(device);
    WGPUDeviceLostReason reason = loss->reason;
    const char *message = loss->message;
    unlock_loss(loss);
    if (callback.callback) {
        WGPUDevice handle = (WGPUDevice)device;
        callback.callback(&handle, reason, native_string(message), callback.userdata1, callback.userdata2);
    }
    native_device_release(device);
    native_loss_release(loss);
}
static void store_event(doe_callback_event *event, void *userdata)
{
    ((native_loss *)userdata)->event = event;
}

native_loss *native_loss_create(doe_instance *instance, WGPUDeviceLostCallbackInfo callback)
{
    if (callback.nextInChain) return NULL;
    native_loss *loss = calloc(1, sizeof(*loss));
    if (!loss) return NULL;
    if (doe_mutex_init(&loss->mutex, false).status != DOE_HOST_OK) {
        if (doe_mutex_destroy(&loss->mutex).status != DOE_HOST_OK) abort();
        free(loss);
        return NULL;
    }
    atomic_init(&loss->references, 2);
    loss->callback = callback;
    doe_instance_queue_lease lease = doe_instance_borrow_queue(instance);
    doe_callback_owner owner = {lease.queue, lease.userdata, lease.retain, lease.release};
    loss_delivery delivery = {loss};
    doe_callback_request request = {
        .abi_version=DOE_CALLBACKS_ABI_VERSION, .mode=(uint32_t)callback.mode, .owner=&owner,
        .allocator={NULL,NULL,native_allocate,native_deallocate}, .source_context=&delivery,
        .context_size=sizeof(delivery), .context_alignment=_Alignof(loss_delivery),
        .copy=copy_loss, .deliver=deliver_loss
    };
    if (doe_callback_publish(&request, store_event, loss, &loss->future.id) != DOE_CALLBACK_OK) {
        native_loss_release(loss);
        native_loss_release(loss);
        return NULL;
    }
    return loss;
}
void native_loss_bind(native_loss *loss, native_device *device)
{
    lock_loss(loss);
    loss->device = device;
    unlock_loss(loss);
}
void native_loss_complete(native_loss *loss, WGPUDeviceLostReason reason, const char *message)
{
    if (!loss) return;
    lock_loss(loss);
    if (loss->reason) { unlock_loss(loss); return; }
    loss->reason = reason;
    loss->message = message;
    doe_callback_event *event = loss->event;
    loss->event = NULL;
    unlock_loss(loss);
    doe_callback_mark_ready(event);
}

void wgpuDeviceAddRef(WGPUDevice handle)
{
    native_device *device = (native_device *)handle;
    if (!device) return;
    lock_loss(device->loss);
    /* A callback may lend live storage after another thread dropped the last
     * external reference. Acquiring that borrowed object is legal; its logical
     * destruction and cleared weak loss identity remain irreversible. */
    unsigned previous = atomic_load_explicit(&device->external_references, memory_order_relaxed);
    if (previous == UINT_MAX) abort();
    native_device_retain(device);
    atomic_store_explicit(&device->external_references, previous + 1, memory_order_relaxed);
    unlock_loss(device->loss);
}
void wgpuDeviceRelease(WGPUDevice handle)
{
    native_device *device = (native_device *)handle;
    if (!device) return;
    lock_loss(device->loss);
    bool last_external = native_release_count(&device->external_references);
    if (last_external) device->loss->device = NULL;
    unlock_loss(device->loss);
    if (last_external) wgpuDeviceDestroy(handle);
    native_device_release(device);
}
WGPUFuture wgpuDeviceGetLostFuture(WGPUDevice handle)
{
    native_device *device = (native_device *)handle;
    return device ? device->loss->future : (WGPUFuture){0};
}
