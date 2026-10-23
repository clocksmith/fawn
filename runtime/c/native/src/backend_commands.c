/* SPDX-License-Identifier: MIT */
#include "backend.h"

native_result native_command_create(const native_backend *backend, native_command_handle *out)
{
    native_command_handle result = {.kind=backend->kind};
    native_result status;
    if (backend->kind == WGPUBackendType_Vulkan)
        status = native_from_vulkan(doe_vk_commands_create(backend->value.vulkan.context, &result.value.vulkan, NULL));
    else if (backend->kind == WGPUBackendType_Metal)
        status = native_from_metal(doe_mtl_commands_create(backend->value.metal.context, &result.value.metal, NULL));
    else return NATIVE_UNSUPPORTED;
    if (status == NATIVE_OK) *out = result;
    return status;
}

native_result native_command_submit(native_command_handle *commands)
{
    if (commands->kind == WGPUBackendType_Vulkan)
        return native_from_vulkan(doe_vk_commands_submit(commands->value.vulkan, NULL));
    if (commands->kind == WGPUBackendType_Metal)
        return native_from_metal(doe_mtl_commands_submit(commands->value.metal, NULL));
    return NATIVE_UNSUPPORTED;
}

native_result native_command_wait(native_command_handle *commands)
{
    if (commands->kind == WGPUBackendType_Vulkan)
        return native_from_vulkan(doe_vk_commands_wait(commands->value.vulkan, UINT64_MAX, NULL));
    if (commands->kind == WGPUBackendType_Metal)
        return native_from_metal(doe_mtl_commands_wait(commands->value.metal, NULL));
    return NATIVE_UNSUPPORTED;
}

native_result native_command_poll(native_command_handle *commands)
{
    if (commands->kind == WGPUBackendType_Vulkan)
        return native_from_vulkan(doe_vk_commands_poll(commands->value.vulkan, NULL));
    if (commands->kind == WGPUBackendType_Metal)
        return native_from_metal(doe_mtl_commands_poll(commands->value.metal, NULL));
    return NATIVE_UNSUPPORTED;
}

native_result native_command_recover(native_command_handle *commands)
{
    if (commands->kind == WGPUBackendType_Vulkan)
        return native_from_vulkan(doe_vk_commands_recover(commands->value.vulkan, NULL));
    if (commands->kind == WGPUBackendType_Metal)
        return native_from_metal(doe_mtl_commands_wait(commands->value.metal, NULL));
    return NATIVE_UNSUPPORTED;
}

native_result native_command_release(native_command_handle *commands)
{
    native_result status;
    if (commands->kind == WGPUBackendType_Vulkan)
        status = native_from_vulkan(doe_vk_commands_release(commands->value.vulkan));
    else if (commands->kind == WGPUBackendType_Metal)
        status = native_from_metal(doe_mtl_commands_release(commands->value.metal));
    else return commands->kind == WGPUBackendType_Undefined ? NATIVE_OK : NATIVE_UNSUPPORTED;
    if (status == NATIVE_OK) *commands = (native_command_handle){0};
    return status;
}
