/* SPDX-License-Identifier: MIT */
#include "internal.h"

WGPUCommandEncoder wgpuDeviceCreateCommandEncoder(WGPUDevice handle,
    const WGPUCommandEncoderDescriptor *descriptor)
{
    native_device *device = (native_device *)handle;
    if (!device) return NULL;
    doe_resource_encoder *encoder = NULL;
    native_lock(device);
    doe_resource_status status = descriptor && descriptor->nextInChain ? DOE_RESOURCE_UNSUPPORTED :
        doe_resource_encoder_create(&device->resources, &encoder);
    native_unlock(device);
    if (status != DOE_RESOURCE_OK) native_error(device, WGPUErrorType_Validation, "command encoder creation failed");
    if (encoder && descriptor) native_label(device, encoder, descriptor->label);
    return (WGPUCommandEncoder)encoder;
}
void wgpuCommandEncoderAddRef(WGPUCommandEncoder encoder) { doe_resource_encoder_retain((doe_resource_encoder *)encoder); }
void wgpuCommandEncoderRelease(WGPUCommandEncoder handle)
{
    doe_resource_encoder *encoder = (doe_resource_encoder *)handle;
    native_device *device = doe_resource_encoder_device(encoder);
    if (!device) return;
    native_device_retain(device);
    native_lock(device);
    doe_resource_encoder_release(encoder);
    native_unlock(device);
    native_device_release(device);
}

WGPUComputePassEncoder wgpuCommandEncoderBeginComputePass(WGPUCommandEncoder handle,
    const WGPUComputePassDescriptor *descriptor)
{
    doe_resource_encoder *encoder = (doe_resource_encoder *)handle;
    native_device *device = doe_resource_encoder_device(encoder);
    if (!device) return NULL;
    doe_resource_compute_pass *pass = NULL;
    native_lock(device);
    doe_resource_status status = descriptor && (descriptor->nextInChain || descriptor->timestampWrites) ?
        doe_resource_encoder_invalidate(encoder, DOE_RESOURCE_UNSUPPORTED) :
        doe_resource_encoder_begin_compute(encoder, &pass);
    native_unlock(device);
    if (status != DOE_RESOURCE_OK) native_error(device, WGPUErrorType_Validation, "compute pass begin failed");
    if (pass && descriptor) native_label(device, pass, descriptor->label);
    return (WGPUComputePassEncoder)pass;
}
void wgpuComputePassEncoderAddRef(WGPUComputePassEncoder pass) { doe_resource_compute_pass_retain((doe_resource_compute_pass *)pass); }
void wgpuComputePassEncoderRelease(WGPUComputePassEncoder handle)
{
    doe_resource_compute_pass *pass = (doe_resource_compute_pass *)handle;
    native_device *device = doe_resource_compute_pass_device(pass);
    if (!device) return;
    native_device_retain(device);
    native_lock(device);
    doe_resource_compute_pass_release(pass);
    native_unlock(device);
    native_device_release(device);
}
void wgpuComputePassEncoderSetPipeline(WGPUComputePassEncoder handle, WGPUComputePipeline pipeline)
{
    doe_resource_compute_pass *pass = (doe_resource_compute_pass *)handle;
    native_device *device = doe_resource_compute_pass_device(pass);
    if (!device) return;
    native_lock(device);
    doe_resource_status status = doe_resource_compute_pass_set_pipeline(pass, (doe_resource_pipeline *)pipeline);
    native_unlock(device);
    if (status != DOE_RESOURCE_OK) native_error(device, WGPUErrorType_Validation, "compute pipeline binding failed");
}
void wgpuComputePassEncoderSetBindGroup(WGPUComputePassEncoder handle, uint32_t index,
    WGPUBindGroup group, size_t count, const uint32_t *offsets)
{
    doe_resource_compute_pass *pass = (doe_resource_compute_pass *)handle;
    native_device *device = doe_resource_compute_pass_device(pass);
    if (!device) return;
    doe_resource_dispatch_group binding = {(doe_resource_bind_group *)group, offsets, count};
    native_lock(device);
    doe_resource_status status = doe_resource_compute_pass_set_group(pass, index, &binding);
    native_unlock(device);
    if (status != DOE_RESOURCE_OK) native_error(device, WGPUErrorType_Validation, "compute bind group binding failed");
}
void wgpuComputePassEncoderDispatchWorkgroups(WGPUComputePassEncoder handle, uint32_t x, uint32_t y, uint32_t z)
{
    doe_resource_compute_pass *pass = (doe_resource_compute_pass *)handle;
    native_device *device = doe_resource_compute_pass_device(pass);
    if (!device) return;
    native_lock(device);
    /* Recording keeps dimensions; actual Vulkan admission validates its limits
     * before any submission. No duplicate dimension classifier is kept here. */
    doe_resource_status status = doe_resource_compute_pass_dispatch(pass, x, y, z);
    native_unlock(device);
    if (status != DOE_RESOURCE_OK) native_error(device, WGPUErrorType_Validation, "compute dispatch recording failed");
}
void wgpuComputePassEncoderEnd(WGPUComputePassEncoder handle)
{
    doe_resource_compute_pass *pass = (doe_resource_compute_pass *)handle;
    native_device *device = doe_resource_compute_pass_device(pass);
    if (!device) return;
    native_lock(device);
    doe_resource_status status = doe_resource_compute_pass_end(pass);
    native_unlock(device);
    if (status != DOE_RESOURCE_OK) native_error(device, WGPUErrorType_Validation, "compute pass end failed");
}
void wgpuCommandEncoderCopyBufferToBuffer(WGPUCommandEncoder handle, WGPUBuffer source,
    uint64_t source_offset, WGPUBuffer destination, uint64_t destination_offset, uint64_t size)
{
    doe_resource_encoder *encoder = (doe_resource_encoder *)handle;
    native_device *device = doe_resource_encoder_device(encoder);
    if (!device) return;
    doe_resource_buffer_copy copy = {(doe_resource_buffer *)source, (doe_resource_buffer *)destination,
        source_offset, destination_offset, size};
    native_lock(device);
    doe_resource_status status = doe_resource_encoder_copy_buffer(encoder, &copy, DOE_RESOURCE_DISTINCT_BUFFERS);
    native_unlock(device);
    if (status != DOE_RESOURCE_OK) native_error(device, WGPUErrorType_Validation, "buffer copy recording failed");
}
void wgpuCommandEncoderClearBuffer(WGPUCommandEncoder handle, WGPUBuffer buffer, uint64_t offset, uint64_t size)
{
    doe_resource_encoder *encoder = (doe_resource_encoder *)handle;
    native_device *device = doe_resource_encoder_device(encoder);
    if (!device) return;
    native_lock(device);
    doe_resource_status status = doe_resource_encoder_clear_buffer(encoder, (doe_resource_buffer *)buffer, offset, size);
    native_unlock(device);
    if (status != DOE_RESOURCE_OK) native_error(device, WGPUErrorType_Validation, "buffer clear recording failed");
}
WGPUCommandBuffer wgpuCommandEncoderFinish(WGPUCommandEncoder handle, const WGPUCommandBufferDescriptor *descriptor)
{
    doe_resource_encoder *encoder = (doe_resource_encoder *)handle;
    native_device *device = doe_resource_encoder_device(encoder);
    if (!device) return NULL;
    doe_resource_commands *commands = NULL;
    native_lock(device);
    doe_resource_status status = descriptor && descriptor->nextInChain ?
        doe_resource_encoder_invalidate(encoder, DOE_RESOURCE_UNSUPPORTED) :
        doe_resource_encoder_finish(encoder, &commands);
    native_unlock(device);
    if (status != DOE_RESOURCE_OK) native_error(device, WGPUErrorType_Validation, "command encoder finish failed");
    if (commands && descriptor) native_label(device, commands, descriptor->label);
    return (WGPUCommandBuffer)commands;
}
void wgpuCommandBufferAddRef(WGPUCommandBuffer commands) { doe_resource_commands_retain((doe_resource_commands *)commands); }
void wgpuCommandBufferRelease(WGPUCommandBuffer commands) { doe_resource_commands_release((doe_resource_commands *)commands); }
