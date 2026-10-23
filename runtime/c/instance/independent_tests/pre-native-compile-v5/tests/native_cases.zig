// SPDX-License-Identifier: MIT
// Independent real C-instance consumers; fixture written before candidate inspection.
const std = @import("std");
const ci = @cImport({
    @cInclude("doe_instance.h");
});
const future = @import("src/native/support/doe_future_c_adapter.zig");
const delivery = @import("src/native/support/doe_callback_owner.zig");
const life = @import("src/native/lifecycle/doe_instance_device_native.zig");
const helpers = @import("src/native/support/doe_native_object_helpers.zig");
const objects = @import("src/native/support/doe_native_object_types.zig");
const loss = @import("src/native/support/doe_device_loss.zig");
const surfaces = @import("src/native/surface/doe_surface_native.zig");
const queue = @import("src/native/queue/doe_queue_lifecycle.zig");
const abi = @import("src/core/abi/wgpu_callback_descriptor_types.zig");
const base = @import("src/core/abi/wgpu_core_base_types.zig");
const pool = @import("src/runtime/task_pool.zig");
fn check(ok: bool) void {
    if (!ok) @panic("independent instance invariant failed");
}
fn retain(p: *objects.DoeInstance) void {
    life.doeNativeInstanceAddRef(@ptrCast(p));
}
fn release(p: *objects.DoeInstance) void {
    life.doeNativeInstanceRelease(@ptrCast(p));
}
fn pending(p: *objects.DoeInstance) future.QueueView {
    return helpers.instance_pending(p);
}
const Observation = struct {
    allocations: u32 = 0,
    frees: u32 = 0,
    labels: u32 = 0,
    observed: u32 = 0,
    wait_allocations: u32 = 0,
    wait_frees: u32 = 0,
    fail_wait: bool = false,
    pointer: ?*anyopaque = null,
    size: usize = 0,
    alignment: usize = 0,
    fn objectAlloc(raw: ?*anyopaque, vtable: ?*const anyopaque, n: usize, a: usize) callconv(.c) ?*anyopaque {
        const self: *@This() = @ptrCast(@alignCast(raw.?));
        check(vtable == &object_token);
        const bytes = std.testing.allocator.rawAlloc(n, .fromByteUnits(a), @returnAddress()) orelse return null;
        self.pointer = @ptrCast(bytes);
        self.size = n;
        self.alignment = a;
        self.allocations += 1;
        return self.pointer;
    }
    fn objectFree(raw: ?*anyopaque, vtable: ?*const anyopaque, memory: ?*anyopaque, n: usize, a: usize) callconv(.c) void {
        const self: *@This() = @ptrCast(@alignCast(raw.?));
        check(vtable == &object_token and memory == self.pointer and n == self.size and a == self.alignment and self.labels == 1);
        self.frees += 1;
        const bytes: [*]u8 = @ptrCast(memory.?);
        std.testing.allocator.rawFree(bytes[0..n], .fromByteUnits(a), @returnAddress());
    }
    fn waitAlloc(raw: ?*anyopaque, vtable: ?*const anyopaque, n: usize, a: usize) callconv(.c) ?*anyopaque {
        const self: *@This() = @ptrCast(@alignCast(raw.?));
        check(vtable == &wait_token);
        if (self.fail_wait) return null;
        const bytes = std.testing.allocator.rawAlloc(n, .fromByteUnits(a), @returnAddress()) orelse return null;
        _ = @atomicRmw(u32, &self.wait_allocations, .Add, 1, .monotonic);
        return @ptrCast(bytes);
    }
    fn waitFree(raw: ?*anyopaque, vtable: ?*const anyopaque, memory: ?*anyopaque, n: usize, a: usize) callconv(.c) void {
        const self: *@This() = @ptrCast(@alignCast(raw.?));
        check(vtable == &wait_token and self.frees == 0);
        _ = @atomicRmw(u32, &self.wait_frees, .Add, 1, .monotonic);
        const bytes: [*]u8 = @ptrCast(memory.?);
        std.testing.allocator.rawFree(bytes[0..n], .fromByteUnits(a), @returnAddress());
    }
    fn observe(raw: ?*anyopaque, object: ?*ci.doe_instance, n: usize, a: usize) callconv(.c) void {
        const self: *@This() = @ptrCast(@alignCast(raw.?));
        check(@as(?*anyopaque, @ptrCast(object)) == self.pointer and n == self.size and a == self.alignment);
        self.observed += 1;
    }
    fn label(raw: ?*anyopaque, object: ?*ci.doe_instance) callconv(.c) void {
        const self: *@This() = @ptrCast(@alignCast(raw.?));
        check(@as(?*anyopaque, @ptrCast(object)) == self.pointer and self.frees == 0);
        self.labels += 1;
    }
    fn create(self: *@This()) !*objects.DoeInstance {
        try future.verifyLayout();
        var config: ci.doe_instance_config = std.mem.zeroes(ci.doe_instance_config);
        config.abi_version = 1;
        config.object_allocator = .{ .state = self, .vtable = &object_token, .allocate = objectAlloc, .deallocate = objectFree };
        config.wait_allocator = .{ .state = self, .vtable = &wait_token, .allocate = waitAlloc, .deallocate = waitFree };
        config.hook_userdata = self;
        config.allocated = observe;
        config.remove_label = label;
        var p: ?*ci.doe_instance = null;
        try std.testing.expectEqual(@as(ci.doe_instance_status, ci.DOE_INSTANCE_OK), ci.doe_instance_create(&config, &p));
        try std.testing.expect(p != null and self.observed == 1);
        return @ptrCast(p.?);
    }
    fn final(self: *@This()) !void {
        try std.testing.expectEqual(@as(u32, 1), self.allocations);
        try std.testing.expectEqual(@as(u32, 1), self.frees);
        try std.testing.expectEqual(@as(u32, 1), self.labels);
        try std.testing.expectEqual(self.wait_allocations, self.wait_frees);
    }
};
var object_token: u8 = 0;
var wait_token: u8 = 1;
const Payload = struct {
    completion: future.Completion = .{},
    owner: *objects.DoeInstance,
    count: *u32,
    freed: *u32,
    fn dispatch(completion: *future.Completion) void {
        const self: *@This() = @fieldParentPtr("completion", completion);
        const owner = self.owner;
        const freed = self.freed;
        self.count.* += 1;
        release(owner); // Last caller disappears within actual delivery callback.
        completion.finish();
        check(completion.owner == null and !completion.initialized);
        std.testing.allocator.destroy(self);
        freed.* += 1;
        release(owner); // The pending event's independently owned lease.
    }
    fn publish(self: *@This()) void {
        self.completion.markReady();
    }
};
fn escapedPayload(owner: *objects.DoeInstance, count: *u32, freed: *u32, mode: u32) !*Payload {
    const payload = try std.testing.allocator.create(Payload);
    payload.* = .{ .owner = owner, .count = count, .freed = freed, .completion = .{ .mode = mode } };
    retain(owner);
    {
        var view = pending(owner);
        view.register(&payload.completion, view.newFuture());
        payload.completion.dispatch = Payload.dispatch; // Deliberately after register.
        @memset(std.mem.asBytes(&view), 0xA5); // Never retain an address of this value.
    }
    return payload;
}
test "independent instance: copied local view late dispatch and joined final publisher" {
    var observation = Observation{};
    const owner = try observation.create();
    var count: u32 = 0;
    var freed: u32 = 0;
    const payload = try escapedPayload(owner, &count, &freed, 3);
    const worker = try std.Thread.spawn(.{}, Payload.publish, .{payload});
    worker.join();
    try std.testing.expectEqual(@as(u32, 1), count);
    try std.testing.expectEqual(@as(u32, 1), freed);
    try observation.final();
}
test "independent instance: direct copied view wait and pump keep final call alive" {
    for ([_]u32{ 1, 2 }) |mode| {
        var observation = Observation{};
        const owner = try observation.create();
        var count: u32 = 0;
        var freed: u32 = 0;
        const payload = try escapedPayload(owner, &count, &freed, mode);
        const view = pending(owner);
        const id = payload.completion.storage.id;
        payload.publish();
        if (mode == 1) {
            var infos = [_]abi.WGPUFutureWaitInfo{.{ .future = .{ .id = id }, .completed = 41 }};
            try std.testing.expect(try view.waitAny(&infos, 0));
            try std.testing.expectEqual(@as(u32, 1), infos[0].completed);
        } else view.processEvents();
        try std.testing.expectEqual(@as(u32, 1), count);
        try std.testing.expectEqual(@as(u32, 1), freed);
        try observation.final();
    }
}
test "independent instance: never initialized and ownerless completion cleanup" {
    const Local = struct {
        completion: future.Completion = .{},
        count: u32 = 0,
        fn dispatch(c: *future.Completion) void {
            const p: *@This() = @fieldParentPtr("completion", c);
            p.count += 1;
            c.finish();
        }
    };
    var local = Local{};
    local.completion.finish();
    local.completion.dispatch = Local.dispatch;
    local.completion.markReady();
    try std.testing.expectEqual(@as(u32, 1), local.count);
    try std.testing.expect(!local.completion.initialized and local.completion.owner == null);
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
var held: ?*delivery.Publication = null;
fn hold(p: *delivery.Publication) void {
    held = p;
}
fn readyJob(p: ?*anyopaque) void {
    delivery.markReady(@ptrCast(@alignCast(p.?)));
}
fn transport(p: *delivery.Publication) void {
    pool.submitWithAllocator(std.testing.allocator, .{ .run = readyJob, .ctx = p }) catch delivery.markReady(p);
}
var fallback_count: u32 = 0;
fn failingTransport(p: *delivery.Publication) void {
    var fail = std.testing.FailingAllocator.init(std.testing.allocator, .{ .fail_index = 0 });
    pool.submitWithAllocator(fail.allocator(), .{ .run = readyJob, .ctx = p }) catch {
        fallback_count += 1;
        delivery.markReady(p);
    };
}
test "independent instance: actual callbacks preserve inline transfer modes and final native entry" {
    for ([_]u32{ 0, 1, 2, 3 }) |mode| {
        var observation = Observation{};
        const owner = try observation.create();
        var count: u32 = 0;
        var borrowed: u32 = 17;
        var source = Reply{ .owner = owner, .count = &count, .drop = true, .borrowed = &borrowed };
        held = null;
        const id = try delivery.publish(Reply, std.testing.allocator, owner, mode, source, Reply.deliver, hold);
        source.bytes = @splat(0x11);
        borrowed = 92;
        delivery.markReady(held.?);
        held = null;
        try std.testing.expectEqual(@as(u32, if (mode == 0 or mode == 3) 1 else 0), count);
        if (mode == 1) {
            life.doeNativeInstanceProcessEvents(@ptrCast(owner));
            try std.testing.expectEqual(@as(u32, 0), count);
            var infos = [_]abi.WGPUFutureWaitInfo{ .{ .future = id, .completed = 41 }, .{ .future = id, .completed = 42 } };
            try std.testing.expectEqual(@as(u32, 1), life.doeNativeInstanceWaitAny(@ptrCast(owner), 2, &infos, 0));
            try std.testing.expectEqual(@as(u32, 1), infos[0].completed);
            try std.testing.expectEqual(@as(u32, 1), infos[1].completed);
        } else if (mode == 2) life.doeNativeInstanceProcessEvents(@ptrCast(owner));
        try std.testing.expectEqual(@as(u32, 1), count);
        try observation.final();
    }
}
test "independent instance: actual task submission fallback and pool shutdown" {
    for ([_]bool{ false, true }) |fail| {
        pool.shutdown();
        var observation = Observation{};
        const owner = try observation.create();
        var count: u32 = 0;
        var borrowed: u32 = 92;
        const before = fallback_count;
        _ = try delivery.publish(Reply, std.testing.allocator, owner, 3, .{ .owner = owner, .count = &count, .drop = true, .borrowed = &borrowed }, Reply.deliver, if (fail) failingTransport else transport);
        pool.shutdown();
        try std.testing.expectEqual(@as(u32, 1), count);
        try observation.final();
        try std.testing.expectEqual(before + @as(u32, if (fail) 1 else 0), fallback_count);
    }
    pool.shutdown();
    var observation = Observation{};
    const owner = try observation.create();
    var count: u32 = 0;
    var borrowed: u32 = 92;
    held = null;
    _ = try delivery.publish(Reply, std.testing.allocator, owner, 3, .{ .owner = owner, .count = &count, .drop = false, .borrowed = &borrowed }, Reply.deliver, hold);
    release(owner);
    try std.testing.expectEqual(@as(u32, 0), observation.frees);
    try pool.submitWithAllocator(std.testing.allocator, .{ .run = readyJob, .ctx = held.? });
    held = null;
    pool.shutdown();
    try std.testing.expectEqual(@as(u32, 1), count);
    try observation.final();
}
test "independent instance: allocation and native wait failure preserve instance identity flags" {
    var observation = Observation{};
    const owner = try observation.create();
    const view = pending(owner);
    const before = view.newFuture();
    var count: u32 = 0;
    var borrowed: u32 = 92;
    var fail = std.testing.FailingAllocator.init(std.testing.allocator, .{ .fail_index = 0 });
    try std.testing.expectError(error.OutOfMemory, delivery.ready(Reply, fail.allocator(), owner, 1, .{ .owner = owner, .count = &count, .drop = true, .borrowed = &borrowed }, Reply.deliver));
    try std.testing.expectEqual(before + 1, view.newFuture());
    try std.testing.expectEqual(@as(u32, 0), count);
    var completion: future.Completion = .{};
    const id = view.newFuture();
    view.register(&completion, id);
    var infos = [_]abi.WGPUFutureWaitInfo{.{ .future = .{ .id = id }, .completed = 37 }};
    observation.fail_wait = true;
    try std.testing.expectEqual(@as(u32, 3), life.doeNativeInstanceWaitAny(@ptrCast(owner), 1, &infos, 0));
    try std.testing.expectEqual(@as(u32, 37), infos[0].completed);
    observation.fail_wait = false;
    completion.finish();
    release(owner);
    try observation.final();
}
test "independent instance: real self wait differs from joined foreign thread" {
    const Context = struct {
        owner: *objects.DoeInstance,
        id: *base.WGPUFuture,
        count: *u32,
        fn other(self: *@This()) void {
            var infos = [_]abi.WGPUFutureWaitInfo{.{ .future = self.id.*, .completed = 7 }};
            check(!(pending(self.owner).waitAny(&infos, 0) catch @panic("foreign wait")));
            check(infos[0].completed == 0);
        }
        fn deliver(self: *@This()) void {
            var infos = [_]abi.WGPUFutureWaitInfo{.{ .future = self.id.*, .completed = 8 }};
            check(pending(self.owner).waitAny(&infos, std.math.maxInt(u64)) catch @panic("self wait"));
            check(infos[0].completed == 1);
            const worker = std.Thread.spawn(.{}, other, .{self}) catch @panic("thread");
            worker.join();
            self.count.* += 1;
            release(self.owner);
        }
    };
    var observation = Observation{};
    const owner = try observation.create();
    var id: base.WGPUFuture = undefined;
    var count: u32 = 0;
    id = try delivery.ready(Context, std.testing.allocator, owner, 1, .{ .owner = owner, .id = &id, .count = &count }, Context.deliver);
    var infos = [_]abi.WGPUFutureWaitInfo{.{ .future = id, .completed = 0 }};
    try std.testing.expect(try pending(owner).waitAny(&infos, 0));
    try std.testing.expectEqual(@as(u32, 1), count);
    try observation.final();
}
fn retained(raw: ?*anyopaque) callconv(.c) void {
    life.doeNativeInstanceAddRef(raw);
}
fn released(raw: ?*anyopaque) callconv(.c) void {
    life.doeNativeInstanceRelease(raw);
}
fn noDevice(_: ?*anyopaque) callconv(.c) void {
    @panic("null loss fixture unexpectedly retained a device");
}
const references = loss.References{ .retain_device = noDevice, .release_device = noDevice, .retain_instance = retained, .release_instance = released };
test "independent instance: actual null-device loss event owns final callback lifetime" {
    const Capture = struct {
        owner: *objects.DoeInstance,
        count: u32 = 0,
        fn callback(raw: ?*const anyopaque, reason: abi.WGPUDeviceLostReason, message: base.WGPUStringView, userdata: ?*anyopaque, _: ?*anyopaque) callconv(.c) void {
            const self: *@This() = @ptrCast(@alignCast(userdata.?));
            const handle: *const ?*anyopaque = @ptrCast(@alignCast(raw.?));
            check(handle.* == null and reason == .destroyed and std.mem.eql(u8, message.data.?[0..message.length], "instance-evaluator-loss"));
            self.count += 1;
            release(self.owner);
        }
    };
    var observation = Observation{};
    const owner = try observation.create();
    var capture = Capture{ .owner = owner };
    var info = std.mem.zeroes(abi.WGPUDeviceLostCallbackInfo);
    info.mode = 2;
    info.callback = Capture.callback;
    info.userdata1 = &capture;
    const before = pending(owner).newFuture();
    var fail = std.testing.FailingAllocator.init(std.testing.allocator, .{ .fail_index = 0 });
    try std.testing.expectError(error.OutOfMemory, loss.Event.create(fail.allocator(), pending(owner), owner, info, references));
    try std.testing.expectEqual(before + 1, pending(owner).newFuture());
    const event = try loss.Event.create(std.testing.allocator, pending(owner), owner, info, references);
    event.lose(.destroyed, "instance-evaluator-loss");
    event.lose(.failedCreation, "ignored");
    event.release();
    try std.testing.expectEqual(@as(u32, 0), observation.frees);
    life.doeNativeInstanceProcessEvents(@ptrCast(owner));
    try std.testing.expectEqual(@as(u32, 1), capture.count);
    try observation.final();
}
test "independent instance: real unconfigured surface is sole remaining instance lease" {
    var observation = Observation{};
    const owner = try observation.create();
    var descriptor = std.mem.zeroes(ci.WGPUSurfaceDescriptor);
    const surface = surfaces.doeNativeInstanceCreateSurface(@ptrCast(owner), @ptrCast(&descriptor), null) orelse return error.SurfaceCreateFailed;
    release(owner);
    try std.testing.expectEqual(@as(u32, 0), observation.frees);
    pending(owner).processEvents();
    surfaces.doeNativeSurfaceRelease(surface);
    try observation.final();
    var second = Observation{};
    const owner2 = try second.create();
    var invalid: [@sizeOf(objects.DoeDevice)]u8 align(@alignOf(objects.DoeDevice)) = @splat(0);
    try std.testing.expect(surfaces.doeNativeInstanceCreateSurface(@ptrCast(owner2), @ptrCast(&descriptor), @ptrCast(&invalid)) == null);
    try std.testing.expectEqual(@as(u32, 0), second.frees);
    pending(owner2).processEvents();
    release(owner2);
    try second.final();
}
test "independent instance: invalid process events drains actual legacy global obligation" {
    const Capture = struct {
        count: u32 = 0,
        fn callback(status: abi.WGPUQueueWorkDoneStatus, message: base.WGPUStringView, raw: ?*anyopaque, _: ?*anyopaque) callconv(.c) void {
            const self: *@This() = @ptrCast(@alignCast(raw.?));
            check(status == .success and message.length == 0);
            self.count += 1;
        }
    };
    var capture = Capture{};
    var info = std.mem.zeroes(abi.WGPUQueueWorkDoneCallbackInfo);
    info.mode = 2;
    info.callback = Capture.callback;
    info.userdata1 = &capture;
    _ = queue.doeNativeQueueOnSubmittedWorkDone(null, info);
    try std.testing.expectEqual(@as(u32, 0), capture.count);
    var invalid: [8]u64 = @splat(0);
    invalid[0] = 0xDEADC0DE;
    life.doeNativeInstanceProcessEvents(@ptrCast(&invalid));
    try std.testing.expectEqual(@as(u32, 1), capture.count);
    _ = queue.doeNativeQueueOnSubmittedWorkDone(null, info);
    life.doeNativeInstanceProcessEvents(null);
    try std.testing.expectEqual(@as(u32, 2), capture.count);
}

// Disclosed instrumentation supplement: actual surface owner allocation seam.
test "independent instance: actual surface allocation failure happens before retain" {
    var observation = Observation{};
    const owner = try observation.create();
    var failure = std.testing.FailingAllocator.init(std.testing.allocator, .{ .fail_index = 0 });
    surfaces.test_surface_allocator = failure.allocator();
    defer surfaces.test_surface_allocator = null;
    var descriptor = std.mem.zeroes(ci.WGPUSurfaceDescriptor);
    try std.testing.expect(surfaces.doeNativeInstanceCreateSurface(@ptrCast(owner), @ptrCast(&descriptor), null) == null);
    try std.testing.expectEqual(@as(u32, 0), observation.frees);
    pending(owner).processEvents();
    release(owner);
    try observation.final();
}

test "independent instance: test allocator remains matched through actual surface destruction" {
    surfaces.test_surface_allocator = std.testing.allocator;
    defer surfaces.test_surface_allocator = null;
    var observation = Observation{};
    const owner = try observation.create();
    var descriptor = std.mem.zeroes(ci.WGPUSurfaceDescriptor);
    const surface = surfaces.doeNativeInstanceCreateSurface(@ptrCast(owner), @ptrCast(&descriptor), null) orelse return error.SurfaceCreateFailed;
    release(owner);
    try std.testing.expectEqual(@as(u32, 0), observation.frees);
    surfaces.doeNativeSurfaceRelease(surface);
    try observation.final();
}
