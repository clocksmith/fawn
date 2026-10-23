#include "texture_internal.h"
#include <math.h>

static uint32_t texel_bytes(doe_vk_texture_format format) {
    switch (format) {
        case DOE_VK_FORMAT_R8_UNORM: return 1;
        case DOE_VK_FORMAT_RG8_UNORM:
        case DOE_VK_FORMAT_R16_FLOAT: return 2;
        case DOE_VK_FORMAT_RGBA8_UNORM:
        case DOE_VK_FORMAT_RGBA8_SRGB:
        case DOE_VK_FORMAT_BGRA8_UNORM:
        case DOE_VK_FORMAT_BGRA8_SRGB:
        case DOE_VK_FORMAT_RG16_FLOAT:
        case DOE_VK_FORMAT_R32_UINT:
        case DOE_VK_FORMAT_R32_SINT:
        case DOE_VK_FORMAT_R32_FLOAT: return 4;
        case DOE_VK_FORMAT_RGBA16_FLOAT:
        case DOE_VK_FORMAT_RG32_UINT:
        case DOE_VK_FORMAT_RG32_SINT:
        case DOE_VK_FORMAT_RG32_FLOAT: return 8;
        case DOE_VK_FORMAT_RGBA32_UINT:
        case DOE_VK_FORMAT_RGBA32_SINT:
        case DOE_VK_FORMAT_RGBA32_FLOAT: return 16;
    }
    return 0;
}

doe_vk_status doe_vk_texture_format_caps(doe_vk_context *context,
    doe_vk_texture_format format, doe_vk_format_caps *out) {
    if (context == NULL || out == NULL) return DOE_VK_INVALID_ARGUMENT;
    if (context->lost) return DOE_VK_DEVICE_LOST;
    uint32_t bytes = texel_bytes(format);
    if (bytes == 0) return DOE_VK_UNSUPPORTED;
    VkFormatProperties properties = {0};
    context->vk.vkGetPhysicalDeviceFormatProperties(context->physical, (uint32_t)format, &properties);
    uint32_t flags = properties.optimalTilingFeatures;
    const doe_vk_format_caps caps = {
        .texel_bytes = bytes,
        .sampled = (flags & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) != 0,
        .storage = (flags & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT) != 0,
        .color_attachment = (flags & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT) != 0,
        .blendable = (flags & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT) != 0,
        .filterable = (flags & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT) != 0,
        .copy_src = (flags & VK_FORMAT_FEATURE_TRANSFER_SRC_BIT) != 0,
        .copy_dst = (flags & VK_FORMAT_FEATURE_TRANSFER_DST_BIT) != 0
    };
    *out = caps;
    return DOE_VK_OK;
}

static doe_vk_status validate_texture(doe_vk_context *context,
    const doe_vk_texture_desc *desc, uint32_t *bytes) {
    if (desc == NULL || desc->dimension < DOE_VK_TEXTURE_1D || desc->dimension > DOE_VK_TEXTURE_3D ||
        desc->width == 0 || desc->height == 0 || desc->depth_or_layers == 0 ||
        desc->mip_levels == 0 || desc->usage == 0 || (desc->usage & ~UINT32_C(31)) != 0)
        return DOE_VK_INVALID_ARGUMENT;
    *bytes = texel_bytes(desc->format);
    if (*bytes == 0) return DOE_VK_UNSUPPORTED;
    const VkPhysicalDeviceLimits *limits = &context->properties.limits;
    uint32_t max_dimension = limits->maxImageDimension2D;
    uint32_t largest = desc->width > desc->height ? desc->width : desc->height;
    if (desc->dimension == DOE_VK_TEXTURE_1D) {
        if (desc->height != 1) return DOE_VK_INVALID_ARGUMENT;
        max_dimension = limits->maxImageDimension1D;
    } else if (desc->dimension == DOE_VK_TEXTURE_3D) {
        max_dimension = limits->maxImageDimension3D;
        if (desc->depth_or_layers > max_dimension) return DOE_VK_UNSUPPORTED;
        if (desc->depth_or_layers > largest) largest = desc->depth_or_layers;
    }
    if (desc->width > max_dimension || desc->height > max_dimension ||
        (desc->dimension != DOE_VK_TEXTURE_3D && desc->depth_or_layers > limits->maxImageArrayLayers))
        return DOE_VK_UNSUPPORTED;
    uint32_t mips = 0;
    for (uint32_t value = largest; value != 0; value >>= 1) ++mips;
    if (desc->mip_levels > mips) return DOE_VK_INVALID_ARGUMENT;
    if (desc->cube_compatible && (desc->dimension != DOE_VK_TEXTURE_2D ||
        desc->width != desc->height || desc->depth_or_layers < 6)) return DOE_VK_INVALID_ARGUMENT;
    if (desc->cube_compatible && desc->width > limits->maxImageDimensionCube) return DOE_VK_UNSUPPORTED;
    doe_vk_format_caps caps;
    doe_vk_status status = doe_vk_texture_format_caps(context, desc->format, &caps);
    if (status != DOE_VK_OK) return status;
    if (((desc->usage & DOE_VK_TEXTURE_SAMPLED) && !caps.sampled) ||
        ((desc->usage & DOE_VK_TEXTURE_STORAGE) && !caps.storage) ||
        ((desc->usage & DOE_VK_TEXTURE_COLOR_ATTACHMENT) && !caps.color_attachment) ||
        ((desc->usage & DOE_VK_TEXTURE_COPY_SRC) && !caps.copy_src) ||
        ((desc->usage & DOE_VK_TEXTURE_COPY_DST) && !caps.copy_dst)) return DOE_VK_UNSUPPORTED;
    return DOE_VK_OK;
}

doe_vk_status doe_vk_texture_create(doe_vk_context *context,
    const doe_vk_texture_desc *desc, doe_vk_texture **out, doe_vk_error *error) {
    if (context == NULL || out == NULL) return doe_vk_fail(DOE_VK_INVALID_ARGUMENT, "texture descriptor", error);
    if (context->lost) return doe_vk_fail(DOE_VK_DEVICE_LOST, "texture context", error);
    uint32_t bytes = 0;
    doe_vk_status status = validate_texture(context, desc, &bytes);
    if (status != DOE_VK_OK) return doe_vk_fail(status, "texture admission", error);
    doe_vk_texture *texture = calloc(1, sizeof(*texture));
    if (texture == NULL) return doe_vk_fail(DOE_VK_OUT_OF_MEMORY, "texture owner", error);
    status = doe_vk_context_retain(context);
    if (status != DOE_VK_OK) {
        free(texture);
        return doe_vk_fail(status, "texture context retain", error);
    }
    texture->context = context;
    texture->references = 1;
    texture->desc = *desc;
    texture->texel_bytes = bytes;
    uint64_t layers = desc->dimension == DOE_VK_TEXTURE_3D ? 1 : desc->depth_or_layers;
    uint64_t subresources = layers * desc->mip_levels;
    if (subresources > SIZE_MAX) {
        status = doe_vk_fail(DOE_VK_OUT_OF_MEMORY, "texture subresource state", error);
        goto failed;
    }
    texture->subresource_count = (size_t)subresources;
    texture->initialized = calloc(texture->subresource_count, 1);
    if (texture->initialized == NULL) {
        status = doe_vk_fail(DOE_VK_OUT_OF_MEMORY, "texture subresource state", error);
        goto failed;
    }
    const VkImageCreateInfo image = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .flags = desc->cube_compatible ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0,
        .imageType = (uint32_t)desc->dimension,
        .format = (uint32_t)desc->format,
        .extent = { desc->width, desc->height, desc->dimension == DOE_VK_TEXTURE_3D ? desc->depth_or_layers : 1 },
        .mipLevels = desc->mip_levels,
        .arrayLayers = desc->dimension == DOE_VK_TEXTURE_3D ? 1 : desc->depth_or_layers,
        .samples = 1,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = desc->usage,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
    };
    status = doe_vk_result(context, context->vk.vkCreateImage(context->device, &image, NULL, &texture->image), "vkCreateImage", error);
    if (status != DOE_VK_OK) goto failed;
    VkMemoryRequirements requirements = {0};
    context->vk.vkGetImageMemoryRequirements(context->device, texture->image, &requirements);
    if (requirements.size == 0) {
        status = doe_vk_fail(DOE_VK_NATIVE_FAILURE, "image memory requirements", error);
        goto failed;
    }
    uint32_t index = 0;
    status = doe_vk_memory_type(&context->memory, requirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0, &index);
    if (status != DOE_VK_OK) {
        doe_vk_fail(status, "image memory type", error);
        goto failed;
    }
    const VkMemoryAllocateInfo allocate = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = requirements.size,
        .memoryTypeIndex = index
    };
    status = doe_vk_result(context, context->vk.vkAllocateMemory(context->device, &allocate, NULL, &texture->memory), "vkAllocateMemory image", error);
    if (status != DOE_VK_OK) goto failed;
    status = doe_vk_result(context, context->vk.vkBindImageMemory(context->device, texture->image, texture->memory, 0), "vkBindImageMemory", error);
    if (status != DOE_VK_OK) goto failed;
    *out = texture;
    return doe_vk_fail(DOE_VK_OK, "texture ready", error);
failed:
    doe_vk_texture_release(texture);
    return status;
}

doe_vk_status doe_vk_texture_retain(doe_vk_texture *texture) {
    if (texture == NULL) return DOE_VK_INVALID_ARGUMENT;
    if (texture->references == UINT32_MAX) return DOE_VK_OUT_OF_MEMORY;
    ++texture->references;
    return DOE_VK_OK;
}

void doe_vk_texture_release(doe_vk_texture *texture) {
    if (texture == NULL || --texture->references != 0) return;
    doe_vk_context *context = texture->context;
    if (texture->image != 0) context->vk.vkDestroyImage(context->device, texture->image, NULL);
    if (texture->memory != 0) context->vk.vkFreeMemory(context->device, texture->memory, NULL);
    free(texture->initialized);
    free(texture);
    doe_vk_context_release(context);
}

static doe_vk_status validate_view(doe_vk_texture *texture, const doe_vk_texture_view_desc *desc) {
    if (desc == NULL || desc->dimension < DOE_VK_VIEW_1D || desc->dimension > DOE_VK_VIEW_CUBE_ARRAY ||
        desc->mip_count == 0 || desc->layer_count == 0 ||
        !doe_vk_range(texture->desc.mip_levels, desc->base_mip, desc->mip_count)) return DOE_VK_INVALID_ARGUMENT;
    if (texture->content_status != DOE_VK_OK) return texture->content_status;
    if (!(texture->desc.usage & (DOE_VK_TEXTURE_SAMPLED | DOE_VK_TEXTURE_STORAGE | DOE_VK_TEXTURE_COLOR_ATTACHMENT)))
        return DOE_VK_INVALID_ARGUMENT;
    uint32_t layers = texture->desc.dimension == DOE_VK_TEXTURE_3D ? 1 : texture->desc.depth_or_layers;
    if (!doe_vk_range(layers, desc->base_layer, desc->layer_count)) return DOE_VK_INVALID_ARGUMENT;
    if (texture->desc.dimension == DOE_VK_TEXTURE_1D) {
        if (desc->dimension != DOE_VK_VIEW_1D && desc->dimension != DOE_VK_VIEW_1D_ARRAY)
            return DOE_VK_INVALID_ARGUMENT;
    } else if (texture->desc.dimension == DOE_VK_TEXTURE_3D) {
        if (desc->dimension != DOE_VK_VIEW_3D) return DOE_VK_INVALID_ARGUMENT;
    } else if (desc->dimension != DOE_VK_VIEW_2D && desc->dimension != DOE_VK_VIEW_2D_ARRAY &&
        desc->dimension != DOE_VK_VIEW_CUBE && desc->dimension != DOE_VK_VIEW_CUBE_ARRAY) return DOE_VK_INVALID_ARGUMENT;
    if (desc->dimension == DOE_VK_VIEW_CUBE_ARRAY) return DOE_VK_UNSUPPORTED;
    if (desc->dimension == DOE_VK_VIEW_CUBE && (!texture->desc.cube_compatible || desc->layer_count != 6))
        return DOE_VK_INVALID_ARGUMENT;
    if ((desc->dimension == DOE_VK_VIEW_1D || desc->dimension == DOE_VK_VIEW_2D || desc->dimension == DOE_VK_VIEW_3D) && desc->layer_count != 1)
        return DOE_VK_INVALID_ARGUMENT;
    return DOE_VK_OK;
}

doe_vk_status doe_vk_texture_view_create(doe_vk_texture *texture,
    const doe_vk_texture_view_desc *desc, doe_vk_texture_view **out, doe_vk_error *error) {
    if (texture == NULL || out == NULL) return doe_vk_fail(DOE_VK_INVALID_ARGUMENT, "texture view descriptor", error);
    if (texture->context->lost) return doe_vk_fail(DOE_VK_DEVICE_LOST, "texture view context", error);
    doe_vk_status status = validate_view(texture, desc);
    if (status != DOE_VK_OK) return doe_vk_fail(status, "texture view admission", error);
    doe_vk_texture_view *view = calloc(1, sizeof(*view));
    if (view == NULL) return doe_vk_fail(DOE_VK_OUT_OF_MEMORY, "texture view owner", error);
    status = doe_vk_texture_retain(texture);
    if (status != DOE_VK_OK) {
        free(view);
        return doe_vk_fail(status, "texture view parent retain", error);
    }
    view->texture = texture;
    view->references = 1;
    view->desc = *desc;
    const VkImageViewCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = texture->image,
        .viewType = (uint32_t)desc->dimension,
        .format = (uint32_t)texture->desc.format,
        .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, desc->base_mip, desc->mip_count, desc->base_layer, desc->layer_count }
    };
    doe_vk_context *context = texture->context;
    status = doe_vk_result(context, context->vk.vkCreateImageView(context->device, &info, NULL, &view->view), "vkCreateImageView", error);
    if (status != DOE_VK_OK) {
        doe_vk_texture_view_release(view);
        return status;
    }
    *out = view;
    return doe_vk_fail(DOE_VK_OK, "texture view ready", error);
}

doe_vk_status doe_vk_texture_view_retain(doe_vk_texture_view *view) {
    if (view == NULL) return DOE_VK_INVALID_ARGUMENT;
    if (view->references == UINT32_MAX) return DOE_VK_OUT_OF_MEMORY;
    ++view->references;
    return DOE_VK_OK;
}

void doe_vk_texture_view_release(doe_vk_texture_view *view) {
    if (view == NULL || --view->references != 0) return;
    doe_vk_texture *texture = view->texture;
    doe_vk_context *context = texture->context;
    if (view->view != 0) context->vk.vkDestroyImageView(context->device, view->view, NULL);
    free(view);
    doe_vk_texture_release(texture);
}

doe_vk_status doe_vk_sampler_create(doe_vk_context *context,
    const doe_vk_sampler_desc *desc, doe_vk_sampler **out, doe_vk_error *error) {
    if (context == NULL || desc == NULL || out == NULL ||
        desc->min_filter < DOE_VK_FILTER_NEAREST || desc->min_filter > DOE_VK_FILTER_LINEAR ||
        desc->mag_filter < DOE_VK_FILTER_NEAREST || desc->mag_filter > DOE_VK_FILTER_LINEAR ||
        desc->mipmap_filter < DOE_VK_FILTER_NEAREST || desc->mipmap_filter > DOE_VK_FILTER_LINEAR ||
        desc->address_u < DOE_VK_ADDRESS_REPEAT || desc->address_u > DOE_VK_ADDRESS_CLAMP ||
        desc->address_v < DOE_VK_ADDRESS_REPEAT || desc->address_v > DOE_VK_ADDRESS_CLAMP ||
        desc->address_w < DOE_VK_ADDRESS_REPEAT || desc->address_w > DOE_VK_ADDRESS_CLAMP ||
        !isfinite(desc->min_lod) || !isfinite(desc->max_lod) || desc->min_lod < 0 ||
        desc->max_lod < desc->min_lod || desc->compare_operation > 7)
        return doe_vk_fail(DOE_VK_INVALID_ARGUMENT, "sampler descriptor", error);
    if (context->lost) return doe_vk_fail(DOE_VK_DEVICE_LOST, "sampler context", error);
    doe_vk_sampler *sampler = calloc(1, sizeof(*sampler));
    if (sampler == NULL) return doe_vk_fail(DOE_VK_OUT_OF_MEMORY, "sampler owner", error);
    doe_vk_status status = doe_vk_context_retain(context);
    if (status != DOE_VK_OK) {
        free(sampler);
        return doe_vk_fail(status, "sampler context retain", error);
    }
    sampler->context = context;
    sampler->references = 1;
    const VkSamplerCreateInfo create = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = (uint32_t)desc->mag_filter,
        .minFilter = (uint32_t)desc->min_filter,
        .mipmapMode = (uint32_t)desc->mipmap_filter,
        .addressModeU = (uint32_t)desc->address_u,
        .addressModeV = (uint32_t)desc->address_v,
        .addressModeW = (uint32_t)desc->address_w,
        .maxAnisotropy = 1,
        .compareEnable = desc->compare_enabled,
        .compareOp = desc->compare_operation,
        .minLod = desc->min_lod,
        .maxLod = desc->max_lod
    };
    status = doe_vk_result(context, context->vk.vkCreateSampler(context->device, &create, NULL, &sampler->sampler), "vkCreateSampler", error);
    if (status != DOE_VK_OK) {
        doe_vk_sampler_release(sampler);
        return status;
    }
    *out = sampler;
    return doe_vk_fail(DOE_VK_OK, "sampler ready", error);
}

doe_vk_status doe_vk_sampler_retain(doe_vk_sampler *sampler) {
    if (sampler == NULL) return DOE_VK_INVALID_ARGUMENT;
    if (sampler->references == UINT32_MAX) return DOE_VK_OUT_OF_MEMORY;
    ++sampler->references;
    return DOE_VK_OK;
}

void doe_vk_sampler_release(doe_vk_sampler *sampler) {
    if (sampler == NULL || --sampler->references != 0) return;
    doe_vk_context *context = sampler->context;
    if (sampler->sampler != 0) context->vk.vkDestroySampler(context->device, sampler->sampler, NULL);
    free(sampler);
    doe_vk_context_release(context);
}
