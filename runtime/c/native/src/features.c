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
static bool formats_offer(doe_vk_context *context, const doe_vk_texture_format *formats, size_t count,
    bool (*offered)(const doe_vk_format_caps *))
{
    for (size_t i = 0; i < count; ++i) {
        doe_vk_format_caps caps;
        if (doe_vk_texture_format_caps(context, formats[i], &caps) != DOE_VK_OK || !offered(&caps)) return false;
    }
    return true;
}
static bool filterable(const doe_vk_format_caps *caps) { return caps->sampled && caps->filterable; }
static bool blendable(const doe_vk_format_caps *caps) { return caps->color_attachment && caps->blendable; }
static bool depth_stencil(const doe_vk_format_caps *caps)
{
    return caps->depth_stencil_attachment && caps->sampled && caps->copy_src && caps->copy_dst;
}

/* The one owner of optional WebGPU device features. A feature is reported only
 * when the selected Vulkan device really offers what WebGPU ties to it: native
 * format features for every named format, or the device feature the context
 * enabled at creation (see doe_vk_caps). Admission of each feature's formats and
 * descriptor fields checks the enabled set, never these adapter facts. */
uint32_t native_adapter_features(const native_backend *backend)
{
    if (backend->kind != WGPUBackendType_Vulkan) return 0;
    const doe_vk_caps *caps = &backend->value.vulkan.caps;
    doe_vk_context *context = backend->value.vulkan.context;
    static const doe_vk_texture_format float32[] = {DOE_VK_FORMAT_R32_FLOAT, DOE_VK_FORMAT_RG32_FLOAT,
        DOE_VK_FORMAT_RGBA32_FLOAT};
    static const doe_vk_texture_format depth32_stencil8[] = {DOE_VK_FORMAT_D32_FLOAT_S8_UINT};
    uint32_t features = 0;
    if (native_timestamp_supported(caps)) features |= NATIVE_FEATURE(WGPUFeatureName_TimestampQuery);
    if (formats_offer(context, float32, 3, filterable)) features |= NATIVE_FEATURE(WGPUFeatureName_Float32Filterable);
    if (formats_offer(context, float32, 3, blendable)) features |= NATIVE_FEATURE(WGPUFeatureName_Float32Blendable);
    if (formats_offer(context, depth32_stencil8, 1, depth_stencil))
        features |= NATIVE_FEATURE(WGPUFeatureName_Depth32FloatStencil8);
    if (caps->draw_indirect_first_instance) features |= NATIVE_FEATURE(WGPUFeatureName_IndirectFirstInstance);
    if (caps->depth_clamp) features |= NATIVE_FEATURE(WGPUFeatureName_DepthClipControl);
    return features;
}
bool native_requested_features(const native_backend *backend,
    const WGPUDeviceDescriptor *descriptor, uint32_t *enabled)
{
    *enabled = 0;
    if (!descriptor || !descriptor->requiredFeatureCount) return true;
    if (!descriptor->requiredFeatures) return false;
    uint32_t offered = native_adapter_features(backend), requested = 0;
    for (size_t i = 0; i < descriptor->requiredFeatureCount; ++i) {
        WGPUFeatureName name = descriptor->requiredFeatures[i];
        if (name >= 32 || !(offered & NATIVE_FEATURE(name))) return false;
        requested |= NATIVE_FEATURE(name);
    }
    *enabled = requested;
    return true;
}
bool native_feature_enabled(const native_device *device, WGPUFeatureName feature)
{
    return device && feature < 32 && (device->features & NATIVE_FEATURE(feature));
}
static bool feature_list(uint32_t mask, WGPUSupportedFeatures *features)
{
    if (!features) return true;
    *features = (WGPUSupportedFeatures){0};
    size_t count = 0;
    for (uint32_t name = 0; name < 32; ++name) count += (mask >> name) & 1;
    if (!count) return true;
    WGPUFeatureName *names = malloc(count * sizeof(*names));
    if (!names) return false;
    size_t written = 0;
    for (uint32_t name = 0; name < 32; ++name) if (mask & NATIVE_FEATURE(name)) names[written++] = (WGPUFeatureName)name;
    features->features = names;
    features->featureCount = written;
    return true;
}
void wgpuAdapterGetFeatures(WGPUAdapter handle, WGPUSupportedFeatures *features)
{
    native_adapter *adapter = (native_adapter *)handle;
    /* This void adapter API has no error channel; allocation failure leaves an
     * empty owned output, never an invalid pointer or invented capability. */
    (void)feature_list(adapter ? native_adapter_features(&adapter->backend) : 0, features);
}
void wgpuDeviceGetFeatures(WGPUDevice handle, WGPUSupportedFeatures *features)
{
    native_device *device = (native_device *)handle;
    if (!feature_list(device ? device->features : 0, features))
        native_error(device, WGPUErrorType_OutOfMemory, "feature list allocation failed");
}
WGPUBool wgpuAdapterHasFeature(WGPUAdapter handle, WGPUFeatureName feature)
{
    native_adapter *adapter = (native_adapter *)handle;
    return adapter && feature < 32 && (native_adapter_features(&adapter->backend) & NATIVE_FEATURE(feature));
}
WGPUBool wgpuDeviceHasFeature(WGPUDevice handle, WGPUFeatureName feature)
{
    return native_feature_enabled((native_device *)handle, feature);
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
