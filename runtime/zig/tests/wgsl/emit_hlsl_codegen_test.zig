// emit_hlsl_codegen_test.zig — HLSL compute, binding, and control-flow emission tests.

const std = @import("std");
const mod = @import("../../src/compiler/wgsl/mod.zig");
const dispatch_contract = @import("../../src/contracts/shader_abi/dispatch_info.zig");
const maps = @import("../../src/compiler/wgsl/emit/hlsl/emit_hlsl_maps.zig");

const testing = std.testing;
const allocator = testing.allocator;

const MAX_HLSL_OUTPUT = mod.MAX_HLSL_OUTPUT;
const translateToHlsl = mod.translateToHlsl;

fn contains(haystack: []const u8, needle: []const u8) bool {
    return std.mem.indexOf(u8, haystack, needle) != null;
}

// ============================================================
// HLSL type mapping tests (via IR construction)
// ============================================================

test "hlsl compute: simple shader emits numthreads and SV_DispatchThreadID" {
    const source =
        \\@group(0) @binding(0) var<storage, read_write> data: array<f32, 64>;
        \\
        \\@compute @workgroup_size(64)
        \\fn main(@builtin(global_invocation_id) id: vec3u) {
        \\    data[id.x] = data[id.x] * 2.0;
        \\}
    ;
    var out: [MAX_HLSL_OUTPUT]u8 = undefined;
    const len = try translateToHlsl(allocator, source, &out);
    const hlsl = out[0..len];

    try testing.expect(contains(hlsl, "[numthreads(64, 1, 1)]"));
    try testing.expect(contains(hlsl, "SV_DispatchThreadID"));
    try testing.expect(contains(hlsl, "void main"));
}

test "hlsl compute: num_workgroups builtin lowers via dispatch info contract" {
    const source =
        \\@group(0) @binding(0) var<storage, read_write> data: array<u32, 1>;
        \\
        \\@compute @workgroup_size(1)
        \\fn main(@builtin(num_workgroups) nwg: vec3u) {
        \\    data[0] = nwg.x;
        \\}
    ;
    var out: [MAX_HLSL_OUTPUT]u8 = undefined;
    const len = try translateToHlsl(allocator, source, &out);
    try testing.expect(len > 0);
    const hlsl = out[0..len];
    try testing.expect(contains(hlsl, dispatch_contract.DISPATCH_INFO_CBUFFER_NAME));
    try testing.expect(contains(hlsl, "register(b0, space7)"));
    try testing.expect(contains(hlsl, dispatch_contract.DISPATCH_INFO_FIELD_NAME));
    try testing.expect(contains(hlsl, "const uint3 nwg = doe_num_workgroups;"));
}

test "hlsl compute: multi-dimensional workgroup size" {
    const source =
        \\@group(0) @binding(0) var<storage, read_write> buf: array<u32, 256>;
        \\
        \\@compute @workgroup_size(8, 8, 1)
        \\fn main(@builtin(global_invocation_id) id: vec3u) {
        \\    buf[id.x + id.y * 16u] = id.x;
        \\}
    ;
    var out: [MAX_HLSL_OUTPUT]u8 = undefined;
    const len = try translateToHlsl(allocator, source, &out);
    const hlsl = out[0..len];

    try testing.expect(contains(hlsl, "[numthreads(8, 8, 1)]"));
}

test "hlsl binding: uniform buffer maps to cbuffer register(bN)" {
    const source =
        \\struct Params {
        \\    scale: f32,
        \\    offset: f32,
        \\}
        \\@group(0) @binding(0) var<uniform> params: Params;
        \\
        \\@compute @workgroup_size(1)
        \\fn main(@builtin(global_invocation_id) id: vec3u) {
        \\    let s = params.scale;
        \\}
    ;
    var out: [MAX_HLSL_OUTPUT]u8 = undefined;
    const len = try translateToHlsl(allocator, source, &out);
    const hlsl = out[0..len];

    try testing.expect(contains(hlsl, "register(b0, space0)"));
    try testing.expect(contains(hlsl, "cbuffer"));
}

test "hlsl binding: storage buffer read_write maps to RWStructuredBuffer register(uN)" {
    const source =
        \\@group(0) @binding(0) var<storage, read_write> data: array<f32, 16>;
        \\
        \\@compute @workgroup_size(1)
        \\fn main(@builtin(global_invocation_id) id: vec3u) {
        \\    data[0] = 1.0;
        \\}
    ;
    var out: [MAX_HLSL_OUTPUT]u8 = undefined;
    const len = try translateToHlsl(allocator, source, &out);
    const hlsl = out[0..len];

    try testing.expect(contains(hlsl, "RWStructuredBuffer"));
    try testing.expect(contains(hlsl, "register(u0, space0)"));
}

test "hlsl binding: storage buffer read maps to StructuredBuffer register(tN)" {
    const source =
        \\@group(0) @binding(0) var<storage, read> data: array<f32, 16>;
        \\
        \\@compute @workgroup_size(1)
        \\fn main(@builtin(global_invocation_id) id: vec3u) {
        \\    let v = data[0];
        \\}
    ;
    var out: [MAX_HLSL_OUTPUT]u8 = undefined;
    const len = try translateToHlsl(allocator, source, &out);
    const hlsl = out[0..len];

    try testing.expect(contains(hlsl, "StructuredBuffer"));
    try testing.expect(!contains(hlsl, "RWStructuredBuffer"));
    try testing.expect(contains(hlsl, "register(t0, space0)"));
}

test "hlsl binding: group 1 maps to space1" {
    const source =
        \\struct Data { value: f32 }
        \\@group(1) @binding(2) var<uniform> params: Data;
        \\
        \\@compute @workgroup_size(1)
        \\fn main(@builtin(global_invocation_id) id: vec3u) {
        \\    let v = params.value;
        \\}
    ;
    var out: [MAX_HLSL_OUTPUT]u8 = undefined;
    const len = try translateToHlsl(allocator, source, &out);
    const hlsl = out[0..len];

    try testing.expect(contains(hlsl, "register(b2, space1)"));
}

test "hlsl: atomic helper functions are emitted in preamble" {
    const source =
        \\@group(0) @binding(0) var<storage, read_write> data: array<f32, 4>;
        \\
        \\@compute @workgroup_size(1)
        \\fn main(@builtin(global_invocation_id) id: vec3u) {
        \\    data[0] = 0.0;
        \\}
    ;
    var out: [MAX_HLSL_OUTPUT]u8 = undefined;
    const len = try translateToHlsl(allocator, source, &out);
    const hlsl = out[0..len];

    try testing.expect(contains(hlsl, "doe_atomicLoad"));
    try testing.expect(contains(hlsl, "doe_atomicStore"));
    try testing.expect(contains(hlsl, "doe_atomicAdd"));
    try testing.expect(contains(hlsl, "InterlockedExchange"));
}

test "hlsl operator: assign ops map correctly" {
    try testing.expectEqualStrings("=", maps.assign_op_text(.assign));
    try testing.expectEqualStrings("+=", maps.assign_op_text(.add));
    try testing.expectEqualStrings("-=", maps.assign_op_text(.sub));
    try testing.expectEqualStrings("*=", maps.assign_op_text(.mul));
    try testing.expectEqualStrings("/=", maps.assign_op_text(.div));
    try testing.expectEqualStrings("%=", maps.assign_op_text(.rem));
}

test "hlsl operator: binary ops map correctly" {
    try testing.expectEqualStrings("+", maps.binary_op_text(.add));
    try testing.expectEqualStrings("&&", maps.binary_op_text(.logical_and));
    try testing.expectEqualStrings("||", maps.binary_op_text(.logical_or));
    try testing.expectEqualStrings("<<", maps.binary_op_text(.shift_left));
    try testing.expectEqualStrings(">>", maps.binary_op_text(.shift_right));
}

test "hlsl operator: unary ops map correctly" {
    try testing.expectEqualStrings("-", maps.unary_op_text(.neg));
    try testing.expectEqualStrings("!", maps.unary_op_text(.not));
    try testing.expectEqualStrings("~", maps.unary_op_text(.bit_not));
}

test "hlsl struct: user-defined struct appears in output" {
    const source =
        \\struct Params {
        \\    a: f32,
        \\    b: u32,
        \\    c: vec4f,
        \\}
        \\@group(0) @binding(0) var<uniform> params: Params;
        \\
        \\@compute @workgroup_size(1)
        \\fn main(@builtin(global_invocation_id) id: vec3u) {
        \\    let v = params.a;
        \\}
    ;
    var out: [MAX_HLSL_OUTPUT]u8 = undefined;
    const len = try translateToHlsl(allocator, source, &out);
    const hlsl = out[0..len];

    try testing.expect(contains(hlsl, "struct Params"));
    try testing.expect(contains(hlsl, "float a;"));
    try testing.expect(contains(hlsl, "uint b;"));
    try testing.expect(contains(hlsl, "float4 c;"));
}

test "hlsl control flow: if/else emits correctly" {
    const source =
        \\@group(0) @binding(0) var<storage, read_write> data: array<f32, 4>;
        \\
        \\@compute @workgroup_size(1)
        \\fn main(@builtin(global_invocation_id) id: vec3u) {
        \\    if (id.x == 0u) {
        \\        data[0] = 1.0;
        \\    } else {
        \\        data[0] = 2.0;
        \\    }
        \\}
    ;
    var out: [MAX_HLSL_OUTPUT]u8 = undefined;
    const len = try translateToHlsl(allocator, source, &out);
    const hlsl = out[0..len];

    try testing.expect(contains(hlsl, "if ("));
    try testing.expect(contains(hlsl, "} else {"));
}

test "hlsl control flow: for loop emits while" {
    const source =
        \\@group(0) @binding(0) var<storage, read_write> data: array<f32, 16>;
        \\
        \\@compute @workgroup_size(1)
        \\fn main(@builtin(global_invocation_id) id: vec3u) {
        \\    for (var i: u32 = 0u; i < 16u; i = i + 1u) {
        \\        data[i] = f32(i);
        \\    }
        \\}
    ;
    var out: [MAX_HLSL_OUTPUT]u8 = undefined;
    const len = try translateToHlsl(allocator, source, &out);
    const hlsl = out[0..len];

    try testing.expect(contains(hlsl, "while ("));
}

test "hlsl workgroup: var<workgroup> emits groupshared" {
    const source =
        \\var<workgroup> shared_data: array<f32, 256>;
        \\
        \\@compute @workgroup_size(256)
        \\fn main(@builtin(local_invocation_index) idx: u32) {
        \\    shared_data[idx] = f32(idx);
        \\}
    ;
    var out: [MAX_HLSL_OUTPUT]u8 = undefined;
    const len = try translateToHlsl(allocator, source, &out);
    const hlsl = out[0..len];

    try testing.expect(contains(hlsl, "groupshared"));
}

test "hlsl control flow: consecutive for initializers retain independent scopes" {
    const source =
        \\@group(0) @binding(0) var<storage, read_write> output: array<u32, 1>;
        \\@compute @workgroup_size(1) fn main() {
        \\  for (var inner_row = 0u; inner_row < 2u; inner_row++) { output[0] += inner_row; }
        \\  for (var inner_row = 0u; inner_row < 3u; inner_row++) { output[0] += inner_row; }
        \\}
    ;
    var out: [MAX_HLSL_OUTPUT]u8 = undefined;
    const len = try translateToHlsl(allocator, source, &out);
    const hlsl = out[0..len];
    try testing.expect(contains(hlsl, "{\n        uint __doe_local_0 = 0"));
    try testing.expect(contains(hlsl, "{\n        uint __doe_local_1 = 0"));
    try testing.expect(!contains(hlsl, "\n    uint __doe_local_"));
}

test "hlsl control flow: continue executes for increment without leaking into nested while" {
    const source =
        \\@group(0) @binding(0) var<storage, read_write> output: array<u32, 1>;
        \\@compute @workgroup_size(1) fn main() {
        \\  var total = 0u;
        \\  for (var i = 0u; i < 4u; i++) {
        \\    var j = 0u;
        \\    while (j < 2u) { j++; if (j == 1u) { continue; } total += 10u; }
        \\    if (i == 1u) { i++; continue; }
        \\    total += i;
        \\  }
        \\  output[0] = total;
        \\}
    ;
    var out: [MAX_HLSL_OUTPUT]u8 = undefined;
    const len = try translateToHlsl(allocator, source, &out);
    // One explicit increment, one continuing before outer continue, one at the tail.
    // The nested while must not emit the outer increment on its continue edge.
    try testing.expectEqual(@as(usize, 3), std.mem.count(u8, out[0..len], "i += 1;"));
}

test "hlsl control flow: explicit local shadow block survives emission" {
    const source =
        \\@group(0) @binding(0) var<storage, read_write> output: array<u32, 8>;
        \\@compute @workgroup_size(8) fn main(@builtin(global_invocation_id) gid: vec3u) {
        \\  var value = gid.x + 1u;
        \\  var total = 0u;
        \\  { var value = gid.x + 10u; total = value; }
        \\  total += value;
        \\  output[gid.x] = total;
        \\}
    ;
    var out: [MAX_HLSL_OUTPUT]u8 = undefined;
    const len = try translateToHlsl(allocator, source, &out);
    try testing.expect(contains(out[0..len], "{\n        uint __doe_local_2 ="));
    try testing.expectEqual(@as(usize, 1), std.mem.count(u8, out[0..len], "\n    uint __doe_local_0 ="));
}

test "hlsl control flow: continuing keeps local identity through shadowing" {
    const source =
        \\@group(0) @binding(0) var<storage, read_write> output: array<u32, 8>;
        \\@compute @workgroup_size(8) fn main(@builtin(global_invocation_id) gid: vec3u) {
        \\  var trips = 0u;
        \\  for (var i = 0u; i < 4u; i++) {
        \\    trips++;
        \\    if (trips >= 6u) { break; }
        \\    { var i = 100u; continue; }
        \\  }
        \\  output[gid.x] = trips;
        \\}
    ;
    var out: [MAX_HLSL_OUTPUT]u8 = undefined;
    const len = try translateToHlsl(allocator, source, &out);
    try testing.expectEqual(@as(usize, 2), std.mem.count(u8, out[0..len], "__doe_local_1 += 1;"));
    try testing.expect(!contains(out[0..len], "__doe_local_2 += 1;"));
}

test "hlsl control flow: generated loop flag cannot shadow a source local" {
    const source =
        \\@group(0) @binding(0) var<storage, read_write> output: array<u32, 8>;
        \\@compute @workgroup_size(8) fn main(@builtin(global_invocation_id) gid: vec3u) {
        \\  var doe_loop_break_5 = 0u;
        \\  for (var i = 0u; i < 3u; i++) { doe_loop_break_5 += 1u; }
        \\  output[gid.x] = doe_loop_break_5;
        \\}
    ;
    var out: [MAX_HLSL_OUTPUT]u8 = undefined;
    const len = try translateToHlsl(allocator, source, &out);
    const hlsl = out[0..len];
    try testing.expect(contains(hlsl, "uint __doe_hlsl_user_646f655f6c6f6f705f627265616b5f35 = 0;"));
    try testing.expect(contains(hlsl, "bool __doe_loop_break_"));
    try testing.expect(!contains(hlsl, "bool doe_loop_break_"));
}

test "hlsl indexed workgroup RMW preserves shared identity and captured index" {
    const source =
        \\var<workgroup> signed_values: array<atomic<i32>, 8>;
        \\var<workgroup> unsigned_values: array<atomic<u32>, 8>;
        \\@compute @workgroup_size(8)
        \\fn main(@builtin(local_invocation_index) tid: u32) {
        \\    var index = tid;
        \\    let pointer = &signed_values[index];
        \\    index = 0u;
        \\    let a = atomicAdd(pointer, -3);
        \\    let b = atomicSub(pointer, 2);
        \\    let c = atomicMin(pointer, -7);
        \\    let d = atomicMax(pointer, 5);
        \\    let e = atomicAnd(&unsigned_values[tid], 15u);
        \\    let f = atomicOr(&unsigned_values[tid], 16u);
        \\    let g = atomicXor(&unsigned_values[tid], 3u);
        \\    let h = atomicExchange(&unsigned_values[tid], 9u);
        \\}
    ;
    var out: [MAX_HLSL_OUTPUT]u8 = undefined;
    const len = try translateToHlsl(allocator, source, &out);
    const hlsl = out[0..len];
    try testing.expect(contains(hlsl, "InterlockedAdd(signed_values[index], next, result)"));
    try testing.expect(contains(hlsl, "InterlockedAdd(signed_values[index], 0 - next, result)"));
    try testing.expect(contains(hlsl, "InterlockedMin(signed_values[index], next, result)"));
    try testing.expect(contains(hlsl, "InterlockedMax(signed_values[index], next, result)"));
    try testing.expect(contains(hlsl, "InterlockedAnd(unsigned_values[index], next, result)"));
    try testing.expect(contains(hlsl, "InterlockedOr(unsigned_values[index], next, result)"));
    try testing.expect(contains(hlsl, "InterlockedXor(unsigned_values[index], next, result)"));
    try testing.expect(contains(hlsl, "InterlockedExchange(unsigned_values[index], next, result)"));
    try testing.expect(contains(hlsl, "int __doe_atomicAdd_signed_values(uint index, int next)"));
    try testing.expect(contains(hlsl, "uint __doe_atomicExchange_unsigned_values(uint index, uint next)"));
    try testing.expect(contains(hlsl, "__doe_atomicAdd_signed_values(doe_pointer_index_"));
    try testing.expect(!contains(hlsl, "doe_atomicAdd(signed_values["));
}

test "hlsl indexed compare exchange preserves semantic result types and pointer indices" {
    const source =
        \\var<workgroup> signed_values: array<atomic<i32>, 8>;
        \\var<workgroup> unsigned_values: array<atomic<u32>, 8>;
        \\@group(0) @binding(0) var<storage, read_write> output: array<u32, 8>;
        \\@compute @workgroup_size(8)
        \\fn main(@builtin(local_invocation_index) tid: u32) {
        \\    var index = tid;
        \\    let pointer = &signed_values[index];
        \\    index = 0u;
        \\    let a = atomicCompareExchangeWeak(pointer, -3, -7);
        \\    let b = atomicCompareExchangeWeak(&unsigned_values[tid], 3u, 7u);
        \\    output[tid] = select(bitcast<u32>(a.old_value), b.old_value, a.exchanged && b.exchanged);
        \\}
    ;
    var out: [MAX_HLSL_OUTPUT]u8 = undefined;
    const len = try translateToHlsl(allocator, source, &out);
    const hlsl = out[0..len];
    try testing.expect(contains(hlsl, "__doe_atomic_compare_exchange_result_i32 __doe_atomicCompareExchangeWeak_signed_values(uint index, int expected, int desired)"));
    try testing.expect(contains(hlsl, "__doe_atomic_compare_exchange_result_u32 __doe_atomicCompareExchangeWeak_unsigned_values(uint index, uint expected, uint desired)"));
    try testing.expect(contains(hlsl, "InterlockedCompareExchange(signed_values[index], expected, desired, result.old_value)"));
    try testing.expect(contains(hlsl, "result.exchanged = result.old_value == expected"));
    try testing.expect(contains(hlsl, "__doe_atomicCompareExchangeWeak_signed_values(doe_pointer_index_"));
    try testing.expect(contains(hlsl, "a.old_value"));
    try testing.expect(contains(hlsl, "a.exchanged"));
}

test "hlsl reserved names preserve declarations references and user calls" {
    const source =
        \\struct matrix { out: f32, }
        \\@group(0) @binding(0) var<storage, read> vector: array<matrix>;
        \\@group(0) @binding(1) var<storage, read_write> out: array<f32>;
        \\fn point(in: f32) -> f32 { let sample = in; return sample; }
        \\@compute @workgroup_size(1) fn main() {
        \\    out[0] = point(vector[0].out);
        \\}
    ;
    var out: [MAX_HLSL_OUTPUT]u8 = undefined;
    const len = try translateToHlsl(allocator, source, &out);
    const hlsl = out[0..len];
    try testing.expect(contains(hlsl, "struct __doe_hlsl_user_6d6174726978"));
    try testing.expect(contains(hlsl, "float __doe_hlsl_user_6f7574;"));
    try testing.expect(contains(hlsl, "StructuredBuffer<__doe_hlsl_user_6d6174726978> __doe_hlsl_user_766563746f72"));
    try testing.expect(contains(hlsl, "RWStructuredBuffer<float> __doe_hlsl_user_6f7574"));
    try testing.expect(contains(hlsl, "float __doe_hlsl_user_706f696e74(float __doe_hlsl_user_696e)"));
    try testing.expect(contains(hlsl, "const float __doe_hlsl_user_73616d706c65 = __doe_hlsl_user_696e"));
    try testing.expect(contains(hlsl, " = __doe_hlsl_user_706f696e74(__doe_hlsl_user_766563746f72["));
    try testing.expect(contains(hlsl, "].__doe_hlsl_user_6f7574)"));
}

test "hlsl reserved resource metadata helpers match declarations and calls" {
    const source =
        \\@group(0) @binding(0) var matrix: texture_2d<f32>;
        \\@group(0) @binding(1) var<storage, read_write> out: array<u32>;
        \\@compute @workgroup_size(1) fn main() {
        \\    out[0] = textureDimensions(matrix, 0).x + arrayLength(&out);
        \\}
    ;
    var out: [MAX_HLSL_OUTPUT]u8 = undefined;
    const len = try translateToHlsl(allocator, source, &out);
    const hlsl = out[0..len];
    try testing.expect(contains(hlsl, "Texture2D<float4> __doe_hlsl_user_6d6174726978"));
    try testing.expect(contains(hlsl, "doe_textureDimensions___doe_hlsl_user_6d6174726978(uint level)"));
    try testing.expect(contains(hlsl, "__doe_hlsl_user_6d6174726978.GetDimensions"));
    try testing.expect(contains(hlsl, "doe_textureDimensions___doe_hlsl_user_6d6174726978(uint(0))"));
    try testing.expect(contains(hlsl, "doe_arrayLength___doe_hlsl_user_6f7574()"));
}

test "hlsl reserved entry maps the selected DXC name without changing source identity" {
    const source = "@compute @workgroup_size(1) fn out() {}";
    var out: [MAX_HLSL_OUTPUT]u8 = undefined;
    const len = try translateToHlsl(allocator, source, &out);
    try testing.expect(contains(out[0..len], "void __doe_hlsl_user_6f7574()"));
    const target = try mod.emit_hlsl.entryPointName(allocator, "out");
    defer allocator.free(target);
    try testing.expectEqualStrings("__doe_hlsl_user_6f7574", target);
}

test "hlsl generated name collision: helper-name-collision" {
    const source =
        \\@group(0) @binding(0) var<storage,read_write> out: array<u32>;
        \\fn doe_arrayLength___doe_hlsl_user_6f7574() -> u32 { return 3u; }
        \\@compute @workgroup_size(1) fn main() { out[0] = doe_arrayLength___doe_hlsl_user_6f7574(); }
    ;
    var out: [MAX_HLSL_OUTPUT]u8 = undefined;
    const len = try translateToHlsl(allocator, source, &out);
    try testing.expect(contains(out[0..len], "__doe_hlsl_user_646f655f61727261794c656e6774685f5f5f646f655f686c736c5f757365725f366637353734"));
}

test "hlsl generated name collision: cbuffer-name-collision" {
    const source =
        \\struct Params { value: vec4f, }
        \\@group(0) @binding(0) var<uniform> params: Params;
        \\@group(0) @binding(1) var<storage,read_write> output: array<f32,1>;
        \\fn doe_uniform_params() -> f32 { return params.value.x; }
        \\@compute @workgroup_size(1) fn main() { output[0] = doe_uniform_params(); }
    ;
    var out: [MAX_HLSL_OUTPUT]u8 = undefined;
    const len = try translateToHlsl(allocator, source, &out);
    try testing.expect(contains(out[0..len], "float __doe_hlsl_user_646f655f756e69666f726d5f706172616d73()"));
}

test "hlsl generated name collision: stage-name-collision" {
    const source =
        \\struct main_stage_out { value: f32, }
        \\fn main_impl(value: f32) -> f32 { return value + 1.0; }
        \\@vertex fn main(@builtin(vertex_index) out: u32) -> @builtin(position) vec4f { var item: main_stage_out; item.value = main_impl(f32(out)); return vec4f(item.value, 0.0, 0.0, 1.0); }
    ;
    var out: [MAX_HLSL_OUTPUT]u8 = undefined;
    const len = try translateToHlsl(allocator, source, &out);
    try testing.expect(contains(out[0..len], "struct __doe_hlsl_user_6d61696e5f73746167655f6f7574"));
}

test "hlsl generated name collision: helper-local-shadowing" {
    const source =
        \\@group(0) @binding(0) var<storage,read_write> count: array<u32>;
        \\var<workgroup> result: array<atomic<u32>,4>;
        \\@compute @workgroup_size(1) fn main() { let previous = atomicAdd(&result[0], count[0]); count[0] = previous + arrayLength(&count); }
    ;
    var out: [MAX_HLSL_OUTPUT]u8 = undefined;
    const len = try translateToHlsl(allocator, source, &out);
    try testing.expect(contains(out[0..len], "__doe_hlsl_user_636f756e74.GetDimensions"));
}

test "hlsl generated name collision: texture-name-collision" {
    const source =
        \\@group(0) @binding(0) var matrix: texture_2d<f32>;
        \\@group(0) @binding(1) var<storage,read_write> out: array<u32,1>;
        \\fn doe_textureDimensions___doe_hlsl_user_6d6174726978(level: u32) -> vec2u { return vec2u(level); }
        \\@compute @workgroup_size(1) fn main() { out[0] = textureDimensions(matrix,0).x + doe_textureDimensions___doe_hlsl_user_6d6174726978(2).x; }
    ;
    var out: [MAX_HLSL_OUTPUT]u8 = undefined;
    const len = try translateToHlsl(allocator, source, &out);
    try testing.expect(contains(out[0..len], "uint2 __doe_hlsl_user_646f655f7465787475726544696d656e73696f6e735f5f5f646f655f686c736c5f757365725f366436313734373236393738"));
}
