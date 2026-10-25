//! The bind group layout members a shader binding's IR type fixes: the
//! default pipeline layout's entries
//! (https://gpuweb.github.io/gpuweb/#default-pipeline-layout) and the members
//! explicit layouts are checked against
//! (https://gpuweb.github.io/gpuweb/#abstract-opdef-validating-shader-binding).
//! Shared by render and compute pipeline creation.

const std = @import("std");
const native_shared = @import("../support/doe_native_shared_types.zig");
const wgsl_bindings = @import("../../compiler/wgsl/pipeline/binding_reflection.zig");
const wgsl_ir = @import("../../compiler/wgsl/ir/ir.zig");
const ir_query = @import("../../compiler/wgsl/ir/ir_query.zig");
const abi_binding = @import("../../core/abi/wgpu_binding_base_types.zig");
const abi_texture_dims = @import("../../core/abi/wgpu_texture_base_types.zig");
const texture_contract_values = @import("../../contracts/model/model_texture_value_types.zig");

pub const MAX_FILTERED_GLOBALS = 256;

/// Builtins that sample a texture through a (non-comparison) sampler.
fn filteringBuiltin(name: []const u8) bool {
    const names = [_][]const u8{ "textureSample", "textureSampleBias", "textureSampleGrad", "textureSampleLevel", "textureGather", "textureSampleBaseClampToEdge" };
    for (names) |candidate| if (std.mem.eql(u8, candidate, name)) return true;
    return false;
}

/// Marks the texture globals that the entry point (and the functions it
/// calls) samples through a sampler; the default layout makes those f32
/// textures "float" and the others "unfilterable-float".
pub fn markFilteredTextures(module_ir: *const wgsl_ir.Module, entry: wgsl_ir.FunctionId, filtered: *[MAX_FILTERED_GLOBALS]bool) void {
    var visited = [_]bool{false} ** 256;
    var pending: [256]wgsl_ir.FunctionId = undefined;
    var pending_len: usize = 0;
    if (entry >= visited.len) return;
    visited[entry] = true;
    pending[0] = entry;
    pending_len = 1;
    while (pending_len > 0) {
        pending_len -= 1;
        const function = &module_ir.functions.items[pending[pending_len]];
        for (function.exprs.items) |expr| switch (expr.data) {
            .call => |call| {
                const args = function.expr_args.items[call.args.start..][0..call.args.len];
                if (call.kind == .builtin and filteringBuiltin(call.name)) {
                    for (args) |arg| if (ir_query.findGlobalBase(function, arg)) |global| {
                        if (global < filtered.len) filtered[global] = true;
                    };
                } else if (call.kind == .user) {
                    for (module_ir.functions.items, 0..) |candidate, index| {
                        if (!std.mem.eql(u8, candidate.name, call.name)) continue;
                        if (index < visited.len and !visited[index] and pending_len < pending.len) {
                            visited[index] = true;
                            pending[pending_len] = @intCast(index);
                            pending_len += 1;
                        }
                    }
                }
            },
            else => {},
        };
    }
}

fn scalarSampleType(module_ir: *const wgsl_ir.Module, ty: wgsl_ir.TypeId, filtered: bool) u32 {
    return switch (module_ir.types.get(ty)) {
        .scalar => |scalar| switch (scalar) {
            .i32 => abi_binding.WGPUTextureSampleType_Sint,
            .u32 => abi_binding.WGPUTextureSampleType_Uint,
            else => if (filtered) abi_binding.WGPUTextureSampleType_Float else abi_binding.WGPUTextureSampleType_UnfilterableFloat,
        },
        else => abi_binding.WGPUTextureSampleType_UnfilterableFloat,
    };
}

fn storageFormat(format: wgsl_ir.TextureFormat) u32 {
    return switch (format) {
        .rgba8unorm => texture_contract_values.WGPUTextureFormat_RGBA8Unorm,
        .rgba8snorm => texture_contract_values.WGPUTextureFormat_RGBA8Snorm,
        .rgba8uint => texture_contract_values.WGPUTextureFormat_RGBA8Uint,
        .rgba8sint => texture_contract_values.WGPUTextureFormat_RGBA8Sint,
        .rgba16uint => texture_contract_values.WGPUTextureFormat_RGBA16Uint,
        .rgba16sint => texture_contract_values.WGPUTextureFormat_RGBA16Sint,
        .rgba16float => texture_contract_values.WGPUTextureFormat_RGBA16Float,
        .r32uint => texture_contract_values.WGPUTextureFormat_R32Uint,
        .r32sint => texture_contract_values.WGPUTextureFormat_R32Sint,
        .r32float => texture_contract_values.WGPUTextureFormat_R32Float,
        .rg32uint => texture_contract_values.WGPUTextureFormat_RG32Uint,
        .rg32sint => texture_contract_values.WGPUTextureFormat_RG32Sint,
        .rg32float => texture_contract_values.WGPUTextureFormat_RG32Float,
        .rgba32uint => texture_contract_values.WGPUTextureFormat_RGBA32Uint,
        .rgba32sint => texture_contract_values.WGPUTextureFormat_RGBA32Sint,
        .rgba32float => texture_contract_values.WGPUTextureFormat_RGBA32Float,
    };
}

/// The default bind group layout members for a binding, from its global's
/// IR type (https://gpuweb.github.io/gpuweb/#default-pipeline-layout).
/// Returns false when the binding has no IR global to describe it.
pub fn describeDefaultLayout(module_ir: *const wgsl_ir.Module, meta: wgsl_bindings.BindingMeta, filtered: *const [MAX_FILTERED_GLOBALS]bool, use: *native_shared.RequiredBinding) bool {
    use.min_binding_size = meta.min_binding_size;
    const index = for (module_ir.globals.items, 0..) |global, i| {
        const point = global.binding orelse continue;
        if (point.group == meta.group and point.binding == meta.binding) break i;
    } else return false;
    const is_filtered = index < filtered.len and filtered[index];
    const T = abi_texture_dims;
    use.described = true;
    const global = module_ir.globals.items[index];
    // A storage variable's access defaults to read (WGSL), which binds as
    // "read-only-storage"; only read_write binds as "storage".
    if (meta.kind == .buffer and global.addr_space == .storage)
        use.buffer_type = if (global.access == .read_write) abi_binding.WGPUBufferBindingType_Storage else abi_binding.WGPUBufferBindingType_ReadOnlyStorage;
    switch (module_ir.types.get(global.ty)) {
        .sampler => use.sampler_type = abi_binding.WGPUSamplerBindingType_Filtering,
        .sampler_comparison => use.sampler_type = abi_binding.WGPUSamplerBindingType_Comparison,
        .texture_1d => |sample| {
            use.sample_type = scalarSampleType(module_ir, sample, is_filtered);
            use.view_dimension = T.WGPUTextureViewDimension_1D;
        },
        .texture_2d => |sample| {
            use.sample_type = scalarSampleType(module_ir, sample, is_filtered);
            use.view_dimension = T.WGPUTextureViewDimension_2D;
        },
        .texture_2d_array => |sample| {
            use.sample_type = scalarSampleType(module_ir, sample, is_filtered);
            use.view_dimension = T.WGPUTextureViewDimension_2DArray;
        },
        .texture_cube => |sample| {
            use.sample_type = scalarSampleType(module_ir, sample, is_filtered);
            use.view_dimension = T.WGPUTextureViewDimension_Cube;
        },
        .texture_3d => |sample| {
            use.sample_type = scalarSampleType(module_ir, sample, is_filtered);
            use.view_dimension = T.WGPUTextureViewDimension_3D;
        },
        .texture_multisampled_2d => |sample| {
            // Multisampled textures are never filtered.
            use.sample_type = scalarSampleType(module_ir, sample, false);
            use.view_dimension = T.WGPUTextureViewDimension_2D;
            use.multisampled = true;
        },
        .texture_depth_2d => {
            use.sample_type = abi_binding.WGPUTextureSampleType_Depth;
            use.view_dimension = T.WGPUTextureViewDimension_2D;
        },
        .texture_depth_cube => {
            use.sample_type = abi_binding.WGPUTextureSampleType_Depth;
            use.view_dimension = T.WGPUTextureViewDimension_Cube;
        },
        .storage_texture_2d => |storage| {
            use.storage_format = storageFormat(storage.format);
            use.storage_access = switch (storage.access) {
                .read => abi_binding.WGPUStorageTextureAccess_ReadOnly,
                .write => abi_binding.WGPUStorageTextureAccess_WriteOnly,
                .read_write => abi_binding.WGPUStorageTextureAccess_ReadWrite,
            };
            use.view_dimension = T.WGPUTextureViewDimension_2D;
        },
        else => {},
    }
    return true;
}
