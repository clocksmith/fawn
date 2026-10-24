/* SPDX-License-Identifier: MIT */
#include "metal_frontend.h"
#include "d3d12_texture.h"

static doe_resource_texture_copy texture_copy(const WGPUTexelCopyTextureInfo *texture,
    const WGPUTexelCopyBufferLayout *layout, const WGPUExtent3D *size)
{
    return (doe_resource_texture_copy){.offset=layout->offset, .bytes_per_row=layout->bytesPerRow,
        .rows_per_image=layout->rowsPerImage, .mip=texture->mipLevel,
        .width=size->width, .height=size->height, .depth_or_layers=size->depthOrArrayLayers,
        .origin={texture->origin.x,texture->origin.y,texture->origin.z}, .aspect=texture->aspect};
}
static void record_copy(WGPUCommandEncoder handle, const WGPUTexelCopyBufferInfo *buffer,
    const WGPUTexelCopyTextureInfo *texture, const WGPUExtent3D *size, doe_resource_copy_direction direction)
{
    doe_resource_encoder *encoder = (doe_resource_encoder *)handle;
    native_device *device = doe_resource_encoder_device(encoder);
    if (!device) return;
    native_lock(device);
    const bool ended = doe_resource_encoder_ended(encoder);
    doe_resource_status status;
    if (!buffer || !texture || !size) status = doe_resource_encoder_invalidate(encoder, DOE_RESOURCE_INVALID_ARGUMENT);
    else {
        doe_resource_texture_copy copy = texture_copy(texture, &buffer->layout, size);
        status = doe_resource_encoder_copy_texture(encoder, (doe_resource_buffer *)buffer->buffer,
            (doe_resource_texture *)texture->texture, &copy, direction, DOE_RESOURCE_COPY_WEBGPU);
    }
    native_unlock(device);
    native_encoding_error(device, ended, status, "buffer/texture copy");
}
void wgpuCommandEncoderCopyBufferToTexture(WGPUCommandEncoder encoder,
    const WGPUTexelCopyBufferInfo *source, const WGPUTexelCopyTextureInfo *destination, const WGPUExtent3D *size)
{
    record_copy(encoder, source, destination, size, DOE_RESOURCE_BUFFER_TO_TEXTURE);
}
void wgpuCommandEncoderCopyTextureToBuffer(WGPUCommandEncoder encoder,
    const WGPUTexelCopyTextureInfo *source, const WGPUTexelCopyBufferInfo *destination, const WGPUExtent3D *size)
{
    record_copy(encoder, destination, source, size, DOE_RESOURCE_TEXTURE_TO_BUFFER);
}
void wgpuCommandEncoderCopyTextureToTexture(WGPUCommandEncoder handle,
    const WGPUTexelCopyTextureInfo *source, const WGPUTexelCopyTextureInfo *destination, const WGPUExtent3D *size)
{
    doe_resource_encoder *encoder = (doe_resource_encoder *)handle;
    native_device *device = doe_resource_encoder_device(encoder);
    if (!device) return;
    native_lock(device);
    const bool ended = doe_resource_encoder_ended(encoder);
    doe_resource_status status;
    if (!source || !destination || !size) status = doe_resource_encoder_invalidate(encoder, DOE_RESOURCE_INVALID_ARGUMENT);
    else {
        doe_resource_texture_blit copy = {
            .source={(doe_resource_texture *)source->texture,source->mipLevel,
                {source->origin.x,source->origin.y,source->origin.z},source->aspect},
            .destination={(doe_resource_texture *)destination->texture,destination->mipLevel,
                {destination->origin.x,destination->origin.y,destination->origin.z},destination->aspect},
            .width=size->width,.height=size->height,.depth_or_layers=size->depthOrArrayLayers};
        status = doe_resource_encoder_blit_texture(encoder, &copy, DOE_RESOURCE_COPY_WEBGPU);
    }
    native_unlock(device);
    native_encoding_error(device, ended, status, "texture copy");
}

void wgpuQueueWriteTexture(WGPUQueue handle, const WGPUTexelCopyTextureInfo *destination,
    const void *data, size_t data_size, const WGPUTexelCopyBufferLayout *layout, const WGPUExtent3D *size)
{
    native_device *device = (native_device *)handle;
    if (!device) return;
    if (!destination || !layout || !size || (!data && data_size)) {
        native_error(device, WGPUErrorType_Validation, "invalid queue texture write arguments");
        return;
    }
    if (device->backend.kind == WGPUBackendType_Metal) {
        native_metal_write_texture(device, destination, data, data_size, layout, size);
        return;
    }
    if (device->backend.kind == WGPUBackendType_D3D12) {
        native_d3d12_write_texture(device, destination, data, data_size, layout, size);
        return;
    }
    native_device_retain(device);
    doe_resource_texture *texture = (doe_resource_texture *)destination->texture;
    doe_resource_texture_copy copy = texture_copy(destination, layout, size);
    doe_resource_copy_region region;
    native_lock(device);
    doe_resource_status status = doe_resource_texture_device(texture) == device ?
        doe_resource_texture_copy_validate(data_size, doe_resource_texture_description(texture), &copy,
            DOE_RESOURCE_BUFFER_TO_TEXTURE, DOE_RESOURCE_COPY_QUEUE, &region) : DOE_RESOURCE_DEVICE_MISMATCH;
    native_unlock(device);
    /* writeTexture data needs no offset or bytesPerRow block alignment: stage
     * the touched block rows tightly at offset 0 (row_bytes pitch, `rows` rows
     * per image), which the native copy can address. */
    const uint64_t first = copy.offset;
    const uint64_t images = copy.depth_or_layers;
    uint64_t bytes = status == DOE_RESOURCE_OK ? (uint64_t)region.row_bytes * region.rows * images : 0;
    const bool tight = status == DOE_RESOURCE_OK && region.pitch == region.row_bytes && region.image_rows == region.rows;
    copy.offset = 0;
    if (status == DOE_RESOURCE_OK) {
        copy.bytes_per_row = region.row_bytes;
        copy.rows_per_image = region.rows;
    }
    if (status != DOE_RESOURCE_OK || bytes > SIZE_MAX - 3) {
        native_error(device, WGPUErrorType_Validation, "queue texture write range invalid");
        native_device_release(device);
        return;
    }
    WGPUBufferDescriptor descriptor = {.size=(bytes + 3) & ~UINT64_C(3),
        .usage=WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst};
    /* A valid write whose staging exceeds the backend allocation limit is out of
     * memory, not a validation error of the request. */
    if (descriptor.size > native_allocation_bound(device)) {
        native_error(device, WGPUErrorType_OutOfMemory, "queue texture write staging exceeds the allocation limit");
        native_device_release(device);
        return;
    }
    /* WebGPU bounds writeTexture by the texture, not maxBufferSize: internal
     * staging keeps only the backend's real allocation limit. */
    WGPUBuffer staging = native_device_create_buffer(device, &descriptor, native_allocation_bound(device));
    doe_resource_encoder *encoder = NULL;
    doe_resource_commands *commands = NULL;
    native_lock(device);
    doe_vk_status written = DOE_VK_OK;
    if (staging) {
        native_buffer *payload = doe_resource_buffer_native((doe_resource_buffer *)staging);
        if (tight || !bytes) written = doe_vk_buffer_write(payload->handle.vulkan, 0,
            bytes ? (const unsigned char *)data + first : data, (size_t)bytes);
        else for (uint64_t image = 0; written == DOE_VK_OK && image < images; ++image)
            for (uint64_t row = 0; written == DOE_VK_OK && row < region.rows; ++row)
                written = doe_vk_buffer_write(payload->handle.vulkan, (image * region.rows + row) * region.row_bytes,
                    (const unsigned char *)data + first + (image * region.image_rows + row) * region.pitch,
                    region.row_bytes);
        status = written == DOE_VK_OK ? doe_resource_encoder_create(&device->resources, &encoder) : DOE_RESOURCE_STATE;
        if (status == DOE_RESOURCE_OK)
            status = doe_resource_encoder_copy_texture(encoder, (doe_resource_buffer *)staging,
                texture, &copy, DOE_RESOURCE_BUFFER_TO_TEXTURE, DOE_RESOURCE_COPY_QUEUE);
        if (status == DOE_RESOURCE_OK) status = doe_resource_encoder_finish(encoder, &commands);
    } else status = DOE_RESOURCE_OUT_OF_MEMORY;
    doe_resource_encoder_release(encoder);
    native_unlock(device);
    if (commands) {
        WGPUCommandBuffer command = (WGPUCommandBuffer)commands;
        wgpuQueueSubmit(handle, 1, &command);
        doe_resource_commands_release(commands);
    } else if (staging) native_error(device, written != DOE_VK_OK ?
        native_backend_error(written) : native_resource_error(status), "queue texture write recording failed");
    wgpuBufferRelease(staging);
    native_device_release(device);
}
