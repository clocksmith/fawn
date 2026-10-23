//! Selected/adapted tests, not an unchanged retained-suite qualification.
const std = @import("std");
const adapter = @import("../src/native/support/doe_future_c_adapter.zig");
const delivery = @import("../src/native/support/doe_callback_delivery.zig");
const objects = @import("../src/native/support/doe_native_object_types.zig");
const native = @import("../src/native/lifecycle/doe_instance_device_native.zig");
const abi = @import("../src/core/abi/wgpu_callback_descriptor_types.zig");
const base = @import("../src/core/abi/wgpu_core_base_types.zig");

fn instance(raw: ?*anyopaque) *objects.DoeInstance {
    return @ptrCast(@alignCast(raw.?));
}

const Reply = struct {
    raw: ?*anyopaque,
    count: *u32,
    release_caller: bool = true,
    fn deliver(self: *Reply) void {
        self.count.* += 1;
        if (self.release_caller) native.doeNativeInstanceRelease(self.raw);
    }
};

var worker_completion: ?*adapter.Completion = null;
fn deferPublication(completion: *adapter.Completion) void {
    worker_completion = completion;
}
fn publishWorker(completion: *adapter.Completion) void {
    completion.markReady();
}

test "C futures integration: actual native creation and typed GCC layout" {
    try adapter.verifyLayout();
    const before = adapter.test_deinit_count;
    const raw = native.doeNativeCreateInstance(null) orelse return error.CreationFailed;
    try std.testing.expect(instance(raw).pending_completions.initialized);
    native.doeNativeInstanceAddRef(raw);
    try std.testing.expectEqual(@as(u32, 2), instance(raw).ref_count);
    native.doeNativeInstanceRelease(raw);
    var infos: [0]abi.WGPUFutureWaitInfo = .{};
    try std.testing.expectEqual(@as(u32, 1), native.doeNativeInstanceWaitAny(null, 0, &infos, 0));
    native.doeNativeInstanceProcessEvents(raw);
    native.doeNativeInstanceRelease(raw);
    try std.testing.expectEqual(before + 1, adapter.test_deinit_count);
}

test "C futures integration: immediate publication frees payload and last caller" {
    for ([_]u32{ 0, abi.WGPUCallbackMode_AllowSpontaneous }) |mode| {
        const before = adapter.test_deinit_count;
        const raw = native.doeNativeCreateInstance(null) orelse return error.CreationFailed;
        var count: u32 = 0;
        const future = try delivery.ready(Reply, std.testing.allocator, instance(raw), mode, .{ .raw = raw, .count = &count }, Reply.deliver);
        try std.testing.expect(future.id != 0);
        try std.testing.expectEqual(@as(u32, 1), count);
        try std.testing.expectEqual(before + 1, adapter.test_deinit_count);
    }
}

test "C futures integration: native deferred wait and pump release final instance" {
    for ([_]u32{ abi.WGPUCallbackMode_WaitAnyOnly, abi.WGPUCallbackMode_AllowProcessEvents }) |mode| {
        const before = adapter.test_deinit_count;
        const raw = native.doeNativeCreateInstance(null) orelse return error.CreationFailed;
        var count: u32 = 0;
        const future = try delivery.ready(Reply, std.testing.allocator, instance(raw), mode, .{ .raw = raw, .count = &count }, Reply.deliver);
        try std.testing.expectEqual(@as(u32, 0), count);
        if (mode == abi.WGPUCallbackMode_WaitAnyOnly) {
            native.doeNativeInstanceProcessEvents(raw);
            try std.testing.expectEqual(@as(u32, 0), count);
            var infos = [_]abi.WGPUFutureWaitInfo{ .{ .future = future, .completed = 7 }, .{ .future = future, .completed = 9 } };
            try std.testing.expectEqual(@as(u32, 1), native.doeNativeInstanceWaitAny(raw, infos.len, &infos, 0));
            try std.testing.expectEqual(@as(u32, 1), infos[0].completed);
            try std.testing.expectEqual(@as(u32, 1), infos[1].completed);
        } else native.doeNativeInstanceProcessEvents(raw);
        try std.testing.expectEqual(@as(u32, 1), count);
        try std.testing.expectEqual(before + 1, adapter.test_deinit_count);
    }
}

test "C futures integration: direct pump and wait retain through C return" {
    // No public-export outer reference here: adapter's full-call lease is decisive.
    for ([_]u32{ abi.WGPUCallbackMode_WaitAnyOnly, abi.WGPUCallbackMode_AllowProcessEvents }) |mode| {
        const before = adapter.test_deinit_count;
        const raw = native.doeNativeCreateInstance(null) orelse return error.CreationFailed;
        var count: u32 = 0;
        const future = try delivery.ready(Reply, std.testing.allocator, instance(raw), mode, .{ .raw = raw, .count = &count }, Reply.deliver);
        if (mode == abi.WGPUCallbackMode_WaitAnyOnly) {
            var infos = [_]abi.WGPUFutureWaitInfo{.{ .future = future, .completed = 0 }};
            try std.testing.expect(try instance(raw).pending_completions.waitAny(&infos, 0));
        } else instance(raw).pending_completions.processEvents();
        try std.testing.expectEqual(@as(u32, 1), count);
        try std.testing.expectEqual(before + 1, adapter.test_deinit_count);
    }
}

test "C futures integration: actual asynchronous publisher frees event and instance" {
    const before = adapter.test_deinit_count;
    const raw = native.doeNativeCreateInstance(null) orelse return error.CreationFailed;
    var count: u32 = 0;
    worker_completion = null;
    _ = try delivery.publish(Reply, std.testing.allocator, instance(raw), abi.WGPUCallbackMode_AllowSpontaneous, .{ .raw = raw, .count = &count }, Reply.deliver, deferPublication);
    const worker = try std.Thread.spawn(.{}, publishWorker, .{worker_completion.?});
    worker.join();
    worker_completion = null;
    try std.testing.expectEqual(@as(u32, 1), count);
    try std.testing.expectEqual(before + 1, adapter.test_deinit_count);
}

test "C futures integration: allocation failure preserves caller and wait flags" {
    const raw = native.doeNativeCreateInstance(null) orelse return error.CreationFailed;
    defer native.doeNativeInstanceRelease(raw);
    var count: u32 = 0;
    var failing = std.testing.FailingAllocator.init(std.testing.allocator, .{ .fail_index = 0 });
    try std.testing.expectError(error.OutOfMemory, delivery.ready(Reply, failing.allocator(), instance(raw), abi.WGPUCallbackMode_WaitAnyOnly, .{ .raw = raw, .count = &count }, Reply.deliver));
    try std.testing.expectEqual(@as(u32, 1), instance(raw).ref_count);
    try std.testing.expect(instance(raw).pending_completions.storage.head == null);
    try std.testing.expectEqual(@as(u32, 0), count);
    var pending: adapter.Completion = .{};
    const id = instance(raw).pending_completions.newFuture();
    instance(raw).pending_completions.register(&pending, id);
    defer pending.finish();
    var infos = [_]abi.WGPUFutureWaitInfo{.{ .future = .{ .id = id }, .completed = 37 }};
    adapter.test_wait_allocator = failing.allocator();
    defer adapter.test_wait_allocator = null;
    try std.testing.expectEqual(@as(u32, 3), native.doeNativeInstanceWaitAny(raw, 1, &infos, 0));
    try std.testing.expectEqual(@as(u32, 37), infos[0].completed);
    try std.testing.expectEqual(@as(u32, 1), instance(raw).ref_count);
}

test "C futures integration: self wait and another thread preserve callback obligation" {
    const Context = struct {
        raw: ?*anyopaque,
        future: *base.WGPUFuture,
        fn poll(raw: ?*anyopaque, future: base.WGPUFuture, result: *u32) void {
            var info = [_]abi.WGPUFutureWaitInfo{.{ .future = future, .completed = 0 }};
            result.* = native.doeNativeInstanceWaitAny(raw, 1, &info, 0);
        }
        fn deliver(self: *@This()) void {
            var own = [_]abi.WGPUFutureWaitInfo{.{ .future = self.future.*, .completed = 0 }};
            std.testing.expect(native.doeNativeInstanceWaitAny(self.raw, 1, &own, std.math.maxInt(u64)) == 1) catch @panic("self wait failed");
            var other: u32 = 0;
            const worker = std.Thread.spawn(.{}, poll, .{ self.raw, self.future.*, &other }) catch unreachable;
            worker.join();
            std.testing.expect(other == 2) catch @panic("other-thread obligation retired early");
            native.doeNativeInstanceRelease(self.raw);
        }
    };
    const before = adapter.test_deinit_count;
    const raw = native.doeNativeCreateInstance(null) orelse return error.CreationFailed;
    var future: base.WGPUFuture = undefined;
    future = try delivery.ready(Context, std.testing.allocator, instance(raw), abi.WGPUCallbackMode_WaitAnyOnly, .{ .raw = raw, .future = &future }, Context.deliver);
    var info = [_]abi.WGPUFutureWaitInfo{.{ .future = future, .completed = 0 }};
    try std.testing.expectEqual(@as(u32, 1), native.doeNativeInstanceWaitAny(raw, 1, &info, std.math.maxInt(u64)));
    try std.testing.expectEqual(before + 1, adapter.test_deinit_count);
}

test "C futures integration: pending timeout and real worker wake" {
    const raw = native.doeNativeCreateInstance(null) orelse return error.CreationFailed;
    var count: u32 = 0;
    const future = try delivery.publish(Reply, std.testing.allocator, instance(raw), abi.WGPUCallbackMode_WaitAnyOnly, .{ .raw = raw, .count = &count }, Reply.deliver, deferPublication);
    var info = [_]abi.WGPUFutureWaitInfo{.{ .future = future, .completed = 0 }};
    try std.testing.expectEqual(@as(u32, 2), native.doeNativeInstanceWaitAny(raw, 1, &info, 1000));
    try std.testing.expectEqual(@as(u32, 0), count);
    const Worker = struct {
        fn delayed(completion: *adapter.Completion) void {
            std.Thread.sleep(5 * std.time.ns_per_ms);
            completion.markReady();
        }
    };
    const worker = try std.Thread.spawn(.{}, Worker.delayed, .{worker_completion.?});
    defer worker.join();
    try std.testing.expectEqual(@as(u32, 1), native.doeNativeInstanceWaitAny(raw, 1, &info, std.math.maxInt(u64)));
    try std.testing.expectEqual(@as(u32, 1), count);
    worker_completion = null;
}

test "C futures integration: delayed dispatch assignment and ownerless cleanup" {
    const Probe = struct {
        completion: adapter.Completion = .{},
        count: u32 = 0,
        fn dispatch(completion: *adapter.Completion) void {
            const self: *@This() = @fieldParentPtr("completion", completion);
            self.count += 1;
            completion.finish();
        }
    };
    var pending: adapter.PendingCompletions = .{};
    try pending.init(null);
    defer pending.deinit();
    var probe: Probe = .{};
    pending.register(&probe.completion, pending.newFuture());
    probe.completion.dispatch = Probe.dispatch;
    probe.completion.markReady();
    try std.testing.expectEqual(@as(u32, 1), probe.count);
    try std.testing.expect(!probe.completion.initialized);
    var ownerless: Probe = .{};
    ownerless.completion.finish(); // Request freed before registration is harmless.
    ownerless.completion.dispatch = Probe.dispatch;
    ownerless.completion.markReady();
    try std.testing.expectEqual(@as(u32, 1), ownerless.count);
    try std.testing.expect(!ownerless.completion.initialized);
}

test "C futures integration: readiness order and reentrant issuance boundary" {
    const Probe = struct {
        completion: adapter.Completion = .{ .mode = abi.WGPUCallbackMode_AllowProcessEvents },
        follower: ?*@This() = null,
        sequence: *u32,
        observed: u32 = 0,
        fn dispatch(completion: *adapter.Completion) void {
            const self: *@This() = @fieldParentPtr("completion", completion);
            self.sequence.* += 1;
            self.observed = self.sequence.*;
            if (self.follower) |next| {
                const owner = completion.owner.?;
                next.completion.dispatch = dispatch;
                owner.register(&next.completion, owner.newFuture());
                next.completion.markReady();
            }
            completion.finish();
        }
    };
    var pending: adapter.PendingCompletions = .{};
    try pending.init(null);
    defer pending.deinit();
    var sequence: u32 = 0;
    var follower: Probe = .{ .sequence = &sequence };
    var first: Probe = .{ .sequence = &sequence, .follower = &follower };
    var second: Probe = .{ .sequence = &sequence };
    first.completion.dispatch = Probe.dispatch;
    second.completion.dispatch = Probe.dispatch;
    pending.register(&first.completion, pending.newFuture());
    pending.register(&second.completion, pending.newFuture());
    second.completion.markReady();
    first.completion.markReady();
    pending.processEvents();
    try std.testing.expectEqual(@as(u32, 1), first.observed);
    try std.testing.expectEqual(@as(u32, 2), second.observed);
    try std.testing.expectEqual(@as(u32, 0), follower.observed);
    pending.processEvents();
    try std.testing.expectEqual(@as(u32, 3), follower.observed);
}

test "C futures integration: checked wait copy rejects overflow without reading flags" {
    const raw = native.doeNativeCreateInstance(null) orelse return error.CreationFailed;
    defer native.doeNativeInstanceRelease(raw);
    var info = abi.WGPUFutureWaitInfo{ .future = .{ .id = 0 }, .completed = 71 };
    const oversized_count = std.math.maxInt(usize) / @sizeOf(adapter.c.doe_future_wait_info) + 1;
    const pointer: [*]abi.WGPUFutureWaitInfo = @ptrCast(&info);
    try std.testing.expectError(error.Overflow, instance(raw).pending_completions.waitAny(pointer[0..oversized_count], 0));
    try std.testing.expectEqual(@as(u32, 71), info.completed);
}
