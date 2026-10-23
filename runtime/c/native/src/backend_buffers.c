/* SPDX-License-Identifier: MIT */
#include "internal.h"

native_result native_buffer_create(native_buffer *buffer, uint64_t size, uint32_t usage)
{
    native_backend *backend = &buffer->device->backend;
    if (backend->kind == WGPUBackendType_Vulkan)
        return native_from_vulkan(doe_vk_buffer_create(backend->value.vulkan.context,
            size, usage, DOE_VK_MEMORY_HOST, &buffer->handle.vulkan, NULL));
    if (backend->kind == WGPUBackendType_Metal)
        return native_from_metal(doe_mtl_buffer_create(backend->value.metal.context,
            size, DOE_MTL_SHARED, &buffer->handle.metal, NULL));
    return NATIVE_UNSUPPORTED;
}

void native_buffer_close(native_buffer *buffer)
{
    if (buffer->device->backend.kind == WGPUBackendType_Vulkan) doe_vk_buffer_release(buffer->handle.vulkan);
    else if (buffer->device->backend.kind == WGPUBackendType_Metal) doe_mtl_buffer_release(buffer->handle.metal);
    buffer->handle = (native_buffer_handle){0};
}

native_result native_buffer_read(native_buffer *buffer, uint64_t offset, void *bytes, size_t size)
{
    if (buffer->device->backend.kind == WGPUBackendType_Vulkan)
        return native_from_vulkan(doe_vk_buffer_read(buffer->handle.vulkan, offset, bytes, size));
    if (buffer->device->backend.kind == WGPUBackendType_Metal)
        return native_from_metal(doe_mtl_buffer_read(buffer->handle.metal, offset, bytes, size));
    return NATIVE_UNSUPPORTED;
}

native_result native_buffer_write(native_buffer *buffer, uint64_t offset, const void *bytes, size_t size)
{
    if (buffer->device->backend.kind == WGPUBackendType_Vulkan)
        return native_from_vulkan(doe_vk_buffer_write(buffer->handle.vulkan, offset, bytes, size));
    if (buffer->device->backend.kind == WGPUBackendType_Metal)
        return native_from_metal(doe_mtl_buffer_write(buffer->handle.metal, offset, bytes, size));
    return NATIVE_UNSUPPORTED;
}
