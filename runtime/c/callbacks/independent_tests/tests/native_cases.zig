// SPDX-License-Identifier: MIT
// Independent adapted real instance + generic callback owner CPU fixtures.
const std = @import("std");
const delivery = @import("src/native/support/doe_callback_owner.zig");
const future = @import("src/native/support/doe_future_c_adapter.zig");
const life = @import("src/native/lifecycle/doe_instance_device_native.zig");
const objects = @import("src/native/support/doe_native_object_types.zig");
const abi = @import("src/core/abi/wgpu_callback_descriptor_types.zig");
const base = @import("src/core/abi/wgpu_core_base_types.zig");
const pool = @import("src/runtime/task_pool.zig");
fn check(condition: bool) void {
    if (!condition) @panic("independent callback invariant failed");
}
fn create() !*objects.DoeInstance {
    return @ptrCast(@alignCast(life.doeNativeCreateInstance(null) orelse return error.CreationFailed));
}
fn release(p: *objects.DoeInstance) void {
    life.doeNativeInstanceRelease(@ptrCast(p));
}
const Reply = struct {
    owner: *objects.DoeInstance,
    count: *u32,
    drop: bool,
    bytes: [43]u8 = @splat(0x79),
    borrowed: *u32,
    fn deliver(self: *@This()) void {
        for (self.bytes) |b| check(b == 0x79);
        check(self.borrowed.* == 92);
        self.count.* += 1;
        if (self.drop) release(self.owner);
        self.bytes[0] = 0;
    }
};
var token: ?*delivery.Publication = null;
fn hold(p: *delivery.Publication) void {
    token = p;
}
fn ready_job(p: ?*anyopaque) void {
    delivery.markReady(@ptrCast(@alignCast(p.?)));
}
fn transport(p: *delivery.Publication) void {
    pool.submitWithAllocator(std.testing.allocator, .{ .run = ready_job, .ctx = p }) catch delivery.markReady(p);
}
var fallback_count: usize = 0;
fn failing_transport(p: *delivery.Publication) void {
    var fail = std.testing.FailingAllocator.init(std.testing.allocator, .{ .fail_index = 0 });
    pool.submitWithAllocator(fail.allocator(), .{ .run = ready_job, .ctx = p }) catch {
        fallback_count += 1;
        delivery.markReady(p);
    };
}

test "independent callbacks: exact modes and source-local inline transfer" {
    for ([_]u32{ 0, 1, 2, 3 }) |mode| {
        const owner = try create();
        defer release(owner);
        var count: u32 = 0;
        var borrowed: u32 = 17;
        var source = Reply{ .owner = owner, .count = &count, .drop = false, .borrowed = &borrowed };
        token = null;
        const id = try delivery.publish(Reply, std.testing.allocator, owner, mode, source, Reply.deliver, hold);
        try std.testing.expectEqual(@as(u32, 2), owner.ref_count);
        source.bytes = @splat(0x11);
        borrowed = 92;
        delivery.markReady(token.?);
        token = null;
        try std.testing.expectEqual(@as(u32, if (mode == 0 or mode == 3) 1 else 0), count);
        owner.pending_completions.processEvents();
        try std.testing.expectEqual(@as(u32, if (mode != 1) 1 else 0), count);
        var infos = [_]abi.WGPUFutureWaitInfo{ .{ .future = id, .completed = 41 }, .{ .future = id, .completed = 42 } };
        try std.testing.expect(try owner.pending_completions.waitAny(&infos, 0));
        try std.testing.expectEqual(@as(u32, 1), infos[0].completed);
        try std.testing.expectEqual(@as(u32, 1), infos[1].completed);
        try std.testing.expectEqual(@as(u32, 1), count);
        try std.testing.expectEqual(@as(u32, 1), owner.ref_count);
    }
}

test "independent callbacks: immediate and deferred final release has no observer" {
    for ([_]u32{ 0, 1, 2, 3 }) |mode| {
        const before = future.test_deinit_count;
        const owner = try create();
        var count: u32 = 0;
        var borrowed: u32 = 92;
        const id = try delivery.ready(Reply, std.testing.allocator, owner, mode, .{ .owner = owner, .count = &count, .drop = true, .borrowed = &borrowed }, Reply.deliver);
        if (mode == 1) {
            var infos = [_]abi.WGPUFutureWaitInfo{.{ .future = id, .completed = 0 }};
            try std.testing.expect(try owner.pending_completions.waitAny(&infos, 0));
        } else if (mode == 2) owner.pending_completions.processEvents();
        try std.testing.expectEqual(@as(u32, 1), count);
        try std.testing.expectEqual(before + 1, future.test_deinit_count);
        // Owner and payload are freed; only independent non-owning counters remain.
    }
}

test "independent callbacks: pending worker is final owner and pool drains it" {
    pool.shutdown();
    const before = future.test_deinit_count;
    const owner = try create();
    var count: u32 = 0;
    var borrowed: u32 = 92;
    token = null;
    _ = try delivery.publish(Reply, std.testing.allocator, owner, 3, .{ .owner = owner, .count = &count, .drop = false, .borrowed = &borrowed }, Reply.deliver, hold);
    release(owner);
    try pool.submitWithAllocator(std.testing.allocator, .{ .run = ready_job, .ctx = token.? });
    token = null;
    pool.shutdown();
    try std.testing.expectEqual(@as(u32, 1), count);
    try std.testing.expectEqual(before + 1, future.test_deinit_count);
}

test "independent callbacks: actual task transport and failure-before-transfer fallback" {
    for ([_]bool{ false, true }) |fail| {
        pool.shutdown();
        const before = future.test_deinit_count;
        const prior_fallback = fallback_count;
        const owner = try create();
        var count: u32 = 0;
        var borrowed: u32 = 92;
        if (fail) {
            _ = try delivery.publish(Reply, std.testing.allocator, owner, 3, .{ .owner = owner, .count = &count, .drop = true, .borrowed = &borrowed }, Reply.deliver, failing_transport);
        } else {
            _ = try delivery.publish(Reply, std.testing.allocator, owner, 3, .{ .owner = owner, .count = &count, .drop = true, .borrowed = &borrowed }, Reply.deliver, transport);
        }
        pool.shutdown();
        try std.testing.expectEqual(@as(u32, 1), count);
        try std.testing.expectEqual(before + 1, future.test_deinit_count);
        try std.testing.expectEqual(prior_fallback + @as(usize, if (fail) 1 else 0), fallback_count);
    }
}

test "independent callbacks: admission failure leaves identity resources and instance untouched" {
    const owner = try create();
    defer release(owner);
    const before = owner.pending_completions.newFuture();
    var count: u32 = 0;
    var borrowed: u32 = 92;
    var fail = std.testing.FailingAllocator.init(std.testing.allocator, .{ .fail_index = 0 });
    const reply = Reply{ .owner = owner, .count = &count, .drop = true, .borrowed = &borrowed };
    try std.testing.expectError(error.OutOfMemory, delivery.ready(Reply, fail.allocator(), owner, 1, reply, Reply.deliver));
    try std.testing.expectEqual(@as(u32, 1), owner.ref_count);
    try std.testing.expectEqual(@as(u32, 0), count);
    try std.testing.expectEqual(before + 1, owner.pending_completions.newFuture());
    try std.testing.expectError(error.UnsupportedCallbackMode, delivery.ready(Reply, fail.allocator(), owner, 7, reply, Reply.deliver));
    for ([_]u32{ 1, 2 }) |mode| try std.testing.expectError(error.CallbackInstanceUnavailable, delivery.ready(Reply, fail.allocator(), null, mode, reply, Reply.deliver));
}

test "independent callbacks: ownerless uses mutable separate local and callback before legacy ID" {
    const Local = struct {
        marker: u32,
        seen: *u32,
        nested_id: *u64,
        fn inner(p: *@This()) void {
            p.seen.* += 1;
            p.marker = 99;
        }
        fn outer(p: *@This()) void {
            p.seen.* += 1;
            p.marker = 55;
            const f = delivery.ready(@This(), std.testing.allocator, null, 0, p.*, inner) catch @panic("ownerless nested failed");
            p.nested_id.* = f.id;
        }
    };
    for ([_]u32{ 0, 3 }) |mode| {
        var seen: u32 = 0;
        var nested: u64 = 0;
        const local = Local{ .marker = 17, .seen = &seen, .nested_id = &nested };
        var fail = std.testing.FailingAllocator.init(std.testing.allocator, .{ .fail_index = 0 });
        const id = try delivery.publish(Local, fail.allocator(), null, mode, local, Local.outer, hold);
        try std.testing.expectEqual(@as(u32, 17), local.marker);
        try std.testing.expectEqual(@as(u32, 2), seen);
        try std.testing.expectEqual(nested + 1, id.id);
    }
}

test "independent callbacks: real queue self wait and foreign thread remain registered" {
    const Context = struct {
        owner: *objects.DoeInstance,
        id: *base.WGPUFuture,
        count: *u32,
        fn foreign(p: *@This()) void {
            var infos = [_]abi.WGPUFutureWaitInfo{.{ .future = p.id.*, .completed = 7 }};
            const done = p.owner.pending_completions.waitAny(&infos, 0) catch @panic("foreign wait failed");
            check(!done and infos[0].completed == 0);
        }
        fn deliver(p: *@This()) void {
            var infos = [_]abi.WGPUFutureWaitInfo{.{ .future = p.id.*, .completed = 8 }};
            check(p.owner.pending_completions.waitAny(&infos, std.math.maxInt(u64)) catch @panic("self wait failed"));
            check(infos[0].completed == 1);
            const worker = std.Thread.spawn(.{}, foreign, .{p}) catch @panic("thread failed");
            worker.join();
            p.count.* += 1;
            release(p.owner);
        }
    };
    const before = future.test_deinit_count;
    const owner = try create();
    var id: base.WGPUFuture = undefined;
    var count: u32 = 0;
    id = try delivery.ready(Context, std.testing.allocator, owner, 1, .{ .owner = owner, .id = &id, .count = &count }, Context.deliver);
    var infos = [_]abi.WGPUFutureWaitInfo{.{ .future = id, .completed = 0 }};
    try std.testing.expect(try owner.pending_completions.waitAny(&infos, 0));
    try std.testing.expectEqual(@as(u32, 1), count);
    try std.testing.expectEqual(before + 1, future.test_deinit_count);
}

const AllocatorProbe = struct {
    allocations: usize = 0,
    frees: usize = 0,
    size: usize = 0,
    alignment: std.mem.Alignment = .@"1",
    pointer: ?[*]u8 = null,
    pub const vtable = std.mem.Allocator.VTable{
        .alloc = alloc,
        .resize = std.mem.Allocator.noResize,
        .remap = std.mem.Allocator.noRemap,
        .free = free,
    };
    fn allocator(self: *@This()) std.mem.Allocator {
        return .{ .ptr = self, .vtable = &vtable };
    }
    fn alloc(pointer: *anyopaque, size: usize, alignment: std.mem.Alignment, address: usize) ?[*]u8 {
        const self: *@This() = @ptrCast(@alignCast(pointer));
        self.allocations += 1;
        self.size = size;
        self.alignment = alignment;
        self.pointer = std.testing.allocator.rawAlloc(size, alignment, address);
        return self.pointer;
    }
    fn free(pointer: *anyopaque, memory: []u8, alignment: std.mem.Alignment, address: usize) void {
        const self: *@This() = @ptrCast(@alignCast(pointer));
        check(memory.ptr == self.pointer.? and memory.len == self.size and alignment == self.alignment);
        self.frees += 1;
        std.testing.allocator.rawFree(memory, alignment, address);
    }
};

test "independent callbacks: original allocator identity and over-aligned typed destination" {
    const Aligned = struct {
        marker: u8 align(4096),
        count: *u32,
        fn deliver(self: *@This()) void {
            check(@intFromPtr(self) % 4096 == 0 and self.marker == 37);
            self.count.* += 1;
        }
    };
    const owner = try create();
    defer release(owner);
    var probe = AllocatorProbe{};
    var count: u32 = 0;
    const id = blk: {
        var local_allocator = probe.allocator();
        const f = try delivery.ready(Aligned, local_allocator, owner, 1, .{ .marker = 37, .count = &count }, Aligned.deliver);
        local_allocator = std.testing.failing_allocator;
        break :blk f;
    };
    try std.testing.expectEqual(@as(usize, 1), probe.allocations);
    try std.testing.expectEqual(@as(usize, 0), probe.frees);
    try std.testing.expect(probe.alignment.toByteUnits() >= 4096);
    var infos = [_]abi.WGPUFutureWaitInfo{.{ .future = id, .completed = 0 }};
    try std.testing.expect(try owner.pending_completions.waitAny(&infos, 0));
    try std.testing.expectEqual(@as(u32, 1), count);
    try std.testing.expectEqual(@as(usize, 1), probe.frees);
}

test "independent callbacks: stateless c allocator owns deferred event and exact final destruction" {
    for ([_]u32{ 1, 2 }) |mode| {
        const before = future.test_deinit_count;
        const owner = try create();
        var count: u32 = 0;
        var borrowed: u32 = 92;
        const id = try delivery.ready(Reply, std.heap.c_allocator, owner, mode, .{
            .owner = owner,
            .count = &count,
            .drop = true,
            .borrowed = &borrowed,
        }, Reply.deliver);
        try std.testing.expectEqual(@as(u32, 0), count);
        try std.testing.expectEqual(@as(u32, 2), owner.ref_count);
        try std.testing.expectEqual(before, future.test_deinit_count);
        if (mode == 1) {
            var infos = [_]abi.WGPUFutureWaitInfo{.{ .future = id, .completed = 73 }};
            try std.testing.expect(try owner.pending_completions.waitAny(&infos, 0));
            try std.testing.expectEqual(@as(u32, 1), infos[0].completed);
        } else owner.pending_completions.processEvents();
        try std.testing.expectEqual(@as(u32, 1), count);
        try std.testing.expectEqual(before + 1, future.test_deinit_count);
        // No read of a potentially undefined stateless allocator.ptr is used
        // as an assertion. Correct representation remains a source obligation.
    }
}
