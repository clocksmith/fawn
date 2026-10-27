//! One-way per-device persistence policy. Called before publishing a browser
//! device; shader/pipeline admission freezes the policy before any cache access.
const builtin = @import("builtin");
const abi = @import("../../core/abi/wgpu_core_base_types.zig");
const objects = @import("../support/doe_native_object_types.zig");
const helpers = @import("../support/doe_native_object_helpers.zig");
const runtimes = @import("../support/doe_native_runtime_helpers.zig");

pub export fn doeNativeDeviceDisablePersistentCache(raw: ?*anyopaque) callconv(.c) abi.WGPUStatus {
    const dev = helpers.cast(objects.DoeDevice, raw) orelse return abi.WGPUStatus_Error;
    dev.cache_policy_mutex.lock();
    defer dev.cache_policy_mutex.unlock();
    if (dev.isDestroyed()) return abi.WGPUStatus_Error;
    if (dev.persistent_cache_disabled) return abi.WGPUStatus_Success;
    if (dev.shader_cache_started) return abi.WGPUStatus_Error;
    if (dev.metal_pipeline_cache != null) return abi.WGPUStatus_Error;
    // Native Vulkan construction defers cache creation. A preexisting cache
    // means its input may already have been read, so never claim isolation.
    if (comptime builtin.os.tag == .linux) {
        if (runtimes.device_vk_runtime(dev)) |rt| {
            if (!rt.pipeline_cache.deferred) return abi.WGPUStatus_Error;
            rt.pipeline_cache.persistent = false;
        }
    }
    dev.persistent_cache_disabled = true;
    return abi.WGPUStatus_Success;
}

/// Serializes first use against disable. Backend cache setup happens while the
/// policy lock is held; no caller can report successful disable after that use.
pub fn beginUse(dev: *objects.DoeDevice) void {
    dev.cache_policy_mutex.lock();
    defer dev.cache_policy_mutex.unlock();
    if (dev.shader_cache_started) return;
    dev.shader_cache_started = true;
    if (comptime builtin.os.tag == .linux) {
        if (runtimes.device_vk_runtime(dev)) |rt| {
            rt.pipeline_cache.deferred = false;
            rt.pipeline_cache.create(rt.device) catch {};
        }
    }
}

/// After admission the policy is immutable. This also permits the lazy Metal
/// archive owner to inspect it before creating a memory-only archive.
pub fn persistenceAllowed(dev: *objects.DoeDevice) bool {
    dev.cache_policy_mutex.lock();
    defer dev.cache_policy_mutex.unlock();
    return !dev.persistent_cache_disabled;
}
