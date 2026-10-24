//! Usage-scope records and their WebGPU compatibility rule
//! (https://gpuweb.github.io/gpuweb/#programming-model-synchronization);
//! native/command/doe_usage_scope.zig builds them from native objects.

const std = @import("std");

pub const Error = error{ ResourceUsageConflict, WritableBindingAlias, OutOfMemory };

pub const Usage = enum(u8) { read, storage, storage_read_write, attachment };

pub const Use = struct {
    /// Resource identities (the native object addresses).
    buffer: usize = 0,
    texture: usize = 0,
    base_mip: u32 = 0,
    mip_count: u32 = 0,
    base_layer: u32 = 0,
    layer_count: u32 = 0,
    aspects: u8 = 0,
    usage: Usage = .read,
    offset: u64 = 0,
    size: u64 = 0,
    visibility: u64 = 0,
};

fn overlaps(a: u64, a_count: u64, b: u64, b_count: u64) bool {
    if (a_count == 0 or b_count == 0) return false;
    return if (a <= b) b - a < a_count else a - b < b_count;
}

/// Buffers are one usage identity; texture uses overlap by texture, aspect,
/// mip and layer range. `aliases` also rejects overlapping writable uses
/// visible to a common stage (one draw or dispatch).
pub fn conflict(a: *const Use, b: *const Use, aliases: bool) ?Error {
    if (a.usage == .read and b.usage == .read) return null;
    if (a.buffer != 0) {
        if (b.buffer != a.buffer) return null;
        if (a.usage != b.usage) return error.ResourceUsageConflict;
        if (aliases and (a.visibility & b.visibility) != 0 and overlaps(a.offset, a.size, b.offset, b.size))
            return error.WritableBindingAlias;
        return null;
    }
    if (a.texture == 0 or b.texture != a.texture or (a.aspects & b.aspects) == 0) return null;
    if (!overlaps(a.base_mip, a.mip_count, b.base_mip, b.mip_count) or
        !overlaps(a.base_layer, a.layer_count, b.base_layer, b.layer_count)) return null;
    if (a.usage != b.usage) return error.ResourceUsageConflict;
    if (aliases and (a.visibility & b.visibility) != 0) return error.WritableBindingAlias;
    return null;
}

/// Uses of every resource one draw or dispatch reads or writes, checked
/// pairwise with the aliasing rule.
pub fn validateDispatchUses(uses: []const Use) Error!void {
    for (uses, 0..) |*use, index| {
        for (uses[0..index]) |*earlier| if (conflict(earlier, use, true)) |err| return err;
    }
}

/// A render pass's (or bundle's) accumulated usage scope. Overwritten and
/// unused setters stay in it.
pub const Scope = struct {
    uses: std.ArrayListUnmanaged(Use) = .{},

    pub fn add(self: *Scope, allocator: std.mem.Allocator, uses: []const Use) Error!void {
        for (uses, 0..) |*use, index| {
            for (self.uses.items) |*existing| if (conflict(existing, use, false)) |err| return err;
            for (uses[0..index]) |*earlier| if (conflict(earlier, use, false)) |err| return err;
        }
        try self.uses.ensureUnusedCapacity(allocator, uses.len);
        for (uses) |use| {
            if (!present(self.uses.items, &use)) self.uses.appendAssumeCapacity(use);
        }
    }

    pub fn deinit(self: *Scope, allocator: std.mem.Allocator) void {
        self.uses.deinit(allocator);
        self.* = .{};
    }
};

fn present(uses: []const Use, use: *const Use) bool {
    for (uses) |*existing| {
        if (existing.buffer == use.buffer and existing.texture == use.texture and existing.usage == use.usage and
            existing.aspects == use.aspects and existing.base_mip == use.base_mip and existing.mip_count == use.mip_count and
            existing.base_layer == use.base_layer and existing.layer_count == use.layer_count) return true;
    }
    return false;
}

test "writable bindings visible to one stage do not alias within a dispatch" {
    var first = Use{ .buffer = 1, .offset = 0, .size = 128, .usage = .storage, .visibility = 4 };
    var second = first;
    second.offset = 64;
    try std.testing.expectError(error.WritableBindingAlias, validateDispatchUses(&.{ first, second }));
    second.offset = 128;
    try validateDispatchUses(&.{ first, second });
    second.offset = 0;
    second.visibility = 2;
    first.visibility = 1;
    try validateDispatchUses(&.{ first, second });
}
