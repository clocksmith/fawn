//! WebGPU usage scopes (https://gpuweb.github.io/gpuweb/#programming-model-synchronization):
//! a render pass is one scope that every setBindGroup, vertex/index/indirect
//! buffer, attachment and executed bundle adds to; each dispatch is its own
//! scope of the bind groups its pipeline uses. Within a scope a buffer, or a
//! texture subresource, is either only read or used with one writable usage.
//! Within one draw or dispatch, writable storage bindings visible to the same
//! stage must not alias.

const std = @import("std");
const objects = @import("../support/doe_native_object_types.zig");
const shared = @import("../support/doe_native_shared_types.zig");
const binding_contract = @import("../../contracts/binding.zig");
const abi_binding = @import("../../core/abi/wgpu_binding_base_types.zig");
const abi_texture = @import("../../core/abi/wgpu_texture_base_types.zig");
const recording = @import("doe_command_recording.zig");
const helpers = @import("../support/doe_native_object_helpers.zig");
const types = @import("../support/doe_usage_scope_types.zig");
pub const Error = types.Error;
pub const Usage = types.Usage;
pub const Use = types.Use;
pub const Scope = types.Scope;
pub const conflict = types.conflict;
pub const validateDispatchUses = types.validateDispatchUses;

const ASPECT_COLOR: u8 = 1;
const ASPECT_DEPTH: u8 = 2;
const ASPECT_STENCIL: u8 = 4;

/// The most uses one bind group contributes (one per binding).
pub const MAX_GROUP_USES = shared.MAX_BIND;

fn formatAspects(format: u32) u8 {
    if (!abi_texture.isDepthStencilFormat(format)) return ASPECT_COLOR;
    var aspects: u8 = 0;
    if (format != abi_texture.WGPUTextureFormat_Stencil8) aspects |= ASPECT_DEPTH;
    if (abi_texture.hasStencilAspect(format)) aspects |= ASPECT_STENCIL;
    return aspects;
}

/// The subresources a view covers, with `usage`.
pub fn viewUse(view: *const objects.DoeTextureView, usage: Usage) Use {
    var aspects = formatAspects(view.tex.format);
    if (view.aspect == abi_texture.WGPUTextureAspect_DepthOnly) aspects &= ASPECT_DEPTH;
    if (view.aspect == abi_texture.WGPUTextureAspect_StencilOnly) aspects &= ASPECT_STENCIL;
    const volume = view.tex.dimension == abi_texture.WGPUTextureDimension_3D;
    return .{
        .texture = @intFromPtr(view.tex),
        .base_mip = view.base_mip_level,
        .mip_count = @max(1, view.mip_level_count),
        .base_layer = if (volume) 0 else view.base_array_layer,
        .layer_count = if (volume) 1 else @max(1, view.array_layer_count),
        .aspects = aspects,
        .usage = usage,
    };
}

pub fn bufferUse(buffer: *const objects.DoeBuffer) Use {
    return .{ .buffer = @intFromPtr(buffer), .size = buffer.size, .usage = .read };
}

fn layoutEntries(layout: *const objects.DoeBindGroupLayout) []const shared.DoeBindGroupLayoutEntry {
    const entries = layout.entries orelse return &.{};
    return entries[0..@min(entries.len, layout.entry_count)];
}

/// Appends the uses of `group`'s buffer and texture bindings, with the
/// dynamic offsets it is bound with, and returns how many it added.
pub fn groupUses(group: *const objects.DoeBindGroup, dynamic_offsets: *const shared.BindGroupDynamicOffsets, out: []Use) usize {
    const layout = group.layout orelse return 0;
    var count: usize = 0;
    for (layoutEntries(layout)) |entry| {
        if (entry.binding >= shared.MAX_BIND or count == out.len) continue;
        const binding: usize = entry.binding;
        switch (entry.resource_kind) {
            binding_contract.layoutResourceKindCode(.buffer) => {
                const buffer = group.retained_buffers[binding] orelse continue;
                out[count] = .{
                    .buffer = @intFromPtr(buffer),
                    .offset = group.offsets[binding] +| dynamic_offsets[binding],
                    .size = group.buffer_sizes[binding],
                    .usage = if (entry.buffer_binding_type == abi_binding.WGPUBufferBindingType_Storage) .storage else .read,
                    .visibility = entry.visibility,
                };
            },
            binding_contract.layoutResourceKindCode(.texture), binding_contract.layoutResourceKindCode(.storage_texture) => {
                const view = helpers.cast(objects.DoeTextureView, group.texture_views[binding]) orelse continue;
                const usage: Usage = if (entry.resource_kind == binding_contract.layoutResourceKindCode(.texture)) .read else switch (entry.texture_sample_type) {
                    abi_binding.WGPUStorageTextureAccess_ReadOnly => .read,
                    abi_binding.WGPUStorageTextureAccess_WriteOnly => .storage,
                    abi_binding.WGPUStorageTextureAccess_ReadWrite => .storage_read_write,
                    // Automatic render layouts do not record the access.
                    else => .read,
                };
                out[count] = viewUse(view, usage);
                out[count].visibility = entry.visibility;
            },
            else => continue,
        }
        count += 1;
    }
    return count;
}

/// Whether a pipeline uses bind group `index`: a nonempty group of its
/// explicit layout, or a group its entry points use under an automatic layout.
fn pipelineUsesGroup(layout: ?*const objects.DoePipelineLayout, required: ?[]const shared.RequiredBinding, index: usize) bool {
    if (layout) |explicit| {
        if (index >= explicit.bind_group_layout_count) return false;
        const group_layout = explicit.bind_group_layouts[index] orelse return false;
        return group_layout.entry_count != 0;
    }
    for (required orelse return false) |use| if (use.group == index) return true;
    return false;
}

/// Appends the uses of the bound groups a pipeline uses; returns the count.
pub fn pipelineGroupUses(
    layout: ?*const objects.DoePipelineLayout,
    required: ?[]const shared.RequiredBinding,
    groups: []const ?*objects.DoeBindGroup,
    offsets: []const shared.BindGroupDynamicOffsets,
    out: []Use,
) usize {
    var count: usize = 0;
    for (groups, 0..) |maybe_group, index| {
        const group = maybe_group orelse continue;
        if (!pipelineUsesGroup(layout, required, index)) continue;
        count += groupUses(group, &offsets[index], out[count..]);
    }
    return count;
}

/// Adds `uses` to a render pass's scope; a conflict invalidates the encoder.
pub fn admitPass(pass: *objects.DoeRenderPass, uses: []const Use) bool {
    pass.usage_scope.add(pass.enc.allocator, uses) catch |err| {
        recording.fail(pass.enc, err);
        return false;
    };
    return true;
}

pub fn admitPassBuffer(pass: *objects.DoeRenderPass, buffer: *const objects.DoeBuffer) bool {
    return admitPass(pass, &.{bufferUse(buffer)});
}

pub fn admitPassGroup(pass: *objects.DoeRenderPass, group: *const objects.DoeBindGroup, offsets: *const shared.BindGroupDynamicOffsets) bool {
    var uses: [MAX_GROUP_USES]Use = undefined;
    return admitPass(pass, uses[0..groupUses(group, offsets, &uses)]);
}

/// The pass's color, resolve and depth/stencil attachments; a read-only
/// depth or stencil aspect is a read.
pub fn admitPassAttachments(pass: *objects.DoeRenderPass) bool {
    var uses: [2 * @import("../../contracts/model/model_render_types.zig").MAX_COLOR_ATTACHMENTS + 2]Use = undefined;
    var count: usize = 0;
    const slots = @min(pass.color_attachment_count, pass.color_attachments.len);
    for (pass.color_attachments[0..slots]) |slot| {
        for ([_]u64{ slot.view_handle, slot.resolve_view_handle }) |handle| {
            if (handle == 0) continue;
            uses[count] = viewUse(@ptrFromInt(handle), .attachment);
            count += 1;
        }
    }
    if (pass.depth_target_view_handle != 0) {
        const depth = viewUse(@ptrFromInt(pass.depth_target_view_handle), .attachment);
        for ([_]u8{ ASPECT_DEPTH, ASPECT_STENCIL }, [_]bool{ pass.depth_read_only, pass.stencil_read_only }) |aspect, read_only| {
            if (depth.aspects & aspect == 0) continue;
            uses[count] = depth;
            uses[count].aspects = aspect;
            if (read_only) uses[count].usage = .read;
            count += 1;
        }
    }
    return admitPass(pass, uses[0..count]);
}

/// A draw's writable bindings do not alias.
pub fn admitDraw(pass: *objects.DoeRenderPass, pipeline: *const objects.DoeRenderPipeline) bool {
    var uses: [shared.MAX_RENDER_BIND_GROUPS * MAX_GROUP_USES]Use = undefined;
    const required = if (pipeline.required_binding_kinds_ready) pipeline.required_bindings.slice() else null;
    const count = pipelineGroupUses(pipeline.layout, required, pass.bind_groups[0..], pass.dynamic_offsets[0..], &uses);
    validateDispatchUses(uses[0..count]) catch |err| {
        recording.fail(pass.enc, err);
        return false;
    };
    return true;
}

test "a scope admits reads, or one writable usage, per buffer and texture subresource" {
    var buffer = objects.DoeBuffer{ .size = 256 };
    var other = objects.DoeBuffer{ .size = 256 };
    var scope = Scope{};
    defer scope.deinit(std.testing.allocator);
    try scope.add(std.testing.allocator, &.{ bufferUse(&buffer), bufferUse(&other) });
    var storage = bufferUse(&buffer);
    storage.usage = .storage;
    try std.testing.expectError(error.ResourceUsageConflict, scope.add(std.testing.allocator, &.{storage}));
    var other_storage = bufferUse(&other);
    other_storage.usage = .storage;
    var fresh = Scope{};
    defer fresh.deinit(std.testing.allocator);
    // Two storage uses of one buffer share a scope.
    try fresh.add(std.testing.allocator, &.{ other_storage, other_storage });
    try std.testing.expectEqual(@as(usize, 1), fresh.uses.items.len);

    var device = objects.DoeDevice{};
    var texture = objects.DoeTexture{ .device_ref = &device, .width = 4, .height = 4, .depth_or_array_layers = 2, .mip_level_count = 2, .format = abi_texture.WGPUTextureFormat_RGBA8Unorm };
    const layer0 = objects.DoeTextureView{ .tex = &texture, .mip_level_count = 1, .array_layer_count = 1 };
    var layer1 = layer0;
    layer1.base_array_layer = 1;
    var mip1 = layer0;
    mip1.base_mip_level = 1;
    // Disjoint subresources never conflict; the same one does.
    try std.testing.expect(conflict(&viewUse(&layer0, .attachment), &viewUse(&layer1, .read), false) == null);
    try std.testing.expect(conflict(&viewUse(&layer0, .attachment), &viewUse(&mip1, .read), false) == null);
    try std.testing.expectEqual(error.ResourceUsageConflict, conflict(&viewUse(&layer0, .attachment), &viewUse(&layer0, .read), false).?);
    try std.testing.expectEqual(error.ResourceUsageConflict, conflict(&viewUse(&layer0, .storage), &viewUse(&layer0, .storage_read_write), false).?);
}
