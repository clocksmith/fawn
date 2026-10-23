// SPDX-License-Identifier: MIT
const std = @import("std");
const c = @cImport({
    @cInclude("doe_instance.h");
});
fn record(comptime name: []const u8, comptime fields: anytype) void {
    const T = @field(c, name);
    std.debug.print("type {s} {d} {d}\n", .{ name, @sizeOf(T), @alignOf(T) });
    inline for (fields) |field| std.debug.print("field {s}.{s} {d} {d}\n", .{ name, field, @offsetOf(T, field), @sizeOf(@FieldType(T, field)) });
}
pub fn main() void {
    record("doe_instance_status", .{});
    record("doe_instance_allocator", .{ "state", "vtable", "allocate", "deallocate" });
    record("doe_instance_config", .{ "abi_version", "object_allocator", "wait_allocator", "hook_userdata", "allocated", "external_texture_count", "remove_label" });
    record("doe_instance_queue_lease", .{ "queue", "userdata", "retain", "release" });
    inline for (.{ "doe_instance_allocate", "doe_instance_deallocate", "doe_instance_allocated", "doe_instance_external_count", "doe_instance_remove_label", "doe_instance_lease_hook" }) |name| record(name, .{});
    record("doe_future_storage_requirements", .{ "size", "alignment" });
    record("doe_future_wait_info", .{ "id", "completed" });
    record("WGPUFuture", .{"id"});
    record("WGPUFutureWaitInfo", .{ "future", "completed" });
    record("WGPUSupportedInstanceFeatures", .{ "featureCount", "features" });
    record("WGPUInstanceLimits", .{ "nextInChain", "timedWaitAnyMaxCount" });
    record("WGPUBool", .{});
    record("WGPUWaitStatus", .{});
    record("WGPUInstanceFeatureName", .{});
    inline for (.{ "DOE_INSTANCE_ABI_VERSION", "DOE_INSTANCE_OK", "DOE_INSTANCE_INVALID_ARGUMENT", "DOE_INSTANCE_UNSUPPORTED_VERSION", "DOE_INSTANCE_OUT_OF_MEMORY", "DOE_INSTANCE_LAYOUT_OVERFLOW", "DOE_INSTANCE_QUEUE_INITIALIZATION", "WGPUWaitStatus_Success", "WGPUWaitStatus_TimedOut", "WGPUWaitStatus_Error" }) |name| std.debug.print("enum {s} {d}\n", .{ name, @field(c, name) });
}
