//! Private per-device opt-out. Shader validation and memory caches do not reserve
//! persistence; disk owners serialize their actual attempts with this policy.
const abi = @import("../../core/abi/wgpu_core_base_types.zig");
const objects = @import("../support/doe_native_object_types.zig");
const helpers = @import("../support/doe_native_object_helpers.zig");

pub export fn doeNativeDeviceDisablePersistentCache(raw: ?*anyopaque) callconv(.c) abi.WGPUStatus {
    const dev = helpers.cast(objects.DoeDevice, raw) orelse return abi.WGPUStatus_Error;
    if (dev.isDestroyed()) return abi.WGPUStatus_Error;
    return if (dev.cache_policy.disable()) abi.WGPUStatus_Success else abi.WGPUStatus_Error;
}

pub fn reservePersistence(dev: *objects.DoeDevice) bool {
    return dev.cache_policy.reserve();
}

pub fn persistenceAllowed(dev: *objects.DoeDevice) bool {
    return dev.cache_policy.persistenceAllowed();
}
