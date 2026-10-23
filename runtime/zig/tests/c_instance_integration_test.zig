//! Selected/adapted tests, not an unchanged retained-suite qualification.
const std = @import("std");
const adapter = @import("../src/native/support/doe_future_c_adapter.zig");
const delivery = @import("../src/native/support/doe_callback_owner.zig");
const objects = @import("../src/native/support/doe_native_object_types.zig");
const native = @import("../src/native/lifecycle/doe_instance_device_native.zig");
const abi = @import("../src/core/abi/wgpu_callback_descriptor_types.zig");
const base = @import("../src/core/abi/wgpu_core_base_types.zig");
const instance_adapter = @import("../src/native/support/doe_instance_c_adapter.zig");
const helpers = @import("../src/native/support/doe_native_object_helpers.zig");
const task_pool = @import("../src/runtime/task_pool.zig");

fn instance(raw: ?*anyopaque) *objects.DoeInstance {
    return @ptrCast(@alignCast(raw.?));
}

// The native allocator thunk still performs the real C allocation. The copied
// test binding makes actual final deallocation visible to Zig's leak checker.
fn newInstance() ?*anyopaque {
    const previous = instance_adapter.test_object_allocator;
    if (previous == null) instance_adapter.test_object_allocator = std.testing.allocator;
    defer instance_adapter.test_object_allocator = previous;
    return native.doeNativeCreateInstance(null);
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

var worker_completion: ?*delivery.Publication = null;
fn deferPublication(completion: *delivery.Publication) void {
    worker_completion = completion;
}
fn publishWorker(completion: *delivery.Publication) void {
    delivery.markReady(completion);
}

test "C instance integration: adapted futures actual native creation and typed GCC layout" {
    try adapter.verifyLayout();
    const before = instance_adapter.test_finalized_count;
    const raw = newInstance() orelse return error.CreationFailed;
    try std.testing.expect(helpers.cast(objects.DoeInstance, raw) == instance(raw));
    native.doeNativeInstanceAddRef(raw);
    try std.testing.expectEqual(before, instance_adapter.test_finalized_count);
    native.doeNativeInstanceRelease(raw);
    var infos: [0]abi.WGPUFutureWaitInfo = .{};
    try std.testing.expectEqual(@as(u32, 1), native.doeNativeInstanceWaitAny(null, 0, &infos, 0));
    native.doeNativeInstanceProcessEvents(raw);
    native.doeNativeInstanceRelease(raw);
    try std.testing.expectEqual(before + 1, instance_adapter.test_finalized_count);
}

test "C instance integration: adapted futures immediate publication frees payload and last caller" {
    for ([_]u32{ 0, abi.WGPUCallbackMode_AllowSpontaneous }) |mode| {
        const before = instance_adapter.test_finalized_count;
        const raw = newInstance() orelse return error.CreationFailed;
        var count: u32 = 0;
        const future = try delivery.ready(Reply, std.testing.allocator, instance(raw), mode, .{ .raw = raw, .count = &count }, Reply.deliver);
        try std.testing.expect(future.id != 0);
        try std.testing.expectEqual(@as(u32, 1), count);
        try std.testing.expectEqual(before + 1, instance_adapter.test_finalized_count);
    }
}

test "C instance integration: adapted futures native deferred wait and pump release final instance" {
    for ([_]u32{ abi.WGPUCallbackMode_WaitAnyOnly, abi.WGPUCallbackMode_AllowProcessEvents }) |mode| {
        const before = instance_adapter.test_finalized_count;
        const raw = newInstance() orelse return error.CreationFailed;
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
        try std.testing.expectEqual(before + 1, instance_adapter.test_finalized_count);
    }
}

test "C instance integration: adapted futures direct pump and wait retain through C return" {
    // No public-export outer reference here: adapter's full-call lease is decisive.
    for ([_]u32{ abi.WGPUCallbackMode_WaitAnyOnly, abi.WGPUCallbackMode_AllowProcessEvents }) |mode| {
        const before = instance_adapter.test_finalized_count;
        const raw = newInstance() orelse return error.CreationFailed;
        var count: u32 = 0;
        const future = try delivery.ready(Reply, std.testing.allocator, instance(raw), mode, .{ .raw = raw, .count = &count }, Reply.deliver);
        if (mode == abi.WGPUCallbackMode_WaitAnyOnly) {
            var infos = [_]abi.WGPUFutureWaitInfo{.{ .future = future, .completed = 0 }};
            try std.testing.expect(try helpers.instance_pending(instance(raw)).waitAny(&infos, 0));
        } else helpers.instance_pending(instance(raw)).processEvents();
        try std.testing.expectEqual(@as(u32, 1), count);
        try std.testing.expectEqual(before + 1, instance_adapter.test_finalized_count);
    }
}

test "C instance integration: adapted futures actual asynchronous publisher frees event and instance" {
    const before = instance_adapter.test_finalized_count;
    const raw = newInstance() orelse return error.CreationFailed;
    var count: u32 = 0;
    worker_completion = null;
    _ = try delivery.publish(Reply, std.testing.allocator, instance(raw), abi.WGPUCallbackMode_AllowSpontaneous, .{ .raw = raw, .count = &count }, Reply.deliver, deferPublication);
    const worker = try std.Thread.spawn(.{}, publishWorker, .{worker_completion.?});
    worker.join();
    worker_completion = null;
    try std.testing.expectEqual(@as(u32, 1), count);
    try std.testing.expectEqual(before + 1, instance_adapter.test_finalized_count);
}

test "C instance integration: adapted futures allocation failure preserves caller and wait flags" {
    var failing = std.testing.FailingAllocator.init(std.testing.allocator, .{ .fail_index = 0 });
    instance_adapter.test_wait_allocator = failing.allocator();
    defer instance_adapter.test_wait_allocator = null;
    const raw = newInstance() orelse return error.CreationFailed;
    defer native.doeNativeInstanceRelease(raw);
    var count: u32 = 0;
    try std.testing.expectError(error.OutOfMemory, delivery.ready(Reply, failing.allocator(), instance(raw), abi.WGPUCallbackMode_WaitAnyOnly, .{ .raw = raw, .count = &count }, Reply.deliver));
    native.doeNativeInstanceProcessEvents(raw);
    try std.testing.expectEqual(@as(u32, 0), count);
    try std.testing.expectEqual(@as(u32, 0), count);
    var pending: adapter.Completion = .{};
    const id = helpers.instance_pending(instance(raw)).newFuture();
    helpers.instance_pending(instance(raw)).register(&pending, id);
    defer pending.finish();
    var infos = [_]abi.WGPUFutureWaitInfo{.{ .future = .{ .id = id }, .completed = 37 }};
    try std.testing.expectEqual(@as(u32, 3), native.doeNativeInstanceWaitAny(raw, 1, &infos, 0));
    try std.testing.expectEqual(@as(u32, 37), infos[0].completed);
    native.doeNativeInstanceProcessEvents(raw);
}

test "C instance integration: adapted futures self wait and another thread preserve callback obligation" {
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
    const before = instance_adapter.test_finalized_count;
    const raw = newInstance() orelse return error.CreationFailed;
    var future: base.WGPUFuture = undefined;
    future = try delivery.ready(Context, std.testing.allocator, instance(raw), abi.WGPUCallbackMode_WaitAnyOnly, .{ .raw = raw, .future = &future }, Context.deliver);
    var info = [_]abi.WGPUFutureWaitInfo{.{ .future = future, .completed = 0 }};
    try std.testing.expectEqual(@as(u32, 1), native.doeNativeInstanceWaitAny(raw, 1, &info, std.math.maxInt(u64)));
    try std.testing.expectEqual(before + 1, instance_adapter.test_finalized_count);
}

test "C instance integration: adapted futures pending timeout and real worker wake" {
    const raw = newInstance() orelse return error.CreationFailed;
    var count: u32 = 0;
    const future = try delivery.publish(Reply, std.testing.allocator, instance(raw), abi.WGPUCallbackMode_WaitAnyOnly, .{ .raw = raw, .count = &count }, Reply.deliver, deferPublication);
    var info = [_]abi.WGPUFutureWaitInfo{.{ .future = future, .completed = 0 }};
    try std.testing.expectEqual(@as(u32, 2), native.doeNativeInstanceWaitAny(raw, 1, &info, 1000));
    try std.testing.expectEqual(@as(u32, 0), count);
    const Worker = struct {
        fn delayed(completion: *delivery.Publication) void {
            std.Thread.sleep(5 * std.time.ns_per_ms);
            delivery.markReady(completion);
        }
    };
    const worker = try std.Thread.spawn(.{}, Worker.delayed, .{worker_completion.?});
    defer worker.join();
    try std.testing.expectEqual(@as(u32, 1), native.doeNativeInstanceWaitAny(raw, 1, &info, std.math.maxInt(u64)));
    try std.testing.expectEqual(@as(u32, 1), count);
    worker_completion = null;
}

test "C instance integration: adapted futures delayed dispatch assignment and ownerless cleanup" {
    const Probe = struct {
        completion: adapter.Completion = .{},
        count: u32 = 0,
        fn dispatch(completion: *adapter.Completion) void {
            const self: *@This() = @fieldParentPtr("completion", completion);
            self.count += 1;
            completion.finish();
        }
    };
    const raw = newInstance() orelse return error.CreationFailed;
    defer native.doeNativeInstanceRelease(raw);
    const pending = helpers.instance_pending(instance(raw));
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

test "C instance integration: adapted futures readiness order and reentrant issuance boundary" {
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
    const raw = newInstance() orelse return error.CreationFailed;
    defer native.doeNativeInstanceRelease(raw);
    const pending = helpers.instance_pending(instance(raw));
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

test "C instance integration: adapted futures checked wait copy rejects overflow without reading flags" {
    const raw = newInstance() orelse return error.CreationFailed;
    defer native.doeNativeInstanceRelease(raw);
    var info = abi.WGPUFutureWaitInfo{ .future = .{ .id = 0 }, .completed = 71 };
    const oversized_count = std.math.maxInt(usize) / @sizeOf(adapter.c.doe_future_wait_info) + 1;
    const pointer: [*]abi.WGPUFutureWaitInfo = @ptrCast(&info);
    try std.testing.expectError(error.Overflow, helpers.instance_pending(instance(raw)).waitAny(pointer[0..oversized_count], 0));
    try std.testing.expectEqual(@as(u32, 3), native.doeNativeInstanceWaitAny(raw, oversized_count, pointer, 0));
    try std.testing.expectEqual(@as(u32, 71), info.completed);
}

const AdapterCapture = struct {
    raw: ?*anyopaque,
    called: u32 = 0,
    fn callback(status: abi.WGPURequestAdapterStatus, result_adapter: base.WGPUAdapter, message: base.WGPUStringView, userdata: ?*anyopaque, _: ?*anyopaque) callconv(.c) void {
        const self: *@This() = @ptrCast(@alignCast(userdata.?));
        std.testing.expect(status == .unavailable and result_adapter == null) catch @panic("wrong unavailable result");
        std.testing.expectEqualStrings("metal default device unavailable", message.data[0..message.length]) catch @panic("wrong unavailable message");
        self.called += 1;
        native.doeNativeInstanceRelease(self.raw);
    }
};

test "C instance integration: adapted callbacks real unavailable adapter owns final instance in every mode" {
    const backend = try std.process.getEnvVarOwned(std.testing.allocator, "DOE_BACKEND");
    defer std.testing.allocator.free(backend);
    try std.testing.expectEqualStrings("metal", backend);
    for ([_]u32{ 0, 1, 2, 3 }) |mode| {
        const before = instance_adapter.test_finalized_count;
        const raw = newInstance() orelse return error.CreationFailed;
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
        try std.testing.expectEqual(before + 1, instance_adapter.test_finalized_count);
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

test "C instance integration: adapted callbacks over-aligned typed inline context survives stack source" {
    const before = instance_adapter.test_finalized_count;
    const raw = newInstance() orelse return error.CreationFailed;
    var calls: u32 = 0;
    var reply = OwnedReply{ .raw = raw, .calls = &calls, .message = @splat(0x5a) };
    const future = try delivery.ready(OwnedReply, std.testing.allocator, instance(raw), 1, reply, OwnedReply.deliver);
    @memset(&reply.message, 0x71);
    var infos = [_]abi.WGPUFutureWaitInfo{.{ .future = future, .completed = 0 }};
    try std.testing.expectEqual(@as(u32, 1), native.doeNativeInstanceWaitAny(raw, 1, &infos, 0));
    try std.testing.expectEqual(@as(u32, 1), calls);
    try std.testing.expectEqual(before + 1, instance_adapter.test_finalized_count);
}

test "C instance integration: adapted callbacks stateless c allocator owns deferred payload" {
    const before = instance_adapter.test_finalized_count;
    const raw = newInstance() orelse return error.CreationFailed;
    var calls: u32 = 0;
    const future = try delivery.ready(OwnedReply, std.heap.c_allocator, instance(raw), 1, .{
        .raw = raw,
        .calls = &calls,
        .message = @splat(0x5a),
    }, OwnedReply.deliver);
    try std.testing.expectEqual(@as(u32, 0), calls);
    try std.testing.expectEqual(before, instance_adapter.test_finalized_count);
    var infos = [_]abi.WGPUFutureWaitInfo{.{ .future = future, .completed = 0 }};
    try std.testing.expectEqual(@as(u32, 1), native.doeNativeInstanceWaitAny(raw, 1, &infos, 0));
    try std.testing.expectEqual(@as(u32, 1), infos[0].completed);
    try std.testing.expectEqual(@as(u32, 1), calls);
    try std.testing.expectEqual(before + 1, instance_adapter.test_finalized_count);
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

test "C instance integration: adapted callbacks actual task pool publication drains and failed submit falls back" {
    task_pool.shutdown();
    defer task_pool.shutdown();
    for ([_]bool{ true, false }) |fail_submit| {
        const before = instance_adapter.test_finalized_count;
        const raw = newInstance() orelse return error.CreationFailed;
        var calls: u32 = 0;
        var failing = std.testing.FailingAllocator.init(std.testing.allocator, .{ .fail_index = 0 });
        submit_allocator = if (fail_submit) failing.allocator() else std.testing.allocator;
        submit_fallbacks = 0;
        _ = try delivery.publish(OwnedReply, std.testing.allocator, instance(raw), 3, .{ .raw = raw, .calls = &calls, .message = @splat(0x5a) }, OwnedReply.deliver, publishTask);
        task_pool.shutdown(); // Joins the real worker; no sleep as completion evidence.
        try std.testing.expectEqual(@as(u32, 1), calls);
        try std.testing.expectEqual(@as(usize, if (fail_submit) 1 else 0), submit_fallbacks);
        try std.testing.expectEqual(before + 1, instance_adapter.test_finalized_count);
    }
}

test "C instance integration: adapted callbacks ownerless source copy and ID ordering preserve reentry" {
    const OwnerlessReply = struct {
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
    const context = OwnerlessReply{ .count = &count, .inner = &inner, .recurse = true };
    const outer = try delivery.ready(OwnerlessReply, std.testing.failing_allocator, null, 0, context, OwnerlessReply.deliver);
    try std.testing.expectEqual(@as(u32, 2), count);
    try std.testing.expectEqual(inner + 1, outer.id);
    try std.testing.expect(context.recurse);
    try std.testing.expectError(error.CallbackInstanceUnavailable, delivery.ready(OwnerlessReply, std.testing.failing_allocator, null, 1, context, OwnerlessReply.deliver));
    try std.testing.expectError(error.UnsupportedCallbackMode, delivery.ready(OwnerlessReply, std.testing.failing_allocator, null, 99, context, OwnerlessReply.deliver));
}

test "C instance integration: copied stack view and late dispatch survive freeing worker callback" {
    const Payload = struct {
        completion: adapter.Completion = .{},
        raw: ?*anyopaque,
        calls: *u32,
        fn dispatch(completion: *adapter.Completion) void {
            const self: *@This() = @fieldParentPtr("completion", completion);
            const raw = self.raw;
            self.calls.* += 1;
            completion.finish();
            std.testing.allocator.destroy(self);
            native.doeNativeInstanceRelease(raw); // Event lease.
            native.doeNativeInstanceRelease(raw); // Last caller lease.
        }
        fn publish(self: *@This()) void {
            self.completion.markReady(); // Its full-call lease survives self's free.
        }
    };
    const before = instance_adapter.test_finalized_count;
    const raw = newInstance() orelse return error.CreationFailed;
    var calls: u32 = 0;
    const payload = try std.testing.allocator.create(Payload);
    payload.* = .{ .raw = raw, .calls = &calls };
    native.doeNativeInstanceAddRef(raw);
    {
        var short_lived_view = helpers.instance_pending(instance(raw));
        short_lived_view.register(&payload.completion, short_lived_view.newFuture());
        short_lived_view = undefined;
    }
    payload.completion.dispatch = Payload.dispatch;
    const worker = try std.Thread.spawn(.{}, Payload.publish, .{payload});
    worker.join();
    try std.testing.expectEqual(@as(u32, 1), calls);
    try std.testing.expectEqual(before + 1, instance_adapter.test_finalized_count);
}

test "C instance integration: actual loss event null device retires one instance obligation" {
    const loss = @import("../src/native/support/doe_device_loss.zig");
    const Capture = struct {
        raw: ?*anyopaque,
        calls: u32 = 0,
        fn ignore(_: ?*anyopaque) callconv(.c) void {}
        fn callback(device: ?*const anyopaque, reason: abi.WGPUDeviceLostReason, _: base.WGPUStringView, userdata: ?*anyopaque, _: ?*anyopaque) callconv(.c) void {
            const self: *@This() = @ptrCast(@alignCast(userdata.?));
            const handle: *const ?*anyopaque = @ptrCast(@alignCast(device.?));
            std.testing.expect(handle.* == null and reason == .destroyed) catch @panic("loss result changed");
            self.calls += 1;
            native.doeNativeInstanceRelease(self.raw);
        }
    };
    const before = instance_adapter.test_finalized_count;
    const raw = newInstance() orelse return error.CreationFailed;
    var capture = Capture{ .raw = raw };
    var info = std.mem.zeroes(abi.WGPUDeviceLostCallbackInfo);
    info.mode = abi.WGPUCallbackMode_AllowProcessEvents;
    info.callback = Capture.callback;
    info.userdata1 = &capture;
    const references = loss.References{
        .retain_device = Capture.ignore,
        .release_device = Capture.ignore,
        .retain_instance = native.doeNativeInstanceAddRef,
        .release_instance = native.doeNativeInstanceRelease,
    };
    var failing = std.testing.FailingAllocator.init(std.testing.allocator, .{ .fail_index = 0 });
    try std.testing.expectError(error.OutOfMemory, loss.Event.create(failing.allocator(), helpers.instance_pending(instance(raw)), raw, info, references));
    native.doeNativeInstanceProcessEvents(raw);
    const event = try loss.Event.create(std.testing.allocator, helpers.instance_pending(instance(raw)), raw, info, references);
    event.lose(.destroyed, "destroyed");
    event.lose(.failedCreation, "ignored duplicate");
    event.release(); // Only actual pending event obligation remains.
    try std.testing.expectEqual(@as(u32, 0), capture.calls);
    native.doeNativeInstanceProcessEvents(raw);
    try std.testing.expectEqual(@as(u32, 1), capture.calls);
    try std.testing.expectEqual(before + 1, instance_adapter.test_finalized_count);
}

test "C instance integration: real unconfigured surface owns final instance lease" {
    const surfaces = @import("../src/native/surface/doe_surface_native.zig");
    const before = instance_adapter.test_finalized_count;
    const raw = newInstance() orelse return error.CreationFailed;
    const surface = surfaces.doeNativeInstanceCreateSurface(raw, null, null) orelse return error.SurfaceCreationFailed;
    try std.testing.expect(helpers.cast(objects.DoeInstance, surface) == null);
    native.doeNativeInstanceRelease(raw);
    try std.testing.expectEqual(before, instance_adapter.test_finalized_count);
    native.doeNativeInstanceProcessEvents(raw); // Borrowed identity is live through the real surface.
    var calls: u32 = 0;
    _ = try delivery.ready(Reply, std.testing.allocator, instance(raw), 2, .{ .raw = raw, .count = &calls, .release_caller = false }, Reply.deliver);
    native.doeNativeInstanceProcessEvents(raw);
    try std.testing.expectEqual(@as(u32, 1), calls);
    try std.testing.expectEqual(before, instance_adapter.test_finalized_count);
    surfaces.doeNativeSurfaceRelease(surface);
    try std.testing.expectEqual(before + 1, instance_adapter.test_finalized_count);
}

test "C instance integration: actual surface allocation and bind rejection preserve caller" {
    const surfaces = @import("../src/native/surface/doe_surface_native.zig");
    const before = instance_adapter.test_finalized_count;
    const raw = newInstance() orelse return error.CreationFailed;
    var failing = std.testing.FailingAllocator.init(std.testing.allocator, .{ .fail_index = 0 });
    surfaces.test_surface_allocator = failing.allocator();
    try std.testing.expect(surfaces.doeNativeInstanceCreateSurface(raw, null, null) == null);
    surfaces.test_surface_allocator = null;
    var wrong_marker: [@sizeOf(objects.DoeDevice)]u8 align(@alignOf(objects.DoeDevice)) = @splat(0);
    try std.testing.expectEqual(@as(u32, 0), std.mem.readInt(u32, wrong_marker[@offsetOf(objects.DoeDevice, "magic")..][0..4], .little));
    try std.testing.expect(surfaces.doeNativeInstanceCreateSurface(raw, null, &wrong_marker) == null);
    native.doeNativeInstanceProcessEvents(raw);
    try std.testing.expectEqual(before, instance_adapter.test_finalized_count);
    native.doeNativeInstanceRelease(raw);
    try std.testing.expectEqual(before + 1, instance_adapter.test_finalized_count);
    const no_instance = surfaces.doeNativeInstanceCreateSurface(null, null, null) orelse return error.SurfaceCreationFailed;
    surfaces.doeNativeSurfaceRelease(no_instance);
}

test "C instance integration: real instance allocation failure leaves no observation" {
    const before = instance_adapter.test_allocated_count;
    var failing = std.testing.FailingAllocator.init(std.testing.allocator, .{ .fail_index = 0 });
    instance_adapter.test_object_allocator = failing.allocator();
    defer instance_adapter.test_object_allocator = null;
    try std.testing.expect(newInstance() == null);
    try std.testing.expectEqual(before, instance_adapter.test_allocated_count);
}

test "C instance integration: invalid pump drains actual no-instance work-done registry" {
    const queue = @import("../src/native/queue/doe_queue_lifecycle.zig");
    const Capture = struct {
        fn callback(status: abi.WGPUQueueWorkDoneStatus, _: base.WGPUStringView, userdata: ?*anyopaque, _: ?*anyopaque) callconv(.c) void {
            const calls: *u32 = @ptrCast(@alignCast(userdata.?));
            std.testing.expect(status == .success) catch @panic("work-done result changed");
            calls.* += 1;
        }
    };
    var calls: u32 = 0;
    const future = queue.doeNativeQueueOnSubmittedWorkDone(null, .{
        .nextInChain = null,
        .mode = abi.WGPUCallbackMode_AllowProcessEvents,
        .callback = Capture.callback,
        .userdata1 = &calls,
        .userdata2 = null,
    });
    try std.testing.expect(future.id != 0);
    try std.testing.expectEqual(@as(u32, 0), calls);
    var invalid: u32 = 0;
    native.doeNativeInstanceProcessEvents(&invalid);
    try std.testing.expectEqual(@as(u32, 1), calls);
    native.doeNativeInstanceProcessEvents(null);
    try std.testing.expectEqual(@as(u32, 1), calls);
}

test "C instance integration: explicit recursive pump captures fresh follower boundary" {
    const Probe = struct {
        completion: adapter.Completion = .{ .mode = abi.WGPUCallbackMode_AllowProcessEvents },
        follower: ?*@This() = null,
        calls: *u32,
        fn dispatch(completion: *adapter.Completion) void {
            const self: *@This() = @fieldParentPtr("completion", completion);
            self.calls.* += 1;
            if (self.follower) |follower| {
                const view = completion.owner.?;
                follower.completion.dispatch = dispatch;
                view.register(&follower.completion, view.newFuture());
                follower.completion.markReady();
                view.processEvents();
                std.testing.expectEqual(@as(u32, 2), self.calls.*) catch @panic("recursive pump suppressed follower");
            }
            completion.finish();
        }
    };
    const raw = newInstance() orelse return error.CreationFailed;
    defer native.doeNativeInstanceRelease(raw);
    const view = helpers.instance_pending(instance(raw));
    var calls: u32 = 0;
    var follower: Probe = .{ .calls = &calls };
    var first: Probe = .{ .calls = &calls, .follower = &follower };
    first.completion.dispatch = Probe.dispatch;
    view.register(&first.completion, view.newFuture());
    first.completion.markReady();
    view.processEvents();
    try std.testing.expectEqual(@as(u32, 2), calls);
}
