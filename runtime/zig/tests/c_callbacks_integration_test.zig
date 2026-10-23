//! Actual native instances plus the selected payload owner; no GPU success claim.
const std = @import("std");
const native = @import("../src/native/lifecycle/doe_instance_device_native.zig");
const objects = @import("../src/native/support/doe_native_object_types.zig");
const future_adapter = @import("../src/native/support/doe_future_c_adapter.zig");
const delivery = @import("../src/native/support/doe_callback_owner.zig");
const abi = @import("../src/core/abi/wgpu_callback_descriptor_types.zig");
const base = @import("../src/core/abi/wgpu_core_base_types.zig");
const task_pool = @import("../src/runtime/task_pool.zig");

fn instance(raw: ?*anyopaque) *objects.DoeInstance {
    return @ptrCast(@alignCast(raw.?));
}

const AdapterCapture = struct {
    raw: ?*anyopaque,
    called: u32 = 0,
    fn callback(status: abi.WGPURequestAdapterStatus, adapter: base.WGPUAdapter, message: base.WGPUStringView, userdata: ?*anyopaque, _: ?*anyopaque) callconv(.c) void {
        const self: *@This() = @ptrCast(@alignCast(userdata.?));
        std.testing.expect(status == .unavailable and adapter == null) catch @panic("wrong unavailable result");
        std.testing.expectEqualStrings("metal default device unavailable", message.data[0..message.length]) catch @panic("wrong unavailable message");
        self.called += 1;
        native.doeNativeInstanceRelease(self.raw);
    }
};

test "C callbacks integration: real unavailable adapter owns final instance in every mode" {
    const backend = try std.process.getEnvVarOwned(std.testing.allocator, "DOE_BACKEND");
    defer std.testing.allocator.free(backend);
    try std.testing.expectEqualStrings("metal", backend);
    for ([_]u32{ 0, 1, 2, 3 }) |mode| {
        const before = future_adapter.test_deinit_count;
        const raw = native.doeNativeCreateInstance(null) orelse return error.CreationFailed;
        var capture = AdapterCapture{ .raw = raw };
        const future = native.doeNativeInstanceRequestAdapter(raw, null, .{
            .nextInChain = null,
            .mode = mode,
            .callback = AdapterCapture.callback,
            .userdata1 = &capture,
            .userdata2 = null,
        });
        try std.testing.expect(future.id != 0);
        if (mode == 1) {
            native.doeNativeInstanceProcessEvents(raw);
            try std.testing.expectEqual(@as(u32, 0), capture.called);
            var infos = [_]abi.WGPUFutureWaitInfo{ .{ .future = future, .completed = 0 }, .{ .future = future, .completed = 0 } };
            try std.testing.expectEqual(@as(u32, 1), native.doeNativeInstanceWaitAny(raw, 2, &infos, 0));
            try std.testing.expectEqual(@as(u32, 1), infos[0].completed);
            try std.testing.expectEqual(@as(u32, 1), infos[1].completed);
        } else if (mode == 2) native.doeNativeInstanceProcessEvents(raw);
        try std.testing.expectEqual(@as(u32, 1), capture.called);
        try std.testing.expectEqual(before + 1, future_adapter.test_deinit_count);
    }
}

const OwnedReply = struct {
    raw: ?*anyopaque,
    calls: *u32,
    message: [32]u8 align(128),
    fn deliver(self: *@This()) void {
        std.testing.expect(@intFromPtr(self) % @alignOf(@This()) == 0) catch @panic("context misaligned");
        for (self.message) |byte| std.testing.expectEqual(@as(u8, 0x5a), byte) catch @panic("inline bytes changed");
        self.calls.* += 1;
        native.doeNativeInstanceRelease(self.raw);
    }
};

test "C callbacks integration: over-aligned typed inline context survives stack source" {
    const before = future_adapter.test_deinit_count;
    const raw = native.doeNativeCreateInstance(null) orelse return error.CreationFailed;
    var calls: u32 = 0;
    var reply = OwnedReply{ .raw = raw, .calls = &calls, .message = @splat(0x5a) };
    const future = try delivery.ready(OwnedReply, std.testing.allocator, instance(raw), 1, reply, OwnedReply.deliver);
    @memset(&reply.message, 0x71);
    var infos = [_]abi.WGPUFutureWaitInfo{.{ .future = future, .completed = 0 }};
    try std.testing.expectEqual(@as(u32, 1), native.doeNativeInstanceWaitAny(raw, 1, &infos, 0));
    try std.testing.expectEqual(@as(u32, 1), calls);
    try std.testing.expectEqual(before + 1, future_adapter.test_deinit_count);
}

test "C callbacks integration: stateless c allocator owns deferred payload" {
    const before = future_adapter.test_deinit_count;
    const raw = native.doeNativeCreateInstance(null) orelse return error.CreationFailed;
    var calls: u32 = 0;
    const future = try delivery.ready(OwnedReply, std.heap.c_allocator, instance(raw), 1, .{
        .raw = raw,
        .calls = &calls,
        .message = @splat(0x5a),
    }, OwnedReply.deliver);
    try std.testing.expectEqual(@as(u32, 0), calls);
    try std.testing.expectEqual(before, future_adapter.test_deinit_count);
    var infos = [_]abi.WGPUFutureWaitInfo{.{ .future = future, .completed = 0 }};
    try std.testing.expectEqual(@as(u32, 1), native.doeNativeInstanceWaitAny(raw, 1, &infos, 0));
    try std.testing.expectEqual(@as(u32, 1), infos[0].completed);
    try std.testing.expectEqual(@as(u32, 1), calls);
    try std.testing.expectEqual(before + 1, future_adapter.test_deinit_count);
}

var submit_allocator: std.mem.Allocator = undefined;
var submit_fallbacks: usize = 0;
fn publishTask(token: *delivery.Publication) void {
    task_pool.submitWithAllocator(submit_allocator, .{ .run = struct {
        fn run(raw: ?*anyopaque) void {
            delivery.markReady(@ptrCast(@alignCast(raw.?)));
        }
    }.run, .ctx = token }) catch {
        submit_fallbacks += 1;
        delivery.markReady(token);
    };
}

test "C callbacks integration: actual task pool publication drains and failed submit falls back" {
    task_pool.shutdown();
    defer task_pool.shutdown();
    for ([_]bool{ true, false }) |fail_submit| {
        const before = future_adapter.test_deinit_count;
        const raw = native.doeNativeCreateInstance(null) orelse return error.CreationFailed;
        var calls: u32 = 0;
        var failing = std.testing.FailingAllocator.init(std.testing.allocator, .{ .fail_index = 0 });
        submit_allocator = if (fail_submit) failing.allocator() else std.testing.allocator;
        submit_fallbacks = 0;
        _ = try delivery.publish(OwnedReply, std.testing.allocator, instance(raw), 3, .{ .raw = raw, .calls = &calls, .message = @splat(0x5a) }, OwnedReply.deliver, publishTask);
        task_pool.shutdown(); // Joins the real worker; no sleep as completion evidence.
        try std.testing.expectEqual(@as(u32, 1), calls);
        try std.testing.expectEqual(@as(usize, if (fail_submit) 1 else 0), submit_fallbacks);
        try std.testing.expectEqual(before + 1, future_adapter.test_deinit_count);
    }
}

test "C callbacks integration: ownerless source copy and ID ordering preserve reentry" {
    const Reply = struct {
        count: *u32,
        inner: *u64,
        recurse: bool,
        fn deliver(self: *@This()) void {
            self.count.* += 1;
            if (self.recurse) {
                const nested = delivery.ready(@This(), std.testing.failing_allocator, null, 3, .{ .count = self.count, .inner = self.inner, .recurse = false }, deliver) catch unreachable;
                self.inner.* = nested.id;
                self.recurse = false;
            }
        }
    };
    var count: u32 = 0;
    var inner: u64 = 0;
    const context = Reply{ .count = &count, .inner = &inner, .recurse = true };
    const outer = try delivery.ready(Reply, std.testing.failing_allocator, null, 0, context, Reply.deliver);
    try std.testing.expectEqual(@as(u32, 2), count);
    try std.testing.expectEqual(inner + 1, outer.id);
    try std.testing.expect(context.recurse);
    try std.testing.expectError(error.CallbackInstanceUnavailable, delivery.ready(Reply, std.testing.failing_allocator, null, 1, context, Reply.deliver));
    try std.testing.expectError(error.UnsupportedCallbackMode, delivery.ready(Reply, std.testing.failing_allocator, null, 99, context, Reply.deliver));
}
