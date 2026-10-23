//! Typed boundary for the opaque C instance. No object or registry policy lives here.
const std = @import("std");
const future_adapter = @import("doe_future_c_adapter.zig");
const upstream = @import("../../core/abi/generated/webgpu_upstream.zig");
const capabilities = @import("../../core/abi/procs/wgpu_p1_capability_procs.zig");
pub const c = @cImport({
    @cInclude("doe_instance.h");
});

pub const Instance = c.doe_instance;

// Non-owning test observations and real allocator overrides, never production APIs.
pub var test_allocated_count: usize = 0;
pub var test_finalized_count: usize = 0;
pub var test_object_allocator: ?std.mem.Allocator = null;
pub var test_wait_allocator: ?std.mem.Allocator = null;

fn allocatorFromTokens(state: ?*anyopaque, vtable: ?*const anyopaque) std.mem.Allocator {
    return .{ .ptr = state orelse undefined, .vtable = @ptrCast(@alignCast(vtable.?)) };
}

fn allocate(state: ?*anyopaque, vtable: ?*const anyopaque, size: usize, alignment: usize) callconv(.c) ?*anyopaque {
    return allocatorFromTokens(state, vtable).rawAlloc(size, .fromByteUnits(alignment), @returnAddress());
}

fn deallocate(state: ?*anyopaque, vtable: ?*const anyopaque, memory: ?*anyopaque, size: usize, alignment: usize) callconv(.c) void {
    const bytes: [*]u8 = @ptrCast(memory.?);
    allocatorFromTokens(state, vtable).rawFree(bytes[0..size], .fromByteUnits(alignment), @returnAddress());
}

pub fn allocatorBinding(allocator: std.mem.Allocator) c.doe_instance_allocator {
    return .{ .state = allocator.ptr, .vtable = allocator.vtable, .allocate = allocate, .deallocate = deallocate };
}

pub fn create(config: c.doe_instance_config) ?*Instance {
    future_adapter.verifyLayout() catch |err| std.debug.panic("C instance future ABI: {s}", .{@errorName(err)});
    var instance: ?*Instance = null;
    const status = c.doe_instance_create(&config, &instance);
    return if (status == c.DOE_INSTANCE_OK) instance else null;
}

pub fn fromHandle(raw: ?*anyopaque) ?*Instance {
    return c.doe_instance_from_handle(raw);
}

pub const retain = c.doe_instance_retain;
pub const release = c.doe_instance_release;
pub const undoRetain = c.doe_instance_undo_retain;
pub const processEvents = c.doe_instance_process_events;

pub fn borrow(instance: *Instance) future_adapter.QueueView {
    const value = c.doe_instance_borrow_queue(instance);
    return .{ .queue_ptr = @ptrCast(@alignCast(value.queue.?)), .lease = .{
        .context = value.userdata,
        .retain = value.retain.?,
        .release = value.release.?,
    } };
}

pub fn waitAny(instance: *Instance, count: usize, infos: [*]upstream.WGPUFutureWaitInfo, timeout_ns: u64) u32 {
    // Both records are imports of the very same pinned webgpu.h declaration,
    // not the unrelated doe_future_wait_info. C owns allocation and typed copy.
    return c.doe_instance_wait_any(instance, count, @ptrCast(infos), timeout_ns);
}

pub fn features() capabilities.SupportedInstanceFeatures {
    const value = c.doe_instance_features();
    return .{ .featureCount = value.featureCount, .features = if (value.features == null) null else @ptrCast(value.features) };
}

pub fn limits() capabilities.InstanceLimits {
    const value = c.doe_instance_limits();
    return .{ .nextInChain = value.nextInChain, .timedWaitAnyMaxCount = value.timedWaitAnyMaxCount };
}

pub fn hasFeature(feature: capabilities.WGPUInstanceFeatureName) u32 {
    return c.doe_instance_has_feature(feature);
}

pub fn freeFeatures(value: capabilities.SupportedInstanceFeatures) void {
    c.doe_instance_features_free_members(.{ .featureCount = value.featureCount, .features = if (value.features) |pointer| @ptrCast(pointer) else null });
}

comptime {
    if (@sizeOf(upstream.WGPUFutureWaitInfo) != @sizeOf(c.WGPUFutureWaitInfo) or
        @alignOf(upstream.WGPUFutureWaitInfo) != @alignOf(c.WGPUFutureWaitInfo) or
        @offsetOf(upstream.WGPUFutureWaitInfo, "future") != @offsetOf(c.WGPUFutureWaitInfo, "future") or
        @offsetOf(upstream.WGPUFutureWaitInfo, "completed") != @offsetOf(c.WGPUFutureWaitInfo, "completed") or
        @sizeOf(upstream.WGPUFuture) != @sizeOf(c.WGPUFuture) or
        @alignOf(upstream.WGPUFuture) != @alignOf(c.WGPUFuture) or
        @offsetOf(upstream.WGPUFuture, "id") != @offsetOf(c.WGPUFuture, "id") or
        @sizeOf(upstream.WGPUBool) != @sizeOf(c.WGPUBool))
        @compileError("C instance pinned WebGPU record provenance/layout mismatch");
}
