const std = @import("std");
const program_identity_trace = @import("../diagnostics/doe_program_identity_trace.zig");
const c_instance_selected = @import("build_options").experimental_c_instance;
const instance_adapter = if (c_instance_selected) @import("doe_instance_c_adapter.zig") else struct {};
const future_owner = @import("doe_future_owner.zig");
const leases = @import("../../contracts/resource_lease.zig");

var gpa = std.heap.GeneralPurposeAllocator(.{}){};

pub const alloc = gpa.allocator();
pub const label_store = @import("doe_label_owner.zig");

pub fn make(comptime T: type) ?*T {
    return create(T, alloc) catch null;
}

pub fn create(comptime T: type, allocator: std.mem.Allocator) std.mem.Allocator.Error!*T {
    const object = try allocator.create(T);
    program_identity_trace.recordNativeObjectCreate(T, object);
    return object;
}

pub fn cast(comptime T: type, p: ?*anyopaque) ?*T {
    if (comptime c_instance_selected and T == instance_adapter.Instance) return instance_adapter.fromHandle(p);
    const ptr = p orelse return null;
    const result: *T = @ptrCast(@alignCast(ptr));
    if (result.magic != T.TYPE_MAGIC) return null;
    return result;
}

pub fn object_add_ref(comptime T: type, raw: ?*anyopaque) void {
    const obj = cast(T, raw) orelse return;
    if (comptime c_instance_selected and T == instance_adapter.Instance) {
        instance_adapter.retain(obj);
    } else leases.retainCount(&obj.ref_count);
}

pub fn object_should_destroy(obj: anytype) bool {
    if (comptime c_instance_selected and @typeInfo(@TypeOf(obj)).pointer.child == instance_adapter.Instance)
        @compileError("C instance alone owns finalization; use native instance release");
    return leases.releaseCount(&obj.ref_count);
}

pub fn toOpaque(p: anytype) ?*anyopaque {
    return @ptrCast(p);
}

/// Narrow instance accessor; selecting C statically requires its opaque pointer.
pub fn instance_pending(instance: anytype) future_owner.QueueRef {
    if (comptime c_instance_selected and @TypeOf(instance) != *instance_adapter.Instance)
        @compileError("instance_pending requires the selected C instance pointer");
    return if (c_instance_selected) instance_adapter.borrow(instance) else future_owner.borrow(&instance.pending_completions);
}

pub fn instance_undo_retain(instance: anytype) void {
    if (comptime c_instance_selected and @TypeOf(instance) != *instance_adapter.Instance)
        @compileError("instance_undo_retain requires the selected C instance pointer");
    if (c_instance_selected) {
        instance_adapter.undoRetain(instance);
    } else {
        const last_reference = leases.releaseCount(&instance.ref_count);
        std.debug.assert(!last_reference);
    }
}
