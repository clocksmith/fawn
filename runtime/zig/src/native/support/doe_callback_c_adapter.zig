//! Typed retained-native boundary; C alone owns payload policy and retirement.
const std = @import("std");
const base = @import("../../core/abi/wgpu_core_base_types.zig");
const objects = @import("doe_native_object_types.zig");
const c = @cImport({
    @cInclude("doe_callbacks.h");
});

pub const Publication = c.doe_callback_event;

pub fn markReady(publication: *Publication) void {
    c.doe_callback_mark_ready(publication);
}

fn allocatorFromTokens(state: ?*anyopaque, vtable: ?*const anyopaque) std.mem.Allocator {
    // Stateless allocators may supply an undefined, unused state token.
    // Preserve meaningful state identity without imposing non-NULL on that token.
    return .{ .ptr = state orelse undefined, .vtable = @ptrCast(@alignCast(vtable.?)) };
}

fn allocate(state: ?*anyopaque, vtable: ?*const anyopaque, size: usize, alignment: usize) callconv(.c) ?*anyopaque {
    const allocator = allocatorFromTokens(state, vtable);
    return allocator.rawAlloc(size, .fromByteUnits(alignment), @returnAddress());
}

fn deallocate(state: ?*anyopaque, vtable: ?*const anyopaque, memory: ?*anyopaque, size: usize, alignment: usize) callconv(.c) void {
    const allocator = allocatorFromTokens(state, vtable);
    const bytes: [*]u8 = @ptrCast(memory.?);
    allocator.rawFree(bytes[0..size], .fromByteUnits(alignment), @returnAddress());
}

pub fn ready(
    comptime Context: type,
    allocator: std.mem.Allocator,
    instance: ?*objects.DoeInstance,
    mode: u32,
    context: Context,
    comptime deliver: fn (*Context) void,
) !base.WGPUFuture {
    return publish(Context, allocator, instance, mode, context, deliver, markReady);
}

pub fn publish(
    comptime Context: type,
    allocator: std.mem.Allocator,
    instance: ?*objects.DoeInstance,
    mode: u32,
    context: Context,
    comptime deliver: fn (*Context) void,
    comptime publisher: fn (*Publication) void,
) !base.WGPUFuture {
    const Thunks = struct {
        fn copy(destination: ?*anyopaque, source: ?*const anyopaque) callconv(.c) void {
            const target: *Context = @ptrCast(@alignCast(destination.?));
            const original: *const Context = @ptrCast(@alignCast(source.?));
            target.* = original.*;
        }
        fn dispatch(raw: ?*anyopaque) callconv(.c) void {
            deliver(@ptrCast(@alignCast(raw.?)));
        }
        fn publishEvent(event: ?*Publication, _: ?*anyopaque) callconv(.c) void {
            publisher(event.?);
        }
    };
    var local = context;
    var owner: c.doe_callback_owner = undefined;
    if (instance) |value| {
        const pending = @import("doe_native_object_helpers.zig").instance_pending(value);
        const lease = pending.lease orelse @panic("C callbacks missing native instance lease");
        owner = .{
            .queue = @ptrCast(pending.borrowedQueue()),
            .userdata = lease.context,
            .retain = lease.retain,
            .release = lease.release,
        };
    }
    const request = c.doe_callback_request{
        .abi_version = c.DOE_CALLBACKS_ABI_VERSION,
        .mode = mode,
        .owner = if (instance != null) &owner else null,
        .allocator = .{ .state = allocator.ptr, .vtable = allocator.vtable, .allocate = allocate, .deallocate = deallocate },
        .source_context = &local,
        .context_size = @sizeOf(Context),
        .context_alignment = @alignOf(Context),
        .copy = Thunks.copy,
        .deliver = Thunks.dispatch,
    };
    var id: u64 = undefined;
    return switch (c.doe_callback_publish(&request, Thunks.publishEvent, null, &id)) {
        c.DOE_CALLBACK_OK => .{ .id = id },
        c.DOE_CALLBACK_UNSUPPORTED_MODE => error.UnsupportedCallbackMode,
        c.DOE_CALLBACK_INSTANCE_UNAVAILABLE => error.CallbackInstanceUnavailable,
        c.DOE_CALLBACK_OUT_OF_MEMORY, c.DOE_CALLBACK_LAYOUT_OVERFLOW => error.OutOfMemory,
        else => |status| std.debug.panic("C callbacks native request failed closed: status {d}", .{status}),
    };
}
