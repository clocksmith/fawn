/* SPDX-License-Identifier: MIT */
#include "internal.h"
#include <string.h>

/* The backend owns guard meaning, private scratch and GPU ordering. Compile its
 * exact generated source, never inspect or snapshot caller argument bytes here. */
static doe_vk_status compile_guard(native_device *device, const char *source,
    size_t length, doe_shader **shader, doe_shader_view *view)
{
    doe_shader_error error = {0};
    doe_shader_status status = native_compile_shader(source,length,shader,&error);
    if (status != DOE_SHADER_OK)
        return status == DOE_SHADER_OUT_OF_MEMORY ? DOE_VK_OUT_OF_MEMORY : DOE_VK_NATIVE_FAILURE;
    if (doe_shader_borrow(*shader,view) != DOE_SHADER_OK ||
        !native_shader_capabilities(view,&device->backend.value.vulkan.caps)) {
        doe_shader_release(*shader);
        *shader = NULL;
        return DOE_VK_NATIVE_FAILURE;
    }
    return DOE_VK_OK;
}

doe_vk_status native_dispatch_guard_initialize(native_device *device)
{
    if (device->backend.kind != WGPUBackendType_Vulkan) return DOE_VK_UNSUPPORTED;
    if (device->dispatch_validator) return DOE_VK_OK;
    uint32_t bound = device->backend.limits.maxComputeWorkgroupsPerDimension;
    uint32_t limits[3] = {bound,bound,bound};
    doe_vk_status status = doe_vk_dispatch_validator_validate(&device->backend.value.vulkan.caps,limits);
    char source[DOE_VK_INDIRECT_SOURCE_CAPACITY];
    size_t length = 0;
    if (status == DOE_VK_OK)
        status = doe_vk_dispatch_validator_source(limits,source,sizeof(source),&length);
    doe_shader *shader = NULL;
    doe_shader_view view;
    if (status == DOE_VK_OK) status = compile_guard(device,source,length,&shader,&view);
    if (status == DOE_VK_OK) {
        doe_vk_dispatch_validator_desc descriptor = {.spirv=view.words,.word_count=view.counts.words};
        memcpy(descriptor.max_workgroup_count,limits,sizeof(limits));
        status = doe_vk_dispatch_validator_create(device->backend.value.vulkan.context,
            &descriptor,&device->dispatch_validator,NULL);
    }
    doe_shader_release(shader);
    return status;
}

doe_vk_status native_draw_guard_initialize(native_device *device)
{
    if (device->backend.kind != WGPUBackendType_Vulkan) return DOE_VK_UNSUPPORTED;
    if (device->draw_validator) return DOE_VK_OK;
    doe_vk_status status = doe_vk_draw_validator_validate(&device->backend.value.vulkan.caps);
    char source[DOE_VK_DRAW_INDIRECT_SOURCE_CAPACITY];
    size_t length = 0;
    if (status == DOE_VK_OK) status = doe_vk_draw_validator_source(source,sizeof(source),&length);
    doe_shader *shader = NULL;
    doe_shader_view view;
    if (status == DOE_VK_OK) status = compile_guard(device,source,length,&shader,&view);
    if (status == DOE_VK_OK)
        status = doe_vk_draw_validator_create(device->backend.value.vulkan.context,
            view.words,view.counts.words,&device->draw_validator,NULL);
    doe_shader_release(shader);
    return status;
}

void native_indirect_guards_release(native_device *device)
{
    doe_vk_dispatch_validator_release(device->dispatch_validator);
    doe_vk_draw_validator_release(device->draw_validator);
    device->dispatch_validator = NULL;
    device->draw_validator = NULL;
}

void wgpuComputePassEncoderDispatchWorkgroupsIndirect(WGPUComputePassEncoder handle,
    WGPUBuffer arguments, uint64_t offset)
{
    doe_resource_compute_pass *pass = (doe_resource_compute_pass *)handle;
    native_device *device = doe_resource_compute_pass_device(pass);
    if (!device) return;
    native_lock(device);
    const bool ended = doe_resource_compute_pass_ended(pass);
    doe_resource_status status = device->backend.kind == WGPUBackendType_Vulkan ?
        doe_resource_compute_pass_dispatch_indirect(pass,(doe_resource_buffer *)arguments,offset) :
        doe_resource_compute_pass_invalidate(pass,DOE_RESOURCE_UNSUPPORTED);
    native_unlock(device);
    native_encoding_error(device, ended, status, "indirect dispatch admission failed or selected backend unsupported");
}

static void pass_indirect(WGPURenderPassEncoder handle, WGPUBuffer arguments, uint64_t offset, bool indexed)
{
    doe_resource_render_pass *pass = (doe_resource_render_pass *)handle;
    native_device *device = doe_resource_render_pass_device(pass);
    if (!device) return;
    native_lock(device);
    const bool ended = doe_resource_render_pass_ended(pass);
    doe_resource_status status = device->backend.kind == WGPUBackendType_Vulkan ?
        doe_resource_render_pass_draw_indirect(pass,(doe_resource_buffer *)arguments,offset,indexed) :
        doe_resource_render_pass_invalidate(pass,DOE_RESOURCE_UNSUPPORTED);
    native_unlock(device);
    native_encoding_error(device, ended, status, "indirect draw admission failed or selected backend unsupported");
}

static void bundle_indirect(WGPURenderBundleEncoder handle, WGPUBuffer arguments, uint64_t offset, bool indexed)
{
    doe_resource_bundle_encoder *encoder = (doe_resource_bundle_encoder *)handle;
    native_device *device = doe_resource_bundle_encoder_device(encoder);
    if (!device) return;
    native_lock(device);
    doe_resource_status status = device->backend.kind == WGPUBackendType_Vulkan ?
        doe_resource_bundle_encoder_draw_indirect(encoder,(doe_resource_buffer *)arguments,offset,indexed) :
        doe_resource_bundle_encoder_invalidate(encoder,DOE_RESOURCE_UNSUPPORTED);
    native_unlock(device);
    if (status != DOE_RESOURCE_OK) native_error(device,native_resource_error(status),
        "indirect bundle draw admission failed or selected backend unsupported");
}

void wgpuRenderPassEncoderDrawIndirect(WGPURenderPassEncoder handle, WGPUBuffer arguments, uint64_t offset)
{
    pass_indirect(handle,arguments,offset,false);
}

void wgpuRenderPassEncoderDrawIndexedIndirect(WGPURenderPassEncoder handle, WGPUBuffer arguments, uint64_t offset)
{
    pass_indirect(handle,arguments,offset,true);
}

void wgpuRenderBundleEncoderDrawIndirect(WGPURenderBundleEncoder handle, WGPUBuffer arguments, uint64_t offset)
{
    bundle_indirect(handle,arguments,offset,false);
}

void wgpuRenderBundleEncoderDrawIndexedIndirect(WGPURenderBundleEncoder handle, WGPUBuffer arguments, uint64_t offset)
{
    bundle_indirect(handle,arguments,offset,true);
}
