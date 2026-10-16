// coverage_builtin_spec_test.zig — WGSL spec conformance tests for builtins
// added in the builtins expansion: pack/unpack 2x16, atomicCompareExchangeWeak,
// derivative builtins, texture query builtins, math builtins with renamed MSL
// mappings, and quantizeToF16.

const std = @import("std");
const mod = @import("../../src/compiler/wgsl/mod.zig");
const translateToMsl = mod.translateToMsl;
const translateToSpirv = mod.translateToSpirv;
const MAX_OUTPUT = mod.MAX_OUTPUT;
const MAX_SPIRV_OUTPUT = mod.MAX_SPIRV_OUTPUT;
const analysis = @import("../../src/compiler/wgsl/pipeline/analysis.zig");
const validate_ir = @import("../../src/compiler/wgsl/ir/ir_validate.zig").validate;

const PACKED_INTEGER_SOURCE =
    \\requires packed_4x8_integer_dot_product;
    \\@group(0) @binding(0) var<storage, read_write> output: array<u32>;
    \\var<private> calls: u32;
    \\fn word() -> u32 { calls += 1u; return 0x80ff017fu; }
    \\fn negative_values() -> vec4i { calls += 1u; return vec4i(-129, -128, 127, 128); }
    \\fn positive_values() -> vec4u { calls += 1u; return vec4u(0u, 255u, 256u, 0xffffffffu); }
    \\@compute @workgroup_size(1) fn main() {
    \\    output[0] = pack4xI8(negative_values());
    \\    output[1] = pack4xI8Clamp(negative_values());
    \\    output[2] = pack4xU8(positive_values());
    \\    output[3] = pack4xU8Clamp(positive_values());
    \\    output[4] = u32(unpack4xI8(word()).w);
    \\    output[5] = unpack4xU8(word()).w;
    \\    output[6] = u32(dot4I8Packed(word(), word()));
    \\    output[7] = dot4U8Packed(word(), word());
    \\    output[8] = calls;
    \\}
;

test "packed integers preserve source calls across all shader targets" {
    var text: [MAX_OUTPUT]u8 = undefined;
    inline for (.{ mod.translateToMsl, mod.translateToHlsl }) |translate| {
        const len = try translate(std.testing.allocator, PACKED_INTEGER_SOURCE, &text);
        // Declarations and definitions are not evaluations of the source call.
        try std.testing.expectEqual(@as(usize, 6), std.mem.count(u8, text[0..len], "word(") -
            std.mem.count(u8, text[0..len], "uint word("));
        try std.testing.expectEqual(@as(usize, 2), std.mem.count(u8, text[0..len], "negative_values(") -
            std.mem.count(u8, text[0..len], "int4 negative_values("));
        try std.testing.expectEqual(@as(usize, 2), std.mem.count(u8, text[0..len], "positive_values(") -
            std.mem.count(u8, text[0..len], "uint4 positive_values("));
        try std.testing.expect(contains(text[0..len], "__doe_integer_dot_i4("));
        try std.testing.expect(contains(text[0..len], "__doe_integer_dot_u4("));
        try std.testing.expect(!contains(text[0..len], "dot4I8Packed"));
    }
    var binary: [MAX_SPIRV_OUTPUT]u8 = undefined;
    try std.testing.expect(try translateToSpirv(std.testing.allocator, PACKED_INTEGER_SOURCE, &binary) > 0);
}

fn packedAllocationControl(allocator: std.mem.Allocator) !void {
    var module = try analysis.analyzeToIr(allocator, PACKED_INTEGER_SOURCE);
    defer module.deinit();
    try validate_ir(&module);
}

test "packed integers unwind every lowering allocation failure" {
    try std.testing.checkAllAllocationFailures(std.testing.allocator, packedAllocationControl, .{});
}

const PACKED_CONSTANT_SOURCE =
    \\const signed_word = pack4xI8(vec4i(-129, -128, 127, 128));
    \\const unsigned_word = pack4xU8(vec4u(0u, 255u, 256u, 0xffffffffu));
    \\const signed_clamp = pack4xI8Clamp(vec4i(-129, -128, 127, 128));
    \\const unsigned_clamp = pack4xU8Clamp(vec4u(0u, 255u, 256u, 0xffffffffu));
    \\const signed_bytes = unpack4xI8(0x80ff017f);
    \\const unsigned_bytes = unpack4xU8(0x80ff017f);
    \\const signed_dot = dot4I8Packed(0x80808080, 0x7f7f7f7f);
    \\const unsigned_dot = dot4U8Packed(0xffffffff, 0xffffffff);
    \\const splat = pack4xI8(vec4i(-1));
    \\const joined = pack4xU8(vec4u(vec2u(1u, 2u), vec2u(3u, 4u)));
    \\@group(0) @binding(0) var<storage, read_write> output: array<u32>;
    \\@compute @workgroup_size(1) fn main() { output[0] = signed_word + unsigned_word; }
;

fn packedConstantAllocationControl(allocator: std.mem.Allocator) !void {
    var module = try analysis.analyzeToIr(allocator, PACKED_CONSTANT_SOURCE);
    defer module.deinit();
    try validate_ir(&module);
    const expected = [_]u64{ 0x807f807f, 0xff00ff00, 0x7f7f8080, 0xffffff00, 0, 0, @as(u32, @bitCast(@as(i32, -65024))), 260100, 0xffffffff, 0x04030201 };
    for (expected, 0..) |value, index| {
        const constant = module.globals.items[index].initializer.?;
        if (index == 4 or index == 5) {
            const bytes = if (index == 4) [_]u32{ 127, 1, 0xffffffff, 0xffffff80 } else [_]u32{ 127, 1, 255, 128 };
            for (bytes, constant.composite) |byte, actual| try std.testing.expectEqual(@as(u64, byte), actual.int);
        } else try std.testing.expectEqual(value, constant.int);
    }
}

test "packed integers fold all constant overloads and composite constructors" {
    try packedConstantAllocationControl(std.testing.allocator);
    var output: [MAX_OUTPUT]u8 = undefined;
    inline for (.{ mod.translateToMsl, mod.translateToHlsl }) |translate| _ = try translate(std.testing.allocator, PACKED_CONSTANT_SOURCE, &output);
    var binary: [MAX_SPIRV_OUTPUT]u8 = undefined;
    _ = try translateToSpirv(std.testing.allocator, PACKED_CONSTANT_SOURCE, &binary);
}

test "packed integers unwind constant ownership on allocation failure" {
    try std.testing.checkAllAllocationFailures(std.testing.allocator, packedConstantAllocationControl, .{});
}

test "packed integers reject mismatched argument types and arity" {
    inline for (.{
        .{ "pack4xI8()", error.UnsupportedBuiltin },
        .{ "pack4xU8(vec4u(1u), vec4u(2u))", error.UnsupportedBuiltin },
        .{ "pack4xI8(vec4u(1u))", error.TypeMismatch },
        .{ "pack4xU8Clamp(vec4i(1))", error.TypeMismatch },
        .{ "pack4xI8Clamp(vec3i(1))", error.TypeMismatch },
        .{ "pack4xU8(vec4f(1.0))", error.TypeMismatch },
        .{ "unpack4xI8(1i)", error.TypeMismatch },
        .{ "unpack4xI8(1u, 2u)", error.UnsupportedBuiltin },
        .{ "dot4I8Packed(1u)", error.UnsupportedBuiltin },
        .{ "dot4U8Packed(1u, 2i)", error.TypeMismatch },
        .{ "dot4U8Packed(-1, 2)", error.InvalidIr },
    }) |case| {
        var output: [MAX_SPIRV_OUTPUT]u8 = undefined;
        try std.testing.expectError(case[1], translateToSpirv(std.testing.allocator, "@compute @workgroup_size(1) fn main() { let value = " ++ case[0] ++ "; }", &output));
    }
}

test "normalized packing uses the specified GLSL instruction numbers" {
    const source =
        \\@group(0) @binding(0) var<storage, read> input: array<f32>;
        \\@group(0) @binding(1) var<storage, read_write> output: array<u32>;
        \\@compute @workgroup_size(1) fn main() {
        \\    output[0] = pack4x8unorm(vec4f(input[0], input[1], input[2], input[3]));
        \\    output[1] = pack4x8snorm(vec4f(input[0], input[1], input[2], input[3]));
        \\    output[2] = pack2x16float(vec2f(input[0], input[1]));
        \\}
    ;
    var binary: [MAX_SPIRV_OUTPUT]u8 = undefined;
    const len = try translateToSpirv(std.testing.allocator, source, &binary);
    var seen = [_]bool{ false, false, false };
    var offset: usize = 20;
    while (offset < len) {
        const instruction = std.mem.readInt(u32, binary[offset..][0..4], .little);
        if (@as(u16, @truncate(instruction)) == 12) {
            const number = std.mem.readInt(u32, binary[offset + 16 ..][0..4], .little);
            for ([_]u32{ 55, 54, 58 }, 0..) |expected, index| if (number == expected) {
                seen[index] = true;
            };
        }
        offset += (instruction >> 16) * 4;
    }
    for (seen) |present| try std.testing.expect(present);
}

fn contains(haystack: []const u8, needle: []const u8) bool {
    return std.mem.indexOf(u8, haystack, needle) != null;
}

fn expectMslContains(source: []const u8, needles: []const []const u8) !void {
    var out: [MAX_OUTPUT]u8 = undefined;
    const len = try translateToMsl(std.testing.allocator, source, &out);
    try std.testing.expect(len > 0);
    const msl = out[0..len];
    for (needles) |needle| {
        try std.testing.expect(contains(msl, needle));
    }
}

test "builtins: unpack4xU8 lowers once to unsigned vector arithmetic across targets" {
    const source =
        \\@group(0) @binding(0) var<storage, read_write> output: array<vec4<u32>>;
        \\var<private> calls: u32;
        \\fn next_word() -> u32 { calls += 1u; return 0x80ff017fu; }
        \\@compute @workgroup_size(1) fn main() {
        \\    output[0] = unpack4xU8(next_word());
        \\    output[1] = vec4<u32>(calls);
        \\}
    ;
    var text: [MAX_OUTPUT]u8 = undefined;
    inline for (.{ mod.translateToMsl, mod.translateToHlsl }) |translate| {
        const len = try translate(std.testing.allocator, source, &text);
        try std.testing.expect(!contains(text[0..len], "unpack4xU8"));
        try std.testing.expect(contains(text[0..len], ">>"));
        try std.testing.expect(contains(text[0..len], "255"));
        // One definition and one evaluation, even when the input mutates state.
        try std.testing.expectEqual(@as(usize, 2), std.mem.count(u8, text[0..len], "next_word("));
    }
    var binary: [MAX_SPIRV_OUTPUT]u8 = undefined;
    const len = try translateToSpirv(std.testing.allocator, source, &binary);
    const opcode = @import("../../src/compiler/wgsl/emit/spirv/spirv_builder.zig").Opcode;
    var offset: usize = 20;
    var calls: usize = 0;
    while (offset < len) {
        const instruction = std.mem.readInt(u32, binary[offset..][0..4], .little);
        if (@as(u16, @truncate(instruction)) == opcode.FunctionCall) calls += 1;
        offset += (instruction >> 16) * 4;
    }
    // The entry wrapper calls main; main calls next_word once.
    try std.testing.expectEqual(@as(usize, 2), calls);
}

test "builtins: unpack4xU8 rejects wrong arity and non-unsigned arguments" {
    inline for (.{
        .{ "", error.UnsupportedBuiltin },
        .{ "1u, 2u", error.UnsupportedBuiltin },
        .{ "1i", error.TypeMismatch },
        .{ "1.0f", error.TypeMismatch },
        .{ "vec2<u32>(1u)", error.TypeMismatch },
        .{ "-1", error.InvalidIr },
        .{ "0x100000000", error.InvalidIr },
    }) |case| {
        var output: [MAX_SPIRV_OUTPUT]u8 = undefined;
        try std.testing.expectError(case[1], translateToSpirv(std.testing.allocator, "@compute @workgroup_size(1) fn main() { let value = unpack4xU8(" ++ case[0] ++ "); }", &output));
    }
}

test "builtins: unpack4xU8 materializes representable abstract unsigned input" {
    const source =
        \\@group(0) @binding(0) var<storage, read_write> output: array<vec4<u32>>;
        \\@compute @workgroup_size(1) fn main() { output[0] = unpack4xU8(0xffffffff); }
    ;
    var output: [MAX_SPIRV_OUTPUT]u8 = undefined;
    try std.testing.expect(try translateToSpirv(std.testing.allocator, source, &output) > 0);
}

test "builtins: atomicCompareExchangeWeak result members through MSL and SPIR-V" {
    const source =
        \\struct Counters { value: atomic<u32>, }
        \\@group(0) @binding(0) var<storage, read_write> counters: Counters;
        \\@group(0) @binding(1) var<storage, read_write> output: array<u32>;
        \\
        \\@compute @workgroup_size(1)
        \\fn main() {
        \\    let exchanged = atomicCompareExchangeWeak(&counters.value, 0u, 1u);
        \\    output[0] = select(exchanged.old_value, 2u, exchanged.exchanged);
        \\}
    ;
    const msl_needles = [_][]const u8{
        "atomic_compare_exchange_weak_explicit",
        "_doe_cew_x",
        ".exchanged",
        ".old_value",
    };
    try expectMslContains(source, &msl_needles);

    var spirv_out: [MAX_SPIRV_OUTPUT]u8 = undefined;
    const spirv_len = try translateToSpirv(std.testing.allocator, source, &spirv_out);
    try std.testing.expect(spirv_len > 0);
}

// ============================================================
// Pack / unpack 2x16 builtins
// ============================================================

test "builtins: pack2x16snorm and pack2x16unorm through MSL" {
    const source =
        \\@group(0) @binding(0) var<storage, read_write> out: array<u32>;
        \\
        \\@compute @workgroup_size(1)
        \\fn main(@builtin(global_invocation_id) id: vec3u) {
        \\    let v = vec2f(0.5, -0.5);
        \\    out[id.x] = pack2x16snorm(v) + pack2x16unorm(v);
        \\}
    ;
    const needles = [_][]const u8{ "pack_float_to_snorm2x16(", "pack_float_to_unorm2x16(" };
    try expectMslContains(source, &needles);
}

test "builtins: unpack2x16snorm and unpack2x16unorm through MSL" {
    const source =
        \\@group(0) @binding(0) var<storage, read_write> data: array<u32>;
        \\@group(0) @binding(1) var<storage, read_write> out: array<f32>;
        \\
        \\@compute @workgroup_size(1)
        \\fn main(@builtin(global_invocation_id) id: vec3u) {
        \\    let packed = data[id.x];
        \\    let a = unpack2x16snorm(packed);
        \\    let b = unpack2x16unorm(packed);
        \\    out[id.x] = a.x + b.x;
        \\}
    ;
    const needles = [_][]const u8{ "unpack_snorm2x16_to_float(", "unpack_unorm2x16_to_float(" };
    try expectMslContains(source, &needles);
}

// ============================================================
// Math builtins with renamed MSL mappings
// ============================================================

test "builtins: faceForward through MSL emits faceforward" {
    const source =
        \\@group(0) @binding(0) var<storage, read_write> data: array<f32>;
        \\
        \\@compute @workgroup_size(1)
        \\fn main(@builtin(global_invocation_id) id: vec3u) {
        \\    let n = vec3f(0.0, 1.0, 0.0);
        \\    let i = vec3f(0.0, -1.0, 0.0);
        \\    let r = vec3f(0.0, 0.5, 0.0);
        \\    let result = faceForward(n, i, r);
        \\    data[id.x] = result.y;
        \\}
    ;
    const needles = [_][]const u8{"faceforward("};
    try expectMslContains(source, &needles);
}

test "builtins: countOneBits through MSL emits popcount" {
    const source =
        \\@group(0) @binding(0) var<storage, read_write> data: array<u32>;
        \\
        \\@compute @workgroup_size(1)
        \\fn main(@builtin(global_invocation_id) id: vec3u) {
        \\    data[id.x] = countOneBits(data[id.x]);
        \\}
    ;
    const needles = [_][]const u8{"popcount("};
    try expectMslContains(source, &needles);
}

test "builtins: reverseBits through MSL emits reverse_bits" {
    const source =
        \\@group(0) @binding(0) var<storage, read_write> data: array<u32>;
        \\
        \\@compute @workgroup_size(1)
        \\fn main(@builtin(global_invocation_id) id: vec3u) {
        \\    data[id.x] = reverseBits(data[id.x]);
        \\}
    ;
    const needles = [_][]const u8{"reverse_bits("};
    try expectMslContains(source, &needles);
}

test "builtins: countLeadingZeros and countTrailingZeros through MSL" {
    const source =
        \\@group(0) @binding(0) var<storage, read_write> data: array<u32>;
        \\
        \\@compute @workgroup_size(1)
        \\fn main(@builtin(global_invocation_id) id: vec3u) {
        \\    data[id.x] = countLeadingZeros(data[id.x]) + countTrailingZeros(data[id.x]);
        \\}
    ;
    const needles = [_][]const u8{ "clz(", "ctz(" };
    try expectMslContains(source, &needles);
}

test "builtins: saturate through MSL" {
    const source =
        \\@group(0) @binding(0) var<storage, read_write> data: array<f32>;
        \\
        \\@compute @workgroup_size(1)
        \\fn main(@builtin(global_invocation_id) id: vec3u) {
        \\    data[id.x] = saturate(data[id.x]);
        \\}
    ;
    const needles = [_][]const u8{"saturate("};
    try expectMslContains(source, &needles);
}

test "builtins: reflect and refract through MSL" {
    const source =
        \\@group(0) @binding(0) var<storage, read_write> data: array<f32>;
        \\
        \\@compute @workgroup_size(1)
        \\fn main(@builtin(global_invocation_id) id: vec3u) {
        \\    let i = vec3f(1.0, 0.0, 0.0);
        \\    let n = vec3f(0.0, 1.0, 0.0);
        \\    let r = reflect(i, n);
        \\    let t = refract(i, n, 1.5);
        \\    data[id.x] = r.x + t.x;
        \\}
    ;
    const needles = [_][]const u8{ "reflect(", "refract(" };
    try expectMslContains(source, &needles);
}

test "builtins: transpose and determinant through MSL" {
    const source =
        \\@group(0) @binding(0) var<storage, read_write> data: array<f32>;
        \\
        \\@compute @workgroup_size(1)
        \\fn main(@builtin(global_invocation_id) id: vec3u) {
        \\    let m = mat2x2f(1.0, 2.0, 3.0, 4.0);
        \\    let t = transpose(m);
        \\    let d = determinant(m);
        \\    data[id.x] = t[0][0] + d;
        \\}
    ;
    const needles = [_][]const u8{ "transpose(", "determinant(" };
    try expectMslContains(source, &needles);
}

// ============================================================
// Bit manipulation builtins
// ============================================================

test "builtins: extractBits and insertBits through MSL" {
    const source =
        \\@group(0) @binding(0) var<storage, read_write> data: array<u32>;
        \\
        \\@compute @workgroup_size(1)
        \\fn main(@builtin(global_invocation_id) id: vec3u) {
        \\    let v = data[id.x];
        \\    let extracted = extractBits(v, 4u, 8u);
        \\    let inserted = insertBits(v, extracted, 16u, 8u);
        \\    data[id.x] = inserted;
        \\}
    ;
    const needles = [_][]const u8{ "extract_bits(", "insert_bits(" };
    try expectMslContains(source, &needles);
}

test "builtins: firstLeadingBit and firstTrailingBit through MSL" {
    const source =
        \\@group(0) @binding(0) var<storage, read_write> data: array<u32>;
        \\
        \\@compute @workgroup_size(1)
        \\fn main(@builtin(global_invocation_id) id: vec3u) {
        \\    let v = data[id.x];
        \\    let flb = firstLeadingBit(v);
        \\    let ftb = firstTrailingBit(v);
        \\    data[id.x] = flb + ftb;
        \\}
    ;
    // firstLeadingBit uses (31 - clz(...)), firstTrailingBit uses ctz(...)
    const needles = [_][]const u8{ "clz(", "ctz(" };
    try expectMslContains(source, &needles);
}

// ============================================================
// quantizeToF16
// ============================================================

test "builtins: quantizeToF16 through MSL" {
    const source =
        \\@group(0) @binding(0) var<storage, read_write> data: array<f32>;
        \\
        \\@compute @workgroup_size(1)
        \\fn main(@builtin(global_invocation_id) id: vec3u) {
        \\    data[id.x] = quantizeToF16(data[id.x]);
        \\}
    ;
    // MSL: float(half(x))
    const needles = [_][]const u8{"float(half("};
    try expectMslContains(source, &needles);
}

// ============================================================
// Texture query builtins
// ============================================================

test "builtins: textureNumLevels through MSL" {
    const source =
        \\@group(0) @binding(0) var my_tex: texture_2d<f32>;
        \\@group(0) @binding(1) var<storage, read_write> out: array<u32>;
        \\
        \\@compute @workgroup_size(1)
        \\fn main(@builtin(global_invocation_id) id: vec3u) {
        \\    out[id.x] = textureNumLevels(my_tex);
        \\}
    ;
    const needles = [_][]const u8{"get_num_mip_levels()"};
    try expectMslContains(source, &needles);
}

test "builtins: textureNumLayers through MSL" {
    const source =
        \\@group(0) @binding(0) var my_tex: texture_2d_array<f32>;
        \\@group(0) @binding(1) var<storage, read_write> out: array<u32>;
        \\
        \\@compute @workgroup_size(1)
        \\fn main(@builtin(global_invocation_id) id: vec3u) {
        \\    out[id.x] = textureNumLayers(my_tex);
        \\}
    ;
    const needles = [_][]const u8{"get_array_size()"};
    try expectMslContains(source, &needles);
}

// ============================================================
// Derivative builtins (fragment stage)
// ============================================================

test "builtins: dpdx dpdy fwidth through MSL" {
    const source =
        \\@group(0) @binding(0) var my_tex: texture_2d<f32>;
        \\@group(0) @binding(1) var my_sampler: sampler;
        \\
        \\@fragment
        \\fn fs_main(@location(0) uv: vec2f) -> @location(0) vec4f {
        \\    let dx = dpdx(uv.x);
        \\    let dy = dpdy(uv.x);
        \\    let fw = fwidth(uv.x);
        \\    return vec4f(dx, dy, fw, 1.0);
        \\}
    ;
    const needles = [_][]const u8{ "dfdx(", "dfdy(", "fwidth(" };
    try expectMslContains(source, &needles);
}

test "builtins: dpdxCoarse dpdyCoarse fwidthCoarse through MSL" {
    const source =
        \\@fragment
        \\fn fs_main(@location(0) uv: vec2f) -> @location(0) vec4f {
        \\    let dx = dpdxCoarse(uv.x);
        \\    let dy = dpdyCoarse(uv.x);
        \\    let fw = fwidthCoarse(uv.x);
        \\    return vec4f(dx, dy, fw, 1.0);
        \\}
    ;
    const needles = [_][]const u8{ "dfdx(", "dfdy(", "fwidth(" };
    try expectMslContains(source, &needles);
}

test "builtins: dpdxFine dpdyFine fwidthFine through MSL" {
    const source =
        \\@fragment
        \\fn fs_main(@location(0) uv: vec2f) -> @location(0) vec4f {
        \\    let dx = dpdxFine(uv.x);
        \\    let dy = dpdyFine(uv.x);
        \\    let fw = fwidthFine(uv.x);
        \\    return vec4f(dx, dy, fw, 1.0);
        \\}
    ;
    const needles = [_][]const u8{ "dfdx(", "dfdy(", "fwidth(" };
    try expectMslContains(source, &needles);
}

// ============================================================
// MSL passthrough surface coverage
// ============================================================

test "builtins: passthrough surface coverage — every mapped name resolves" {
    // Verify the passthrough map handles all names it claims.
    const msl_maps = @import("../../src/compiler/wgsl/emit/msl/emit_msl_maps.zig");
    const names = [_][]const u8{
        "abs",         "acos",              "asin",               "atan",        "atan2",
        "ceil",        "cos",               "cosh",               "cross",       "determinant",
        "distance",    "dot",               "exp",                "exp2",        "fma",
        "floor",       "fract",             "ldexp",              "length",      "log",
        "log2",        "mix",               "normalize",          "pow",         "reflect",
        "refract",     "round",             "saturate",           "sign",        "sin",
        "sinh",        "smoothstep",        "sqrt",               "step",        "tan",
        "tanh",        "transpose",         "trunc",              "faceForward", "countOneBits",
        "reverseBits", "countLeadingZeros", "countTrailingZeros",
    };
    for (names) |name| {
        try std.testing.expect(msl_maps.msl_builtin_passthrough_name(name) != null);
    }
}

test "packed integers admit requires and reject unknown or misplaced requirements" {
    var output: [MAX_SPIRV_OUTPUT]u8 = undefined;
    _ = try translateToSpirv(std.testing.allocator, "requires packed_4x8_integer_dot_product, packed_4x8_integer_dot_product,; @compute @workgroup_size(1) fn main() {}", &output);
    try std.testing.expectError(error.UnsupportedConstruct, translateToSpirv(std.testing.allocator, "requires unknown_extension; @compute @workgroup_size(1) fn main() {}", &output));
    inline for (.{ "requires;", "requires packed_4x8_integer_dot_product", "const x = 1u; requires packed_4x8_integer_dot_product;" }) |source| {
        try std.testing.expectError(error.UnexpectedToken, translateToSpirv(std.testing.allocator, source ++ " @compute @workgroup_size(1) fn main() {}", &output));
    }
}

test "packed integers materialize inferred constructors without converting typed signed vectors" {
    const source =
        \\@group(0) @binding(0) var<storage, read_write> output: array<u32>;
        \\const packed_constant = pack4xU8(vec4(1, 2, 3, 4));
        \\@compute @workgroup_size(1) fn main() {
        \\    output[0] = pack4xU8(vec4(1, 2, 3, 4));
        \\    output[1] = pack4xI8(vec4(-1, -2, 3, 4));
        \\    output[2] = pack4xU8(vec4(output[0]));
        \\    output[3] = pack4xU8(vec4(vec2(1, 2), vec2(3, 4)));
        \\    output[4] = packed_constant;
        \\}
    ;
    var output: [MAX_SPIRV_OUTPUT]u8 = undefined;
    _ = try translateToSpirv(std.testing.allocator, source, &output);
    inline for (.{ "pack4xU8(vec4(-1, 2, 3, 4))", "pack4xI8(vec4(0x80000000, 2, 3, 4))", "pack4xU8(vec4(0x100000000, 2, 3, 4))" }) |expression| {
        try std.testing.expectError(error.InvalidIr, translateToSpirv(std.testing.allocator, "@compute @workgroup_size(1) fn main() { let x = " ++ expression ++ "; }", &output));
        try std.testing.expectError(error.InvalidIr, translateToSpirv(std.testing.allocator, "const x = " ++ expression ++ "; @compute @workgroup_size(1) fn main() {}", &output));
    }
}
