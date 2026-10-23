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
bool native_requested_features(const native_backend *backend,
    const WGPUDeviceDescriptor *descriptor, bool *timestamps)
{
    *timestamps = false;
    if (!descriptor || !descriptor->requiredFeatureCount) return true;
    if (descriptor->requiredFeatureCount != 1 || !descriptor->requiredFeatures ||
        descriptor->requiredFeatures[0] != WGPUFeatureName_TimestampQuery || (backend->kind != WGPUBackendType_Vulkan || !native_timestamp_supported(&backend->value.vulkan.caps)))
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
    (void)feature_list(adapter && adapter->backend.kind == WGPUBackendType_Vulkan && native_timestamp_supported(&adapter->backend.value.vulkan.caps), features);
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
    return adapter && feature == WGPUFeatureName_TimestampQuery && adapter->backend.kind == WGPUBackendType_Vulkan && native_timestamp_supported(&adapter->backend.value.vulkan.caps);
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

/* The one owner of reported WGSL language features. A feature is reported only
 * when tests/wgsl_language_features.c proves its probe compiles and creates a
 * real Vulkan pipeline through the public path both with and without
 * `requires <name>;`. The comments record the observed public-path rejection;
 * the test fails if any unreported probe becomes fully accepted. */
static bool wgsl_feature_reported(WGPUWGSLLanguageFeatureName feature)
{
    switch (feature) {
    case WGPUWGSLLanguageFeatureName_ReadonlyAndReadwriteStorageTextures:
        return false; /* Usage compiles and pipelines; `requires` is rejected. */
    case WGPUWGSLLanguageFeatureName_Packed4x8IntegerDotProduct: /* Builtins unknown. */
    case WGPUWGSLLanguageFeatureName_UnrestrictedPointerParameters: /* Function pointers only. */
    case WGPUWGSLLanguageFeatureName_PointerCompositeAccess: /* No member access through pointers. */
    case WGPUWGSLLanguageFeatureName_UniformBufferStandardLayout: /* Conservative uniform layout. */
    case WGPUWGSLLanguageFeatureName_SubgroupId: /* No device admits subgroups. */
    case WGPUWGSLLanguageFeatureName_TextureAndSamplerLet: /* Handle-typed lets rejected. */
    case WGPUWGSLLanguageFeatureName_SubgroupUniformity: /* No device admits subgroups. */
    case WGPUWGSLLanguageFeatureName_TextureFormatsTier1: /* Extended storage formats rejected. */
    case WGPUWGSLLanguageFeatureName_LinearIndexing: /* Linear index builtins rejected. */
    case WGPUWGSLLanguageFeatureName_ImmediateAddressSpace: /* Immediates rejected. */
    case WGPUWGSLLanguageFeatureName_BufferView: /* Untyped buffers rejected. */
        return false;
    case WGPUWGSLLanguageFeatureName_Force32:
        break;
    }
    return false;
}

void wgpuInstanceGetWGSLLanguageFeatures(WGPUInstance instance, WGPUSupportedWGSLLanguageFeatures *features)
{
    if (!features) return;
    *features = (WGPUSupportedWGSLLanguageFeatures){0};
    if (!instance) return;
    size_t count = 0;
    for (uint32_t name = WGPUWGSLLanguageFeatureName_ReadonlyAndReadwriteStorageTextures;
            name <= WGPUWGSLLanguageFeatureName_BufferView; ++name)
        count += wgsl_feature_reported((WGPUWGSLLanguageFeatureName)name);
    if (!count) return; /* Empty list owns no storage; FreeMembers accepts NULL. */
    WGPUWGSLLanguageFeatureName *names = malloc(count * sizeof(*names));
    /* This void API has no error channel: allocation failure leaves an empty
     * owned output, never an invalid pointer or a partial list. */
    if (!names) return;
    size_t written = 0;
    for (uint32_t name = WGPUWGSLLanguageFeatureName_ReadonlyAndReadwriteStorageTextures;
            name <= WGPUWGSLLanguageFeatureName_BufferView; ++name)
        if (wgsl_feature_reported((WGPUWGSLLanguageFeatureName)name)) names[written++] = (WGPUWGSLLanguageFeatureName)name;
    *features = (WGPUSupportedWGSLLanguageFeatures){.featureCount=written,.features=names};
}
WGPUBool wgpuInstanceHasWGSLLanguageFeature(WGPUInstance instance, WGPUWGSLLanguageFeatureName feature)
{
    return instance && wgsl_feature_reported(feature);
}
void wgpuSupportedWGSLLanguageFeaturesFreeMembers(WGPUSupportedWGSLLanguageFeatures features)
{
    free((void *)features.features);
}
