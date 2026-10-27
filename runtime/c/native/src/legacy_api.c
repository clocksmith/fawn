/* SPDX-License-Identifier: MIT
 * Additive retained public ABI adapters; ownership stays with canonical owners. */
#include "internal.h"
#include "doe_native_external_texture.h"
#include <stdlib.h>

WGPUInstance wgpuAdapterGetInstance(WGPUAdapter handle)
{
    native_adapter *adapter = (native_adapter *)handle;
    if (!adapter) return NULL;
    doe_instance_retain(adapter->instance);
    return (WGPUInstance)adapter->instance;
}

WGPUAdapter wgpuDeviceGetAdapter(WGPUDevice handle)
{
    native_device *device = (native_device *)handle;
    if (!device) return NULL;
    wgpuAdapterAddRef((WGPUAdapter)device->adapter);
    return (WGPUAdapter)device->adapter;
}

void wgpuDeviceSetUncapturedErrorCallback(WGPUDevice handle,
    DoeLegacyUncapturedErrorCallback callback, void *userdata1, void *userdata2)
{
    native_device *device = (native_device *)handle;
    if (!device) return;
    native_lock(device);
    device->legacy_uncaptured = callback;
    device->uncaptured = (WGPUUncapturedErrorCallbackInfo){.userdata1=userdata1,.userdata2=userdata2};
    native_unlock(device);
}

void wgpuDeviceTick(WGPUDevice handle)
{
    native_device *device = (native_device *)handle;
    if (!device) return;
    native_device_retain(device);
    native_lock(device);
    native_progress_pending(device);
    native_unlock(device);
    native_device_release(device);
}

void wgpuExternalTextureDestroy(WGPUExternalTexture texture) { doeNativeExternalTextureDestroy(texture); }
void wgpuExternalTextureExpire(WGPUExternalTexture texture) { doeNativeExternalTextureExpire(texture); }
void wgpuExternalTextureRefresh(WGPUExternalTexture texture) { doeNativeExternalTextureRefresh(texture); }

WGPUStatus wgpuAdapterGetFormatCapabilities(WGPUAdapter adapter, WGPUTextureFormat format,
    DoeLegacyFormatCapabilities *capabilities)
{
    (void)adapter;
    (void)format;
    (void)capabilities;
    return WGPUStatus_Error;
}

/* Only provider-owned arrays may be freed, as with pinned FreeMembers APIs.
 * Doe never produces a nonempty output for these unadvertised extensions. */
void wgpuAdapterPropertiesMemoryHeapsFreeMembers(DoeLegacyCapabilityArray properties) { free((void *)properties.members); }
void wgpuAdapterPropertiesSubgroupMatrixConfigsFreeMembers(DoeLegacyCapabilityArray properties) { free((void *)properties.members); }
void wgpuDawnDrmFormatCapabilitiesFreeMembers(DoeLegacyCapabilityArray properties) { free((void *)properties.members); }

void wgpuComputePassEncoderWriteTimestamp(WGPUComputePassEncoder handle, WGPUQuerySet set, uint32_t index)
{
    (void)set;
    (void)index;
    doe_resource_compute_pass *pass = (doe_resource_compute_pass *)handle;
    native_device *device = doe_resource_compute_pass_device(pass);
    if (!device) return;
    native_lock(device);
    bool ended = doe_resource_compute_pass_ended(pass);
    doe_resource_status status = doe_resource_compute_pass_invalidate(pass, DOE_RESOURCE_STATE);
    native_unlock(device);
    native_encoding_error(device, ended, status, "legacy timestamp requires an open parent encoder");
}

void wgpuRenderPassEncoderWriteTimestamp(WGPURenderPassEncoder handle, WGPUQuerySet set, uint32_t index)
{
    (void)set;
    (void)index;
    doe_resource_render_pass *pass = (doe_resource_render_pass *)handle;
    native_device *device = doe_resource_render_pass_device(pass);
    if (!device) return;
    native_lock(device);
    bool ended = doe_resource_render_pass_ended(pass);
    doe_resource_status status = doe_resource_render_pass_invalidate(pass, DOE_RESOURCE_STATE);
    native_unlock(device);
    native_encoding_error(device, ended, status, "legacy timestamp requires an open parent encoder");
}
