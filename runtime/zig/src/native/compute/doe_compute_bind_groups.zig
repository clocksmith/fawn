const std = @import("std");
const native_shared = @import("../support/doe_native_shared_types.zig");
const native_types = @import("../support/doe_native_object_types.zig");
const native_helpers = @import("../support/doe_native_object_helpers.zig");

pub const MAX_BIND = native_shared.MAX_BIND;
pub const MAX_COMPUTE_BIND_GROUPS = native_shared.MAX_COMPUTE_BIND_GROUPS;
pub const MAX_FLAT_BIND = native_shared.MAX_FLAT_BIND;

pub const DoeBindGroup = native_types.DoeBindGroup;

pub const FlatResources = struct {
    textures: [MAX_FLAT_BIND]?*anyopaque = [_]?*anyopaque{null} ** MAX_FLAT_BIND,
    samplers: [MAX_FLAT_BIND]?*anyopaque = [_]?*anyopaque{null} ** MAX_FLAT_BIND,
    texture_count: u32 = 0,
    sampler_count: u32 = 0,

    pub fn hasNonBufferResources(self: *const FlatResources) bool {
        return self.texture_count != 0 or self.sampler_count != 0;
    }
};

comptime {
    if (MAX_FLAT_BIND != MAX_BIND * MAX_COMPUTE_BIND_GROUPS) {
        @compileError("doe_compute_bind_groups: flat slot constants drifted");
    }
}

pub inline fn flatBindSlot(group_index: usize, binding_index: usize) usize {
    return group_index * MAX_BIND + binding_index;
}

pub fn bindingCount(bg: *const DoeBindGroup) usize {
    const count: usize = @intCast(bg.count);
    std.debug.assert(count <= MAX_BIND);
    return count;
}

pub fn populateFlatBindings(
    bind_groups: []const ?*DoeBindGroup,
    bufs: *[MAX_FLAT_BIND]?*anyopaque,
    buf_offsets: *[MAX_FLAT_BIND]u64,
    buf_sizes: *[MAX_FLAT_BIND]u64,
) u32 {
    std.debug.assert(bind_groups.len <= MAX_COMPUTE_BIND_GROUPS);

    var total: u32 = 0;
    for (bind_groups, 0..) |maybe_bg, group_index| {
        const bg = maybe_bg orelse continue;
        const count = bindingCount(bg);
        if (count == 0) continue;

        for (0..count) |binding_index| {
            const slot = flatBindSlot(group_index, binding_index);
            bufs[slot] = bg.buffers[binding_index];
            buf_offsets[slot] = bg.offsets[binding_index];
            buf_sizes[slot] = bg.buffer_sizes[binding_index];
        }
        total = @intCast(flatBindSlot(group_index, count - 1) + 1);
    }
    return total;
}

/// Add setBindGroup dynamic offsets to flattened buffer binding offsets.
pub fn applyDynamicOffsets(
    dynamic_offsets: *const [MAX_COMPUTE_BIND_GROUPS]native_shared.BindGroupDynamicOffsets,
    buf_offsets: *[MAX_FLAT_BIND]u64,
) void {
    for (dynamic_offsets, 0..) |group_offsets, group_index| {
        for (group_offsets, 0..) |offset, binding_index| {
            buf_offsets[flatBindSlot(group_index, binding_index)] += offset;
        }
    }
}

pub const DispatchBindingError = error{ DispatchBindGroupMissing, DispatchBindGroupIncompatible };

/// Bind groups an explicit pipeline layout requires at dispatch: each nonempty
/// group must be bound with the same or an equal-entry layout
/// (https://gpuweb.github.io/gpuweb/#abstract-opdef-validate-encoder-bind-groups).
/// Automatic layouts are not checked here.
pub fn validateDispatchBindGroups(
    layout: ?*const native_types.DoePipelineLayout,
    bind_groups: []const ?*DoeBindGroup,
) DispatchBindingError!void {
    const pipeline_layout = layout orelse return;
    const count: usize = @min(pipeline_layout.bind_group_layout_count, MAX_COMPUTE_BIND_GROUPS);
    for (pipeline_layout.bind_group_layouts[0..count], 0..) |maybe_required, index| {
        const required = maybe_required orelse continue;
        if (required.entry_count == 0) continue;
        const bound = (if (index < bind_groups.len) bind_groups[index] else null) orelse
            return error.DispatchBindGroupMissing;
        const bound_layout = bound.layout orelse return error.DispatchBindGroupIncompatible;
        if (bound_layout != required and !layoutsEquivalent(bound_layout, required))
            return error.DispatchBindGroupIncompatible;
    }
}

fn layoutEntries(layout: *const native_types.DoeBindGroupLayout) []const native_shared.DoeBindGroupLayoutEntry {
    const entries = layout.entries orelse return &.{};
    return entries[0..@min(entries.len, layout.entry_count)];
}

fn layoutsEquivalent(a: *const native_types.DoeBindGroupLayout, b: *const native_types.DoeBindGroupLayout) bool {
    if (a.automatic or b.automatic) return false;
    const a_entries = layoutEntries(a);
    const b_entries = layoutEntries(b);
    if (a_entries.len != b_entries.len) return false;
    for (a_entries) |entry| {
        const match = for (b_entries) |candidate| {
            if (candidate.binding == entry.binding) break candidate;
        } else return false;
        if (!std.meta.eql(entry, match)) return false;
    }
    return true;
}

pub fn collectFlatResources(bind_groups: []const ?*DoeBindGroup) FlatResources {
    std.debug.assert(bind_groups.len <= MAX_COMPUTE_BIND_GROUPS);

    var resources: FlatResources = .{};
    for (bind_groups, 0..) |maybe_bg, group_index| {
        const bg = maybe_bg orelse continue;
        const count = bindingCount(bg);
        for (0..count) |binding_index| {
            const slot = flatBindSlot(group_index, binding_index);
            if (bg.textures[binding_index]) |texture| {
                resources.textures[slot] = texture;
                resources.texture_count = @intCast(slot + 1);
            }
            if (bg.samplers[binding_index]) |sampler| {
                resources.samplers[slot] = sampler;
                resources.sampler_count = @intCast(slot + 1);
            }
        }
    }
    return resources;
}

pub fn collectFlatResourcesFromRaw(
    bg_ptrs: [*]const ?*anyopaque,
    bg_count: u32,
) FlatResources {
    var bind_groups: [MAX_COMPUTE_BIND_GROUPS]?*DoeBindGroup =
        [_]?*DoeBindGroup{null} ** MAX_COMPUTE_BIND_GROUPS;
    for (0..@min(bg_count, MAX_COMPUTE_BIND_GROUPS)) |index| {
        bind_groups[index] = native_helpers.cast(DoeBindGroup, bg_ptrs[index]);
    }
    return collectFlatResources(bind_groups[0..]);
}

test "flatBindSlot matches compute bind slot layout" {
    try std.testing.expectEqual(@as(usize, 0), flatBindSlot(0, 0));
    try std.testing.expectEqual(@as(usize, MAX_BIND), flatBindSlot(1, 0));
    try std.testing.expectEqual(@as(usize, MAX_FLAT_BIND - 1), flatBindSlot(MAX_COMPUTE_BIND_GROUPS - 1, MAX_BIND - 1));
}

test "applyDynamicOffsets adds per-binding offsets to flattened slots" {
    var dynamic = [_]native_shared.BindGroupDynamicOffsets{native_shared.NO_DYNAMIC_OFFSETS} ** MAX_COMPUTE_BIND_GROUPS;
    dynamic[1][2] = 256;
    var offsets = [_]u64{16} ** MAX_FLAT_BIND;
    applyDynamicOffsets(&dynamic, &offsets);
    try std.testing.expectEqual(@as(u64, 272), offsets[flatBindSlot(1, 2)]);
    try std.testing.expectEqual(@as(u64, 16), offsets[flatBindSlot(0, 2)]);
}

test "validateDispatchBindGroups requires every nonempty explicit group with an equal layout" {
    var entries = [_]native_shared.DoeBindGroupLayoutEntry{.{ .binding = 0, .resource_kind = 1 }};
    var other_entries = [_]native_shared.DoeBindGroupLayoutEntry{.{ .binding = 1, .resource_kind = 1 }};
    var required = native_types.DoeBindGroupLayout{ .entry_count = 1, .entries = entries[0..] };
    var equal = native_types.DoeBindGroupLayout{ .entry_count = 1, .entries = entries[0..] };
    var different = native_types.DoeBindGroupLayout{ .entry_count = 1, .entries = other_entries[0..] };
    var empty = native_types.DoeBindGroupLayout{};
    var layout = native_types.DoePipelineLayout{ .bind_group_layout_count = 2 };
    layout.bind_group_layouts[0] = &empty;
    layout.bind_group_layouts[1] = &required;
    var bound = DoeBindGroup{ .layout = &equal };
    var wrong = DoeBindGroup{ .layout = &different };
    try validateDispatchBindGroups(&layout, &.{ null, &bound });
    try validateDispatchBindGroups(null, &.{});
    try std.testing.expectError(error.DispatchBindGroupMissing, validateDispatchBindGroups(&layout, &.{ null, null }));
    try std.testing.expectError(error.DispatchBindGroupIncompatible, validateDispatchBindGroups(&layout, &.{ null, &wrong }));
    equal.automatic = true;
    try std.testing.expectError(error.DispatchBindGroupIncompatible, validateDispatchBindGroups(&layout, &.{ null, &bound }));
}

test "collectFlatResources preserves separate flattened texture and sampler slots" {
    var group: DoeBindGroup = .{};
    group.count = 18;
    group.textures[16] = @ptrFromInt(0x1000);
    group.samplers[17] = @ptrFromInt(0x2000);
    const resources = collectFlatResources(&.{&group});
    try std.testing.expectEqual(@as(u32, 17), resources.texture_count);
    try std.testing.expectEqual(@as(u32, 18), resources.sampler_count);
    try std.testing.expectEqual(@as(?*anyopaque, @ptrFromInt(0x1000)), resources.textures[16]);
    try std.testing.expectEqual(@as(?*anyopaque, @ptrFromInt(0x2000)), resources.samplers[17]);
}
