/* SPDX-License-Identifier: MIT */
#include "internal.h"
#include "d3d12_frontend.h"

native_failure native_buffer_create(native_buffer *buffer, uint64_t size, uint32_t usage)
{
    native_backend *backend = &buffer->device->backend;
    if (backend->kind == WGPUBackendType_Vulkan) {
        doe_vk_error error = {0};
        doe_vk_status status = doe_vk_buffer_create(backend->value.vulkan.context,
            size, usage, DOE_VK_MEMORY_HOST, &buffer->handle.vulkan, &error);
        return native_failure_vulkan(status, &error);
    }
    if (backend->kind == WGPUBackendType_Metal) {
        doe_mtl_error error = {0};
        doe_mtl_status status = doe_mtl_buffer_create(backend->value.metal.context,
            size, DOE_MTL_SHARED, &buffer->handle.metal, &error);
        return native_failure_metal(status, &error);
    }
    if (backend->kind == WGPUBackendType_D3D12) return native_d3d12_buffer_create(buffer, size, usage);
    return native_failure_status(NATIVE_UNSUPPORTED);
}

void native_buffer_close(native_buffer *buffer)
{
    if (buffer->device->backend.kind == WGPUBackendType_Vulkan) doe_vk_buffer_release(buffer->handle.vulkan);
    else if (buffer->device->backend.kind == WGPUBackendType_Metal) doe_mtl_buffer_release(buffer->handle.metal);
    else if (buffer->device->backend.kind == WGPUBackendType_D3D12) native_d3d12_buffer_close(buffer);
    buffer->handle = (native_buffer_handle){0};
}

native_failure native_buffer_read(native_buffer *buffer, uint64_t offset, void *bytes, size_t size)
{
    if (buffer->device->backend.kind == WGPUBackendType_Vulkan)
        return native_failure_vulkan(doe_vk_buffer_read(buffer->handle.vulkan, offset, bytes, size), NULL);
    if (buffer->device->backend.kind == WGPUBackendType_Metal)
        return native_failure_metal(doe_mtl_buffer_read(buffer->handle.metal, offset, bytes, size), NULL);
    if (buffer->device->backend.kind == WGPUBackendType_D3D12)
        return native_d3d12_buffer_read(buffer, offset, bytes, size);
    return native_failure_status(NATIVE_UNSUPPORTED);
}

native_failure native_buffer_write(native_buffer *buffer, uint64_t offset, const void *bytes, size_t size)
{
    if (buffer->device->backend.kind == WGPUBackendType_Vulkan)
        return native_failure_vulkan(doe_vk_buffer_write(buffer->handle.vulkan, offset, bytes, size), NULL);
    if (buffer->device->backend.kind == WGPUBackendType_Metal)
        return native_failure_metal(doe_mtl_buffer_write(buffer->handle.metal, offset, bytes, size), NULL);
    if (buffer->device->backend.kind == WGPUBackendType_D3D12)
        return native_d3d12_buffer_write(buffer, offset, bytes, size);
    return native_failure_status(NATIVE_UNSUPPORTED);
}
