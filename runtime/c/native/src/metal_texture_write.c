/* SPDX-License-Identifier: MIT */
#include "metal_frontend.h"
#include <stdlib.h>
#include <string.h>

void native_metal_write_texture(native_device *device, const WGPUTexelCopyTextureInfo *destination,
    const void *data, size_t data_size, const WGPUTexelCopyBufferLayout *layout, const WGPUExtent3D *size)
{
    native_device_retain(device);
    doe_resource_texture *texture = (doe_resource_texture *)destination->texture;
    doe_resource_texture_copy copy = {.offset=layout->offset, .bytes_per_row=layout->bytesPerRow,
        .rows_per_image=layout->rowsPerImage, .mip=destination->mipLevel, .width=size->width,
        .height=size->height, .depth_or_layers=size->depthOrArrayLayers,
        .origin={destination->origin.x,destination->origin.y,destination->origin.z}, .aspect=destination->aspect};
    doe_resource_copy_region region = {0};
    native_lock(device);
    doe_resource_status status = doe_resource_texture_write_validate(device, texture, data_size, &copy, &region);
    if (status == DOE_RESOURCE_OK) doe_resource_texture_retain(texture);
    native_unlock(device);
    if (status != DOE_RESOURCE_OK) {
        native_error(device, native_resource_error(status), "Metal queue texture write range or format invalid");
        native_device_release(device);
        return;
    }
    /* Admission includes usage for empty writes, without staging or GPU work. */
    if (!copy.width || !copy.height || !copy.depth_or_layers) {
        doe_resource_texture_release(texture);
        native_device_release(device);
        return;
    }
    uint64_t row_bytes = region.row_bytes;
    uint64_t pitch = (row_bytes + 255) / 256 * 256;
    const uint64_t allocation_bound = native_allocation_bound(device);
    if (pitch > UINT32_MAX || region.rows > SIZE_MAX / pitch) {
        native_error(device, WGPUErrorType_OutOfMemory, "Metal queue texture write staging extent exceeds limit");
        doe_resource_texture_release(texture);
        native_device_release(device);
        return;
    }
    uint64_t bytes = pitch * region.rows;
    if (copy.depth_or_layers > SIZE_MAX / bytes || bytes * copy.depth_or_layers > allocation_bound) {
        native_error(device, WGPUErrorType_OutOfMemory, "Metal queue texture write staging extent exceeds limit");
        doe_resource_texture_release(texture);
        native_device_release(device);
        return;
    }
    bytes *= copy.depth_or_layers;
    unsigned char *packed = calloc(1, (size_t)bytes);
    if (!packed) {
        native_error(device, WGPUErrorType_OutOfMemory, "Metal queue texture write staging allocation failed");
        doe_resource_texture_release(texture);
        native_device_release(device);
        return;
    }
    const unsigned char *source = data;
    for (uint32_t z = 0; z < copy.depth_or_layers; ++z)
        for (uint32_t y = 0; y < region.rows; ++y)
            memcpy(packed + pitch * ((uint64_t)z * region.rows + y),
                source + copy.offset + (uint64_t)region.pitch * ((uint64_t)z * region.image_rows + y), (size_t)row_bytes);
    WGPUBufferDescriptor descriptor = {.size=bytes, .usage=WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst};
    /* Caller data has queue-write layout rules; this private aligned copy is
     * bounded by native allocation capacity, not public device maxBufferSize. */
    WGPUBuffer staging = native_device_create_buffer(device, &descriptor, allocation_bound);
    doe_resource_encoder *encoder = NULL;
    doe_resource_commands *commands = NULL;
    native_lock(device);
    native_failure written = native_failure_status(NATIVE_OK);
    if (staging) {
        native_buffer *payload = doe_resource_buffer_native((doe_resource_buffer *)staging);
        written = native_buffer_write(payload, 0, packed, (size_t)bytes);
        status = written.status == NATIVE_OK ? doe_resource_encoder_create(&device->resources, &encoder) : DOE_RESOURCE_STATE;
        copy.offset = 0;
        copy.bytes_per_row = (uint32_t)pitch;
        copy.rows_per_image = region.rows;
        if (status == DOE_RESOURCE_OK) status = doe_resource_encoder_copy_texture(encoder,
            (doe_resource_buffer *)staging, texture, &copy, DOE_RESOURCE_BUFFER_TO_TEXTURE, DOE_RESOURCE_COPY_NATIVE);
        if (status == DOE_RESOURCE_OK) status = doe_resource_encoder_finish(encoder, &commands);
    } else status = DOE_RESOURCE_OUT_OF_MEMORY;
    doe_resource_encoder_release(encoder);
    native_unlock(device);
    free(packed);
    if (commands) {
        WGPUCommandBuffer command = (WGPUCommandBuffer)commands;
        wgpuQueueSubmit((WGPUQueue)device, 1, &command);
        doe_resource_commands_release(commands);
    } else if (staging) {
        WGPUErrorType type = written.status == NATIVE_OK ? native_resource_error(status) :
            native_result_error(written.status);
        char message[NATIVE_FAILURE_MESSAGE_CAPACITY];
        if (written.status == NATIVE_OK) written = native_failure_status(NATIVE_INVALID_STATE);
        native_failure_format(&written, "Metal queue texture write recording failed", message);
        native_error(device, type, message);
    }
    wgpuBufferRelease(staging);
    doe_resource_texture_release(texture);
    native_device_release(device);
}
