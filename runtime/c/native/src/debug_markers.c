/* SPDX-License-Identifier: MIT
 * Public marker strings are diagnostics, not retained command data. State and
 * balance belong to each canonical recording owner, with no frontend counters. */
#include "internal.h"
#include "runtime_receipt.h"

/* Push and marker labels go to the opt-in runtime receipt after the owner
 * lock is released; Pop carries no label. */
static void receipt_debug(native_receipt_scope scope, doe_resource_debug_operation operation,
    const WGPUStringView *label, bool valid, doe_resource_status status)
{
    if (!label || !valid || operation == DOE_RESOURCE_DEBUG_POP) return;
    native_receipt_debug(scope, operation == DOE_RESOURCE_DEBUG_MARKER, *label, status == DOE_RESOURCE_OK);
}

static void command_debug(WGPUCommandEncoder handle, doe_resource_debug_operation operation,
    const WGPUStringView *label)
{
    doe_resource_encoder *owner = (doe_resource_encoder *)handle;
    native_device *device = doe_resource_encoder_device(owner);
    if (!device) return;
    size_t size;
    bool valid = !label || native_string_size(*label, &size);
    native_lock(device);
    const bool ended = doe_resource_encoder_ended(owner);
    doe_resource_status status = valid ? doe_resource_encoder_debug(owner, operation) :
        doe_resource_encoder_invalidate(owner, DOE_RESOURCE_INVALID_ARGUMENT);
    native_unlock(device);
    receipt_debug(NATIVE_RECEIPT_COMMAND_ENCODER, operation, label, valid, status);
    native_encoding_error(device, ended, status, "command debug recording failed");
}
void wgpuCommandEncoderInsertDebugMarker(WGPUCommandEncoder handle, WGPUStringView label)
{
    command_debug(handle, DOE_RESOURCE_DEBUG_MARKER, &label);
}
void wgpuCommandEncoderPushDebugGroup(WGPUCommandEncoder handle, WGPUStringView label)
{
    command_debug(handle, DOE_RESOURCE_DEBUG_PUSH, &label);
}
void wgpuCommandEncoderPopDebugGroup(WGPUCommandEncoder handle)
{
    command_debug(handle, DOE_RESOURCE_DEBUG_POP, NULL);
}

static void compute_debug(WGPUComputePassEncoder handle, doe_resource_debug_operation operation,
    const WGPUStringView *label)
{
    doe_resource_compute_pass *owner = (doe_resource_compute_pass *)handle;
    native_device *device = doe_resource_compute_pass_device(owner);
    if (!device) return;
    size_t size;
    bool valid = !label || native_string_size(*label, &size);
    native_lock(device);
    const bool ended = doe_resource_compute_pass_ended(owner);
    doe_resource_status status = valid ? doe_resource_compute_pass_debug(owner, operation) :
        doe_resource_compute_pass_invalidate(owner, DOE_RESOURCE_INVALID_ARGUMENT);
    native_unlock(device);
    receipt_debug(NATIVE_RECEIPT_COMPUTE_PASS, operation, label, valid, status);
    native_encoding_error(device, ended, status, "compute debug recording failed");
}
void wgpuComputePassEncoderInsertDebugMarker(WGPUComputePassEncoder handle, WGPUStringView label)
{
    compute_debug(handle, DOE_RESOURCE_DEBUG_MARKER, &label);
}
void wgpuComputePassEncoderPushDebugGroup(WGPUComputePassEncoder handle, WGPUStringView label)
{
    compute_debug(handle, DOE_RESOURCE_DEBUG_PUSH, &label);
}
void wgpuComputePassEncoderPopDebugGroup(WGPUComputePassEncoder handle)
{
    compute_debug(handle, DOE_RESOURCE_DEBUG_POP, NULL);
}

static void render_debug(WGPURenderPassEncoder handle, doe_resource_debug_operation operation,
    const WGPUStringView *label)
{
    doe_resource_render_pass *owner = (doe_resource_render_pass *)handle;
    native_device *device = doe_resource_render_pass_device(owner);
    if (!device) return;
    size_t size;
    bool valid = !label || native_string_size(*label, &size);
    native_lock(device);
    const bool ended = doe_resource_render_pass_ended(owner);
    doe_resource_status status = valid ? doe_resource_render_pass_debug(owner, operation) :
        doe_resource_render_pass_invalidate(owner, DOE_RESOURCE_INVALID_ARGUMENT);
    native_unlock(device);
    receipt_debug(NATIVE_RECEIPT_RENDER_PASS, operation, label, valid, status);
    native_encoding_error(device, ended, status, "render debug recording failed");
}
void wgpuRenderPassEncoderInsertDebugMarker(WGPURenderPassEncoder handle, WGPUStringView label)
{
    render_debug(handle, DOE_RESOURCE_DEBUG_MARKER, &label);
}
void wgpuRenderPassEncoderPushDebugGroup(WGPURenderPassEncoder handle, WGPUStringView label)
{
    render_debug(handle, DOE_RESOURCE_DEBUG_PUSH, &label);
}
void wgpuRenderPassEncoderPopDebugGroup(WGPURenderPassEncoder handle)
{
    render_debug(handle, DOE_RESOURCE_DEBUG_POP, NULL);
}

static void bundle_debug(WGPURenderBundleEncoder handle, doe_resource_debug_operation operation,
    const WGPUStringView *label)
{
    doe_resource_bundle_encoder *owner = (doe_resource_bundle_encoder *)handle;
    native_device *device = doe_resource_bundle_encoder_device(owner);
    if (!device) return;
    size_t size;
    bool valid = !label || native_string_size(*label, &size);
    native_lock(device);
    const bool ended = doe_resource_bundle_encoder_ended(owner);
    doe_resource_status status = valid ? doe_resource_bundle_encoder_debug(owner, operation) :
        doe_resource_bundle_encoder_invalidate(owner, DOE_RESOURCE_INVALID_ARGUMENT);
    native_unlock(device);
    receipt_debug(NATIVE_RECEIPT_RENDER_BUNDLE, operation, label, valid, status);
    native_encoding_error(device, ended, status, "bundle debug recording failed");
}
void wgpuRenderBundleEncoderInsertDebugMarker(WGPURenderBundleEncoder handle, WGPUStringView label)
{
    bundle_debug(handle, DOE_RESOURCE_DEBUG_MARKER, &label);
}
void wgpuRenderBundleEncoderPushDebugGroup(WGPURenderBundleEncoder handle, WGPUStringView label)
{
    bundle_debug(handle, DOE_RESOURCE_DEBUG_PUSH, &label);
}
void wgpuRenderBundleEncoderPopDebugGroup(WGPURenderBundleEncoder handle)
{
    bundle_debug(handle, DOE_RESOURCE_DEBUG_POP, NULL);
}
