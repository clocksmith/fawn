// SPDX-License-Identifier: MIT
// Post-handoff explicit retained-record comparison required by P06/B01.
const std = @import("std");
const c = @cImport({ @cInclude("doe_instance.h"); });
const upstream = @import("src/core/abi/generated/webgpu_upstream.zig");
const callbacks = @import("src/core/abi/wgpu_callback_descriptor_types.zig");
const base = @import("src/core/abi/wgpu_core_base_types.zig");
const capabilities = @import("src/core/abi/procs/wgpu_p1_capability_procs.zig");
const instance = @import("src/native/support/doe_instance_c_adapter.zig");
const futures = @import("src/native/support/doe_future_c_adapter.zig");
fn compare(comptime A: type, comptime B: type, comptime fields: anytype) !void {
    try std.testing.expectEqual(@sizeOf(A), @sizeOf(B));
    try std.testing.expectEqual(@alignOf(A), @alignOf(B));
    inline for (fields) |field| {
        try std.testing.expectEqual(@offsetOf(A, field), @offsetOf(B, field));
        try std.testing.expectEqual(@sizeOf(@FieldType(A, field)), @sizeOf(@FieldType(B, field)));
        try std.testing.expectEqual(@alignOf(@FieldType(A, field)), @alignOf(@FieldType(B, field)));
    }
}
test "independent native ABI: all retained boundary fields and actual C calls" {
    try futures.verifyLayout();
    try compare(c.WGPUFuture, base.WGPUFuture, .{"id"});
    try compare(c.WGPUFuture, upstream.WGPUFuture, .{"id"});
    try compare(c.WGPUFutureWaitInfo, callbacks.WGPUFutureWaitInfo, .{"future", "completed"});
    try compare(c.WGPUFutureWaitInfo, upstream.WGPUFutureWaitInfo, .{"future", "completed"});
    try compare(c.WGPUSupportedInstanceFeatures, capabilities.SupportedInstanceFeatures, .{"featureCount", "features"});
    try compare(c.WGPUInstanceLimits, capabilities.InstanceLimits, .{"nextInChain", "timedWaitAnyMaxCount"});
    try compare(c.WGPUBool, base.WGPUBool, .{});
    try compare(c.WGPUInstanceFeatureName, capabilities.WGPUInstanceFeatureName, .{});
    const features = instance.features();
    try std.testing.expect(features.featureCount == 0 and features.features == null);
    const limits = instance.limits();
    try std.testing.expect(limits.nextInChain == null and limits.timedWaitAnyMaxCount == 0);
    instance.freeFeatures(features);
    try std.testing.expectEqual(@as(u32, 0), instance.hasFeature(c.WGPUInstanceFeatureName_TimedWaitAny));
}
