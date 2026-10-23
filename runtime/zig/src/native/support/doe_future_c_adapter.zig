//! Native-only FFI adaptation. C owns identities, queue state and delivery policy.
const std = @import("std");
const builtin = @import("builtin");
const abi = @import("../../core/abi/wgpu_callback_descriptor_types.zig");
pub const c = @cImport({
    @cInclude("doe_futures_zig_bridge.h");
});

pub const Lease = struct {
    context: ?*anyopaque,
    retain: *const fn (?*anyopaque) callconv(.c) void,
    release: *const fn (?*anyopaque) callconv(.c) void,
};

// Test observation only: never owns a reference or substitutes native destruction.
pub var test_deinit_count: usize = 0;
pub var test_wait_allocator: ?std.mem.Allocator = null;

fn fatal(operation: []const u8, status: c.doe_future_status) noreturn {
    std.debug.panic("C futures {s} failed closed: status {d}", .{ operation, status });
}

fn requireOk(operation: []const u8, status: c.doe_future_status) void {
    if (status != c.DOE_FUTURE_OK) fatal(operation, status);
}

fn acquire(lease: ?Lease) void {
    if (lease) |value| value.retain(value.context);
}

fn release(lease: ?Lease) void {
    if (lease) |value| value.release(value.context);
}

pub fn legacyFuture() u64 {
    var id: u64 = undefined;
    requireOk("legacy identity", c.doe_future_legacy_id(&id));
    return id;
}

pub const Completion = struct {
    storage: c.doe_future_completion = undefined,
    initialized: bool = false,
    owner: ?QueueView = null,
    mode: u32 = abi.WGPUCallbackMode_AllowSpontaneous,
    dispatch: ?*const fn (*Completion) void = null,

    fn prepare(self: *Completion) void {
        if (self.initialized) return;
        requireOk("completion init", c.doe_futures_zig_completion_init(&self.storage, self.mode, trampoline, self));
        self.initialized = true;
    }

    fn trampoline(_: ?*c.doe_future_completion, userdata: ?*anyopaque) callconv(.c) void {
        const self: *Completion = @ptrCast(@alignCast(userdata.?));
        // Pipeline requests install dispatch AFTER registration. Read it now and
        // never touch this wrapper after the callback may have freed its payload.
        const dispatch = self.dispatch orelse fatal("missing dispatch", c.DOE_FUTURE_INVALID_STATE);
        dispatch(self);
    }

    pub fn beginDelivery(self: *Completion) void {
        if (!self.initialized) return;
        requireOk("begin delivery", c.doe_future_begin_delivery(&self.storage));
    }

    pub fn markReady(self: *Completion) void {
        self.prepare();
        const lease = if (self.owner) |owner| owner.lease else null;
        acquire(lease);
        defer release(lease);
        const status = c.doe_future_mark_ready(&self.storage);
        // self may no longer exist. Only the snapshotted lease and status survive.
        requireOk("publish readiness", status);
    }

    pub fn finish(self: *Completion) void {
        if (!self.initialized) return;
        requireOk("finish completion", c.doe_future_finish(&self.storage));
        requireOk("completion deinit", c.doe_future_completion_deinit(&self.storage));
        self.initialized = false;
        self.owner = null;
    }
};

pub const PendingCompletions = struct {
    storage: c.doe_future_queue = undefined,
    initialized: bool = false,
    // Null only for explicitly initialized, caller-quiesced stack fixtures.
    lease: ?Lease = null,

    pub fn init(self: *PendingCompletions, lease: ?Lease) !void {
        if (self.initialized) fatal("queue already initialized", c.DOE_FUTURE_INVALID_STATE);
        try verifyLayout();
        const status = c.doe_futures_zig_queue_init(&self.storage);
        if (status == c.DOE_FUTURE_CLEANUP_REQUIRED) fatal("queue init cleanup", status);
        if (status != c.DOE_FUTURE_OK) return error.FutureQueueInitialization;
        self.lease = lease;
        self.initialized = true;
    }

    pub fn deinit(self: *PendingCompletions) void {
        if (!self.initialized) return;
        requireOk("queue deinit", c.doe_future_queue_deinit(&self.storage));
        self.initialized = false;
        if (builtin.is_test) test_deinit_count += 1;
    }

    fn queue(self: *PendingCompletions) *c.doe_future_queue {
        if (!self.initialized) fatal("uninitialized queue", c.DOE_FUTURE_INVALID_STATE);
        return &self.storage;
    }

    /// Copying the view borrows; it never acquires a reference.
    pub fn view(self: *PendingCompletions) QueueView {
        return .{ .queue_ptr = self.queue(), .lease = self.lease };
    }

    pub fn borrowedQueue(self: *PendingCompletions) *c.doe_future_queue {
        return self.view().borrowedQueue();
    }

    pub fn newFuture(self: *PendingCompletions) u64 {
        return self.view().newFuture();
    }

    pub fn register(self: *PendingCompletions, completion: *Completion, id: u64) void {
        self.view().register(completion, id);
    }

    pub fn processEvents(self: *PendingCompletions) void {
        self.view().processEvents();
    }

    pub fn waitAny(self: *PendingCompletions, infos: []abi.WGPUFutureWaitInfo, timeout_ns: u64) !bool {
        return self.view().waitAny(infos, timeout_ns);
    }
};

/// Non-owning value. Its user must already hold the actual queue owner's lease.
/// Null lease is only for initialized and externally quiesced standalone fixtures.
pub const QueueView = struct {
    queue_ptr: *c.doe_future_queue,
    lease: ?Lease,

    /// Borrow the actual initialized queue; the caller supplies its own lease.
    pub fn borrowedQueue(self: QueueView) *c.doe_future_queue {
        return self.queue_ptr;
    }

    pub fn newFuture(self: QueueView) u64 {
        var id: u64 = undefined;
        requireOk("instance identity", c.doe_future_new_id(self.queue_ptr, &id));
        return id;
    }

    pub fn register(self: QueueView, completion: *Completion, id: u64) void {
        completion.prepare();
        completion.owner = self;
        requireOk("register completion", c.doe_future_register(self.queue_ptr, &completion.storage, id));
    }

    pub fn processEvents(self: QueueView) void {
        const queue_ptr = self.queue_ptr;
        const lease = self.lease;
        acquire(lease);
        defer release(lease);
        requireOk("process events", c.doe_future_process_events(queue_ptr));
    }

    pub fn waitAny(self: QueueView, infos: []abi.WGPUFutureWaitInfo, timeout_ns: u64) !bool {
        if (infos.len == 0) return true;
        const lease = self.lease;
        acquire(lease);
        defer release(lease);
        _ = try std.math.mul(usize, infos.len, @sizeOf(c.doe_future_wait_info));
        const allocator = if (builtin.is_test) test_wait_allocator orelse std.heap.page_allocator else std.heap.page_allocator;
        const copied = try allocator.alloc(c.doe_future_wait_info, infos.len);
        defer allocator.free(copied);
        for (infos, copied) |info, *copy| copy.* = .{ .id = info.future.id, .completed = info.completed };
        const queue_ptr = self.queue_ptr;
        const status = c.doe_future_wait_any(queue_ptr, copied.ptr, copied.len, timeout_ns);
        for (infos, copied) |*info, copy| info.completed = copy.completed;
        return switch (status) {
            c.DOE_FUTURE_OK => true,
            c.DOE_FUTURE_TIMED_OUT => false,
            c.DOE_FUTURE_UNKNOWN_ID => error.UnknownFuture,
            else => error.FutureWaitFailed,
        };
    }
};

/// Compare every private imported field offset with the exact GCC archive.
/// No guessed storage arrays, aliasing assumptions or stable private ABI claim.
pub fn verifyLayout() !void {
    const Queue = c.doe_future_queue;
    const Node = c.doe_future_completion;
    var queue_layout: [c.DOE_FUTURES_QUEUE_LAYOUT_COUNT]usize = undefined;
    var node_layout: [c.DOE_FUTURES_COMPLETION_LAYOUT_COUNT]usize = undefined;
    c.doe_futures_zig_queue_layout(&queue_layout);
    c.doe_futures_zig_completion_layout(&node_layout);
    const queue_expected = [_]usize{ @sizeOf(Queue), @alignOf(Queue) } ++ offsets(Queue, .{ "mutex", "changed", "attributes", "head", "active", "instance_id", "next_sequence", "state", "mutex_live", "condition_live", "attributes_live" });
    const node_expected = [_]usize{ @sizeOf(Node), @alignOf(Node) } ++ offsets(Node, .{ "owner", "next", "dispatch", "userdata", "id", "delivery_thread", "mode", "ready", "delivering", "initialized" });
    if (!std.mem.eql(usize, &queue_layout, &queue_expected) or !std.mem.eql(usize, &node_layout, &node_expected)) return error.FutureBridgeLayoutMismatch;
}

fn offsets(comptime T: type, comptime fields: anytype) [fields.len]usize {
    var result: [fields.len]usize = undefined;
    inline for (fields, 0..) |field, index| result[index] = @offsetOf(T, field);
    return result;
}
