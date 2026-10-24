/* SPDX-License-Identifier: MIT */
#include "d3d12_texture.h"
#include "d3d12_texture_desc.h"
#include "d3d12_binding_internal.h"
#include <stdlib.h>

#if defined(_WIN32)
static native_failure initialize(native_device *device,doe_dx_texture *texture)
{
    doe_dx_texture_footprint footprint;
    doe_dx_status status = doe_dx_texture_copy_footprint(texture,(doe_dx_texture_layout){0},&footprint);
    if (status != DOE_DX_OK) return native_failure_d3d12(status,NULL);
    if (footprint.span > native_allocation_bound(device)) return native_failure_status(NATIVE_OUT_OF_MEMORY);
    native_command_handle commands = {0};
    native_failure failure = native_command_create(&device->backend,&commands);
    if (failure.status != NATIVE_OK) return failure;
    doe_dx_buffer *zero = NULL;
    doe_dx_error detail = {0};
    status = doe_dx_buffer_create(commands.value.d3d12.context,footprint.span,DOE_DX_UPLOAD,&zero,&detail);
    if (status == DOE_DX_OK)
        status = doe_dx_commands_upload_texture(commands.value.d3d12.commands,zero,
            (doe_dx_texture_layout){0},texture,&detail);
    failure = native_failure_d3d12(status,&detail);
    if (failure.status == NATIVE_OK) failure = native_submit_backend_commands(device,commands);
    else if (native_command_release(&commands) != NATIVE_OK) abort();
    /* The submitted command owns both allocations independently of this lease.
     * Unknown completion keeps that command and its existing pending device. */
    doe_dx_buffer_release(zero);
    return failure;
}
#endif

WGPUTexture native_d3d12_create_texture(native_device *device,const WGPUTextureDescriptor *descriptor)
{
    const WGPUChainedStruct *chain = descriptor ? descriptor->nextInChain : NULL;
    if (!descriptor || (chain && (chain->sType != WGPUSType_TextureBindingViewDimension || chain->next))) {
        native_error(device,WGPUErrorType_Validation,"D3D12 texture descriptor unsupported");
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
    doe_dx_texture_desc native;
    doe_resource_status status = doe_resource_texture_validate(&desc,&limits);
    if (status != DOE_RESOURCE_OK || !native_d3d12_texture_descriptor(&desc,&native)) {
        native_error(device,WGPUErrorType_Validation,"D3D12 texture shape, format or usage not composed");
        return NULL;
    }
    native_texture *payload = calloc(1,sizeof(*payload));
    if (!payload) { native_error(device,WGPUErrorType_OutOfMemory,"D3D12 texture payload allocation failed"); return NULL; }
    payload->device = device;
    native_device_retain(device);
    doe_resource_texture *texture = NULL;
    native_lock(device);
    native_failure failure = native_failure_status(native_device_result(device));
    if (failure.status == NATIVE_OK) failure = native_settle_pending(device).failure;
#if defined(_WIN32)
    if (failure.status == NATIVE_OK) {
        doe_dx_error detail = {0};
        doe_dx_status created = doe_dx_texture_create(device->backend.value.d3d12.context,&native,&payload->d3d12,&detail);
        failure = native_failure_d3d12(created,&detail);
    }
    if (failure.status == NATIVE_OK) failure = initialize(device,payload->d3d12);
#else
    failure = native_failure_status(NATIVE_UNAVAILABLE);
#endif
    doe_resource_status adopted = DOE_RESOURCE_INVALID_ARGUMENT;
    if (failure.status == NATIVE_OK)
        adopted = doe_resource_texture_create(&device->resources,&desc,&limits,
            (doe_resource_native){payload,NULL,native_texture_release},&texture);
    native_unlock(device);
    if (!texture) {
        native_texture_release(NULL,payload);
        char message[NATIVE_FAILURE_MESSAGE_CAPACITY];
        WGPUErrorType type = failure.status == NATIVE_OK ? native_resource_error(adopted) : native_result_error(failure.status);
        if (failure.status == NATIVE_OK) failure = native_failure_status(NATIVE_FAILURE);
        native_failure_format(&failure,"D3D12 texture creation or proven zero initialization failed",message);
        native_error(device,type,message);
    } else native_label(device,texture,descriptor->label);
    native_device_release(device);
    return (WGPUTexture)texture;
}

WGPUTextureView native_d3d12_create_view(doe_resource_texture *texture,const WGPUTextureViewDescriptor *descriptor)
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
        !desc.mip_count || !desc.layer_count ? DOE_RESOURCE_RANGE : doe_resource_view_validate(texture,&desc,&resolved);
    if (status == DOE_RESOURCE_OK && (resolved.dimension != WGPUTextureViewDimension_2D ||
        resolved.format != doe_resource_texture_description(texture)->format || resolved.aspect != WGPUTextureAspect_All ||
        resolved.base_mip || resolved.mip_count != 1 || resolved.base_layer || resolved.layer_count != 1))
        status = DOE_RESOURCE_UNSUPPORTED;
    native_view *payload = status == DOE_RESOURCE_OK ? calloc(1,sizeof(*payload)) : NULL;
    doe_resource_view *view = NULL;
    if (payload) {
        payload->device = device;
        /* This whole-resource immutable view is the canonical parent span.
         * D3D descriptors are later command-owned; no borrowed COM view escapes. */
        status = doe_resource_view_create(texture,&resolved,(doe_resource_native){payload,NULL,native_view_release},&view);
    } else if (status == DOE_RESOURCE_OK) status = DOE_RESOURCE_OUT_OF_MEMORY;
    native_unlock(device);
    if (!view) {
        if (payload) native_view_release(NULL,payload);
        native_error(device,native_resource_error(status),"D3D12 texture view unsupported or allocation failed");
    } else native_label(device,view,descriptor->label);
    return (WGPUTextureView)view;
}

WGPUSampler native_d3d12_create_sampler(native_device *device,const WGPUSamplerDescriptor *descriptor)
{
    WGPUSamplerDescriptor defaults = WGPU_SAMPLER_DESCRIPTOR_INIT, resolved;
    if (!descriptor) descriptor = &defaults;
    doe_dx_sampler_desc native;
    doe_resource_status status = doe_resource_sampler_validate(descriptor,&resolved);
    if (status != DOE_RESOURCE_OK || !native_d3d12_sampler_descriptor(&resolved,&native)) {
        native_error(device,WGPUErrorType_Validation,"D3D12 sampler settings not composed");
        return NULL;
    }
    native_sampler *payload = calloc(1,sizeof(*payload));
    if (!payload) { native_error(device,WGPUErrorType_OutOfMemory,"D3D12 sampler allocation failed"); return NULL; }
    payload->device = device;
    doe_resource_sampler *sampler = NULL;
    native_lock(device);
    if (!native_device_alive(device)) status = DOE_RESOURCE_STATE;
    else status = doe_resource_sampler_create(&device->resources,&resolved,
        (doe_resource_native){payload,NULL,native_sampler_release},&sampler);
    native_unlock(device);
    if (!sampler) {
        native_sampler_release(NULL,payload);
        native_error(device,native_resource_error(status),"D3D12 sampler allocation failed or device unavailable");
    } else native_label(device,sampler,descriptor->label);
    return (WGPUSampler)sampler;
}
