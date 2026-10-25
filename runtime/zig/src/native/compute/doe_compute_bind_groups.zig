const std = @import("std");
const native_shared = @import("../support/doe_native_shared_types.zig");
const native_types = @import("../support/doe_native_object_types.zig");
const native_helpers = @import("../support/doe_native_object_helpers.zig");
const binding_contract = @import("../../contracts/binding.zig");
const abi_binding = @import("../../core/abi/wgpu_binding_base_types.zig");

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

pub const PipelineBindingError = error{ BindGroupMissing, BindGroupIncompatible };
pub const DispatchBindingError = error{ DispatchBindGroupMissing, DispatchBindGroupIncompatible, DispatchBindingUnsupported };
pub const DrawBindingError = error{ DrawBindGroupMissing, DrawBindGroupIncompatible, DrawBindingUnsupported };

/// Bind groups, dispatches and draws execute through tables indexed by
/// binding number below MAX_BIND in each group. A pipeline whose entry points
/// use a larger (valid) binding number is checked like any other but cannot
/// execute, and its dispatches and draws report that instead of silently
/// leaving the binding out.
fn usesUnexecutableBinding(required: ?[]const native_shared.RequiredBinding) bool {
    for (required orelse return false) |use| {
        if (use.binding >= MAX_BIND) return true;
    }
    return false;
}

/// Bind groups a compute pipeline requires at dispatch.
pub fn validateDispatchBindGroups(
    layout: ?*const native_types.DoePipelineLayout,
    required: ?[]const native_shared.RequiredBinding,
    auto_layout_id: u64,
    bind_groups: []const ?*DoeBindGroup,
) DispatchBindingError!void {
    validatePipelineBindGroups(layout, required, auto_layout_id, bind_groups) catch |err| return switch (err) {
        error.BindGroupMissing => error.DispatchBindGroupMissing,
        error.BindGroupIncompatible => error.DispatchBindGroupIncompatible,
    };
    if (usesUnexecutableBinding(required)) return error.DispatchBindingUnsupported;
}

/// Bind groups a render pipeline requires at a pass or bundle draw.
pub fn validateDrawBindGroups(
    pipeline: *const native_types.DoeRenderPipeline,
    bind_groups: []const ?*DoeBindGroup,
) DrawBindingError!void {
    const required = if (pipeline.required_binding_kinds_ready) pipeline.required_bindings.slice() else null;
    validatePipelineBindGroups(pipeline.layout, required, pipeline.auto_layout_id, bind_groups) catch |err| return switch (err) {
        error.BindGroupMissing => error.DrawBindGroupMissing,
        error.BindGroupIncompatible => error.DrawBindGroupIncompatible,
    };
    if (usesUnexecutableBinding(required)) return error.DrawBindingUnsupported;
}

/// Bind groups a pipeline requires
/// (https://gpuweb.github.io/gpuweb/#abstract-opdef-validate-encoder-bind-groups).
/// An explicit layout requires each nonempty group bound with the same or an
/// equal-entry layout; another pipeline's automatic layout never matches. An
/// automatic layout requires, for every binding the entry points use
/// (`required`), a bound group made from one of this pipeline's own layouts
/// (`auto_layout_id`) whose entries have the required resource kinds.
pub fn validatePipelineBindGroups(
    layout: ?*const native_types.DoePipelineLayout,
    required: ?[]const native_shared.RequiredBinding,
    auto_layout_id: u64,
    bind_groups: []const ?*DoeBindGroup,
) PipelineBindingError!void {
    const pipeline_layout = layout orelse {
        for (required orelse return) |use| {
            const bound = (if (use.group < bind_groups.len) bind_groups[use.group] else null) orelse
                return error.BindGroupMissing;
            const bound_layout = bound.layout orelse return error.BindGroupIncompatible;
            if (auto_layout_id == 0 or bound_layout.auto_layout_id != auto_layout_id)
                return error.BindGroupIncompatible;
            if (!layoutHasBinding(bound_layout, use.binding, use.kind)) return error.BindGroupIncompatible;
        }
        return;
    };
    const count: usize = @min(pipeline_layout.bind_group_layout_count, MAX_COMPUTE_BIND_GROUPS);
    for (pipeline_layout.bind_group_layouts[0..count], 0..) |maybe_required, index| {
        const group_layout = maybe_required orelse continue;
        if (group_layout.entry_count == 0) continue;
        const bound = (if (index < bind_groups.len) bind_groups[index] else null) orelse
            return error.BindGroupMissing;
        const bound_layout = bound.layout orelse return error.BindGroupIncompatible;
        if (bound_layout != group_layout and !layoutsEquivalent(bound_layout, group_layout))
            return error.BindGroupIncompatible;
    }
}

/// Every binding the entry points use must be in its group of the explicit
/// pipeline layout with a matching resource kind, visible to every stage
/// that uses it, and, for a buffer, of the buffer type the shader declares
/// (https://gpuweb.github.io/gpuweb/#abstract-opdef-validating-shader-binding).
/// Texture sample types and dimensions, sampler comparison and storage
/// texture access and format are not compared: reflection does not record
/// them. Entries of a Flat4 layout record only binding numbers and pass.
pub fn pipelineLayoutAdmitsBindings(layout: *const native_types.DoePipelineLayout, required: []const native_shared.RequiredBinding) bool {
    for (required) |use| {
        const kind = use.kind;
        if (use.group >= layout.bind_group_layout_count) return false;
        const group_layout = layout.bind_group_layouts[use.group] orelse return false;
        const binding = use.binding;
        if (!layoutHasBinding(group_layout, binding, kind)) return false;
        if (group_layout.entries_inline) continue;
        const entry = for (layoutEntries(group_layout)) |candidate| {
            if (candidate.binding == binding) break candidate;
        } else unreachable;
        const stages: u64 = use.stages;
        if (entry.visibility & stages != stages) return false;
        if (kind == binding_contract.layoutResourceKindCode(.buffer)) {
            const declared = if (entry.buffer_binding_type == abi_binding.WGPUBufferBindingType_Undefined)
                abi_binding.WGPUBufferBindingType_Uniform
            else
                entry.buffer_binding_type;
            const required_type = use.buffer_type;
            // Reflection reports a storage variable with the default access
            // (var<storage>, which is read) as read_write, so a read-only
            // storage layout entry also serves a "storage" requirement.
            const default_access_gap = !use.described and required_type == abi_binding.WGPUBufferBindingType_Storage and
                declared == abi_binding.WGPUBufferBindingType_ReadOnlyStorage;
            if (declared != required_type and !default_access_gap) return false;
        }
        if (use.described and !entryAdmitsShaderType(entry, use)) return false;
    }
    return true;
}

/// "validating shader binding" for the members the shader's IR types fix
/// (https://gpuweb.github.io/gpuweb/#abstract-opdef-validating-shader-binding):
/// sampler comparison, texture multisampling, view dimension and sample type,
/// and storage texture view dimension, format and access. Undefined layout
/// members take their WebGPU defaults.
fn entryAdmitsShaderType(entry: native_shared.DoeBindGroupLayoutEntry, use: native_shared.RequiredBinding) bool {
    const T = @import("../../core/abi/wgpu_texture_base_types.zig");
    const B = abi_binding;
    const dimension = if (entry.texture_view_dimension == T.WGPUTextureViewDimension_Undefined) T.WGPUTextureViewDimension_2D else entry.texture_view_dimension;
    if (use.kind == binding_contract.layoutResourceKindCode(.sampler)) {
        return (entry.sampler_binding_type == B.WGPUSamplerBindingType_Comparison) == (use.sampler_type == B.WGPUSamplerBindingType_Comparison);
    }
    if (use.kind == binding_contract.layoutResourceKindCode(.texture)) {
        if (entry.resource_kind != binding_contract.layoutResourceKindCode(.texture)) return true;
        if (entry.texture_multisampled != use.multisampled or dimension != use.view_dimension) return false;
        const layout_sample = if (entry.texture_sample_type == B.WGPUTextureSampleType_Undefined) B.WGPUTextureSampleType_Float else entry.texture_sample_type;
        const shader_float = use.sample_type == B.WGPUTextureSampleType_Float or use.sample_type == B.WGPUTextureSampleType_UnfilterableFloat;
        return switch (layout_sample) {
            B.WGPUTextureSampleType_Float, B.WGPUTextureSampleType_UnfilterableFloat => shader_float,
            B.WGPUTextureSampleType_Sint, B.WGPUTextureSampleType_Uint => use.sample_type == layout_sample,
            // texture_1d and texture_3d are not depth-compatible.
            B.WGPUTextureSampleType_Depth => use.sample_type == B.WGPUTextureSampleType_Depth or
                (shader_float and dimension != T.WGPUTextureViewDimension_1D and dimension != T.WGPUTextureViewDimension_3D),
            else => false,
        };
    }
    if (use.kind == binding_contract.layoutResourceKindCode(.storage_texture)) {
        // Storage texture layout entries keep their access in texture_sample_type.
        const access = if (entry.texture_sample_type == B.WGPUStorageTextureAccess_Undefined) B.WGPUStorageTextureAccess_WriteOnly else entry.texture_sample_type;
        // A read-write entry also serves a write-only shader texture.
        const access_admits = access == use.storage_access or
            (access == B.WGPUStorageTextureAccess_ReadWrite and use.storage_access == B.WGPUStorageTextureAccess_WriteOnly);
        return dimension == use.view_dimension and entry.storage_texture_format == use.storage_format and access_admits;
    }
    return true;
}

/// The layout has an entry for `binding` of resource kind `kind`; an
/// external texture entry serves a texture binding.
fn layoutHasBinding(layout: *const native_types.DoeBindGroupLayout, binding: u32, kind: u8) bool {
    const entry = for (layoutEntries(layout)) |candidate| {
        if (candidate.binding == binding) break candidate;
    } else return false;
    const external = binding_contract.layoutResourceKindCode(.external_texture);
    const texture = binding_contract.layoutResourceKindCode(.texture);
    return entry.resource_kind == kind or (entry.resource_kind == external and kind == texture);
}

fn layoutEntries(layout: *const native_types.DoeBindGroupLayout) []const native_shared.DoeBindGroupLayoutEntry {
    const entries = layout.entries orelse return &.{};
    return entries[0..@min(entries.len, layout.entry_count)];
}

/// Group-equivalent layouts (https://gpuweb.github.io/gpuweb/#group-equivalent):
/// the same exclusive pipeline, if any, and equal entries.
fn layoutsEquivalent(a: *const native_types.DoeBindGroupLayout, b: *const native_types.DoeBindGroupLayout) bool {
    if (a.automatic or b.automatic) {
        if (a.auto_layout_id == 0 or a.auto_layout_id != b.auto_layout_id) return false;
    }
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

test "validateDispatchBindGroups requires explicit groups with equal layouts and automatic groups with matching bindings" {
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
    try validateDispatchBindGroups(&layout, null, 0, &.{ null, &bound });
    try validateDispatchBindGroups(null, null, 0, &.{});
    try std.testing.expectError(error.DispatchBindGroupMissing, validateDispatchBindGroups(&layout, null, 0, &.{ null, null }));
    try std.testing.expectError(error.DispatchBindGroupIncompatible, validateDispatchBindGroups(&layout, null, 0, &.{ null, &wrong }));
    equal.automatic = true;
    try std.testing.expectError(error.DispatchBindGroupIncompatible, validateDispatchBindGroups(&layout, null, 0, &.{ null, &bound }));
    // Automatic layouts: group 1 binding 0 must be a buffer, bound from this
    // pipeline's (id 7) own layouts.
    var uses = [_]native_shared.RequiredBinding{.{ .group = 1, .binding = 0, .kind = 1 }};
    equal.auto_layout_id = 7;
    different.auto_layout_id = 7;
    try validateDispatchBindGroups(null, &uses, 7, &.{ null, &bound });
    try std.testing.expectError(error.DispatchBindGroupMissing, validateDispatchBindGroups(null, &uses, 7, &.{ &bound, null }));
    try std.testing.expectError(error.DispatchBindGroupIncompatible, validateDispatchBindGroups(null, &uses, 7, &.{ null, &wrong }));
    uses[0].kind = 3;
    try std.testing.expectError(error.DispatchBindGroupIncompatible, validateDispatchBindGroups(null, &uses, 7, &.{ null, &bound }));
    // Another pipeline's automatic layout or an explicit layout never matches.
    uses[0].kind = 1;
    try std.testing.expectError(error.DispatchBindGroupIncompatible, validateDispatchBindGroups(null, &uses, 8, &.{ null, &bound }));
    var explicit = DoeBindGroup{ .layout = &required };
    try std.testing.expectError(error.DispatchBindGroupIncompatible, validateDispatchBindGroups(null, &uses, 7, &.{ null, &explicit }));
    // A binding number past the execution slot table is still checked, and
    // a compatible group for it reports that it cannot execute.
    uses[0].binding = 40;
    try std.testing.expectError(error.DispatchBindGroupIncompatible, validateDispatchBindGroups(null, &uses, 7, &.{ null, &bound }));
    var high_entries = [_]native_shared.DoeBindGroupLayoutEntry{.{ .binding = 40, .resource_kind = 1 }};
    var high_layout = native_types.DoeBindGroupLayout{ .entry_count = 1, .entries = high_entries[0..], .auto_layout_id = 7, .automatic = true };
    var high = DoeBindGroup{ .layout = &high_layout };
    try std.testing.expectError(error.DispatchBindingUnsupported, validateDispatchBindGroups(null, &uses, 7, &.{ null, &high }));
}

test "validateDrawBindGroups compares every layout entry member, including visibility" {
    var fragment_entries = [_]native_shared.DoeBindGroupLayoutEntry{.{ .binding = 0, .resource_kind = 1, .visibility = 2 }};
    var vertex_entries = [_]native_shared.DoeBindGroupLayoutEntry{.{ .binding = 0, .resource_kind = 1, .visibility = 1 }};
    var required = native_types.DoeBindGroupLayout{ .entry_count = 1, .entries = fragment_entries[0..] };
    var other_stage = native_types.DoeBindGroupLayout{ .entry_count = 1, .entries = vertex_entries[0..] };
    var layout = native_types.DoePipelineLayout{ .bind_group_layout_count = 1 };
    layout.bind_group_layouts[0] = &required;
    const pipeline = native_types.DoeRenderPipeline{ .layout = &layout };
    var bound = DoeBindGroup{ .layout = &required };
    var wrong = DoeBindGroup{ .layout = &other_stage };
    try validateDrawBindGroups(&pipeline, &.{&bound});
    try std.testing.expectError(error.DrawBindGroupMissing, validateDrawBindGroups(&pipeline, &.{null}));
    try std.testing.expectError(error.DrawBindGroupIncompatible, validateDrawBindGroups(&pipeline, &.{&wrong}));
}

test "pipelineLayoutAdmitsBindings requires each used binding in its group with a matching kind, stage and buffer type" {
    const uniform = abi_binding.WGPUBufferBindingType_Uniform;
    var entries = [_]native_shared.DoeBindGroupLayoutEntry{
        .{ .binding = 0, .resource_kind = 1, .visibility = 4, .buffer_binding_type = uniform },
        .{ .binding = 900, .resource_kind = 1, .visibility = 4, .buffer_binding_type = uniform },
    };
    var group = native_types.DoeBindGroupLayout{ .entry_count = 2, .entries = entries[0..] };
    var empty = native_types.DoeBindGroupLayout{};
    var layout = native_types.DoePipelineLayout{ .bind_group_layout_count = 2 };
    layout.bind_group_layouts[0] = &empty;
    layout.bind_group_layouts[1] = &group;
    var uses = [_]native_shared.RequiredBinding{.{ .group = 1, .binding = 0, .kind = 1, .buffer_type = uniform, .stages = 4 }};
    try std.testing.expect(pipelineLayoutAdmitsBindings(&layout, &uses));
    // Another stage, another buffer type.
    uses[0].stages = 2;
    try std.testing.expect(!pipelineLayoutAdmitsBindings(&layout, &uses));
    uses[0].stages = 4;
    uses[0].buffer_type = abi_binding.WGPUBufferBindingType_ReadOnlyStorage;
    try std.testing.expect(!pipelineLayoutAdmitsBindings(&layout, &uses));
    // A read-only storage entry serves a shader storage variable (whose
    // reflected access may be the default read); a storage entry does not
    // serve a read-only one.
    entries[0].buffer_binding_type = abi_binding.WGPUBufferBindingType_ReadOnlyStorage;
    uses[0].buffer_type = abi_binding.WGPUBufferBindingType_Storage;
    try std.testing.expect(pipelineLayoutAdmitsBindings(&layout, &uses));
    entries[0].buffer_binding_type = abi_binding.WGPUBufferBindingType_Storage;
    uses[0].buffer_type = abi_binding.WGPUBufferBindingType_ReadOnlyStorage;
    try std.testing.expect(!pipelineLayoutAdmitsBindings(&layout, &uses));
    entries[0].buffer_binding_type = uniform;
    uses[0].buffer_type = uniform;
    // Binding numbers past the execution slot table: present, absent, other type.
    uses[0].binding = 900;
    try std.testing.expect(pipelineLayoutAdmitsBindings(&layout, &uses));
    uses[0].binding = 901;
    try std.testing.expect(!pipelineLayoutAdmitsBindings(&layout, &uses));
    uses[0].binding = 900;
    uses[0].buffer_type = abi_binding.WGPUBufferBindingType_Storage;
    try std.testing.expect(!pipelineLayoutAdmitsBindings(&layout, &uses));
    uses[0].buffer_type = uniform;
    // A binding missing from its group, another resource kind, a missing group.
    uses[0] = .{ .group = 0, .binding = 0, .kind = 1, .buffer_type = uniform, .stages = 4 };
    try std.testing.expect(!pipelineLayoutAdmitsBindings(&layout, &uses));
    uses[0] = .{ .group = 1, .binding = 0, .kind = 3, .stages = 4 };
    try std.testing.expect(!pipelineLayoutAdmitsBindings(&layout, &uses));
    uses[0] = .{ .group = 2, .binding = 0, .kind = 1, .buffer_type = uniform, .stages = 4 };
    try std.testing.expect(!pipelineLayoutAdmitsBindings(&layout, &uses));
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

test "explicit layout entries admit only the shader types WebGPU pairs with them" {
    const T = @import("../../core/abi/wgpu_texture_base_types.zig");
    const B = abi_binding;
    const texture_kind: u8 = @intCast(binding_contract.layoutResourceKindCode(.texture));
    const float_2d = native_shared.RequiredBinding{ .group = 0, .binding = 0, .kind = texture_kind, .sample_type = B.WGPUTextureSampleType_UnfilterableFloat, .view_dimension = T.WGPUTextureViewDimension_2D, .described = true };
    var entry = native_shared.DoeBindGroupLayoutEntry{ .resource_kind = texture_kind, .texture_sample_type = B.WGPUTextureSampleType_Float, .texture_view_dimension = T.WGPUTextureViewDimension_2D };
    try std.testing.expect(entryAdmitsShaderType(entry, float_2d));
    entry.texture_sample_type = B.WGPUTextureSampleType_Depth;
    try std.testing.expect(entryAdmitsShaderType(entry, float_2d));
    entry.texture_sample_type = B.WGPUTextureSampleType_Uint;
    try std.testing.expect(!entryAdmitsShaderType(entry, float_2d));
    entry.texture_sample_type = B.WGPUTextureSampleType_Float;
    entry.texture_view_dimension = T.WGPUTextureViewDimension_2DArray;
    try std.testing.expect(!entryAdmitsShaderType(entry, float_2d));
    entry.texture_view_dimension = T.WGPUTextureViewDimension_Undefined;
    entry.texture_multisampled = true;
    try std.testing.expect(!entryAdmitsShaderType(entry, float_2d));

    const storage_kind: u8 = @intCast(binding_contract.layoutResourceKindCode(.storage_texture));
    const r32_write = native_shared.RequiredBinding{ .group = 0, .binding = 0, .kind = storage_kind, .storage_format = 0x1B, .storage_access = B.WGPUStorageTextureAccess_WriteOnly, .view_dimension = T.WGPUTextureViewDimension_2D, .described = true };
    var storage = native_shared.DoeBindGroupLayoutEntry{ .resource_kind = storage_kind, .texture_sample_type = B.WGPUStorageTextureAccess_ReadWrite, .storage_texture_format = 0x1B };
    try std.testing.expect(entryAdmitsShaderType(storage, r32_write));
    storage.texture_sample_type = B.WGPUStorageTextureAccess_ReadOnly;
    try std.testing.expect(!entryAdmitsShaderType(storage, r32_write));

    const sampler_kind: u8 = @intCast(binding_contract.layoutResourceKindCode(.sampler));
    const comparison = native_shared.RequiredBinding{ .group = 0, .binding = 0, .kind = sampler_kind, .sampler_type = B.WGPUSamplerBindingType_Comparison, .described = true };
    try std.testing.expect(!entryAdmitsShaderType(.{ .resource_kind = sampler_kind, .sampler_binding_type = B.WGPUSamplerBindingType_Filtering }, comparison));
    try std.testing.expect(entryAdmitsShaderType(.{ .resource_kind = sampler_kind, .sampler_binding_type = B.WGPUSamplerBindingType_Comparison }, comparison));
}
