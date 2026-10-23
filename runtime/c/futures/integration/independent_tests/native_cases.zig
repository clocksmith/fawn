// SPDX-License-Identifier: MIT
// Independently authored retained-consumer tests; no native lifecycle mocks.
const std = @import("std");
const adapter = @import("src/native/support/doe_future_c_adapter.zig");
const life = @import("src/native/lifecycle/doe_instance_device_native.zig");
const delivery = @import("src/native/support/doe_callback_delivery.zig");
const owner = @import("src/native/support/doe_future_owner.zig");
const objects = @import("src/native/support/doe_native_object_types.zig");
const abi = @import("src/core/abi/wgpu_callback_descriptor_types.zig");
const base = @import("src/core/abi/wgpu_core_base_types.zig");

fn instance() !*objects.DoeInstance {
    const raw = life.doeNativeCreateInstance(null) orelse return error.InstanceCreationFailed;
    return @ptrCast(@alignCast(raw));
}
fn release(inst: *objects.DoeInstance) void {
    life.doeNativeInstanceRelease(@ptrCast(inst));
}
fn wait(inst: ?*objects.DoeInstance, infos: []abi.WGPUFutureWaitInfo, timeout: u64) u32 {
    return life.doeNativeInstanceWaitAny(@ptrCast(inst), infos.len, infos.ptr, timeout);
}
fn pump(inst: *objects.DoeInstance) void {
    life.doeNativeInstanceProcessEvents(@ptrCast(inst));
}
const Count = struct {
    count: *u32,
    fn deliver(self: *@This()) void { self.count.* += 1; }
};
const LastCaller = struct {
    instance: *objects.DoeInstance,
    count: *u32,
    fn deliver(self: *@This()) void {
        self.count.* += 1;
        release(self.instance);
    }
};
var captured: ?*owner.Completion = null;
fn hold(completion: *owner.Completion) void { captured = completion; }

// Names deliberately do not match the author's test filter.
test "independent futures: modes use real instance and actual callback payload cleanup" {
    for ([_]u32{0, 1, 2, 3}) |mode| {
        const inst = try instance();
        defer release(inst);
        var count: u32 = 0;
        const id = try delivery.ready(Count, std.testing.allocator, inst, mode, .{ .count = &count }, Count.deliver);
        try std.testing.expectEqual(@as(u32, if (mode == 0 or mode == 3) 1 else 0), count);
        pump(inst);
        try std.testing.expectEqual(@as(u32, if (mode == 1) 0 else 1), count);
        var infos = [_]abi.WGPUFutureWaitInfo{.{ .future = id, .completed = 77 }, .{ .future = id, .completed = 78 }};
        try std.testing.expectEqual(@as(u32, 1), wait(inst, &infos, 0));
        try std.testing.expectEqual(@as(u32, 1), count);
        try std.testing.expectEqual(@as(u32, 1), infos[0].completed);
        try std.testing.expectEqual(@as(u32, 1), infos[1].completed);
        try std.testing.expectEqual(@as(u32, 1), wait(inst, &infos, 0));
        try std.testing.expectEqual(@as(u32, 1), inst.ref_count);
    }
}

test "independent futures: immediate and deferred callbacks release final caller ref" {
    for ([_]u32{0, 1, 2, 3}) |mode| {
        const before = adapter.test_deinit_count;
        const inst = try instance();
        var count: u32 = 0;
        const id = try delivery.ready(LastCaller, std.testing.allocator, inst, mode,
            .{ .instance = inst, .count = &count }, LastCaller.deliver);
        if (mode == 1) {
            var infos = [_]abi.WGPUFutureWaitInfo{.{ .future = id, .completed = 0 }};
            try std.testing.expectEqual(@as(u32, 1), wait(inst, &infos, 0));
        } else if (mode == 2) pump(inst);
        try std.testing.expectEqual(@as(u32, 1), count);
        try std.testing.expectEqual(before + 1, adapter.test_deinit_count);
        // inst and callback Node are destroyed: never read them after delivery.
    }
}

test "independent futures: worker publication owns final event ref without observer" {
    const Worker = struct {
        fn run(completion: *owner.Completion) void { completion.markReady(); }
    };
    const before = adapter.test_deinit_count;
    const inst = try instance();
    var count: u32 = 0;
    captured = null;
    _ = try delivery.publish(Count, std.testing.allocator, inst, 3,
        .{ .count = &count }, Count.deliver, hold);
    const completion = captured orelse return error.NoCompletion;
    try std.testing.expectEqual(@as(u32, 2), inst.ref_count);
    release(inst);
    const worker = try std.Thread.spawn(.{}, Worker.run, .{completion});
    worker.join();
    try std.testing.expectEqual(@as(u32, 1), count);
    try std.testing.expectEqual(before + 1, adapter.test_deinit_count);
    // Last real ref was released by actual Node.dispatch on this worker.
}

test "independent futures: payload allocation and ownerless failure preserve caller" {
    const inst = try instance();
    defer release(inst);
    var count: u32 = 0;
    var failing = std.testing.FailingAllocator.init(std.testing.allocator, .{ .fail_index = 0 });
    try std.testing.expectError(error.OutOfMemory, delivery.ready(Count, failing.allocator(), inst, 1, .{ .count = &count }, Count.deliver));
    try std.testing.expectEqual(@as(u32, 1), inst.ref_count);
    try std.testing.expectEqual(@as(u32, 0), count);
    try std.testing.expectError(error.UnsupportedCallbackMode, delivery.ready(Count, std.testing.allocator, inst, 9, .{ .count = &count }, Count.deliver));
    for ([_]u32{1, 2}) |mode| {
        try std.testing.expectError(error.CallbackInstanceUnavailable, delivery.ready(Count, std.testing.allocator, null, mode, .{ .count = &count }, Count.deliver));
    }
    for ([_]u32{0, 3}) |mode| {
        const future = try delivery.ready(Count, std.testing.allocator, null, mode, .{ .count = &count }, Count.deliver);
        try std.testing.expect(future.id >> 63 == 1);
    }
    try std.testing.expectEqual(@as(u32, 2), count);
}

test "independent futures: two instances preserve foreign ID prefix flags and timeout ownership" {
    const a = try instance();
    defer release(a);
    const b = try instance();
    defer release(b);
    var count: u32 = 0;
    captured = null;
    const id = try delivery.publish(Count, std.testing.allocator, a, 1, .{ .count = &count }, Count.deliver, hold);
    const completion = captured.?;
    const foreign = b.pending_completions.newFuture();
    var infos = [_]abi.WGPUFutureWaitInfo{
        .{ .future = id, .completed = 21 }, .{ .future = .{ .id = foreign }, .completed = 22 },
        .{ .future = id, .completed = 23 },
    };
    try std.testing.expectEqual(@as(u32, 3), wait(a, &infos, 0));
    try std.testing.expectEqual(@as(u32, 0), infos[0].completed);
    try std.testing.expectEqual(@as(u32, 0), infos[1].completed);
    try std.testing.expectEqual(@as(u32, 23), infos[2].completed);
    try std.testing.expectEqual(@as(u32, 2), wait(a, infos[0..1], 0));
    try std.testing.expectEqual(@as(u32, 2), wait(a, infos[0..1], 1));
    try std.testing.expectEqual(@as(u32, 2), a.ref_count);
    try std.testing.expectEqual(@as(u32, 0), count);
    completion.markReady();
    try std.testing.expectEqual(@as(u32, 1), wait(a, infos[0..1], 0));
    try std.testing.expectEqual(@as(u32, 1), count);
    try std.testing.expectEqual(@as(u32, 1), a.ref_count);
    var empty: [0]abi.WGPUFutureWaitInfo = .{};
    try std.testing.expectEqual(@as(u32, 1), wait(null, &empty, std.math.maxInt(u64)));
}

test "independent futures: callback self wait and other thread poll differ until actual finish" {
    const Shared = struct {
        instance: *objects.DoeInstance,
        id: base.WGPUFuture = .{ .id = 0 },
        self_status: u32 = 0,
        self_completed: u32 = 0,
        other_status: u32 = 0,
        other_completed: u32 = 0,
        count: u32 = 0,
        fn other(self: *@This()) void {
            var infos = [_]abi.WGPUFutureWaitInfo{.{ .future = self.id, .completed = 41 }};
            self.other_status = wait(self.instance, &infos, 0);
            self.other_completed = infos[0].completed;
        }
        fn deliver(pointer: **@This()) void {
            const self = pointer.*;
            self.count += 1;
            var infos = [_]abi.WGPUFutureWaitInfo{.{ .future = self.id, .completed = 42 }};
            self.self_status = wait(self.instance, &infos, std.math.maxInt(u64));
            self.self_completed = infos[0].completed;
            const other_thread = std.Thread.spawn(.{}, other, .{self}) catch @panic("test thread creation failed");
            other_thread.join();
            pump(self.instance);
        }
    };
    const inst = try instance();
    defer release(inst);
    var shared = Shared{ .instance = inst };
    shared.id = try delivery.ready(*Shared, std.testing.allocator, inst, 2, &shared, Shared.deliver);
    pump(inst);
    try std.testing.expectEqual(@as(u32, 1), shared.count);
    try std.testing.expectEqual(@as(u32, 1), shared.self_status);
    try std.testing.expectEqual(@as(u32, 1), shared.self_completed);
    try std.testing.expectEqual(@as(u32, 2), shared.other_status);
    try std.testing.expectEqual(@as(u32, 0), shared.other_completed);
    var infos = [_]abi.WGPUFutureWaitInfo{.{ .future = shared.id, .completed = 0 }};
    try std.testing.expectEqual(@as(u32, 1), wait(inst, &infos, 0));
}

test "independent futures: pump boundary includes old late readiness but excludes new issuance" {
    const Log = struct { values: [4]u32 = @splat(0), length: usize = 0 };
    const Item = struct {
        log: *Log,
        label: u32,
        fn deliver(self: *@This()) void {
            self.log.values[self.log.length] = self.label;
            self.log.length += 1;
        }
    };
    const Leader = struct {
        instance: *objects.DoeInstance,
        log: *Log,
        late: *?*owner.Completion,
        nested: bool,
        fn deliver(self: *@This()) void {
            var first = Item{ .log = self.log, .label = 1 };
            Item.deliver(&first);
            self.late.*.?.markReady();
            _ = delivery.ready(Item, std.testing.allocator, self.instance, 2, .{ .log = self.log, .label = 3 }, Item.deliver) catch @panic("unexpected test allocation failure");
            if (self.nested) pump(self.instance);
        }
    };
    for ([_]bool{false, true}) |nested| {
        const inst = try instance();
        defer release(inst);
        var log = Log{};
        var late: ?*owner.Completion = null;
        _ = try delivery.ready(Leader, std.testing.allocator, inst, 2, .{ .instance = inst, .log = &log, .late = &late, .nested = nested }, Leader.deliver);
        captured = null;
        _ = try delivery.publish(Item, std.testing.allocator, inst, 2, .{ .log = &log, .label = 2 }, Item.deliver, hold);
        late = captured;
        pump(inst);
        try std.testing.expectEqual(@as(usize, if (nested) 3 else 2), log.length);
        try std.testing.expectEqualSlices(u32, &[_]u32{1, 2}, log.values[0..2]);
        pump(inst);
        try std.testing.expectEqual(@as(usize, 3), log.length);
        try std.testing.expectEqual(@as(u32, 3), log.values[2]);
    }
}

test "independent futures: wait selection uses issue order not reversed request order" {
    const Log = struct { values: [2]u32 = @splat(0), length: usize = 0 };
    const Item = struct {
        log: *Log,
        label: u32,
        fn deliver(self: *@This()) void {
            self.log.values[self.log.length] = self.label;
            self.log.length += 1;
        }
    };
    const inst = try instance();
    defer release(inst);
    var log = Log{};
    const first = try delivery.ready(Item, std.testing.allocator, inst, 1, .{ .log = &log, .label = 1 }, Item.deliver);
    const second = try delivery.ready(Item, std.testing.allocator, inst, 1, .{ .log = &log, .label = 2 }, Item.deliver);
    var infos = [_]abi.WGPUFutureWaitInfo{ .{ .future = second, .completed = 0 }, .{ .future = first, .completed = 0 } };
    try std.testing.expectEqual(@as(u32, 1), wait(inst, &infos, 0));
    try std.testing.expectEqualSlices(u32, &[_]u32{1, 2}, &log.values);
}

test "independent futures: pipeline late dispatch and ownerless cleanup free their wrappers" {
    const Item = struct {
        completion: owner.Completion = .{ .mode = 3 },
        instance: ?*objects.DoeInstance,
        count: *u32,
        fn dispatch(completion: *owner.Completion) void {
            const self: *@This() = @fieldParentPtr("completion", completion);
            const inst = self.instance;
            self.count.* += 1;
            completion.finish();
            std.testing.allocator.destroy(self);
            if (inst) |value| release(value);
        }
    };
    var count: u32 = 0;
    const inst = try instance();
    const item = try std.testing.allocator.create(Item);
    item.* = .{ .instance = inst, .count = &count };
    life.doeNativeInstanceAddRef(@ptrCast(inst));
    inst.pending_completions.register(&item.completion, inst.pending_completions.newFuture());
    // Pipeline supplies its dispatch only after the registration operation.
    item.completion.dispatch = Item.dispatch;
    release(inst);
    item.completion.markReady();
    const ownerless = try std.testing.allocator.create(Item);
    ownerless.* = .{ .instance = null, .count = &count };
    ownerless.completion.dispatch = Item.dispatch;
    ownerless.completion.markReady();
    var uninitialized: owner.Completion = .{};
    uninitialized.finish();
    try std.testing.expectEqual(@as(u32, 2), count);
}

test "independent futures: wait allocation error and checked overflow preserve original flags" {
    const inst = try instance();
    defer release(inst);
    const id = inst.pending_completions.newFuture();
    var infos = [_]abi.WGPUFutureWaitInfo{.{ .future = .{ .id = id }, .completed = 97 }};
    var failing = std.testing.FailingAllocator.init(std.testing.allocator, .{ .fail_index = 0 });
    adapter.test_wait_allocator = failing.allocator();
    defer adapter.test_wait_allocator = null;
    try std.testing.expectEqual(@as(u32, 3), wait(inst, &infos, 0));
    try std.testing.expectEqual(@as(u32, 97), infos[0].completed);
    try std.testing.expectEqual(@as(u32, 1), inst.ref_count);
    const pointer: [*]abi.WGPUFutureWaitInfo = @ptrCast(&infos);
    const too_many = std.math.maxInt(usize) / @sizeOf(adapter.c.doe_future_wait_info) + 1;
    // The adapter checks multiplication before touching this deliberately oversized view.
    try std.testing.expectError(error.Overflow, inst.pending_completions.waitAny(pointer[0..too_many], 0));
    try std.testing.expectEqual(@as(u32, 97), infos[0].completed);
}

test "independent futures: C Zig actual storage layout is source-bound" {
    try adapter.verifyLayout();
    try std.testing.expect(@sizeOf(adapter.c.doe_future_queue) > 0);
}

test "independent futures: outstanding queue obligation fails closed subprocess" {
    const requested = std.process.getEnvVarOwned(std.testing.allocator, "EVAL_BUSY_QUEUE") catch return error.SkipZigTest;
    defer std.testing.allocator.free(requested);
    const inst = try instance();
    var completion: owner.Completion = .{};
    inst.pending_completions.register(&completion, inst.pending_completions.newFuture());
    // This deliberately violates external quiescence, with no synthetic event ref.
    release(inst);
    return error.UnexpectedSuccessfulDestruction;
}

test "independent futures: another thread waits through callback execution until finish" {
    const Shared = struct {
        inst: *objects.DoeInstance,
        id: base.WGPUFuture,
        mutex: std.Thread.Mutex = .{},
        changed: std.Thread.Condition = .{},
        callback_entered: bool = false,
        allow_finish: bool = false,
        waiter_status: u32 = 0,
        waiter_flag: u32 = 0,
        fn deliver(pointer: **@This()) void {
            const self = pointer.*;
            self.mutex.lock();
            defer self.mutex.unlock();
            self.callback_entered = true;
            self.changed.broadcast();
            while (!self.allow_finish) self.changed.wait(&self.mutex);
        }
        fn awaitCompletion(self: *@This()) void {
            var infos = [_]abi.WGPUFutureWaitInfo{.{ .future = self.id, .completed = 0 }};
            self.waiter_status = wait(self.inst, &infos, std.math.maxInt(u64));
            self.waiter_flag = infos[0].completed;
        }
        fn publish(completion: *owner.Completion) void { completion.markReady(); }
    };
    const inst = try instance();
    defer release(inst);
    var shared = Shared{ .inst = inst, .id = .{ .id = 0 } };
    captured = null;
    shared.id = try delivery.publish(*Shared, std.testing.allocator, inst, 3, &shared, Shared.deliver, hold);
    const waiter = try std.Thread.spawn(.{}, Shared.awaitCompletion, .{&shared});
    const publisher = try std.Thread.spawn(.{}, Shared.publish, .{captured.?});
    shared.mutex.lock();
    while (!shared.callback_entered) shared.changed.wait(&shared.mutex);
    shared.mutex.unlock();
    // Observe only, under the real C mutex: both producer and waiter entered C.
    while (true) {
        const q = &inst.pending_completions.storage;
        try std.testing.expectEqual(@as(c_int, 0), adapter.c.pthread_mutex_lock(&q.mutex));
        const both_active = q.active >= 2;
        try std.testing.expectEqual(@as(c_int, 0), adapter.c.pthread_mutex_unlock(&q.mutex));
        if (both_active) break;
        std.Thread.yield() catch {};
    }
    var infos = [_]abi.WGPUFutureWaitInfo{.{ .future = shared.id, .completed = 99 }};
    try std.testing.expectEqual(@as(u32, 2), wait(inst, &infos, 0));
    try std.testing.expectEqual(@as(u32, 0), infos[0].completed);
    shared.mutex.lock();
    shared.allow_finish = true;
    shared.changed.broadcast();
    shared.mutex.unlock();
    publisher.join();
    waiter.join();
    try std.testing.expectEqual(@as(u32, 1), shared.waiter_status);
    try std.testing.expectEqual(@as(u32, 1), shared.waiter_flag);
    try std.testing.expectEqual(@as(u32, 1), inst.ref_count);
}
