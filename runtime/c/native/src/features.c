/* SPDX-License-Identifier: MIT */
#include "internal.h"
#include <stdlib.h>

uint32_t native_timestamp_workgroup(const doe_vk_caps *caps)
{
    for (uint32_t size = 64; size; size /= 2)
        if (doe_vk_timestamp_normalizer_validate(caps, DOE_RESOURCE_MAX_QUERIES, size) == DOE_VK_OK) return size;
    return 0;
}
bool native_timestamp_supported(const doe_vk_caps *caps)
{
    return native_timestamp_workgroup(caps) != 0;
}
bool native_requested_features(const doe_vk_caps *caps,
    const WGPUDeviceDescriptor *descriptor, bool *timestamps)
{
    *timestamps = false;
    if (!descriptor || !descriptor->requiredFeatureCount) return true;
    if (descriptor->requiredFeatureCount != 1 || !descriptor->requiredFeatures ||
        descriptor->requiredFeatures[0] != WGPUFeatureName_TimestampQuery || !native_timestamp_supported(caps))
        return false;
    *timestamps = true;
    return true;
}
static bool feature_list(bool timestamps, WGPUSupportedFeatures *features)
{
    if (!features) return true;
    *features = (WGPUSupportedFeatures){0};
    if (!timestamps) return true;
    WGPUFeatureName *names = malloc(sizeof(*names));
    if (!names) return false;
    names[0] = WGPUFeatureName_TimestampQuery;
    features->features = names;
    features->featureCount = 1;
    return true;
}
void wgpuAdapterGetFeatures(WGPUAdapter handle, WGPUSupportedFeatures *features)
{
    native_adapter *adapter = (native_adapter *)handle;
    /* This void adapter API has no error channel; allocation failure leaves an
     * empty owned output, never an invalid pointer or invented capability. */
    (void)feature_list(adapter && native_timestamp_supported(&adapter->caps), features);
}
void wgpuDeviceGetFeatures(WGPUDevice handle, WGPUSupportedFeatures *features)
{
    native_device *device = (native_device *)handle;
    if (!feature_list(device && device->timestamp_enabled, features))
        native_error(device, WGPUErrorType_OutOfMemory, "feature list allocation failed");
}
WGPUBool wgpuAdapterHasFeature(WGPUAdapter handle, WGPUFeatureName feature)
{
    native_adapter *adapter = (native_adapter *)handle;
    return adapter && feature == WGPUFeatureName_TimestampQuery && native_timestamp_supported(&adapter->caps);
}
WGPUBool wgpuDeviceHasFeature(WGPUDevice handle, WGPUFeatureName feature)
{
    native_device *device = (native_device *)handle;
    return device && feature == WGPUFeatureName_TimestampQuery && device->timestamp_enabled;
}
void wgpuSupportedFeaturesFreeMembers(WGPUSupportedFeatures features) { free((void *)features.features); }

/* These are the accepted instance owner's conservative capability results,
 * not claims that every operation exercised experimentally is advertised. */
void wgpuGetInstanceFeatures(WGPUSupportedInstanceFeatures *features)
{
    if (features) *features = doe_instance_features();
}
WGPUStatus wgpuGetInstanceLimits(WGPUInstanceLimits *limits)
{
    if (!limits || limits->nextInChain) return WGPUStatus_Error;
    *limits = doe_instance_limits();
    return WGPUStatus_Success;
}
WGPUBool wgpuHasInstanceFeature(WGPUInstanceFeatureName feature)
{
    return doe_instance_has_feature(feature);
}
void wgpuSupportedInstanceFeaturesFreeMembers(WGPUSupportedInstanceFeatures features)
{
    doe_instance_features_free_members(features);
}
