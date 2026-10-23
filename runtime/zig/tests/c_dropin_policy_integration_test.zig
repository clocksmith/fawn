//! Source-bound actual resolver tests. They do not qualify a prebuilt DSO or GPU.
const std = @import("std");
const options = @import("build_options");
const adapter = @import("../src/dropin/dropin_policy_c_adapter.zig");
const config = @import("../src/dropin/dropin_runtime_config.zig");
const lib = @import("../src/dropin/wgpu_dropin_lib.zig");
const manifest = @import("../src/dropin/dropin_proc_manifest.zig");
const native = @import("../src/native/mod.zig");
const abi = @import("../src/core/abi/wgpu_handle_types.zig");
const capability = @import("../src/core/abi/procs/wgpu_p1_capability_procs.zig");
const c = @cImport({
    @cInclude("doe_dropin_policy_private.h");
    @cInclude("webgpu.h");
});

test "C drop-in policy integration: immutable descriptor and native manifest projection" {
    try std.testing.expect(options.experimental_c_dropin_policy);
    const descriptor = adapter.testDescriptor();
    try std.testing.expectEqual(@as(usize, switch (options.build_tier) {
        .compute => 69,
        .headless => 106,
        .full => 114,
    }), descriptor.manifest_count);
    try std.testing.expectEqual(@as(usize, 19), descriptor.lane_name_count);
    try std.testing.expectEqual(@sizeOf(c.DoeDropinPolicyDescriptor), @sizeOf(@TypeOf(descriptor.*)));
    try std.testing.expectEqual(@sizeOf(c.WGPUStringView), @sizeOf(abi.WGPUStringView));
    try std.testing.expectEqual(@offsetOf(c.WGPUStringView, "length"), @offsetOf(abi.WGPUStringView, "length"));
    try std.testing.expectEqual(@as(usize, c.WGPU_STRLEN), abi.WGPU_STRLEN);
    try std.testing.expect(descriptor.config == c.doe_dropin_policy_embedded_config());
    for (manifest.ACTIVE_IDENTITIES) |entry| {
        try std.testing.expectEqual(entry.owner, config.symbolOwnerForName(entry.symbol));
    }
}

test "C drop-in policy integration: actual required resolver cache preserves function identity" {
    const Fn = @TypeOf(&native.doeNativeDeviceCreateBuffer);
    const first = lib.loadRequiredProc(Fn, "wgpuDeviceCreateBuffer");
    const second = lib.loadRequiredProc(Fn, "wgpuDeviceCreateBuffer");
    try std.testing.expectEqual(&native.doeNativeDeviceCreateBuffer, first);
    try std.testing.expectEqual(first, second);
    const expected: capability.WGPUProc = @ptrCast(&native.doeNativeDeviceCreateBuffer);
    const symbol = "wgpuDeviceCreateBuffer";
    for ([_]usize{ symbol.len, abi.WGPU_STRLEN }) |length| {
        try std.testing.expectEqual(expected, lib.wgpuGetProcAddress(.{ .data = symbol.ptr, .length = length }));
    }
}

test "C drop-in policy integration: null and explicit empty retain error delivery" {
    try std.testing.expect(lib.wgpuGetProcAddress(.{ .data = null, .length = abi.WGPU_STRLEN }) == null);
    try std.testing.expectEqual(@as(u32, 3), lib.doeWgpuDropinLastErrorCode());
    try std.testing.expect(lib.wgpuGetProcAddress(.{ .data = "".ptr, .length = 0 }) == null);
    try std.testing.expectEqual(@as(u32, 3), lib.doeWgpuDropinLastErrorCode());
}
