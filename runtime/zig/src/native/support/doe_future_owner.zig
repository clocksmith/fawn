//! One build-selected identity domain. Default has no C import or link dependency.
const selected = @import("build_options").experimental_c_futures;
const owner = if (selected) @import("doe_future_c_adapter.zig") else @import("doe_future_ids.zig");

pub const Completion = owner.Completion;
pub const PendingCompletions = owner.PendingCompletions;
pub const legacyFuture = owner.legacyFuture;

pub fn initialize(pending: *PendingCompletions, instance: ?*anyopaque, retain: *const fn (?*anyopaque) callconv(.c) void, release: *const fn (?*anyopaque) callconv(.c) void) !void {
    if (selected) try pending.init(.{ .context = instance, .retain = retain, .release = release });
}

pub fn deinitialize(pending: *PendingCompletions) void {
    if (selected) pending.deinit();
}
