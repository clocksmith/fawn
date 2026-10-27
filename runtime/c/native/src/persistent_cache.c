/* SPDX-License-Identifier: MIT */
#include "internal.h"
#include "doe_native_device_policy.h"

WGPUStatus doeNativeDeviceDisablePersistentCache(WGPUDevice handle)
{
    native_device *device = (native_device *)handle;
    if (!device) return WGPUStatus_Error;
    native_device_retain(device);
    native_lock(device);
    const bool disabled = native_device_alive(device) &&
        native_persistent_cache_disable(&device->persistent_cache);
    native_unlock(device);
    native_device_release(device);
    return disabled ? WGPUStatus_Success : WGPUStatus_Error;
}
