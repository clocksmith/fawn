/* SPDX-License-Identifier: MIT */
#ifndef DOE_NATIVE_BACKEND_H
#define DOE_NATIVE_BACKEND_H
#include "webgpu.h"
#include "doe_native_device_policy.h"
#include "doe_vk.h"
#include "doe_metal.h"
#include "../../backends/d3d12/include/doe_d3d12.h"

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
    uint32_t upload_policy; /* Per-device queue-write mechanism, not adapter mutation. */
    WGPULimits limits; /* Public limits: real facts plus explicit frontend admission bounds. */
    union {
        struct { doe_vk_context *context; doe_vk_caps caps; doe_vk_options options; } vulkan;
        struct { doe_mtl_context *context; doe_mtl_caps caps; } metal;
        struct { doe_dx_context *context; doe_dx_caps caps; doe_dx_options options;
            /* Lazily retained FXC [0] and DXC [1] modules, released after the context. */
            struct doe_dx_shader_compiler *_Atomic compilers[2];
            /* DEFAULT buffers whose zero fill is deferred, under the device lock. */
            struct native_d3d12_buffer *deferred_zero;
            } d3d12;
    } value;
} native_backend;
typedef union native_buffer_handle {
    doe_vk_buffer *vulkan;
    doe_mtl_buffer *metal;
    struct native_d3d12_buffer *d3d12;
} native_buffer_handle;
typedef struct native_command_handle {
    WGPUBackendType kind;
    union {
        doe_vk_commands *vulkan;
        doe_mtl_commands *metal;
        /* Borrowed context is protected by the existing pending device lease. */
        struct { doe_dx_context *context; doe_dx_commands *commands; } d3d12;
    } value;
} native_command_handle;

native_result native_from_vulkan(doe_vk_status status);
native_result native_from_metal(doe_mtl_status status);
native_result native_from_d3d12(doe_dx_status status);
/* Used only by still Vulkan-specific lowering, never a Metal capability claim. */
doe_vk_status native_to_vulkan(native_result status);
WGPUErrorType native_result_error(native_result status);
/* surface_platforms: exact DOE_VK_SURFACE_* bits from a compatibleSurface, else 0.
 * Vulkan requires their complete WSI chain or fails; Metal rejects any bit. */
native_result native_backend_open(WGPUBackendType kind, bool fallback, uint32_t surface_platforms,
    native_backend *out);
/* As native_backend_open, but a Vulkan source adapter with identical surface
 * platforms lends its loader and instance; selection, caps and admission rerun.
 * NULL, another kind or other platforms open their own instance. */
native_result native_backend_open_shared(WGPUBackendType kind, bool fallback, uint32_t surface_platforms,
    const native_backend *source, native_backend *out);
native_result native_backend_open_luid(uint32_t low, int32_t high,
    uint32_t surface_platforms, native_backend *out);
native_result native_backend_device(const native_backend *adapter, native_backend *out);
native_result native_backend_device_policy(const native_backend *adapter, const DoeNativeDevicePolicy *policy, native_backend *out);
void native_backend_close(native_backend *backend);
native_result native_backend_status(const native_backend *backend);
WGPUStatus native_backend_info(const native_backend *backend, WGPUAdapterInfo *out);
WGPUStatus native_vulkan_limits(const doe_vk_caps *caps, WGPULimits *out);
WGPUStatus native_metal_limits(const doe_mtl_caps *caps, WGPULimits *out);
native_failure native_failure_status(native_result status);
native_failure native_failure_vulkan(doe_vk_status status, const doe_vk_error *error);
native_failure native_failure_metal(doe_mtl_status status, const doe_mtl_error *error);
native_failure native_failure_d3d12(doe_dx_status status, const doe_dx_error *error);
/* Output is always terminated. Success formats empty; absent native detail uses
 * the public operation's fallback. Formatting occurs after the device unlock. */
void native_failure_format(const native_failure *failure, const char *fallback,
    char output[NATIVE_FAILURE_MESSAGE_CAPACITY]);
native_failure native_command_create(const native_backend *backend, native_command_handle *out);
native_failure native_command_submit(native_command_handle *commands);
native_failure native_command_wait(native_command_handle *commands);
/* A Vulkan submission the queue accepted and nobody has observed terminal. */
bool native_command_in_flight(const native_command_handle *commands);
native_failure native_command_poll(native_command_handle *commands);
native_failure native_command_recover(native_command_handle *commands);
native_result native_command_release(native_command_handle *commands);
#endif
