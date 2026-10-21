const std = @import("std");
const ir = @import("../../ir/ir.zig");
const operator_text = @import("../operator_text.zig");

pub const unary_op_text = operator_text.unaryOp;
pub const binary_op_text = operator_text.binaryOp;
pub const assign_op_text = operator_text.assignOp;

pub fn hlsl_builtin_name(builtin: ir.Builtin) []const u8 {
    return switch (builtin) {
        .position => "SV_Position",
        .frag_depth => "SV_Depth",
        .front_facing => "SV_IsFrontFace",
        .global_invocation_id => "SV_DispatchThreadID",
        .local_invocation_id => "SV_GroupThreadID",
        .local_invocation_index => "SV_GroupIndex",
        .workgroup_id => "SV_GroupID",
        .num_workgroups => "UNSUPPORTED_BUILTIN",
        .sample_index => "SV_SampleIndex",
        .sample_mask => "SV_Coverage",
        .vertex_index => "SV_VertexID",
        .instance_index => "SV_InstanceID",
        .subgroup_size, .subgroup_invocation_id => "UNSUPPORTED_BUILTIN",
        .clip_distances => "SV_ClipDistance",
        .primitive_index => "SV_PrimitiveID",
        else => "UNSUPPORTED_BUILTIN",
    };
}

/// Returns the HLSL intrinsic call for builtins that map to function calls
/// rather than entry-point parameter semantics, or null for standard semantics.
pub fn hlsl_intrinsic_builtin(builtin: ir.Builtin) ?[]const u8 {
    return switch (builtin) {
        .subgroup_size => "WaveGetLaneCount()",
        .subgroup_invocation_id => "WaveGetLaneIndex()",
        else => null,
    };
}

pub fn hlsl_builtin_has_semantic(builtin: ir.Builtin) bool {
    return !std.mem.eql(u8, hlsl_builtin_name(builtin), "UNSUPPORTED_BUILTIN");
}

pub fn texture_component(sample_ty: ir.TypeId) []const u8 {
    _ = sample_ty;
    return "float";
}

pub fn hlsl_bitcast_fn(module: *const ir.Module, result_ty: ir.TypeId) []const u8 {
    const scalar = switch (module.types.get(result_ty)) {
        .scalar => |s| s,
        .vector => |v| switch (module.types.get(v.elem)) {
            .scalar => |s| s,
            else => return "asuint",
        },
        else => return "asuint",
    };
    return switch (scalar) {
        .f32, .abstract_float, .f16 => "asfloat",
        .i32, .abstract_int => "asint",
        else => "asuint",
    };
}

const HLSL_RENAMED_BUILTINS = [_]struct { wgsl: []const u8, hlsl: []const u8 }{
    .{ .wgsl = "fract", .hlsl = "frac" },
    .{ .wgsl = "inverseSqrt", .hlsl = "rsqrt" },
    .{ .wgsl = "mix", .hlsl = "lerp" },
    .{ .wgsl = "countOneBits", .hlsl = "countbits" },
    .{ .wgsl = "reverseBits", .hlsl = "reversebits" },
    .{ .wgsl = "firstLeadingBit", .hlsl = "firstbithigh" },
    .{ .wgsl = "firstTrailingBit", .hlsl = "firstbitlow" },
    .{ .wgsl = "atomicLoad", .hlsl = "doe_atomicLoad" },
    .{ .wgsl = "atomicStore", .hlsl = "doe_atomicStore" },
    .{ .wgsl = "atomicAdd", .hlsl = "doe_atomicAdd" },
    .{ .wgsl = "atomicSub", .hlsl = "doe_atomicSub" },
    .{ .wgsl = "atomicMin", .hlsl = "doe_atomicMin" },
    .{ .wgsl = "atomicMax", .hlsl = "doe_atomicMax" },
    .{ .wgsl = "atomicAnd", .hlsl = "doe_atomicAnd" },
    .{ .wgsl = "atomicOr", .hlsl = "doe_atomicOr" },
    .{ .wgsl = "atomicXor", .hlsl = "doe_atomicXor" },
    .{ .wgsl = "atomicExchange", .hlsl = "doe_atomicExchange" },
    .{ .wgsl = "unpack2x16float", .hlsl = "doe_unpack2x16float" },
    .{ .wgsl = "pack2x16float", .hlsl = "doe_pack2x16float" },
    .{ .wgsl = "pack2x16unorm", .hlsl = "doe_pack2x16unorm" },
    .{ .wgsl = "pack2x16snorm", .hlsl = "doe_pack2x16snorm" },
    .{ .wgsl = "unpack2x16unorm", .hlsl = "doe_unpack2x16unorm" },
    .{ .wgsl = "unpack2x16snorm", .hlsl = "doe_unpack2x16snorm" },
    .{ .wgsl = "unpack4x8unorm", .hlsl = "doe_unpack4x8unorm" },
    .{ .wgsl = "unpack4x8snorm", .hlsl = "doe_unpack4x8snorm" },
    .{ .wgsl = "pack4x8unorm", .hlsl = "doe_pack4x8unorm" },
    .{ .wgsl = "pack4x8snorm", .hlsl = "doe_pack4x8snorm" },
    .{ .wgsl = "subgroupAdd", .hlsl = "WaveActiveSum" },
    .{ .wgsl = "subgroupExclusiveAdd", .hlsl = "WavePrefixSum" },
    .{ .wgsl = "subgroupMin", .hlsl = "WaveActiveMin" },
    .{ .wgsl = "subgroupMax", .hlsl = "WaveActiveMax" },
    .{ .wgsl = "subgroupBroadcast", .hlsl = "WaveReadLaneAt" },
    .{ .wgsl = "subgroupShuffle", .hlsl = "WaveReadLaneAt" },
    .{ .wgsl = "subgroupAnd", .hlsl = "WaveActiveBitAnd" },
    .{ .wgsl = "subgroupOr", .hlsl = "WaveActiveBitOr" },
    .{ .wgsl = "subgroupXor", .hlsl = "WaveActiveBitXor" },
    .{ .wgsl = "subgroupAll", .hlsl = "WaveActiveAllTrue" },
    .{ .wgsl = "subgroupAny", .hlsl = "WaveActiveAnyTrue" },
};

pub fn hlsl_renamed_builtin(name: []const u8) ?[]const u8 {
    for (HLSL_RENAMED_BUILTINS) |entry| {
        if (std.mem.eql(u8, name, entry.wgsl)) return entry.hlsl;
    }
    return null;
}

pub fn hlsl_builtin_passthrough(name: []const u8) bool {
    const list = [_][]const u8{
        "abs",         "acos",     "asin",       "atan",      "atan2",
        "ceil",        "clamp",    "cos",        "cosh",      "cross",
        "determinant", "distance", "dot",        "exp",       "exp2",
        "floor",       "fma",      "ldexp",      "length",    "log",
        "log2",        "max",      "min",        "normalize", "pow",
        "reflect",     "refract",  "round",      "saturate",  "sign",
        "sin",         "sinh",     "smoothstep", "sqrt",      "step",
        "tan",         "tanh",     "transpose",  "trunc",
    };
    inline for (list) |candidate| {
        if (std.mem.eql(u8, name, candidate)) return true;
    }
    return false;
}

// Continuing clauses can be emitted at a continue site inside a shadowing scope.
// Preserve IR identity for colliding locals instead of resolving their source names again.
pub fn local_name(module: *const ir.Module, function: ir.Function, index: u32, buffer: *[32]u8) error{OutputTooLarge}![]const u8 {
    const name = function.locals.items[index].name;
    var collides = false;
    for (function.locals.items, 0..) |local, other| {
        if (other != index and std.mem.eql(u8, local.name, name)) collides = true;
    }
    for (function.params.items) |param| {
        if (std.mem.eql(u8, param.name, name)) collides = true;
    }
    for (module.globals.items) |global| {
        if (std.mem.eql(u8, global.name, name)) collides = true;
    }
    if (!collides) return name;
    // WGSL reserves double-underscore names, so source identifiers cannot collide.
    return std.fmt.bufPrint(buffer, "__doe_local_{d}", .{index}) catch error.OutputTooLarge;
}
