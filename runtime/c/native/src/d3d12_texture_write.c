/* SPDX-License-Identifier: MIT */
#include "d3d12_texture.h"
#include "d3d12_texture_desc.h"
#include <stdlib.h>
#include <string.h>

void native_d3d12_write_texture(native_device *device,const WGPUTexelCopyTextureInfo *destination,
    const void *data,size_t data_size,const WGPUTexelCopyBufferLayout *layout,const WGPUExtent3D *size)
{
    native_device_retain(device);
    doe_resource_texture *texture = (doe_resource_texture *)destination->texture;
    doe_resource_texture_copy copy = {.offset=layout->offset,.bytes_per_row=layout->bytesPerRow,
        .rows_per_image=layout->rowsPerImage,.mip=destination->mipLevel,.width=size->width,
        .height=size->height,.depth_or_layers=size->depthOrArrayLayers,
        .origin={destination->origin.x,destination->origin.y,destination->origin.z},.aspect=destination->aspect};
    doe_resource_copy_region admitted = {0};
    native_lock(device);
    doe_resource_status status = doe_resource_texture_device(texture) != device ? DOE_RESOURCE_DEVICE_MISMATCH :
        !doe_resource_texture_available(texture) ? DOE_RESOURCE_UNAVAILABLE :
        doe_resource_texture_copy_validate(data_size,doe_resource_texture_description(texture),&copy,
            DOE_RESOURCE_BUFFER_TO_TEXTURE,DOE_RESOURCE_COPY_QUEUE,&admitted);
    if (status == DOE_RESOURCE_OK) doe_resource_texture_retain(texture);
    native_unlock(device);
    if (status != DOE_RESOURCE_OK) {
        native_error(device,native_resource_error(status),"D3D12 queue texture write range invalid");
        native_device_release(device);
        return;
    }
    /* Canonical recording below checks usage/availability even for zero writes. */
    uint64_t pitch = ((uint64_t)admitted.row_bytes + 255) & ~UINT64_C(255);
    bool nonempty = copy.width && copy.height && copy.depth_or_layers;
    uint64_t bytes = nonempty ? pitch * admitted.rows : 0;
    uint64_t bound = native_allocation_bound(device);
    if (copy.depth_or_layers > 1 || copy.mip || copy.origin[2] || pitch > UINT32_MAX ||
        bytes > SIZE_MAX || bytes > bound) {
        native_error(device,bytes > bound || bytes > SIZE_MAX ? WGPUErrorType_OutOfMemory : WGPUErrorType_Validation,
            "D3D12 queue texture write shape or staging not composed");
        doe_resource_texture_release(texture);
        native_device_release(device);
        return;
    }
    /* A zero transfer still needs a real resource packet for object admission.
     * The staging allocation is nonzero; no caller byte is read. */
    uint64_t allocation = bytes ? bytes : 4;
    unsigned char *packed = calloc(1,(size_t)allocation);
    if (!packed) {
        native_error(device,WGPUErrorType_OutOfMemory,"D3D12 queue texture write staging allocation failed");
        doe_resource_texture_release(texture);
        native_device_release(device);
        return;
    }
    const unsigned char *source = data;
    for (uint32_t row = 0; nonempty && row < admitted.rows; ++row)
        memcpy(packed + (uint64_t)row * pitch,
            source + copy.offset + (uint64_t)row * admitted.pitch,admitted.row_bytes);
    WGPUBufferDescriptor descriptor = {.size=allocation,.usage=WGPUBufferUsage_CopySrc|WGPUBufferUsage_CopyDst};
    WGPUBuffer staging = native_device_create_buffer(device,&descriptor,bound);
    doe_resource_encoder *encoder = NULL;
    doe_resource_commands *commands = NULL;
    native_lock(device);
    native_failure written = native_failure_status(NATIVE_OK);
    if (staging) {
        native_buffer *payload = doe_resource_buffer_native((doe_resource_buffer *)staging);
        written = native_buffer_write(payload,0,packed,(size_t)allocation);
        status = written.status == NATIVE_OK ? doe_resource_encoder_create(&device->resources,&encoder) : DOE_RESOURCE_STATE;
        copy.offset = 0;
        copy.bytes_per_row = (uint32_t)pitch;
        copy.rows_per_image = admitted.rows;
        copy.aspect = admitted.aspect;
        if (status == DOE_RESOURCE_OK)
            status = doe_resource_encoder_copy_texture(encoder,(doe_resource_buffer *)staging,texture,
                &copy,DOE_RESOURCE_BUFFER_TO_TEXTURE,DOE_RESOURCE_COPY_NATIVE);
        if (status == DOE_RESOURCE_OK) status = doe_resource_encoder_finish(encoder,&commands);
    } else status = DOE_RESOURCE_OUT_OF_MEMORY;
    doe_resource_encoder_release(encoder);
    native_unlock(device);
    free(packed);
    if (commands) {
        WGPUCommandBuffer command = (WGPUCommandBuffer)commands;
        wgpuQueueSubmit((WGPUQueue)device,1,&command);
        doe_resource_commands_release(commands);
    } else if (staging) {
        char message[NATIVE_FAILURE_MESSAGE_CAPACITY];
        WGPUErrorType type = written.status == NATIVE_OK ? native_resource_error(status) : native_result_error(written.status);
        if (written.status == NATIVE_OK) written = native_failure_status(NATIVE_INVALID_ARGUMENT);
        native_failure_format(&written,"D3D12 queue texture write recording failed",message);
        native_error(device,type,message);
    }
    wgpuBufferRelease(staging);
    doe_resource_texture_release(texture);
    native_device_release(device);
}
