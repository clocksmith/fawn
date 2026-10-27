/* SPDX-License-Identifier: MIT */
#include "backend.h"
#include "d3d12_frontend.h"
#include "../../backends/metal/src/progress.h"

native_failure native_command_create(const native_backend *backend, native_command_handle *out)
{
    if (backend->kind == WGPUBackendType_D3D12) return native_d3d12_command_create(backend, out);
    native_command_handle result = {.kind=backend->kind};
    native_failure failure;
    if (backend->kind == WGPUBackendType_Vulkan) {
        doe_vk_error error = {0};
        doe_vk_status status = doe_vk_commands_create(backend->value.vulkan.context, &result.value.vulkan, &error);
        failure = native_failure_vulkan(status, &error);
    } else if (backend->kind == WGPUBackendType_Metal) {
        doe_mtl_error error = {0};
        doe_mtl_status status = doe_mtl_commands_create(backend->value.metal.context, &result.value.metal, &error);
        failure = native_failure_metal(status, &error);
    } else return native_failure_status(NATIVE_UNSUPPORTED);
    if (failure.status == NATIVE_OK) *out = result;
    return failure;
}

native_failure native_command_submit(native_command_handle *commands)
{
    if (commands->kind == WGPUBackendType_D3D12) return native_d3d12_command_submit(commands);
    if (commands->kind == WGPUBackendType_Vulkan) {
        doe_vk_error error = {0};
        doe_vk_status status = doe_vk_commands_submit(commands->value.vulkan, &error);
        return native_failure_vulkan(status, &error);
    }
    if (commands->kind == WGPUBackendType_Metal) {
        doe_mtl_error error = {0};
        doe_mtl_status status = doe_mtl_commands_submit(commands->value.metal, &error);
        return native_failure_metal(status, &error);
    }
    return native_failure_status(NATIVE_UNSUPPORTED);
}

native_failure native_command_wait(native_command_handle *commands)
{
    if (commands->kind == WGPUBackendType_D3D12) return native_d3d12_command_progress(commands, 30000);
    if (commands->kind == WGPUBackendType_Vulkan) {
        doe_vk_error error = {0};
        doe_vk_status status = doe_vk_commands_wait(commands->value.vulkan, UINT64_MAX, &error);
        return native_failure_vulkan(status, &error);
    }
    if (commands->kind == WGPUBackendType_Metal) {
        doe_mtl_error error = {0};
        doe_mtl_status status = doe_mtl_commands_wait(commands->value.metal, &error);
        return native_failure_metal(status, &error);
    }
    return native_failure_status(NATIVE_UNSUPPORTED);
}

bool native_command_in_flight(const native_command_handle *commands)
{
    return (commands->kind == WGPUBackendType_Vulkan && doe_vk_commands_in_flight(commands->value.vulkan)) ||
        (commands->kind == WGPUBackendType_Metal && doe_mtl_commands_in_flight(commands->value.metal));
}

native_failure native_command_poll(native_command_handle *commands)
{
    if (commands->kind == WGPUBackendType_D3D12) return native_d3d12_command_progress(commands, 0);
    if (commands->kind == WGPUBackendType_Vulkan) {
        doe_vk_error error = {0};
        doe_vk_status status = doe_vk_commands_poll(commands->value.vulkan, &error);
        return native_failure_vulkan(status, &error);
    }
    if (commands->kind == WGPUBackendType_Metal) {
        doe_mtl_error error = {0};
        doe_mtl_status status = doe_mtl_commands_poll(commands->value.metal, &error);
        return native_failure_metal(status, &error);
    }
    return native_failure_status(NATIVE_UNSUPPORTED);
}

native_failure native_command_recover(native_command_handle *commands)
{
    if (commands->kind == WGPUBackendType_D3D12) return native_d3d12_command_progress(commands, 0);
    if (commands->kind == WGPUBackendType_Vulkan) {
        doe_vk_error error = {0};
        doe_vk_status status = doe_vk_commands_recover(commands->value.vulkan, &error);
        return native_failure_vulkan(status, &error);
    }
    if (commands->kind == WGPUBackendType_Metal) {
        doe_mtl_error error = {0};
        doe_mtl_status status = doe_mtl_commands_wait(commands->value.metal, &error);
        return native_failure_metal(status, &error);
    }
    return native_failure_status(NATIVE_UNSUPPORTED);
}

native_result native_command_release(native_command_handle *commands)
{
    native_result status;
    if (commands->kind == WGPUBackendType_Vulkan)
        status = native_from_vulkan(doe_vk_commands_release(commands->value.vulkan));
    else if (commands->kind == WGPUBackendType_Metal)
        status = native_from_metal(doe_mtl_commands_release(commands->value.metal));
    else if (commands->kind == WGPUBackendType_D3D12)
        status = native_d3d12_command_release(commands);
    else return commands->kind == WGPUBackendType_Undefined ? NATIVE_OK : NATIVE_UNSUPPORTED;
    if (status == NATIVE_OK) *commands = (native_command_handle){0};
    return status;
}
