/* SPDX-License-Identifier: MIT */
#include "feature_formats.h"
#include "d3d12_frontend.h"
#include "d3d12_texture_desc.h"

/* One canonical resource predicate determines which optional formats add a
 * requirement. No independently maintained feature-format inventory can drift
 * from texture/layout/render admission. The pinned ABI's format range is finite. */
static bool vulkan_tier(doe_vk_context *context,uint32_t features)
{
    for (uint32_t name=WGPUTextureFormat_R8Unorm;name<=WGPUTextureFormat_ASTC12x12UnormSrgb;++name) {
        WGPUTextureFormat format=(WGPUTextureFormat)name;
        bool baseline_rw=false, enabled_rw=false;
        const bool baseline=doe_resource_format_storage(format,0,&baseline_rw);
        const bool enabled=doe_resource_format_storage(format,features,&enabled_rw);
        const bool render=doe_resource_format_color_renderable(format,features) &&
            !doe_resource_format_color_renderable(format,0);
        const bool storage=enabled && (!baseline || (enabled_rw && !baseline_rw));
        if (!render && !storage) continue;
        doe_vk_texture_format native;
        doe_vk_format_caps caps;
        if (!native_texture_format(UINT32_MAX,format,&native) ||
            doe_vk_texture_format_caps(context,native,&caps)!=DOE_VK_OK) return false;
        if (storage && (!caps.storage || !caps.sampled || !caps.copy_src || !caps.copy_dst)) return false;
        if (render && (!caps.color_attachment || !caps.blendable || !caps.filterable ||
            !(caps.attachment_sample_counts & 4u))) return false;
    }
    return true;
}

uint32_t native_format_adapter_features(const native_backend *backend)
{
    bool bgra=false,tier1=false,tier2=false;
    if (backend->kind==WGPUBackendType_Metal) {
        const doe_mtl_caps *caps=&backend->value.metal.caps;
        bgra=caps->bgra8unorm_storage;
        tier1=caps->texture_formats_tier1 && caps->rg11b10_renderable && caps->store_and_resolve;
        tier2=tier1 && caps->texture_formats_tier2;
    } else if (backend->kind==WGPUBackendType_D3D12) {
        const doe_dx_caps *caps=&backend->value.d3d12.caps;
        bgra=caps->bgra8unorm_storage;
        tier1=caps->texture_formats_tier1 && caps->rg11b10ufloat_renderable;
        tier2=tier1 && caps->texture_formats_tier2;
    } else if (backend->kind==WGPUBackendType_Vulkan) {
        doe_vk_context *context=backend->value.vulkan.context;
        doe_vk_format_caps caps;
        bgra=doe_vk_texture_format_caps(context,DOE_VK_FORMAT_BGRA8_UNORM,&caps)==DOE_VK_OK &&
            caps.storage && caps.sampled && caps.copy_src && caps.copy_dst;
        const bool rg11=doe_vk_texture_format_caps(context,DOE_VK_FORMAT_RG11B10_UFLOAT,&caps)==DOE_VK_OK &&
            caps.color_attachment && caps.blendable && (caps.attachment_sample_counts & 4u);
        tier1=rg11 && backend->value.vulkan.caps.storage_image_extended_formats &&
            vulkan_tier(context,DOE_RESOURCE_FORMAT_TIER1);
        tier2=tier1 && vulkan_tier(context,DOE_RESOURCE_FORMAT_TIER1|DOE_RESOURCE_FORMAT_TIER2);
    }
    return (bgra ? NATIVE_FEATURE(WGPUFeatureName_BGRA8UnormStorage) : 0) |
        (tier1 ? NATIVE_FEATURE(WGPUFeatureName_TextureFormatsTier1) : 0) |
        (tier2 ? NATIVE_FEATURE(WGPUFeatureName_TextureFormatsTier2) : 0);
}

uint32_t native_feature_dependencies(uint32_t features)
{
    /* https://gpuweb.github.io/gpuweb/#feature-dependencies */
    if (features & NATIVE_FEATURE(WGPUFeatureName_TextureFormatsTier2))
        features |= NATIVE_FEATURE(WGPUFeatureName_TextureFormatsTier1);
    if (features & NATIVE_FEATURE(WGPUFeatureName_TextureFormatsTier1))
        features |= NATIVE_FEATURE(WGPUFeatureName_RG11B10UfloatRenderable);
    if (features & NATIVE_FEATURE(WGPUFeatureName_TextureCompressionBCSliced3D))
        features |= NATIVE_FEATURE(WGPUFeatureName_TextureCompressionBC);
    if (features & NATIVE_FEATURE(WGPUFeatureName_TextureCompressionASTCSliced3D))
        features |= NATIVE_FEATURE(WGPUFeatureName_TextureCompressionASTC);
    return features;
}

/* Core is an intersection of frontend limits and queried format requirements,
 * not an alias for a D3D feature level or successful shader compilation. */
bool native_d3d12_core_supported(const native_backend *backend)
{
    if (!backend || backend->kind!=WGPUBackendType_D3D12) return false;
    const doe_dx_caps *caps=&backend->value.d3d12.caps;
    WGPULimits limits={0};
    if (caps->shader_model<0x60 || caps->limits.max_descriptors<40 ||
        native_d3d12_limits(caps,&limits)!=WGPUStatus_Success ||
        !native_limits_support_core(&limits)) return false;
#if defined(_WIN32)
    if (!backend->value.d3d12.context) return false;
    for (uint32_t name=WGPUTextureFormat_R8Unorm;name<=WGPUTextureFormat_ASTC12x12UnormSrgb;++name) {
        WGPUTextureFormat format=(WGPUTextureFormat)name;
        if (!native_format_feature_enabled(0,format)) continue;
        doe_resource_format_properties required;
        doe_dx_format mapped;
        doe_dx_format_caps actual;
        doe_dx_error error={0};
        if (doe_resource_format_properties_get(format,0,&required)!=DOE_RESOURCE_OK ||
            !native_d3d12_format(format,&mapped) ||
            doe_dx_context_format_caps(backend->value.d3d12.context,mapped,&actual,&error)!=DOE_DX_OK)
            return false;
        const bool depth_stencil=required.depth || required.stencil;
        if (!actual.texture_2d || !actual.texture_cube ||
            (!depth_stencil && (!actual.texture_1d || !actual.texture_3d)) ||
            (required.sampled && required.depth && !actual.shader_load) ||
            (required.sampled && required.stencil && !actual.stencil_load) ||
            (required.sampled && !depth_stencil && !actual.shader_load) ||
            (required.filterable && !(required.depth ? actual.comparison : actual.filterable)) ||
            (required.renderable && !(depth_stencil ? actual.depth_stencil : actual.render_target)) ||
            (required.blendable && !actual.blendable) ||
            (required.multisample && (!actual.sample4 || !actual.multisample_load)) ||
            (required.multisample && !depth_stencil && required.filterable && !actual.resolve) ||
            (required.storage && !actual.storage_store) ||
            (required.read_write && !actual.storage_load)) return false;
    }
    return true;
#else
    return false;
#endif
}
