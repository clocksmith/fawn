//! CPU diagnostic topology, never a successful GPU/backend RequestDevice.
//! Linked only by the selected tests and separate test-only native library.
const objects = @import("../../src/native/support/doe_native_object_types.zig");
const helpers = @import("../../src/native/support/doe_native_object_helpers.zig");
const native = @import("../../src/native/lifecycle/doe_instance_device_native.zig");
const scopes = @import("../../src/native/support/doe_error_scope_owner.zig");
const storage = @import("../../src/native/command/doe_command_storage.zig");
const build_options = @import("build_options");
const abi = @import("../../src/core/abi/wgpu_handle_types.zig");

/// Owns one external device reference. The instance output is borrowed from the
/// adapter retained by the device. Use real native AddRef to extend that borrow.
pub export fn doeTestErrorScopesCreateDevice(instance_out: ?*abi.WGPUInstance) callconv(.c) abi.WGPUDevice {
    if (instance_out) |out| out.* = null;
    const instance_raw = native.doeNativeCreateInstance(null) orelse return null;
    const instance = helpers.cast(objects.DoeInstance, instance_raw).?;
    const adapter = helpers.make(objects.DoeAdapter) orelse {
        native.doeNativeInstanceRelease(instance_raw);
        return null;
    };
    // Transfer the created instance's owned reference into the real adapter.
    adapter.* = .{ .instance = instance, .backend = .vulkan };
    const device = helpers.make(objects.DoeDevice) orelse {
        native.doeNativeAdapterRelease(helpers.toOpaque(adapter));
        return null;
    };
    device.* = .{
        .adapter = adapter,
        .backend = .vulkan,
        .command_storage = storage.Pool.init(helpers.alloc, build_options.native_command_storage_max_retained_bytes),
    };
    // No whole-object assignment is permitted after this exact stable address.
    scopes.initialize(&device.error_scopes) catch {
        native.doeNativeDeviceReleaseInternal(helpers.toOpaque(device));
        return null;
    };
    if (instance_out) |out| out.* = @ptrCast(instance_raw);
    return @ptrCast(device);
}
