/* SPDX-License-Identifier: MIT */
#include "internal.h"
#include "feature_formats.h"
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
/* Render attachment, blending and 4x multisampling (resolve uses the same). */
static bool renderable_4x(const doe_vk_format_caps *caps)
{
    return caps->color_attachment && caps->blendable && (caps->attachment_sample_counts & 4);
}
static bool block_sampled(const doe_vk_format_caps *caps)
{
    return caps->sampled && caps->filterable && caps->copy_src && caps->copy_dst;
}
static bool block_volume(const doe_vk_format_caps *caps) { return block_sampled(caps) && caps->volume; }
static bool depth_stencil(const doe_vk_format_caps *caps)
{
    return caps->depth_stencil_attachment && caps->sampled && caps->copy_src && caps->copy_dst;
}

/* The one owner of optional WebGPU device features. A feature is reported only
 * when the selected Vulkan device really offers what WebGPU ties to it: native
 * format features for every named format, or the device feature the context
 * enabled at creation (see doe_vk_caps). Admission of each feature's formats and
 * descriptor fields checks the enabled set, never these adapter facts. */
bool native_vulkan_subgroups_supported(const doe_vk_caps *caps)
{
    /* WGSL's admitted compute/fragment operator set needs every corresponding
     * native class, including compute quads and half operations when f16 is offered. */
    return caps && caps->subgroup_size >= 4 && caps->subgroup_size <= 128 &&
        !(caps->subgroup_size & (caps->subgroup_size - 1)) &&
        (caps->subgroup_stages & 48u) == 48u && (caps->subgroup_operations & 191u) == 191u &&
        caps->subgroup_quad_all_stages && (!caps->shader_f16 || caps->subgroup_extended_types);
}
uint32_t native_adapter_features(const native_backend *backend)
{
    if (backend->kind == WGPUBackendType_D3D12) {
        const doe_dx_caps *caps = &backend->value.d3d12.caps;
        uint32_t features = caps->timestamp_frequency && caps->shader_model >= 0x60 ?
            NATIVE_FEATURE(WGPUFeatureName_TimestampQuery) : 0;
        if (caps->native_16bit_shader_ops && caps->shader_model >= 0x62)
            features |= NATIVE_FEATURE(WGPUFeatureName_ShaderF16);
        if (caps->wave_ops && caps->shader_model >= 0x60 && caps->wave_lane_count_min >= 4 &&
            caps->wave_lane_count_max <= 128 && caps->wave_lane_count_min <= caps->wave_lane_count_max)
            features |= NATIVE_FEATURE(WGPUFeatureName_Subgroups);
        if (caps->texture_compression_bc) features |= NATIVE_FEATURE(WGPUFeatureName_TextureCompressionBC);
        if (caps->depth32float_stencil8) features |= NATIVE_FEATURE(WGPUFeatureName_Depth32FloatStencil8);
        if (caps->feature_level >= 0xb000) features |= NATIVE_FEATURE(WGPUFeatureName_DepthClipControl) |
            NATIVE_FEATURE(WGPUFeatureName_ClipDistances) | NATIVE_FEATURE(WGPUFeatureName_PrimitiveIndex) |
            NATIVE_FEATURE(WGPUFeatureName_DualSourceBlending);
        /* Both indexed and nonindexed indirect records are GPU-normalized;
         * builtin consumers use the matching root-constant command signature. */
        if (caps->feature_level >= 0xb000 && caps->shader_model >= 0x60)
            features |= NATIVE_FEATURE(WGPUFeatureName_IndirectFirstInstance);
        if (caps->texture_compression_bc && caps->texture_compression_bc_sliced_3d)
            features |= NATIVE_FEATURE(WGPUFeatureName_TextureCompressionBCSliced3D);
        if (caps->float32_filterable) features |= NATIVE_FEATURE(WGPUFeatureName_Float32Filterable);
        if (caps->float32_blendable) features |= NATIVE_FEATURE(WGPUFeatureName_Float32Blendable);
        if (caps->rg11b10ufloat_renderable) features |= NATIVE_FEATURE(WGPUFeatureName_RG11B10UfloatRenderable);
        if (caps->feature_level >= 0xb000) features |= NATIVE_FEATURE(WGPUFeatureName_TextureComponentSwizzle);
        if (native_d3d12_core_supported(backend)) features |= NATIVE_FEATURE(WGPUFeatureName_CoreFeaturesAndLimits);
        return features | native_format_adapter_features(backend);
    }
    if (backend->kind == WGPUBackendType_Metal) {
        const doe_mtl_caps *caps = &backend->value.metal.caps;
        return native_format_adapter_features(backend) | NATIVE_FEATURE(WGPUFeatureName_CoreFeaturesAndLimits) |
            NATIVE_FEATURE(WGPUFeatureName_ShaderF16) |
            NATIVE_FEATURE(WGPUFeatureName_ClipDistances) |
            (caps->primitive_index ? NATIVE_FEATURE(WGPUFeatureName_PrimitiveIndex) : 0) |
            (caps->dual_source_blending ? NATIVE_FEATURE(WGPUFeatureName_DualSourceBlending) : 0) |
            (caps->subgroups ? NATIVE_FEATURE(WGPUFeatureName_Subgroups) : 0) |
            (caps->texture_swizzle ? NATIVE_FEATURE(WGPUFeatureName_TextureComponentSwizzle) : 0) |
            NATIVE_FEATURE(WGPUFeatureName_DepthClipControl) |
            NATIVE_FEATURE(WGPUFeatureName_IndirectFirstInstance) |
            NATIVE_FEATURE(WGPUFeatureName_Depth32FloatStencil8) |
            (caps->bc_texture_compression ? NATIVE_FEATURE(WGPUFeatureName_TextureCompressionBC) |
                NATIVE_FEATURE(WGPUFeatureName_TextureCompressionBCSliced3D) : 0) |
            (caps->etc2_texture_compression ? NATIVE_FEATURE(WGPUFeatureName_TextureCompressionETC2) : 0) |
            (caps->astc_texture_compression ? NATIVE_FEATURE(WGPUFeatureName_TextureCompressionASTC) |
                (caps->astc_sliced_3d ? NATIVE_FEATURE(WGPUFeatureName_TextureCompressionASTCSliced3D) : 0) : 0) |
            (caps->float32_filterable ? NATIVE_FEATURE(WGPUFeatureName_Float32Filterable) : 0) |
            (caps->float32_blendable ? NATIVE_FEATURE(WGPUFeatureName_Float32Blendable) : 0) |
            (caps->rg11b10_renderable && caps->store_and_resolve ? NATIVE_FEATURE(WGPUFeatureName_RG11B10UfloatRenderable) : 0) |
            (native_metal_timestamp_supported(&backend->value.metal.caps) ?
                NATIVE_FEATURE(WGPUFeatureName_TimestampQuery) : 0);
    }
    /* No compatibility mode: every adapter that can create a device is core. */
    if (backend->kind != WGPUBackendType_Vulkan) return NATIVE_FEATURE(WGPUFeatureName_CoreFeaturesAndLimits);
    const doe_vk_caps *caps = &backend->value.vulkan.caps;
    doe_vk_context *context = backend->value.vulkan.context;
    static const doe_vk_texture_format float32[] = {DOE_VK_FORMAT_R32_FLOAT, DOE_VK_FORMAT_RG32_FLOAT,
        DOE_VK_FORMAT_RGBA32_FLOAT};
    static const doe_vk_texture_format depth32_stencil8[] = {DOE_VK_FORMAT_D32_FLOAT_S8_UINT};
    static const doe_vk_texture_format rg11b10[] = {DOE_VK_FORMAT_RG11B10_UFLOAT};
    static const doe_vk_texture_format bc[] = {DOE_VK_FORMAT_BC1_RGBA_UNORM, DOE_VK_FORMAT_BC1_RGBA_SRGB,
        DOE_VK_FORMAT_BC2_RGBA_UNORM, DOE_VK_FORMAT_BC2_RGBA_SRGB, DOE_VK_FORMAT_BC3_RGBA_UNORM,
        DOE_VK_FORMAT_BC3_RGBA_SRGB, DOE_VK_FORMAT_BC4_R_UNORM, DOE_VK_FORMAT_BC4_R_SNORM,
        DOE_VK_FORMAT_BC5_RG_UNORM, DOE_VK_FORMAT_BC5_RG_SNORM, DOE_VK_FORMAT_BC6H_RGB_UFLOAT,
        DOE_VK_FORMAT_BC6H_RGB_FLOAT, DOE_VK_FORMAT_BC7_RGBA_UNORM, DOE_VK_FORMAT_BC7_RGBA_SRGB};
    static const doe_vk_texture_format etc2[] = {
        DOE_VK_FORMAT_ETC2_RGB8_UNORM,
        DOE_VK_FORMAT_ETC2_RGB8_SRGB,
        DOE_VK_FORMAT_ETC2_RGB8A1_UNORM,
        DOE_VK_FORMAT_ETC2_RGB8A1_SRGB,
        DOE_VK_FORMAT_ETC2_RGBA8_UNORM,
        DOE_VK_FORMAT_ETC2_RGBA8_SRGB,
        DOE_VK_FORMAT_EAC_R11_UNORM,
        DOE_VK_FORMAT_EAC_R11_SNORM,
        DOE_VK_FORMAT_EAC_RG11_UNORM,
        DOE_VK_FORMAT_EAC_RG11_SNORM};
    static const doe_vk_texture_format astc[] = {
        DOE_VK_FORMAT_ASTC_4x4_UNORM,
        DOE_VK_FORMAT_ASTC_4x4_SRGB,
        DOE_VK_FORMAT_ASTC_5x4_UNORM,
        DOE_VK_FORMAT_ASTC_5x4_SRGB,
        DOE_VK_FORMAT_ASTC_5x5_UNORM,
        DOE_VK_FORMAT_ASTC_5x5_SRGB,
        DOE_VK_FORMAT_ASTC_6x5_UNORM,
        DOE_VK_FORMAT_ASTC_6x5_SRGB,
        DOE_VK_FORMAT_ASTC_6x6_UNORM,
        DOE_VK_FORMAT_ASTC_6x6_SRGB,
        DOE_VK_FORMAT_ASTC_8x5_UNORM,
        DOE_VK_FORMAT_ASTC_8x5_SRGB,
        DOE_VK_FORMAT_ASTC_8x6_UNORM,
        DOE_VK_FORMAT_ASTC_8x6_SRGB,
        DOE_VK_FORMAT_ASTC_8x8_UNORM,
        DOE_VK_FORMAT_ASTC_8x8_SRGB,
        DOE_VK_FORMAT_ASTC_10x5_UNORM,
        DOE_VK_FORMAT_ASTC_10x5_SRGB,
        DOE_VK_FORMAT_ASTC_10x6_UNORM,
        DOE_VK_FORMAT_ASTC_10x6_SRGB,
        DOE_VK_FORMAT_ASTC_10x8_UNORM,
        DOE_VK_FORMAT_ASTC_10x8_SRGB,
        DOE_VK_FORMAT_ASTC_10x10_UNORM,
        DOE_VK_FORMAT_ASTC_10x10_SRGB,
        DOE_VK_FORMAT_ASTC_12x10_UNORM,
        DOE_VK_FORMAT_ASTC_12x10_SRGB,
        DOE_VK_FORMAT_ASTC_12x12_UNORM,
        DOE_VK_FORMAT_ASTC_12x12_SRGB};
    uint32_t features = NATIVE_FEATURE(WGPUFeatureName_CoreFeaturesAndLimits);
    if (native_vulkan_subgroups_supported(caps)) features |= NATIVE_FEATURE(WGPUFeatureName_Subgroups);
    if (caps->shader_clip_distance) features |= NATIVE_FEATURE(WGPUFeatureName_ClipDistances);
    if (caps->geometry_shader) features |= NATIVE_FEATURE(WGPUFeatureName_PrimitiveIndex);
    if (caps->dual_source_blend) features |= NATIVE_FEATURE(WGPUFeatureName_DualSourceBlending);
    if (caps->shader_f16) features |= NATIVE_FEATURE(WGPUFeatureName_ShaderF16);
    if (caps->texture_compression_etc2 && formats_offer(context, etc2, sizeof(etc2) / sizeof(etc2[0]), block_sampled))
        features |= NATIVE_FEATURE(WGPUFeatureName_TextureCompressionETC2);
    if (caps->texture_compression_astc && formats_offer(context, astc, sizeof(astc) / sizeof(astc[0]), block_sampled)) {
        features |= NATIVE_FEATURE(WGPUFeatureName_TextureCompressionASTC);
        if (formats_offer(context, astc, sizeof(astc) / sizeof(astc[0]), block_volume))
            features |= NATIVE_FEATURE(WGPUFeatureName_TextureCompressionASTCSliced3D);
    }
    if (native_timestamp_supported(caps)) features |= NATIVE_FEATURE(WGPUFeatureName_TimestampQuery);
    if (formats_offer(context, float32, 3, filterable)) features |= NATIVE_FEATURE(WGPUFeatureName_Float32Filterable);
    if (formats_offer(context, float32, 3, blendable)) features |= NATIVE_FEATURE(WGPUFeatureName_Float32Blendable);
    if (formats_offer(context, depth32_stencil8, 1, depth_stencil))
        features |= NATIVE_FEATURE(WGPUFeatureName_Depth32FloatStencil8);
    if (formats_offer(context, rg11b10, 1, renderable_4x))
        features |= NATIVE_FEATURE(WGPUFeatureName_RG11B10UfloatRenderable);
    if (caps->draw_indirect_first_instance) features |= NATIVE_FEATURE(WGPUFeatureName_IndirectFirstInstance);
    if (caps->depth_clamp) features |= NATIVE_FEATURE(WGPUFeatureName_DepthClipControl);
    /* VkComponentMapping on sampled image views is core Vulkan. */
    features |= NATIVE_FEATURE(WGPUFeatureName_TextureComponentSwizzle);
    /* Enabled textureCompressionBC plus sampled, filterable, copyable formats;
     * sliced 3D additionally needs 3D images of every BC format. */
    if (caps->texture_compression_bc && formats_offer(context, bc, 14, block_sampled)) {
        features |= NATIVE_FEATURE(WGPUFeatureName_TextureCompressionBC);
        if (formats_offer(context, bc, 14, block_volume))
            features |= NATIVE_FEATURE(WGPUFeatureName_TextureCompressionBCSliced3D);
    }
    return features | native_format_adapter_features(backend);
}
bool native_requested_features(const native_backend *backend,
    const WGPUDeviceDescriptor *descriptor, uint32_t *enabled)
{
    /* Core is implicit only on an adapter that actually offers the complete
     * core envelope. Compatibility adapters must not gain it on default creation. */
    const uint32_t offered = native_adapter_features(backend);
    uint32_t requested = offered & NATIVE_FEATURE(WGPUFeatureName_CoreFeaturesAndLimits);
    uint32_t explicit_requested = 0;
    if (descriptor && descriptor->requiredFeatureCount) {
        if (!descriptor->requiredFeatures) return false;
        for (size_t i = 0; i < descriptor->requiredFeatureCount; ++i) {
            uint32_t name = (uint32_t)descriptor->requiredFeatures[i];
            if (name >= 32 || !(offered & NATIVE_FEATURE(name)) ||
                (explicit_requested & NATIVE_FEATURE(name))) return false;
            explicit_requested |= NATIVE_FEATURE(name);
            requested |= NATIVE_FEATURE(name);
        }
    }
    requested = native_feature_dependencies(requested);
    if (requested & ~offered) return false;
    *enabled = requested;
    return true;
}

bool native_feature_enabled(const native_device *device, WGPUFeatureName feature)
{
    return device && (uint32_t)feature < 32 && (device->features & NATIVE_FEATURE(feature));
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
    return adapter && (uint32_t)feature < 32 && (native_adapter_features(&adapter->backend) & NATIVE_FEATURE(feature));
}
WGPUBool wgpuDeviceHasFeature(WGPUDevice handle, WGPUFeatureName feature)
{
    return native_feature_enabled((native_device *)handle, feature);
}
void wgpuSupportedFeaturesFreeMembers(WGPUSupportedFeatures features) { free((void *)features.features); }

/* Capability admission and reporting share the instance owner's contract. */
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
    case WGPUWGSLLanguageFeatureName_UniformBufferStandardLayout:
        return (native_shader_semantic_flags() & DOE_COMPILER_UNIFORM_BUFFER_STANDARD_LAYOUT) != 0;
    case WGPUWGSLLanguageFeatureName_Packed4x8IntegerDotProduct:
        return true; /* Shared typed integer lowering; no native packed intrinsic required. */
    case WGPUWGSLLanguageFeatureName_UnrestrictedPointerParameters:
    case WGPUWGSLLanguageFeatureName_PointerCompositeAccess:
    case WGPUWGSLLanguageFeatureName_TextureAndSamplerLet:
    case WGPUWGSLLanguageFeatureName_ImmediateAddressSpace:
        return true; /* Canonical references/aliases and all three native immediate transports. */
    case WGPUWGSLLanguageFeatureName_SubgroupId: /* Global language exposure pending complete target composition. */
    case WGPUWGSLLanguageFeatureName_SubgroupUniformity: /* Uniformity language contract not yet admitted. */
    case WGPUWGSLLanguageFeatureName_TextureFormatsTier1: /* Extended storage formats rejected. */
    case WGPUWGSLLanguageFeatureName_LinearIndexing: /* Linear index builtins rejected. */
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
