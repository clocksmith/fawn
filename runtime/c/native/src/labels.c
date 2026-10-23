/* SPDX-License-Identifier: MIT */
#include "internal.h"

void native_label(native_device *device, const void *identity, WGPUStringView label)
{
    if (!device || !identity) return;
    size_t size;
    if (!native_string_size(label, &size)) {
        native_error(device, WGPUErrorType_Validation, "invalid label string view");
        return;
    }
    native_lock(device);
    doe_label_status status = doe_label_store_set(device->labels, identity,
        (const unsigned char *)label.data, size);
    native_unlock(device);
    if (status != DOE_LABEL_OK) native_error(device, WGPUErrorType_OutOfMemory, "label storage failed");
}
void wgpuDeviceSetLabel(WGPUDevice device, WGPUStringView label)
{
    native_label((native_device *)device, device, label);
}
void wgpuQueueSetLabel(WGPUQueue handle, WGPUStringView label)
{
    native_device *device = (native_device *)handle;
    if (device) native_label(device, &device->queue_label_key, label);
}
void wgpuBufferSetLabel(WGPUBuffer buffer, WGPUStringView label)
{
    native_label(doe_resource_buffer_device((doe_resource_buffer *)buffer), buffer, label);
}
void wgpuShaderModuleSetLabel(WGPUShaderModule shader, WGPUStringView label)
{
    native_label(doe_resource_shader_device((doe_resource_shader *)shader), shader, label);
}
void wgpuBindGroupLayoutSetLabel(WGPUBindGroupLayout layout, WGPUStringView label)
{
    native_label(doe_resource_layout_device((doe_resource_layout *)layout), layout, label);
}
void wgpuBindGroupSetLabel(WGPUBindGroup group, WGPUStringView label)
{
    native_label(doe_resource_bind_group_device((doe_resource_bind_group *)group), group, label);
}
void wgpuPipelineLayoutSetLabel(WGPUPipelineLayout layout, WGPUStringView label)
{
    native_label(doe_resource_pipeline_layout_device((doe_resource_pipeline_layout *)layout), layout, label);
}
void wgpuComputePipelineSetLabel(WGPUComputePipeline pipeline, WGPUStringView label)
{
    native_label(doe_resource_pipeline_device((doe_resource_pipeline *)pipeline), pipeline, label);
}
void wgpuCommandEncoderSetLabel(WGPUCommandEncoder encoder, WGPUStringView label)
{
    native_label(doe_resource_encoder_device((doe_resource_encoder *)encoder), encoder, label);
}
void wgpuComputePassEncoderSetLabel(WGPUComputePassEncoder pass, WGPUStringView label)
{
    native_label(doe_resource_compute_pass_device((doe_resource_compute_pass *)pass), pass, label);
}
void wgpuCommandBufferSetLabel(WGPUCommandBuffer commands, WGPUStringView label)
{
    native_label(doe_resource_commands_device((doe_resource_commands *)commands), commands, label);
}
