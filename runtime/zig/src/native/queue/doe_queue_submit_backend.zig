//! Backend dispatch of accepted queue submissions, shared by the public
//! queue entry and claimed host-replay compute programs. Neither caller's
//! admission policy lives here: the queue entry consumes command buffers once,
//! a host-replay program has already claimed its buffer.

const native_types = @import("../support/doe_native_object_types.zig");
const native_helpers = @import("../support/doe_native_object_helpers.zig");
const error_scope = @import("../../runtime/diagnostics/error_scope.zig");
const d3d12_submit = @import("doe_queue_submit_d3d12.zig");
const metal_submit = @import("doe_queue_submit_metal.zig");
const vulkan_submit = @import("doe_queue_submit_vulkan.zig");
const availability = @import("doe_submission_validation.zig");

const DoeQueue = native_types.DoeQueue;

/// Replays a command buffer that a host-replay compute program claimed: the
/// retained references are revalidated and the commands go through the same
/// backend submission as doeNativeQueueSubmit, without its single-use
/// consumption, which the program already performed when it claimed them.
/// False after delivering a validation error or for a destroyed device.
pub fn submitClaimed(q: *DoeQueue, commands: *native_types.DoeCommandBuffer) bool {
    if (!q.dev.requireAlive()) return false;
    availability.validate(commands.references.items) catch |err| {
        q.dev.error_scopes.deliver(error_scope.ERROR_TYPE_VALIDATION, availability.message(err));
        return false;
    };
    const one = [_]?*anyopaque{native_helpers.toOpaque(commands)};
    submitBackend(q, one.len, &one);
    return true;
}

pub fn submitBackend(q: *DoeQueue, count: usize, cmd_bufs: [*]const ?*anyopaque) void {
    if (q.dev.backend == .vulkan) {
        vulkan_submit.submit_vulkan_commands(q, count, cmd_bufs);
        return;
    }
    if (q.dev.backend == .d3d12) {
        d3d12_submit.submit_d3d12_commands(q, count, cmd_bufs);
        return;
    }
    metal_submit.submit_metal_commands(q, count, cmd_bufs);
}
