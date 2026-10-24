//! WebGPU binding slot limits
//! (https://gpuweb.github.io/gpuweb/#exceeds-the-binding-slot-limits): per
//! shader stage, the uniform buffers, storage buffers, samplers, sampled
//! textures and storage textures that bind group layout entries, pipeline
//! layouts or a pipeline's reflected bindings use, against the device's
//! resolved per-stage limits. An external texture uses four sampled-texture
//! slots, one sampler slot and one uniform-buffer slot.

const std = @import("std");
const abi_binding = @import("../../core/abi/wgpu_binding_base_types.zig");
const binding_contract = @import("../../contracts/binding.zig");
const native_shared = @import("../support/doe_native_shared_types.zig");
const native_types = @import("../support/doe_native_object_types.zig");

pub const Slot = enum(u3) { uniform_buffer, storage_buffer, sampler, sampled_texture, storage_texture };
const SLOT_COUNT = @typeInfo(Slot).@"enum".fields.len;
const STAGE_BITS = [_]u64{ abi_binding.WGPUShaderStage_Vertex, abi_binding.WGPUShaderStage_Fragment, abi_binding.WGPUShaderStage_Compute };

const KIND_BUFFER = binding_contract.layoutResourceKindCode(.buffer);
const KIND_SAMPLER = binding_contract.layoutResourceKindCode(.sampler);
const KIND_TEXTURE = binding_contract.layoutResourceKindCode(.texture);
const KIND_STORAGE_TEXTURE = binding_contract.layoutResourceKindCode(.storage_texture);
const KIND_EXTERNAL_TEXTURE = binding_contract.layoutResourceKindCode(.external_texture);

pub const Counts = struct {
    used: [STAGE_BITS.len][SLOT_COUNT]u32 = [_][SLOT_COUNT]u32{[_]u32{0} ** SLOT_COUNT} ** STAGE_BITS.len,

    pub fn add(self: *Counts, visibility: u64, slot: Slot, count: u32) void {
        for (STAGE_BITS, 0..) |bit, stage| {
            if (visibility & bit != 0) self.used[stage][@intFromEnum(slot)] +|= count;
        }
    }

    /// One layout resource kind (binding.LayoutResourceKind code) and, for a
    /// buffer, its WGPUBufferBindingType, visible in `visibility`.
    pub fn addKind(self: *Counts, visibility: u64, kind: u32, buffer_type: u32) void {
        if (kind == KIND_BUFFER) {
            self.add(visibility, if (buffer_type == abi_binding.WGPUBufferBindingType_Uniform) .uniform_buffer else .storage_buffer, 1);
        } else if (kind == KIND_SAMPLER) {
            self.add(visibility, .sampler, 1);
        } else if (kind == KIND_TEXTURE) {
            self.add(visibility, .sampled_texture, 1);
        } else if (kind == KIND_STORAGE_TEXTURE) {
            self.add(visibility, .storage_texture, 1);
        } else if (kind == KIND_EXTERNAL_TEXTURE) {
            self.add(visibility, .sampled_texture, 4);
            self.add(visibility, .sampler, 1);
            self.add(visibility, .uniform_buffer, 1);
        }
    }

    pub fn addLayoutEntries(self: *Counts, entries: []const native_shared.DoeBindGroupLayoutEntry) void {
        for (entries) |entry| self.addKind(entry.visibility, entry.resource_kind, entry.buffer_binding_type);
    }

    pub fn addRequired(self: *Counts, bindings: []const native_shared.RequiredBinding) void {
        for (bindings) |binding| self.addKind(binding.stages, binding.kind, binding.buffer_type);
    }

    /// The first exceeded limit's name, or null when every stage fits.
    pub fn failure(self: *const Counts, limits: native_types.ShaderStageLimits) ?[]const u8 {
        for (self.used) |stage| {
            if (stage[@intFromEnum(Slot.uniform_buffer)] > limits.uniform_buffers) return "maxUniformBuffersPerShaderStage";
            if (stage[@intFromEnum(Slot.storage_buffer)] > limits.storage_buffers) return "maxStorageBuffersPerShaderStage";
            if (stage[@intFromEnum(Slot.sampler)] > limits.samplers) return "maxSamplersPerShaderStage";
            if (stage[@intFromEnum(Slot.sampled_texture)] > limits.sampled_textures) return "maxSampledTexturesPerShaderStage";
            if (stage[@intFromEnum(Slot.storage_texture)] > limits.storage_textures) return "maxStorageTexturesPerShaderStage";
        }
        return null;
    }
};

const LIMIT_NAMES = .{ "maxDynamicUniformBuffersPerPipelineLayout", "maxDynamicStorageBuffersPerPipelineLayout", "maxUniformBuffersPerShaderStage", "maxStorageBuffersPerShaderStage", "maxSamplersPerShaderStage", "maxSampledTexturesPerShaderStage", "maxStorageTexturesPerShaderStage" };

/// A static `prefix` ++ limit-name message for a name `Counts.failure` returned.
pub fn message(comptime prefix: []const u8, limit: []const u8) []const u8 {
    inline for (LIMIT_NAMES) |name| {
        if (std.mem.eql(u8, limit, name)) return prefix ++ name;
    }
    return prefix ++ "a binding slot limit";
}

/// The binding slot limit `entries` exceed on `device`, or null: the
/// per-layout dynamic buffer limits, then the per-stage limits.
pub fn layoutEntriesFailure(device: *const native_types.DoeDevice, entries: []const native_shared.DoeBindGroupLayoutEntry) ?[]const u8 {
    var dynamic_uniform: u32 = 0;
    var dynamic_storage: u32 = 0;
    for (entries) |entry| {
        if (entry.resource_kind != KIND_BUFFER or !entry.buffer_has_dynamic_offset) continue;
        if (entry.buffer_binding_type == abi_binding.WGPUBufferBindingType_Uniform) dynamic_uniform += 1 else dynamic_storage += 1;
    }
    if (dynamic_uniform > device.max_dynamic_uniform_buffers_per_pipeline_layout) return "maxDynamicUniformBuffersPerPipelineLayout";
    if (dynamic_storage > device.max_dynamic_storage_buffers_per_pipeline_layout) return "maxDynamicStorageBuffersPerPipelineLayout";
    var counts = Counts{};
    counts.addLayoutEntries(entries);
    return counts.failure(device.stage_limits);
}

test "each stage counts its own slots and external textures use six" {
    const limits = native_types.ShaderStageLimits{};
    var counts = Counts{};
    for (0..16) |_| counts.addKind(abi_binding.WGPUShaderStage_Fragment, KIND_SAMPLER, 0);
    try std.testing.expectEqual(@as(?[]const u8, null), counts.failure(limits));
    counts.addKind(abi_binding.WGPUShaderStage_Vertex, KIND_SAMPLER, 0);
    try std.testing.expectEqual(@as(?[]const u8, null), counts.failure(limits));
    counts.addKind(abi_binding.WGPUShaderStage_Fragment | abi_binding.WGPUShaderStage_Compute, KIND_SAMPLER, 0);
    try std.testing.expectEqualStrings("maxSamplersPerShaderStage", counts.failure(limits).?);

    var external = Counts{};
    for (0..3) |_| external.addKind(abi_binding.WGPUShaderStage_Compute, KIND_EXTERNAL_TEXTURE, 0);
    for (0..9) |_| external.addKind(abi_binding.WGPUShaderStage_Compute, KIND_BUFFER, abi_binding.WGPUBufferBindingType_Uniform);
    try std.testing.expectEqual(@as(?[]const u8, null), external.failure(limits));
    external.addKind(abi_binding.WGPUShaderStage_Compute, KIND_TEXTURE, 0);
    try std.testing.expectEqual(@as(?[]const u8, null), external.failure(limits));
    external.addKind(abi_binding.WGPUShaderStage_Compute, KIND_EXTERNAL_TEXTURE, 0);
    try std.testing.expectEqualStrings("maxUniformBuffersPerShaderStage", external.failure(limits).?);

    var storage = Counts{};
    for (0..9) |_| storage.addKind(abi_binding.WGPUShaderStage_Vertex, KIND_BUFFER, abi_binding.WGPUBufferBindingType_ReadOnlyStorage);
    try std.testing.expectEqualStrings("maxStorageBuffersPerShaderStage", storage.failure(limits).?);
    try std.testing.expectEqual(@as(?[]const u8, null), storage.failure(.{ .storage_buffers = 10 }));
}

test "a layout's dynamic buffers count against the per-layout limits" {
    var device = native_types.DoeDevice{};
    var entries: [9]native_shared.DoeBindGroupLayoutEntry = undefined;
    for (&entries) |*entry| entry.* = .{ .resource_kind = KIND_BUFFER, .buffer_binding_type = abi_binding.WGPUBufferBindingType_Uniform, .buffer_has_dynamic_offset = true, .visibility = abi_binding.WGPUShaderStage_Compute };
    try std.testing.expectEqual(@as(?[]const u8, null), layoutEntriesFailure(&device, entries[0..8]));
    try std.testing.expectEqualStrings("maxDynamicUniformBuffersPerPipelineLayout", layoutEntriesFailure(&device, entries[0..9]).?);
    for (entries[0..5]) |*entry| entry.buffer_binding_type = abi_binding.WGPUBufferBindingType_ReadOnlyStorage;
    try std.testing.expectEqualStrings("maxDynamicStorageBuffersPerPipelineLayout", layoutEntriesFailure(&device, entries[0..5]).?);
    device.max_dynamic_storage_buffers_per_pipeline_layout = 5;
    try std.testing.expectEqual(@as(?[]const u8, null), layoutEntriesFailure(&device, entries[0..5]));
}
