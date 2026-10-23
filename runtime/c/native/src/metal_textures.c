/* SPDX-License-Identifier: MIT */
#include "metal_frontend.h"
#include <stdlib.h>

static native_failure initialize_texture(native_device *device, native_texture *texture,
    const doe_mtl_texture_desc *desc)
{
    native_command_handle commands = {0};
    native_failure failure = native_command_create(&device->backend, &commands);
    doe_mtl_error detail = {0};
    doe_mtl_texture_view *view = NULL;
    doe_mtl_buffer *zero = NULL;
    if (failure.status != NATIVE_OK) return failure;
    if (desc->usage & DOE_MTL_TEXTURE_RENDER_TARGET) {
        doe_mtl_texture_view_desc span = {.type=desc->type,.format=desc->format,
            .mip_count=desc->mip_levels,.layer_count=desc->depth_or_layers,
            .red=DOE_MTL_SWIZZLE_RED,.green=DOE_MTL_SWIZZLE_GREEN,
            .blue=DOE_MTL_SWIZZLE_BLUE,.alpha=DOE_MTL_SWIZZLE_ALPHA};
        doe_mtl_status status = doe_mtl_texture_view_create(texture->metal, &span, &view, &detail);
        if (status == DOE_MTL_OK) status = doe_mtl_commands_initialize_attachment(commands.value.metal, view, &detail);
        failure = native_failure_metal(status, &detail);
    } else {
        doe_mtl_format_info format = {0};
        doe_mtl_status status = doe_mtl_texture_format_info(desc->format, &format);
        uint64_t pitch = ((uint64_t)desc->width * format.block_bytes + 255) / 256 * 256;
        uint64_t bytes = 0;
        bool bounded = status == DOE_MTL_OK && format.block_bytes && pitch && desc->height <= SIZE_MAX / pitch;
        if (bounded) {
            bytes = pitch * desc->height;
            bounded = desc->depth_or_layers <= SIZE_MAX / bytes;
        }
        if (bounded) {
            bytes *= desc->depth_or_layers;
            bounded = bytes <= device->backend.limits.maxBufferSize;
        }
        if (!bounded) {
            failure = native_failure_status(NATIVE_UNSUPPORTED);
        } else {
            status = doe_mtl_buffer_create(device->backend.value.metal.context, bytes, DOE_MTL_SHARED, &zero, &detail);
            for (uint32_t mip = 0; status == DOE_MTL_OK && mip < desc->mip_levels; ++mip) {
                uint32_t width = desc->width >> mip, height = desc->height >> mip;
                uint32_t depth = desc->type == DOE_MTL_TEXTURE_3D ? desc->depth_or_layers >> mip : desc->depth_or_layers;
                if (!width) width = 1;
                if (!height) height = 1;
                if (!depth) depth = 1;
                uint32_t row = (uint32_t)(((uint64_t)width * format.block_bytes + 255) / 256 * 256);
                status = doe_mtl_commands_upload_texture(commands.value.metal, zero,
                    (doe_mtl_texture_buffer_layout){.bytes_per_row=row}, texture->metal,
                    (doe_mtl_texture_region){.mip=mip,.width=width,.height=height,.depth_or_layers=depth}, &detail);
            }
            failure = native_failure_metal(status, &detail);
        }
    }
    if (failure.status == NATIVE_OK) failure = native_submit_backend_commands(device, commands);
    else if (native_command_release(&commands) != NATIVE_OK) abort();
    doe_mtl_texture_view_release(view);
    doe_mtl_buffer_release(zero);
    return failure;
}

WGPUTexture native_metal_create_texture(native_device *device, const WGPUTextureDescriptor *descriptor)
{
    const WGPUChainedStruct *chain = descriptor ? descriptor->nextInChain : NULL;
    if (!descriptor || (chain && (chain->sType != WGPUSType_TextureBindingViewDimension || chain->next))) {
        native_error(device, WGPUErrorType_Validation, "Metal texture descriptor unsupported");
        return NULL;
    }
    doe_resource_texture_desc desc = {.dimension=descriptor->dimension ? descriptor->dimension : WGPUTextureDimension_2D,
        .format=descriptor->format,.width=descriptor->size.width,.height=descriptor->size.height,
        .depth_or_layers=descriptor->size.depthOrArrayLayers,.mip_levels=descriptor->mipLevelCount,
        .samples=descriptor->sampleCount,.usage=descriptor->usage,
        .view_formats=descriptor->viewFormats,.view_format_count=descriptor->viewFormatCount};
    doe_resource_texture_limits limits = {device->backend.limits.maxTextureDimension1D,
        device->backend.limits.maxTextureDimension2D,device->backend.limits.maxTextureDimension3D,
        device->backend.limits.maxTextureArrayLayers};
    doe_mtl_texture_desc native = {.width=desc.width,.height=desc.height,.depth_or_layers=desc.depth_or_layers,
        .mip_levels=desc.mip_levels,.sample_count=desc.samples,.allow_format_views=desc.view_format_count != 0};
    doe_resource_status valid = doe_resource_texture_validate(&desc, &limits);
    if (valid != DOE_RESOURCE_OK || !native_metal_format(device->features, desc.format, &native.format)) {
        native_error(device, valid != DOE_RESOURCE_OK ? native_resource_error(valid) : WGPUErrorType_Validation,
            "Metal texture shape, feature or format unsupported");
        return NULL;
    }
    doe_mtl_format_info format;
    if (doe_mtl_texture_format_info(native.format, &format) != DOE_MTL_OK) abort();
    if ((format.depth || format.stencil) && (desc.usage & ~WGPUTextureUsage_RenderAttachment)) {
        native_error(device, WGPUErrorType_Validation, "Metal depth/stencil transfers and sampling are not composed");
        return NULL;
    }
    if (desc.dimension == WGPUTextureDimension_1D) native.type = DOE_MTL_TEXTURE_1D;
    else if (desc.dimension == WGPUTextureDimension_3D) native.type = DOE_MTL_TEXTURE_3D;
    else if (desc.samples > 1) native.type = desc.depth_or_layers > 1 ? DOE_MTL_TEXTURE_2D_MS_ARRAY : DOE_MTL_TEXTURE_2D_MS;
    else native.type = desc.depth_or_layers > 1 ? DOE_MTL_TEXTURE_2D_ARRAY : DOE_MTL_TEXTURE_2D;
    if (desc.usage & WGPUTextureUsage_CopySrc) native.usage |= DOE_MTL_TEXTURE_COPY_SRC;
    if (desc.usage & WGPUTextureUsage_CopyDst) native.usage |= DOE_MTL_TEXTURE_COPY_DST;
    if (desc.usage & WGPUTextureUsage_TextureBinding) native.usage |= DOE_MTL_TEXTURE_SAMPLED;
    if (desc.usage & WGPUTextureUsage_StorageBinding) native.usage |= DOE_MTL_TEXTURE_STORAGE;
    /* Initialization usage is private, never permission in the resource owner.
     * Two-dimensional allocations use the canonical aspect clear; 1D/3D use a
     * real initialized staging upload rather than CPU-mapped private storage. */
    if (desc.dimension == WGPUTextureDimension_2D) native.usage |= DOE_MTL_TEXTURE_RENDER_TARGET;
    else native.usage |= DOE_MTL_TEXTURE_COPY_DST;
    native_texture *payload = calloc(1, sizeof(*payload));
    if (!payload) { native_error(device, WGPUErrorType_OutOfMemory, "Metal texture payload allocation failed"); return NULL; }
    payload->device = device;
    doe_resource_texture *texture = NULL;
    native_lock(device);
    native_failure failure = native_failure_status(native_device_result(device));
    if (failure.status == NATIVE_OK) failure = native_settle_pending(device).failure;
    if (failure.status == NATIVE_OK) {
        doe_mtl_error detail = {0};
        doe_mtl_status status = doe_mtl_texture_create(device->backend.value.metal.context, &native, &payload->metal, &detail);
        failure = native_failure_metal(status, &detail);
    }
    if (failure.status == NATIVE_OK) failure = initialize_texture(device, payload, &native);
    doe_resource_status adopted = DOE_RESOURCE_INVALID_ARGUMENT;
    if (failure.status == NATIVE_OK)
        adopted = doe_resource_texture_create(&device->resources, &desc, &limits,
            (doe_resource_native){payload,NULL,native_texture_release}, &texture);
    native_unlock(device);
    if (!texture) {
        native_texture_release(NULL, payload);
        char message[NATIVE_FAILURE_MESSAGE_CAPACITY];
        WGPUErrorType type = failure.status == NATIVE_OK ? native_resource_error(adopted) : native_result_error(failure.status);
        if (failure.status == NATIVE_OK) failure = native_failure_status(NATIVE_FAILURE);
        native_failure_format(&failure, "Metal texture creation or proven initialization failed", message);
        native_error(device, type, message);
    } else native_label(device, texture, descriptor->label);
    return (WGPUTexture)texture;
}

WGPUTextureView native_metal_create_view(doe_resource_texture *texture, const WGPUTextureViewDescriptor *descriptor)
{
    native_device *device = doe_resource_texture_device(texture);
    WGPUTextureViewDescriptor defaults = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    if (!descriptor) descriptor = &defaults;
    doe_resource_view_desc desc = {.format=descriptor->format,.dimension=descriptor->dimension,
        .base_mip=descriptor->baseMipLevel,.mip_count=descriptor->mipLevelCount,
        .base_layer=descriptor->baseArrayLayer,.layer_count=descriptor->arrayLayerCount,
        .aspect=descriptor->aspect,.usage=descriptor->usage}, resolved;
    native_lock(device);
    doe_resource_status status = descriptor->nextInChain ? DOE_RESOURCE_UNSUPPORTED :
        !descriptor->mipLevelCount || !descriptor->arrayLayerCount ? DOE_RESOURCE_RANGE :
        doe_resource_view_validate(texture, &desc, &resolved);
    doe_mtl_texture_view_desc native = {.red=DOE_MTL_SWIZZLE_RED,.green=DOE_MTL_SWIZZLE_GREEN,
        .blue=DOE_MTL_SWIZZLE_BLUE,.alpha=DOE_MTL_SWIZZLE_ALPHA};
    if (status == DOE_RESOURCE_OK) {
        if (!native_metal_format(device->features, resolved.format, &native.format)) status = DOE_RESOURCE_UNSUPPORTED;
        native.base_mip = resolved.base_mip; native.mip_count = resolved.mip_count;
        native.base_layer = resolved.base_layer; native.layer_count = resolved.layer_count;
        const doe_resource_texture_desc *parent = doe_resource_texture_description(texture);
        switch (resolved.dimension) {
        case WGPUTextureViewDimension_1D: native.type=DOE_MTL_TEXTURE_1D; break;
        case WGPUTextureViewDimension_2D: native.type=parent->samples > 1 ? DOE_MTL_TEXTURE_2D_MS : DOE_MTL_TEXTURE_2D; break;
        case WGPUTextureViewDimension_2DArray: native.type=parent->samples > 1 ? DOE_MTL_TEXTURE_2D_MS_ARRAY : DOE_MTL_TEXTURE_2D_ARRAY; break;
        case WGPUTextureViewDimension_3D: native.type=DOE_MTL_TEXTURE_3D; break;
        case WGPUTextureViewDimension_Cube: native.type=DOE_MTL_TEXTURE_CUBE; break;
        case WGPUTextureViewDimension_CubeArray: native.type=DOE_MTL_TEXTURE_CUBE_ARRAY; break;
        default: status=DOE_RESOURCE_UNSUPPORTED; break;
        }
    }
    native_view *payload = status == DOE_RESOURCE_OK ? calloc(1,sizeof(*payload)) : NULL;
    doe_resource_view *view = NULL;
    native_failure failure = native_failure_status(NATIVE_OK);
    if (payload) {
        payload->device = device;
        native_texture *parent = doe_resource_texture_native(texture);
        doe_mtl_error detail = {0};
        doe_mtl_status created = doe_mtl_texture_view_create(parent->metal, &native, &payload->metal, &detail);
        failure = native_failure_metal(created, &detail);
        if (failure.status == NATIVE_OK)
            status = doe_resource_view_create(texture, &resolved,
                (doe_resource_native){payload,NULL,native_view_release}, &view);
    } else if (status == DOE_RESOURCE_OK) status = DOE_RESOURCE_OUT_OF_MEMORY;
    native_unlock(device);
    if (!view) {
        if (payload) native_view_release(NULL,payload);
        char message[NATIVE_FAILURE_MESSAGE_CAPACITY];
        if (failure.status == NATIVE_OK) failure = native_failure_status(NATIVE_INVALID_ARGUMENT);
        native_failure_format(&failure,"Metal texture view creation failed or unsupported",message);
        native_error(device,status != DOE_RESOURCE_OK ? native_resource_error(status) : native_result_error(failure.status),message);
    } else native_label(device,view,descriptor->label);
    return (WGPUTextureView)view;
}

WGPUSampler native_metal_create_sampler(native_device *device, const WGPUSamplerDescriptor *descriptor)
{
    WGPUSamplerDescriptor defaults = WGPU_SAMPLER_DESCRIPTOR_INIT, resolved;
    if (!descriptor) descriptor = &defaults;
    if (doe_resource_sampler_validate(descriptor,&resolved) != DOE_RESOURCE_OK) {
        native_error(device,WGPUErrorType_Validation,"Metal sampler descriptor invalid"); return NULL;
    }
    doe_mtl_sampler_desc native = {.min_filter=resolved.minFilter == WGPUFilterMode_Linear ? DOE_MTL_FILTER_LINEAR : DOE_MTL_FILTER_NEAREST,
        .mag_filter=resolved.magFilter == WGPUFilterMode_Linear ? DOE_MTL_FILTER_LINEAR : DOE_MTL_FILTER_NEAREST,
        .mip_filter=resolved.mipmapFilter == WGPUMipmapFilterMode_Linear ? DOE_MTL_FILTER_LINEAR : DOE_MTL_FILTER_NEAREST,
        .min_lod=resolved.lodMinClamp,.max_lod=resolved.lodMaxClamp,.max_anisotropy=resolved.maxAnisotropy};
    const WGPUAddressMode modes[3] = {resolved.addressModeU,resolved.addressModeV,resolved.addressModeW};
    doe_mtl_address *targets[3] = {&native.address_u,&native.address_v,&native.address_w};
    for (size_t i=0;i<3;++i) *targets[i] = modes[i] == WGPUAddressMode_Repeat ? DOE_MTL_ADDRESS_REPEAT :
        modes[i] == WGPUAddressMode_MirrorRepeat ? DOE_MTL_ADDRESS_MIRROR : DOE_MTL_ADDRESS_CLAMP;
    if (!native_compare_operation(resolved.compare,0,&native.compare)) {
        native_error(device,WGPUErrorType_Validation,"Metal sampler comparison invalid"); return NULL;
    }
    native_sampler *payload = calloc(1,sizeof(*payload));
    if (!payload) { native_error(device,WGPUErrorType_OutOfMemory,"Metal sampler payload allocation failed"); return NULL; }
    payload->device=device;
    doe_resource_sampler *sampler=NULL;
    native_lock(device);
    native_failure failure=native_failure_status(native_device_result(device));
    if (failure.status==NATIVE_OK) {
        doe_mtl_error detail={0};
        doe_mtl_status created=doe_mtl_sampler_create(device->backend.value.metal.context,&native,&payload->metal,&detail);
        failure=native_failure_metal(created,&detail);
    }
    doe_resource_status adopted=DOE_RESOURCE_INVALID_ARGUMENT;
    if (failure.status==NATIVE_OK) adopted=doe_resource_sampler_create(&device->resources,&resolved,
        (doe_resource_native){payload,NULL,native_sampler_release},&sampler);
    native_unlock(device);
    if (!sampler) {
        native_sampler_release(NULL,payload);
        char message[NATIVE_FAILURE_MESSAGE_CAPACITY];
        WGPUErrorType type = failure.status == NATIVE_OK ? native_resource_error(adopted) : native_result_error(failure.status);
        if (failure.status==NATIVE_OK) failure=native_failure_status(NATIVE_FAILURE);
        native_failure_format(&failure,"Metal sampler creation failed",message);
        native_error(device,type,message);
    } else native_label(device,sampler,descriptor->label);
    return (WGPUSampler)sampler;
}
