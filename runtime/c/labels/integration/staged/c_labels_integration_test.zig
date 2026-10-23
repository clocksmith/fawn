//! Developer native CPU checks, not independent acceptance or GPU evidence.
const std = @import("std");
const options = @import("build_options");
const native = @import("../src/native/mod.zig");
const helpers = @import("../src/native/support/doe_native_object_helpers.zig");
const surface = @import("../src/native/surface/doe_surface_native.zig");
const dropin = @import("../src/dropin/wgpu_dropin_ext_c.zig");

test "C labels integration: binary copies and real instance final cleanup share owner" {
    try std.testing.expect(options.experimental_c_labels);
    const instance = native.doeNativeCreateInstance(null) orelse return error.CreateFailed;
    const key = @intFromPtr(instance);
    var bytes = [_]u8{ 0, 255, 128, 23 };
    const expected = bytes;
    native.doeNativeObjectSetLabel(instance, &bytes, bytes.len);
    @memset(&bytes, 7);
    try std.testing.expectEqualSlices(u8, &expected, helpers.label_store.get(instance));
    native.doeNativeInstanceAddRef(instance);
    native.doeNativeInstanceRelease(instance);
    try std.testing.expectEqualSlices(u8, &expected, helpers.label_store.get(instance));
    native.doeNativeInstanceRelease(instance);
    // Integer-key map query only, never an operation on the released object.
    try std.testing.expectEqual(@as(usize, 0), helpers.label_store.get(@ptrFromInt(key)).len);
}

test "C labels integration: surface setter and final release share owner" {
    const instance = native.doeNativeCreateInstance(null) orelse return error.CreateFailed;
    const raw = surface.doeNativeInstanceCreateSurface(instance, null, null) orelse return error.CreateFailed;
    const key = @intFromPtr(raw);
    const bytes = [_]u8{ 0, 255, 81 };
    dropin.wgpuSurfaceSetLabel(raw, .{ .data = &bytes, .length = bytes.len });
    try std.testing.expectEqualSlices(u8, &bytes, helpers.label_store.get(raw));
    surface.doeNativeSurfaceAddRef(raw);
    surface.doeNativeSurfaceRelease(raw);
    try std.testing.expectEqualSlices(u8, &bytes, helpers.label_store.get(raw));
    native.doeNativeInstanceRelease(instance);
    surface.doeNativeSurfaceRelease(raw);
    try std.testing.expectEqual(@as(usize, 0), helpers.label_store.get(@ptrFromInt(key)).len);
}
