/* SPDX-License-Identifier: MIT */
#ifndef DOE_NATIVE_BACKEND_H
#define DOE_NATIVE_BACKEND_H
#include "webgpu.h"
#include "doe_vk.h"
#include "doe_metal.h"

/* Typed conversion only: backend objects keep their existing ownership. These
 * unions add no reference count, command state, capability inference or retry. */
typedef enum native_result {
    NATIVE_OK, NATIVE_PENDING, NATIVE_UNKNOWN, NATIVE_TIMEOUT,
    NATIVE_INVALID_ARGUMENT, NATIVE_OUT_OF_MEMORY, NATIVE_UNAVAILABLE,
    NATIVE_UNSUPPORTED, NATIVE_FAILURE, NATIVE_DEVICE_LOST, NATIVE_BUSY,
    NATIVE_INVALID_STATE, NATIVE_SURFACE_OUTDATED, NATIVE_SURFACE_LOST
} native_result;
/* A value snapshot, never another command state or status vocabulary. Backend
 * command owners preserve their first native failure; this carries its detail
 * across settlement/release without borrowing a native string or callback. */
#define NATIVE_FAILURE_MESSAGE_CAPACITY 512u
typedef struct native_failure {
    native_result status;
    WGPUBackendType backend;
    int64_t native_code;
    char operation[128];
    char message[256];
} native_failure;
typedef struct native_backend {
    WGPUBackendType kind;
    WGPULimits limits; /* Public limits: real facts plus explicit frontend admission bounds. */
    union {
        struct { doe_vk_context *context; doe_vk_caps caps; doe_vk_options options; } vulkan;
        struct { doe_mtl_context *context; doe_mtl_caps caps; } metal;
    } value;
} native_backend;
typedef union native_buffer_handle {
    doe_vk_buffer *vulkan;
    doe_mtl_buffer *metal;
} native_buffer_handle;
typedef struct native_command_handle {
    WGPUBackendType kind;
    union { doe_vk_commands *vulkan; doe_mtl_commands *metal; } value;
} native_command_handle;

native_result native_from_vulkan(doe_vk_status status);
native_result native_from_metal(doe_mtl_status status);
/* Used only by still Vulkan-specific lowering, never a Metal capability claim. */
doe_vk_status native_to_vulkan(native_result status);
WGPUErrorType native_result_error(native_result status);
/* surface_platforms: exact DOE_VK_SURFACE_* bits from a compatibleSurface, else 0.
 * Vulkan requires their complete WSI chain or fails; Metal rejects any bit. */
native_result native_backend_open(WGPUBackendType kind, bool fallback, uint32_t surface_platforms,
    native_backend *out);
native_result native_backend_device(const native_backend *adapter, native_backend *out);
void native_backend_close(native_backend *backend);
native_result native_backend_status(const native_backend *backend);
WGPUStatus native_backend_info(const native_backend *backend, WGPUAdapterInfo *out);
WGPUStatus native_vulkan_limits(const doe_vk_caps *caps, WGPULimits *out);
native_failure native_failure_status(native_result status);
native_failure native_failure_vulkan(doe_vk_status status, const doe_vk_error *error);
native_failure native_failure_metal(doe_mtl_status status, const doe_mtl_error *error);
/* Output is always terminated. Success formats empty; absent native detail uses
 * the public operation's fallback. Formatting occurs after the device unlock. */
void native_failure_format(const native_failure *failure, const char *fallback,
    char output[NATIVE_FAILURE_MESSAGE_CAPACITY]);
native_failure native_command_create(const native_backend *backend, native_command_handle *out);
native_failure native_command_submit(native_command_handle *commands);
native_failure native_command_wait(native_command_handle *commands);
native_failure native_command_poll(native_command_handle *commands);
native_failure native_command_recover(native_command_handle *commands);
native_result native_command_release(native_command_handle *commands);
#endif
