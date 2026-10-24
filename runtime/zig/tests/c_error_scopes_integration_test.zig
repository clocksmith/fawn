//! Author developer checks. Independent acceptance remains separately frozen.
const std = @import("std");
const objects = @import("../src/native/support/doe_native_object_types.zig");
const helpers = @import("../src/native/support/doe_native_object_helpers.zig");
const native = @import("../src/native/lifecycle/doe_instance_device_native.zig");
const exports = @import("../src/native/diagnostics/doe_error_scope_native.zig");
const public = @import("../src/dropin/wgpu_dropin_ext_a_exports.zig");
const adapter = @import("../src/native/support/doe_error_scope_c_adapter.zig");
const fixture = @import("support/c_error_scopes_fixture.zig");
const abi = @import("../src/core/abi/wgpu_handle_types.zig");
const recording = @import("../src/native/command/doe_command_recording.zig");

const Capture = struct {
    count: usize = 0,
    status: u32 = 0,
    error_type: u32 = 0,
    bytes: [512]u8 = undefined,
    length: usize = 0,
    was_null: bool = false,
    release_device: ?*anyopaque = null,

    fn receive(error_type: u32, message: abi.WGPUStringView, userdata1: ?*anyopaque, userdata2: ?*anyopaque) callconv(.c) void {
        const self: *Capture = @ptrCast(@alignCast(userdata1.?));
        std.debug.assert(userdata2 == &self.count);
        self.count += 1;
        self.error_type = error_type;
        self.length = message.length;
        self.was_null = message.data == null;
        if (message.length != 0) @memcpy(self.bytes[0..message.length], message.data[0..message.length]);
        if (self.release_device) |raw| native.doeNativeDeviceRelease(raw);
    }

    fn standard(status: u32, error_type: u32, message: abi.WGPUStringView, userdata1: ?*anyopaque, userdata2: ?*anyopaque) callconv(.c) void {
        const self: *Capture = @ptrCast(@alignCast(userdata1.?));
        self.status = status;
        receive(error_type, message, userdata1, userdata2);
    }
};

test "C error scopes integration: GCC private layout and actual native legacy exports" {
    try adapter.verifyLayout();
    const raw = fixture.doeTestErrorScopesCreateDevice(null) orelse return error.CreationFailed;
    defer native.doeNativeDeviceRelease(raw);
    const device = helpers.cast(objects.DoeDevice, raw).?;
    try std.testing.expect(device.error_scopes.initialized);
    var capture: Capture = .{};
    exports.doeNativeDevicePushErrorScope(raw, 1);
    exports.doeNativeDeviceInjectError(raw, 2, "first", 5);
    exports.doeNativeDeviceInjectError(raw, 2, "later", 5);
    const future = exports.doeNativeDevicePopErrorScopeFlat(raw, Capture.receive, &capture, &capture.count);
    try std.testing.expectEqual(@as(u64, 5), future.id);
    try std.testing.expectEqual(@as(usize, 1), capture.count);
    try std.testing.expectEqualStrings("first", capture.bytes[0..capture.length]);
    _ = exports.doeNativeDevicePopErrorScopeFlat(raw, Capture.receive, &capture, &capture.count);
    try std.testing.expectEqual(@as(usize, 1), capture.count);
    _ = exports.doeNativeDevicePopErrorScopeFlat(null, Capture.receive, &capture, &capture.count);
    try std.testing.expectEqual(@as(u32, 4), capture.error_type);
    try std.testing.expect(capture.was_null);
}

test "C error scopes integration: logical destroy preserves scope and requireAlive diagnostic" {
    const raw = fixture.doeTestErrorScopesCreateDevice(null) orelse return error.CreationFailed;
    defer native.doeNativeDeviceRelease(raw);
    const device = helpers.cast(objects.DoeDevice, raw).?;
    device.error_scopes.push(1);
    native.doeNativeDeviceDestroy(raw);
    try std.testing.expect(device.error_scopes.initialized);
    try std.testing.expect(!device.requireAlive());
    var capture: Capture = .{};
    _ = device.error_scopes.pop(.{ .callback = Capture.receive, .userdata1 = &capture, .userdata2 = &capture.count });
    try std.testing.expectEqual(@as(u32, 2), capture.error_type);
    try std.testing.expectEqualStrings("device destroyed; create a new device before GPU work", capture.bytes[0..capture.length]);
}

test "C error scopes integration: legacy callbacks release actual final device" {
    for ([_]bool{ false, true }) |uncaptured| {
        const raw = fixture.doeTestErrorScopesCreateDevice(null) orelse return error.CreationFailed;
        var capture: Capture = .{ .release_device = raw };
        if (uncaptured) {
            exports.doeNativeDeviceSetUncapturedErrorCallback(raw, Capture.receive, &capture, &capture.count);
        } else exports.doeNativeDevicePushErrorScope(raw, 1);
        exports.doeNativeDeviceInjectError(raw, 2, null, 999);
        if (!uncaptured) _ = exports.doeNativeDevicePopErrorScopeFlat(raw, Capture.receive, &capture, &capture.count);
        try std.testing.expectEqual(@as(usize, 1), capture.count);
        try std.testing.expectEqual(@as(usize, 0), capture.length);
        try std.testing.expect(!capture.was_null);
    }
}

test "C error scopes integration: standard modes retain existing device lease through final callback" {
    for ([_]u32{ 0, 1, 2, 3 }) |mode| {
        var instance: abi.WGPUInstance = null;
        const raw = fixture.doeTestErrorScopesCreateDevice(&instance) orelse return error.CreationFailed;
        // Keep only the explicitly needed instance reference through wait/pump.
        native.doeNativeInstanceAddRef(instance);
        defer native.doeNativeInstanceRelease(instance);
        var capture: Capture = .{ .release_device = raw };
        exports.doeNativeDevicePushErrorScope(raw, 1);
        exports.doeNativeDeviceInjectError(raw, 2, "standard", 8);
        const future = public.wgpuDevicePopErrorScope(raw, .{
            .nextInChain = null,
            .mode = mode,
            .callback = Capture.standard,
            .userdata1 = &capture,
            .userdata2 = &capture.count,
        });
        if (mode == 1) {
            var waits = [_]@import("../src/core/abi/wgpu_callback_descriptor_types.zig").WGPUFutureWaitInfo{.{ .future = future, .completed = 0 }};
            try std.testing.expectEqual(@as(u32, 1), native.doeNativeInstanceWaitAny(instance, 1, &waits, 0));
        } else if (mode == 2) native.doeNativeInstanceProcessEvents(instance);
        try std.testing.expectEqual(@as(usize, 1), capture.count);
        try std.testing.expectEqual(@as(u32, 1), capture.status);
        try std.testing.expectEqualStrings("standard", capture.bytes[0..capture.length]);
    }
}

test "C error scopes integration: actual recording failure preserves first diagnostic" {
    const raw = fixture.doeTestErrorScopesCreateDevice(null) orelse return error.CreationFailed;
    defer native.doeNativeDeviceRelease(raw);
    const device = helpers.cast(objects.DoeDevice, raw).?;
    var failing = std.testing.FailingAllocator.init(std.testing.allocator, .{ .fail_index = 0 });
    var encoder: objects.DoeCommandEncoder = .{ .dev = device, .allocator = failing.allocator() };
    defer encoder.cmds.deinit(encoder.allocator);
    defer encoder.references.deinit(encoder.allocator);
    device.error_scopes.push(2);
    try std.testing.expect(!recording.reserve(&encoder, 1, 0));
    try std.testing.expect(!recording.reserve(&encoder, 1, 0));
    // finish() generates the first recorded cause.
    recording.reportAtFinish(&encoder);
    var capture: Capture = .{};
    _ = device.error_scopes.pop(.{ .callback = Capture.receive, .userdata1 = &capture, .userdata2 = &capture.count });
    try std.testing.expectEqual(@as(u32, 3), capture.error_type);
    try std.testing.expectEqualStrings(@import("../src/contracts/command_recording.zig").message(error.OutOfMemory), capture.bytes[0..capture.length]);
    try std.testing.expectEqual(@as(usize, 0), encoder.cmds.items.len);
}

test "C error scopes integration: actual immediate layout and recording validation stay rejected" {
    const raw = fixture.doeTestErrorScopesCreateDevice(null) orelse return error.CreationFailed;
    defer native.doeNativeDeviceRelease(raw);
    const device = helpers.cast(objects.DoeDevice, raw).?;
    var encoder: objects.DoeCommandEncoder = .{ .dev = device };
    var pass: objects.DoeComputePass = .{ .enc = &encoder };
    encoder.state = .{ .pass = @intFromPtr(&pass) };
    device.error_scopes.push(1);
    @import("../src/native/command/doe_immediates_external_native.zig").doeNativeComputePassSetImmediates(&pass, 1, null, 0);
    try std.testing.expectEqual(error.ImmediateDataUnsupported, encoder.state.failed);
    recording.reportAtFinish(&encoder);
    var capture: Capture = .{};
    _ = device.error_scopes.pop(.{ .callback = Capture.receive, .userdata1 = &capture, .userdata2 = &capture.count });
    try std.testing.expectEqual(@as(u32, 2), capture.error_type);
    try std.testing.expectEqualStrings("setImmediates: shader-visible immediate data is unsupported; use buffer bindings", capture.bytes[0..capture.length]);
    try std.testing.expectEqual(@as(usize, 0), encoder.cmds.items.len);
    encoder.state = .finished;
    device.error_scopes.push(1);
    try std.testing.expect(!recording.requireOpen(&encoder));
    _ = device.error_scopes.pop(.{ .callback = Capture.receive, .userdata1 = &capture, .userdata2 = &capture.count });
    try std.testing.expectEqualStrings("GPU command recording requires an open encoder or its active pass", capture.bytes[0..capture.length]);
    try std.testing.expectEqual(@as(usize, 0), encoder.references.items.len);
}
