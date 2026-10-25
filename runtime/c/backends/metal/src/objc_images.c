/* SPDX-License-Identifier: MIT */
#include "objc_abi.h"

bool doe_mtl_objc_load_image_selectors(doe_mtl_objc *r) {
    r->texture_class = r->get_class("MTLTextureDescriptor");
    r->sampler_class = r->get_class("MTLSamplerDescriptor");
    r->new_texture = r->selector("newTextureWithDescriptor:");
    r->new_view = r->selector("newTextureViewWithPixelFormat:textureType:levels:slices:swizzle:");
    r->new_sampler = r->selector("newSamplerStateWithDescriptor:");
    r->set_type = r->selector("setTextureType:");
    r->set_format = r->selector("setPixelFormat:");
    r->set_width = r->selector("setWidth:");
    r->set_height = r->selector("setHeight:");
    r->set_depth = r->selector("setDepth:");
    r->set_mips = r->selector("setMipmapLevelCount:");
    r->set_layers = r->selector("setArrayLength:");
    r->set_samples = r->selector("setSampleCount:");
    r->set_storage = r->selector("setStorageMode:");
    r->set_usage = r->selector("setUsage:");
    r->set_min_filter = r->selector("setMinFilter:");
    r->set_mag_filter = r->selector("setMagFilter:");
    r->set_mip_filter = r->selector("setMipFilter:");
    r->set_address_u = r->selector("setSAddressMode:");
    r->set_address_v = r->selector("setTAddressMode:");
    r->set_address_w = r->selector("setRAddressMode:");
    r->set_min_lod = r->selector("setLodMinClamp:");
    r->set_max_lod = r->selector("setLodMaxClamp:");
    r->set_compare = r->selector("setCompareFunction:");
    r->set_anisotropy = r->selector("setMaxAnisotropy:");
    r->upload_image = r->selector("copyFromBuffer:sourceOffset:sourceBytesPerRow:sourceBytesPerImage:sourceSize:toTexture:destinationSlice:destinationLevel:destinationOrigin:options:");
    r->read_image = r->selector("copyFromTexture:sourceSlice:sourceLevel:sourceOrigin:sourceSize:toBuffer:destinationOffset:destinationBytesPerRow:destinationBytesPerImage:options:");
    r->copy_image = r->selector("copyFromTexture:sourceSlice:sourceLevel:sourceOrigin:sourceSize:toTexture:destinationSlice:destinationLevel:destinationOrigin:");
    r->set_texture = r->selector("setTexture:atIndex:");
    r->set_sampler = r->selector("setSamplerState:atIndex:");
    r->supports_family = r->selector("supportsFamily:");
    r->supports_bc = r->selector("supportsBCTextureCompression");
    r->supports_samples = r->selector("supportsTextureSampleCount:");
    r->supports_depth24 = r->selector("isDepth24Stencil8PixelFormatSupported");
    r->supports_float_filter = r->selector("supports32BitFloatFiltering");
    return r->texture_class != NULL && r->sampler_class != NULL &&
        r->new_texture != NULL && r->new_view != NULL && r->new_sampler != NULL &&
        r->set_type != NULL && r->set_format != NULL && r->set_width != NULL &&
        r->set_height != NULL && r->set_depth != NULL && r->set_mips != NULL &&
        r->set_layers != NULL && r->set_samples != NULL && r->set_storage != NULL &&
        r->set_usage != NULL && r->set_min_filter != NULL && r->set_mag_filter != NULL &&
        r->set_mip_filter != NULL && r->set_address_u != NULL && r->set_address_v != NULL &&
        r->set_address_w != NULL && r->set_min_lod != NULL && r->set_max_lod != NULL &&
        r->set_compare != NULL && r->set_anisotropy != NULL && r->upload_image != NULL &&
        r->read_image != NULL && r->copy_image != NULL && r->set_texture != NULL &&
        r->set_sampler != NULL && r->supports_family != NULL && r->supports_bc != NULL &&
        r->supports_samples != NULL && r->supports_depth24 != NULL && r->supports_float_filter != NULL;
}

void doe_mtl_objc_image_caps(doe_mtl_objc *r, void *device, doe_mtl_caps *caps) {
    uint32_t bit;
    if (r->responds_call(device, r->responds, r->supports_samples))
        for (bit = 0; bit < 5; ++bit)
            if (r->samples_call(device, r->supports_samples, (size_t)1 << bit)) caps->sample_count_mask |= 1u << bit;
    if (r->responds_call(device, r->responds, r->supports_depth24))
        caps->depth24_stencil8 = r->bool_call(device, r->supports_depth24) != 0;
    if (r->responds_call(device, r->responds, r->supports_float_filter))
        caps->float32_filterable = r->bool_call(device, r->supports_float_filter) != 0;
    /* Conservative published family limits. Unrecognized older devices keep
     * buffer compute, but have no admitted image dimensions rather than guesses. */
    if (r->responds_call(device, r->responds, r->supports_family)) {
        /* Native SIMD reductions start at Apple7, later than permutations.
         * Keep non-Apple GPUs unavailable pending their exact operation/quirk
         * admission. Apple family membership, not device-name guessing, binds
         * the documented 32-lane family range; this is not a measured PSO width.
         * https://developer.apple.com/metal/Metal-Feature-Set-Tables.pdf */
        /* supportsFamily itself requires the macOS 10.15/iOS 13 generation;
         * Mac2 therefore has MSL 2.2, while Apple7 requires iOS 14/MSL 2.3.
         * Those are primitive_id's language minima as well as family bounds. */
        caps->primitive_index = r->family_call(device, r->supports_family, 1007) ||
            r->family_call(device, r->supports_family, 2002);
        caps->dual_source_blending = r->family_call(device, r->supports_family, 1002) ||
            r->family_call(device, r->supports_family, 2002);
        caps->subgroups = r->family_call(device, r->supports_family, 1007) != 0;
        if (caps->subgroups) caps->subgroup_min_size = caps->subgroup_max_size = 32;
        /* Apple Metal Feature Set Tables: Apple2/3 512, Apple4+ and Mac1/2
         * 1024 total threads. Per-axis device properties are not a product
         * limit; each actual PSO maxTotalThreadsPerThreadgroup is still checked.
         * https://developer.apple.com/metal/Metal-Feature-Set-Tables.pdf */
        if (r->family_call(device, r->supports_family, 2001) ||
            r->family_call(device, r->supports_family, 2002) ||
            r->family_call(device, r->supports_family, 1004)) caps->max_threads_per_workgroup = 1024;
        else if (r->family_call(device, r->supports_family, 1001) ||
            r->family_call(device, r->supports_family, 1002) ||
            r->family_call(device, r->supports_family, 1003)) caps->max_threads_per_workgroup = 512;
        /* Apple pixel-format tables give R/RG/RGBA32Float blending and
         * RG11B10Float color/blend/resolve on these known families. Filtering
         * remains the independently queried device property above.
         * https://developer.apple.com/metal/Metal-Feature-Set-Tables.pdf */
        bool color_formats = r->family_call(device, r->supports_family, 1002) ||
            r->family_call(device, r->supports_family, 2002);
        caps->float32_blendable = color_formats;
        caps->rg11b10_renderable = color_formats && (caps->sample_count_mask & 5) == 5;
        /* Apple feature tables: EAC/ETC and ASTC LDR start at Apple2. */
        caps->etc2_texture_compression = r->family_call(device, r->supports_family, 1002) != 0;
        caps->astc_texture_compression = caps->etc2_texture_compression;
        /* Apple capabilities table retains the Mac2 column omitted by the
         * newer Apple-only PDF: texture swizzle starts at Apple2 or Mac2. */
        caps->texture_swizzle = caps->etc2_texture_compression || r->family_call(device, r->supports_family, 2002);
        caps->astc_sliced_3d = r->family_call(device, r->supports_family, 1003) != 0;
        caps->separate_depth_stencil = r->family_call(device, r->supports_family, 1002) != 0;
        caps->depth_resolve = r->family_call(device, r->supports_family, 1003) ||
            r->family_call(device, r->supports_family, 2002);
        caps->stencil_resolve = r->family_call(device, r->supports_family, 1005) ||
            r->family_call(device, r->supports_family, 2002);
        caps->store_and_resolve = caps->depth_resolve;
        caps->counting_visibility = caps->depth_resolve;
        caps->multisample_array = r->family_call(device, r->supports_family, 1007) ||
            r->family_call(device, r->supports_family, 2002);
        if (r->family_call(device, r->supports_family, 2002) ||
            r->family_call(device, r->supports_family, 1003)) {
            caps->max_texture_1d = 16384;
            caps->max_texture_2d = 16384;
        } else if (r->family_call(device, r->supports_family, 1002)) {
            caps->max_texture_1d = 8192;
            caps->max_texture_2d = 8192;
        }
        if (caps->max_texture_2d != 0) {
            caps->max_texture_3d = 2048;
            caps->max_texture_layers = 2048;
            caps->max_visibility_offset = caps->multisample_array ? 262136 : 65528;
        }
    }
    if (r->responds_call(device, r->responds, r->supports_bc))
        caps->bc_texture_compression = r->bool_call(device, r->supports_bc) != 0;
}

doe_mtl_status doe_mtl_objc_texture(void *userdata, void *device,
    const doe_mtl_texture_desc *desc, void **out, doe_mtl_error *error) {
    doe_mtl_objc *r = userdata;
    void *pool = doe_mtl_objc_pool(r), *descriptor, *texture;
    size_t layers = 1, depth = 1, usage = 0;
    if (pool == NULL) return doe_mtl_fail(DOE_MTL_NATIVE_FAILURE, "autorelease pool", error);
    descriptor = doe_mtl_objc_object(r, r->texture_class, r->new_object);
    if (descriptor == NULL) {
        doe_mtl_objc_drain(r, pool);
        return doe_mtl_fail(DOE_MTL_NATIVE_FAILURE, "MTLTextureDescriptor", error);
    }
    if (desc->type == DOE_MTL_TEXTURE_3D) depth = desc->depth_or_layers;
    else if (desc->type == DOE_MTL_TEXTURE_CUBE_ARRAY) layers = desc->depth_or_layers / 6;
    else if (desc->type == DOE_MTL_TEXTURE_1D_ARRAY || desc->type == DOE_MTL_TEXTURE_2D_ARRAY ||
             desc->type == DOE_MTL_TEXTURE_2D_MS_ARRAY)
        layers = desc->depth_or_layers;
    if (desc->usage & (DOE_MTL_TEXTURE_SAMPLED | DOE_MTL_TEXTURE_STORAGE)) usage |= 1;
    if (desc->usage & DOE_MTL_TEXTURE_STORAGE) usage |= 2;
    if (desc->usage & DOE_MTL_TEXTURE_RENDER_TARGET) usage |= 4;
    if (desc->allow_format_views) usage |= 16;
    r->set_size_call(descriptor, r->set_type, (size_t)desc->type);
    r->set_size_call(descriptor, r->set_format, (size_t)desc->format);
    r->set_size_call(descriptor, r->set_width, desc->width);
    r->set_size_call(descriptor, r->set_height, desc->height);
    r->set_size_call(descriptor, r->set_depth, depth);
    r->set_size_call(descriptor, r->set_layers, layers);
    r->set_size_call(descriptor, r->set_mips, desc->mip_levels);
    r->set_size_call(descriptor, r->set_samples, desc->sample_count);
    r->set_size_call(descriptor, r->set_storage, 2); /* MTLStorageModePrivate */
    r->set_size_call(descriptor, r->set_usage, usage);
    texture = r->named_call(device, r->new_texture, descriptor);
    doe_mtl_objc_void(r, descriptor, r->release);
    doe_mtl_objc_drain(r, pool);
    if (texture == NULL) return doe_mtl_fail(DOE_MTL_NATIVE_FAILURE, "newTextureWithDescriptor:", error);
    *out = texture;
    return doe_mtl_fail(DOE_MTL_OK, "newTextureWithDescriptor:", error);
}

doe_mtl_status doe_mtl_objc_view(void *userdata, void *texture,
    const doe_mtl_texture_view_desc *desc, void **out, doe_mtl_error *error) {
    doe_mtl_objc *r = userdata;
    void *pool = doe_mtl_objc_pool(r), *view;
    const doe_mtl_range_abi mips = {desc->base_mip, desc->mip_count};
    const doe_mtl_range_abi layers = {desc->base_layer, desc->layer_count};
    const doe_mtl_swizzle_abi swizzle = {(uint8_t)desc->red, (uint8_t)desc->green,
        (uint8_t)desc->blue, (uint8_t)desc->alpha};
    if (pool == NULL) return doe_mtl_fail(DOE_MTL_NATIVE_FAILURE, "autorelease pool", error);
    if (!r->responds_call(texture, r->responds, r->new_view)) {
        doe_mtl_objc_drain(r, pool);
        return doe_mtl_fail(DOE_MTL_UNSUPPORTED, "Metal swizzled texture view", error);
    }
    view = r->view_call(texture, r->new_view, (size_t)desc->format,
        (size_t)desc->type, mips, layers, swizzle);
    doe_mtl_objc_drain(r, pool);
    if (view == NULL) return doe_mtl_fail(DOE_MTL_NATIVE_FAILURE, "newTextureView", error);
    *out = view;
    return doe_mtl_fail(DOE_MTL_OK, "newTextureView", error);
}

doe_mtl_status doe_mtl_objc_sampler(void *userdata, void *device,
    const doe_mtl_sampler_desc *desc, void **out, doe_mtl_error *error) {
    doe_mtl_objc *r = userdata;
    void *pool = doe_mtl_objc_pool(r), *descriptor, *sampler;
    if (pool == NULL) return doe_mtl_fail(DOE_MTL_NATIVE_FAILURE, "autorelease pool", error);
    descriptor = doe_mtl_objc_object(r, r->sampler_class, r->new_object);
    if (descriptor == NULL) {
        doe_mtl_objc_drain(r, pool);
        return doe_mtl_fail(DOE_MTL_NATIVE_FAILURE, "MTLSamplerDescriptor", error);
    }
    r->set_size_call(descriptor, r->set_min_filter, (size_t)desc->min_filter);
    r->set_size_call(descriptor, r->set_mag_filter, (size_t)desc->mag_filter);
    r->set_size_call(descriptor, r->set_mip_filter, (size_t)desc->mip_filter + 1);
    r->set_size_call(descriptor, r->set_address_u, (size_t)desc->address_u);
    r->set_size_call(descriptor, r->set_address_v, (size_t)desc->address_v);
    r->set_size_call(descriptor, r->set_address_w, (size_t)desc->address_w);
    r->set_size_call(descriptor, r->set_compare, desc->compare);
    r->set_size_call(descriptor, r->set_anisotropy, desc->max_anisotropy);
    r->set_float_call(descriptor, r->set_min_lod, desc->min_lod);
    r->set_float_call(descriptor, r->set_max_lod, desc->max_lod);
    sampler = r->named_call(device, r->new_sampler, descriptor);
    doe_mtl_objc_void(r, descriptor, r->release);
    doe_mtl_objc_drain(r, pool);
    if (sampler == NULL) return doe_mtl_fail(DOE_MTL_NATIVE_FAILURE, "newSamplerStateWithDescriptor:", error);
    *out = sampler;
    return doe_mtl_fail(DOE_MTL_OK, "newSamplerStateWithDescriptor:", error);
}
