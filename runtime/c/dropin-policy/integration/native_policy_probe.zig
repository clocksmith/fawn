//! Fixture source: copy to runtime/zig/tests in each private matched source tree.
//! Append diagnostics_owned_hook.zig to that tree's diagnostic owner first.
//! Compile under zig test, never as a release DSO/export addition.
const std = @import("std");
const lib = @import("../src/dropin/wgpu_dropin_lib.zig");
const native = @import("../src/native/mod.zig");
const diagnostics = @import("../src/dropin/dropin_diagnostics.zig");
const abi = @import("../src/core/abi/wgpu_handle_types.zig");

fn observe(name: [:0]const u8) void {
    diagnostics.clear();
    const proc = lib.wgpuGetProcAddress(.{ .data = name.ptr, .length = name.len });
    var records: [diagnostics.MAX_DIAGNOSTIC_RECORDS]diagnostics.OwnedDiagnosticRecord = undefined;
    const count = diagnostics.snapshotOwnedForTest(&records);
    std.debug.print("lookup name={s} present={} error={d} records={d}\n", .{
        name, proc != null, lib.doeWgpuDropinLastErrorCode(), count,
    });
    for (records[0..count]) |record| {
        std.debug.print("record symbol={s} owner={s} resolved={} fallback={}\n", .{
            record.symbol[0..record.symbol_len], record.owner[0..record.owner_len],
            record.resolved,                     record.fallback_used,
        });
    }
}

test "native policy probe: actual required cache and owned diagnostics" {
    const Fn = @TypeOf(&native.doeNativeDeviceCreateBuffer);
    const first = lib.loadRequiredProc(Fn, "wgpuDeviceCreateBuffer");
    try std.testing.expectEqual(first, lib.loadRequiredProc(Fn, "wgpuDeviceCreateBuffer"));
    try std.testing.expectEqual(&native.doeNativeDeviceCreateBuffer, first);
    observe("wgpuDeviceCreateBuffer");
    observe("wgpuGetProcAddress");
    observe("wgpuHasInstanceFeature");
    observe("wgpuDefinitelyMissing");
}

test "native policy probe: process-wide error and owned diagnostic copy" {
    const Worker = struct {
        fn run() void {
            _ = lib.wgpuGetProcAddress(.{ .data = null, .length = abi.WGPU_STRLEN });
        }
    };
    lib.doeWgpuDropinClearLastError();
    const thread = try std.Thread.spawn(.{}, Worker.run, .{});
    thread.join();
    try std.testing.expectEqual(@as(u32, 3), lib.doeWgpuDropinLastErrorCode());
    var before: [diagnostics.MAX_DIAGNOSTIC_RECORDS]diagnostics.OwnedDiagnosticRecord = undefined;
    const count = diagnostics.snapshotOwnedForTest(&before);
    diagnostics.clear();
    // Values remain valid after ring mutation; this does not call broken snapshot.
    for (before[0..count]) |record| {
        try std.testing.expect(record.symbol_len <= diagnostics.MAX_SYMBOL_BYTES);
        try std.testing.expect(record.owner_len <= diagnostics.MAX_OWNER_BYTES);
        try std.testing.expect(!record.fallback_used);
    }
}

// Run only in a separate death invocation using this exact test filter.
test "native policy probe: missing required terminates" {
    _ = lib.loadRequiredProc(*const fn () callconv(.c) void, "wgpuMissingRequiredPolicyProbe");
    return error.MissingRequiredReturnedSuccess;
}
