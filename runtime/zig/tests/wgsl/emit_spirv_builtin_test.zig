// emit_spirv_builtin_test.zig — SPIR-V builtin and texture emission tests.

const std = @import("std");
const spirv = @import("../../src/compiler/wgsl/emit/spirv/spirv_builder.zig");
const mod = @import("../../src/compiler/wgsl/mod.zig");
const parser = @import("../../src/compiler/wgsl/frontend/parser.zig");
const sema = @import("../../src/compiler/wgsl/frontend/sema.zig");
const ir_builder = @import("../../src/compiler/wgsl/ir/ir_builder.zig");
const ir_validate = @import("../../src/compiler/wgsl/ir/ir_validate.zig");
const loop_independence = @import("../../src/compiler/wgsl/ir/ir_loop_independence.zig");

const testing = std.testing;
const allocator = testing.allocator;

const MAX_SPIRV_OUTPUT = mod.MAX_SPIRV_OUTPUT;
const translateToSpirv = mod.translateToSpirv;

test "SPIR-V GLSL mappings preserve runtime ldexp types and single evaluation" {
    var binary: [MAX_SPIRV_OUTPUT]u8 = undefined;
    const len = try translateToSpirv(allocator, @embedFile("glsl_instruction_runtime.wgsl"), &binary);
    var offset: usize = 5;
    var ldexp_count: usize = 0;
    var calls: usize = 0;
    var callees: [3]u32 = undefined;
    var clamp_count: usize = 0;
    while (offset < len / 4) {
        const instruction = read_u32_le(&binary, offset * 4);
        const words = instruction >> 16;
        try testing.expect(words > 0);
        const opcode: u16 = @truncate(instruction);
        if (opcode == spirv.Opcode.FunctionCall) {
            try testing.expect(calls < callees.len);
            const callee = read_u32_le(&binary, (offset + 3) * 4);
            for (callees[0..calls]) |previous| try testing.expect(previous != callee);
            callees[calls] = callee;
            calls += 1;
        }
        if (opcode == spirv.Opcode.ExtInst) {
            const extended = read_u32_le(&binary, (offset + 4) * 4);
            if (extended == 53) ldexp_count += 1;
            if (extended == 43) clamp_count += 1;
            try testing.expect(extended != 71 and extended != 44);
        }
        offset += words;
    }
    try testing.expectEqual(@as(usize, 8), ldexp_count);
    // Two side-effecting arguments plus the generated entry-point wrapper.
    try testing.expectEqual(@as(usize, 3), calls);
    try testing.expectEqual(@as(usize, 1), clamp_count);
}

test "SPIR-V ldexp rejects invalid runtime signatures" {
    const prefix = "@group(0) @binding(0) var<storage, read> x: vec4<f32>;" ++
        "@group(0) @binding(1) var<storage, read> e: vec4<i32>;" ++
        "@compute @workgroup_size(1) fn main() { let result = ";
    inline for (.{ "ldexp(x.x, 2u)", "ldexp(x.x, x.y)", "ldexp(e.x, e.y)", "ldexp(x, e.xy)", "ldexp(x, e.x)", "ldexp(x.x, e)", "ldexp(true, e.x)" }) |expression| {
        var binary: [MAX_SPIRV_OUTPUT]u8 = undefined;
        try testing.expectError(error.TypeMismatch, translateToSpirv(allocator, prefix ++ expression ++ "; }", &binary));
    }
    inline for (.{ "ldexp()", "ldexp(x.x)", "ldexp(x.x, e.x, e.y)" }) |expression| {
        var binary: [MAX_SPIRV_OUTPUT]u8 = undefined;
        try testing.expectError(error.UnsupportedBuiltin, translateToSpirv(allocator, prefix ++ expression ++ "; }", &binary));
    }
}

test "SPIR-V ldexp and saturate retain floating widths and shapes" {
    inline for (.{ "f16", "f32" }) |element| {
        inline for (.{ "", "2", "3", "4" }) |lanes| {
            const value = if (lanes.len == 0) element else "vec" ++ lanes ++ "<" ++ element ++ ">";
            const exponent = if (lanes.len == 0) "i32" else "vec" ++ lanes ++ "<i32>";
            const source = "enable f16; @group(0) @binding(0) var<storage, read> x: " ++ value ++ ";" ++
                "@group(0) @binding(1) var<storage, read> e: " ++ exponent ++ ";" ++
                "@group(0) @binding(2) var<storage, read_write> y: " ++ value ++ ";" ++
                "@compute @workgroup_size(1) fn main() { y = saturate(ldexp(x, e)); }";
            var binary: [MAX_SPIRV_OUTPUT]u8 = undefined;
            try testing.expect((try translateToSpirv(allocator, source, &binary)) > 20);
        }
    }
}

test "SPIR-V ldexp materializes abstract integer values as floats" {
    inline for (.{ "ldexp(1, 3)", "ldexp(1, e)", "ldexp(1.5, e)" }) |expression| {
        const source = "@group(0) @binding(0) var<storage, read> e: i32;" ++
            "@group(0) @binding(1) var<storage, read_write> y: f32;" ++
            "@compute @workgroup_size(1) fn main() { let x = " ++ expression ++ "; y = x; }";
        var binary: [MAX_SPIRV_OUTPUT]u8 = undefined;
        try testing.expect((try translateToSpirv(allocator, source, &binary)) > 20);
    }
}

test "abstract inference: local const unpack and inferred vector ldexp reach SPIR-V" {
    const source =
        \\requires packed_4x8_integer_dot_product;
        \\@group(0) @binding(0) var<storage, read_write> output: array<u32>;
        \\@compute @workgroup_size(1) fn main() {
        \\    const word = 7;
        \\    output[0u] = unpack4xU8(word + 1).x;
        \\    output[1u] = u32(ldexp(vec2(1,2), vec2(3,4)).y);
        \\}
    ;
    var binary: [MAX_SPIRV_OUTPUT]u8 = undefined;
    try testing.expect((try translateToSpirv(allocator, source, &binary)) > 20);
}

fn abstractInferenceTranslation(test_allocator: std.mem.Allocator) !void {
    const source =
        \\requires packed_4x8_integer_dot_product;
        \\const word = 99;
        \\@group(0) @binding(0) var<storage, read_write> output: array<u32>;
        \\@compute @workgroup_size(1) fn main() {
        \\    const word = 7;
        \\    { const word = 3; output[0u] = unpack4xU8(word + 1).x; }
        \\    output[1u] = unpack4xU8(word + 1).x;
        \\    let pair = vec2(1,2);
        \\    output[2u] = u32(pair.x) + u32(ldexp(vec2(1,2),vec2(3,4)).y);
        \\}
    ;
    var tree = try parser.parseSource(test_allocator, source);
    defer tree.deinit();
    var analyzed = try sema.analyze(test_allocator, &tree);
    defer analyzed.deinit();
    // Each use must retain its own resolved lexical const, not the global or
    // the nearest same-spelled declaration in a later traversal.
    var observed: usize = 0;
    for (tree.nodes.items, 0..) |node, index| {
        if (node.tag != .ident_expr or !std.mem.eql(u8, tree.tokenSlice(node.main_token), "word")) continue;
        const initializer = analyzed.node_info.items[index].const_initializer;
        try testing.expect(initializer < tree.nodes.items.len);
        const expected = if (observed == 0) "3" else "7";
        try testing.expectEqualStrings(expected, tree.tokenSlice(tree.nodes.items[initializer].main_token));
        observed += 1;
    }
    try testing.expectEqual(@as(usize, 2), observed);
    var binary: [MAX_SPIRV_OUTPUT]u8 = undefined;
    try testing.expect((try translateToSpirv(test_allocator, source, &binary)) > 20);
}

test "abstract inference: lexical const materialization survives every allocation failure" {
    try testing.checkAllAllocationFailures(allocator, abstractInferenceTranslation, .{});
}

test "abstract inference: vector IR compatibility still rejects concrete sign and shape mismatch" {
    var tree = try parser.parseSource(allocator, "@compute @workgroup_size(1) fn main() { let pair = vec2i(1,2); }");
    defer tree.deinit();
    var analyzed = try sema.analyze(allocator, &tree);
    defer analyzed.deinit();
    var module = try ir_builder.build(allocator, &tree, &analyzed);
    defer module.deinit();
    try ir_validate.validate(&module);
    const original = module.functions.items[0].locals.items[0].ty;
    const element = module.types.get(original).vector.elem;
    module.functions.items[0].locals.items[0].ty = try module.types.intern(.{ .vector = .{ .elem = element, .len = 3 } });
    try testing.expectError(error.InvalidIr, ir_validate.validate(&module));
    const unsigned = try module.types.intern(.{ .scalar = .u32 });
    module.functions.items[0].locals.items[0].ty = try module.types.intern(.{ .vector = .{ .elem = unsigned, .len = 2 } });
    try testing.expectError(error.InvalidIr, ir_validate.validate(&module));
}

fn contextualVectorTranslation(test_allocator: std.mem.Allocator) !void {
    const source =
        \\@group(0) @binding(0) var<storage,read_write> output: array<vec2u>;
        \\fn take(x:vec2u)->vec2u { return x; }
        \\fn give()->vec2u { return vec2(1,2); }
        \\fn signed()->vec2i { return vec2(3,4); }
        \\@compute @workgroup_size(1) fn main(){
        \\  let typed:vec2i=vec2(1,2);
        \\  let unsigned:vec2u=vec2(3,4);
        \\  output[0u]=take(vec2(5,6)); output[1u]=give();
        \\  output[2u]=bitcast<vec2u>(vec2(1,2));
        \\  output[3u]=bitcast<vec2u>(vec2(1.0,2.0));
        \\  output[4u]=vec2u(typed+signed())+unsigned;
        \\}
    ;
    var tree = try parser.parseSource(test_allocator, source);
    defer tree.deinit();
    var analyzed = try sema.analyze(test_allocator, &tree);
    defer analyzed.deinit();
    var module = try ir_builder.build(test_allocator, &tree, &analyzed);
    defer module.deinit();
    try ir_validate.validate(&module);
    // Typed user-call/return boundaries cannot rely on target-specific implicit
    // coercion: the shared IR carries an ordinary constructor explicitly.
    var calls: usize = 0;
    var returns: usize = 0;
    for (module.functions.items) |function| {
        for (function.exprs.items) |expression| {
            if (expression.data != .call or expression.data.call.kind != .user or
                !std.mem.eql(u8, expression.data.call.name, "take")) continue;
            const argument = function.exprs.items[function.expr_args.items[expression.data.call.args.start]];
            try testing.expect(argument.data == .construct);
            try testing.expectEqual(analyzed.u32_type, module.types.get(argument.ty).vector.elem);
            calls += 1;
        }
        if (!std.mem.eql(u8, function.name, "give")) continue;
        for (function.stmts.items) |statement| {
            if (statement != .return_ or statement.return_ == null) continue;
            const value = function.exprs.items[statement.return_.?];
            try testing.expect(value.data == .construct);
            try testing.expectEqual(function.return_type, value.ty);
            returns += 1;
        }
    }
    try testing.expectEqual(@as(usize, 1), calls);
    try testing.expectEqual(@as(usize, 1), returns);
    var binary: [MAX_SPIRV_OUTPUT]u8 = undefined;
    try testing.expect((try translateToSpirv(test_allocator, source, &binary)) > 20);
}

test "abstract inference: vector contexts and bitcasts survive every allocation failure" {
    try testing.checkAllAllocationFailures(allocator, contextualVectorTranslation, .{});
}

test "SPIR-V integer dot keeps signed and unsigned products and reductions in integer arithmetic" {
    inline for (.{ "i32", "u32" }) |element| {
        inline for (.{ "2", "3", "4" }) |lanes| {
            const source = "@group(0) @binding(0) var<storage, read> input: array<vec" ++ lanes ++ "<" ++ element ++ ">>;" ++
                "@group(0) @binding(1) var<storage, read_write> output: array<" ++ element ++ ">;" ++
                "@compute @workgroup_size(1) fn main() { output[0] = dot(input[0], input[1]); }";
            var binary: [MAX_SPIRV_OUTPUT]u8 = undefined;
            const len = try translateToSpirv(allocator, source, &binary);
            var offset: usize = 5;
            var multiplies: usize = 0;
            var adds: usize = 0;
            while (offset < len / 4) {
                const instruction = read_u32_le(&binary, offset * 4);
                const words = instruction >> 16;
                try testing.expect(words > 0);
                const opcode: u16 = @truncate(instruction);
                if (opcode == spirv.Opcode.IMul) multiplies += 1;
                if (opcode == spirv.Opcode.IAdd) adds += 1;
                try testing.expect(opcode != spirv.Opcode.Dot);
                try testing.expect(opcode != spirv.Opcode.ConvertSToF and opcode != spirv.Opcode.ConvertUToF);
                offset += words;
            }
            try testing.expectEqual(@as(usize, 1), multiplies);
            try testing.expectEqual(@as(usize, lanes[0] - '0' - 1), adds);
        }
    }
}

test "named and direct pointers preserve mutation through calls and branches" {
    const prefix =
        \\@group(0) @binding(0) var<storage, read_write> output: array<u32>;
        \\fn change(p: ptr<function, u32>) { *p += 5u; }
    ;
    const suffix =
        \\  output[0] = x;
        \\  if (x > 0u) { change(&x); } else { x = 99u; }
        \\  output[1] = x;
        \\}
    ;
    const cases = .{
        prefix ++ "@compute @workgroup_size(1) fn main() { var x = 1u; change(&x);" ++ suffix,
        prefix ++ "@compute @workgroup_size(1) fn main() { var x = 1u; let pointerAlias = &x; change(pointerAlias);" ++ suffix,
    };
    inline for (cases) |source| {
        var module = try mod.analyzeToIr(allocator, source);
        defer module.deinit();
        try ir_validate.validate(&module);
        var binary: [MAX_SPIRV_OUTPUT]u8 = undefined;
        try testing.expect((try translateToSpirv(allocator, source, &binary)) > 20);
    }
}

test "pointer aliases preserve member and index references" {
    const source =
        \\struct Pair { a: u32, b: u32 }
        \\@group(0) @binding(0) var<storage, read_write> output: array<u32>;
        \\@compute @workgroup_size(1) fn main() {
        \\  var pair = Pair(3u, 4u);
        \\  let pair_ptr = &pair;
        \\  (*pair_ptr).b += 2u;
        \\  var values = array<u32, 2>(5u, 6u);
        \\  let values_ptr = &values;
        \\  (*values_ptr)[1u] += 3u;
        \\  output[0] = pair.b;
        \\  output[1] = values[1u];
        \\}
    ;
    var binary: [MAX_SPIRV_OUTPUT]u8 = undefined;
    try testing.expect((try translateToSpirv(allocator, source, &binary)) > 20);
}

test "pointer parameters reject scalar arguments" {
    const source =
        \\fn change(p: ptr<function, u32>) { *p += 5u; }
        \\@compute @workgroup_size(1) fn main() { var x = 1u; change(x); }
    ;
    var binary: [MAX_SPIRV_OUTPUT]u8 = undefined;
    try testing.expectError(error.TypeMismatch, translateToSpirv(allocator, source, &binary));
}

const DOT_LOOPS =
    \\@group(0) @binding(0) var<storage, read> data: array<vec4<f32>>;
    \\@group(0) @binding(1) var<storage, read_write> output: array<f32>;
    \\fn reduce() -> f32 {
    \\    var total = 0.0;
    \\    for (var outer = 0u; outer < 2u; outer++) {
    \\        for (var i = 0u; i < 16u; i++) {
    \\            total = total + dot(data[i], data[i+1u]) + dot(data[i+2u], data[i+3u]);
    \\        }
    \\    }
    \\    for (var i = 0u; i < 16u; i++) { total += dot(data[i], data[i+1u]); }
    \\    return total;
    \\}
;

test "spirv compute policy preserves innermost multiple-dot loops without changing other loops or stages" {
    const cases = .{
        .{ DOT_LOOPS ++ "@compute @workgroup_size(1) fn main() { output[0] = reduce(); }", @as(u32, 1) },
        .{ DOT_LOOPS ++ "@fragment fn main() -> @location(0) vec4<f32> { return vec4<f32>(reduce()); }", @as(u32, 0) },
        .{ DOT_LOOPS ++ "@compute @workgroup_size(1) fn main() { output[0] = reduce(); } @fragment fn fragment() -> @location(0) vec4<f32> { return vec4<f32>(reduce()); }", @as(u32, 0) },
    };
    inline for (cases) |case| {
        var binary: [MAX_SPIRV_OUTPUT]u8 = undefined;
        const len = try translateToSpirv(allocator, case[0], &binary);
        var offset: usize = 5;
        var preserved: u32 = 0;
        var loops: u32 = 0;
        while (offset < len / 4) {
            const instruction = read_u32_le(&binary, offset * 4);
            const word_count = instruction >> 16;
            try testing.expect(word_count > 0);
            if (@as(u16, @truncate(instruction)) == spirv.Opcode.LoopMerge) {
                loops += 1;
                if (read_u32_le(&binary, (offset + 3) * 4) == spirv.LoopControl.DontUnroll) preserved += 1;
            }
            offset += word_count;
        }
        try testing.expectEqual(@as(u32, 3), loops);
        try testing.expectEqual(if (@import("build_options").spirv_compute_preserve_multi_dot_loops) case[1] else @as(u32, 0), preserved);
    }
}

fn read_u32_le(bytes: []const u8, offset: usize) u32 {
    return std.mem.readInt(u32, @as(*const [4]u8, @ptrCast(bytes[offset .. offset + 4].ptr)), .little);
}

test "spirv unrolls independent dot outputs but preserves recurrence alias and side effect cases" {
    const prefix =
        \\override COUNT: u32 = 4u;
        \\@group(0) @binding(0) var<storage, read> data: array<vec4<f32>>;
        \\@group(0) @binding(1) var<storage, read_write> output: array<f32>;
        \\fn read_vec(i: u32) -> vec4<f32> { let index = i + 1u; return data[index]; }
        \\fn mutating_vec(i: u32) -> vec4<f32> { output[0] = 1.0; return data[i]; }
        \\@compute @workgroup_size(1) fn main() {
        \\  var results: array<f32, 8>;
        \\  var total = 0.0;
    ;
    const cases = .{
        .{ "for (var i = 0u; i < COUNT; i++) { let a = read_vec(i); results[i] = results[i] + dot(a, a) + dot(data[i], a); }", true },
        .{ "for (var i = 0u; i < 9u; i++) { results[i] += dot(data[i], data[i]) + dot(data[i+1u], data[i]); }", false },
        .{ "for (var i = 0u; i < 4u; i++) { results[0] += dot(data[i], data[i]) + dot(data[i+1u], data[i]); }", false },
        .{ "for (var i = 0u; i < 4u; i++) { results[i] = results[i+1u] + dot(data[i], data[i]) + dot(data[i+1u], data[i]); }", false },
        .{ "for (var i = 0u; i < 4u; i++) { let a = mutating_vec(i); results[i] += dot(a, a) + dot(data[i], a); }", false },
        .{ "for (var i = 0u; i < 4u; i++) { total += 1.0; results[i] += dot(data[i], data[i]) + dot(data[i+1u], data[i]); }", false },
        .{ "for (var i = 0u; i < 4u; i += 2u) { results[i] += dot(data[i], data[i]) + dot(data[i+1u], data[i]); }", false },
        .{ "for (var i = 0u; i < 4u; i++) { results[u32(f32(i))] += dot(data[i], data[i]) + dot(data[i+1u], data[i]); }", false },
        .{ "for (var i = 0u; i < 4u; i++) { let previous = results; results[i] = previous[i+1u] + dot(data[i], data[i]) + dot(data[i+1u], data[i]); }", false },
    };
    inline for (cases) |case| {
        var binary: [MAX_SPIRV_OUTPUT]u8 = undefined;
        const source = prefix ++ case[0] ++ "output[0] = results[0] + total; }";
        var analyzed = try mod.analyzeToIr(allocator, source);
        defer analyzed.deinit();
        var analyzed_loops: usize = 0;
        for (analyzed.functions.items) |*function| {
            for (function.stmts.items) |statement| {
                if (statement != .loop_) continue;
                analyzed_loops += 1;
                try testing.expectEqual(case[1], loop_independence.independentIndexedLoop(&analyzed, function, statement.loop_));
            }
        }
        try testing.expectEqual(@as(usize, 1), analyzed_loops);
        const len = try translateToSpirv(allocator, source, &binary);
        var offset: usize = 5;
        var unrolled: u32 = 0;
        var preserved: u32 = 0;
        while (offset < len / 4) {
            const instruction = read_u32_le(&binary, offset * 4);
            if (@as(u16, @truncate(instruction)) == spirv.Opcode.LoopMerge and
                read_u32_le(&binary, (offset + 3) * 4) == spirv.LoopControl.Unroll) unrolled += 1;
            if (@as(u16, @truncate(instruction)) == spirv.Opcode.LoopMerge and
                read_u32_le(&binary, (offset + 3) * 4) == spirv.LoopControl.DontUnroll) preserved += 1;
            offset += instruction >> 16;
        }
        const enabled = @import("build_options").spirv_compute_unroll_independent_dot_loops;
        try testing.expectEqual(@as(u32, @intFromBool(enabled and case[1])), unrolled);
        const preserve = @import("build_options").spirv_compute_preserve_multi_dot_loops;
        try testing.expectEqual(@as(u32, @intFromBool(preserve and !(enabled and case[1]))), preserved);
    }
}

test "spirv independence analysis rejects pointer aliases before emission" {
    const source =
        \\@compute @workgroup_size(1) fn main() {
        \\  var results: array<f32, 8>;
        \\  let previous = &results;
        \\  for (var i = 0u; i < 4u; i++) {
        \\    results[i] = (*previous)[i + 1u] + 1.0;
        \\  }
        \\}
    ;
    var tree = try parser.parseSource(allocator, source);
    defer tree.deinit();
    var semantic = try sema.analyze(allocator, &tree);
    defer semantic.deinit();
    var module = try ir_builder.build(allocator, &tree, &semantic);
    defer module.deinit();
    var examined: usize = 0;
    for (module.functions.items) |*function| {
        for (function.stmts.items) |statement| {
            if (statement != .loop_) continue;
            examined += 1;
            try testing.expect(!loop_independence.independentIndexedLoop(&module, function, statement.loop_));
        }
    }
    try testing.expectEqual(@as(usize, 1), examined);
}

fn count_spirv_opcode(binary: []const u8, opcode: u16) u32 {
    const word_count = binary.len / 4;
    var i: usize = 5;
    var count: u32 = 0;
    while (i < word_count) {
        const w = read_u32_le(binary, i * 4);
        const op = @as(u16, @truncate(w));
        const wc = w >> 16;
        if (op == opcode) count += 1;
        i += wc;
    }
    return count;
}

fn count_spirv_capability(binary: []const u8, capability: u32) u32 {
    const word_count = binary.len / 4;
    var i: usize = 5;
    var count: u32 = 0;
    while (i < word_count) {
        const w = read_u32_le(binary, i * 4);
        const op = @as(u16, @truncate(w));
        const wc: usize = @intCast(w >> 16);
        if (op == spirv.Opcode.Capability and wc == 2 and read_u32_le(binary, (i + 1) * 4) == capability) {
            count += 1;
        }
        i += wc;
    }
    return count;
}

fn spirv_constant_u32(binary: []const u8, result_id: u32) ?u32 {
    const word_count = binary.len / 4;
    var i: usize = 5;
    while (i < word_count) {
        const w = read_u32_le(binary, i * 4);
        const op = @as(u16, @truncate(w));
        const wc: usize = @intCast(w >> 16);
        if (wc == 0 or i + wc > word_count) return null;
        if (op == spirv.Opcode.Constant and wc >= 4 and read_u32_le(binary, (i + 2) * 4) == result_id) {
            return read_u32_le(binary, (i + 3) * 4);
        }
        i += wc;
    }
    return null;
}

fn expect_atomic_semantics_value(
    binary: []const u8,
    opcode: u16,
    semantics_operand_index: usize,
    expected_value: u32,
) !u32 {
    const word_count = binary.len / 4;
    var i: usize = 5;
    var count: u32 = 0;
    while (i < word_count) {
        const w = read_u32_le(binary, i * 4);
        const op = @as(u16, @truncate(w));
        const wc: usize = @intCast(w >> 16);
        try testing.expect(wc > 0 and i + wc <= word_count);
        if (op == opcode) {
            try testing.expect(semantics_operand_index < wc);
            const semantics_id = read_u32_le(binary, (i + semantics_operand_index) * 4);
            const semantics = spirv_constant_u32(binary, semantics_id);
            try testing.expect(semantics != null);
            try testing.expectEqual(expected_value, semantics.?);
            count += 1;
        }
        i += wc;
    }
    return count;
}

// ============================================================
// HLSL type mapping tests (via IR construction)
// ============================================================

test "spirv builtin: any and all reduce bool vectors" {
    const source =
        \\@group(0) @binding(0) var<storage, read_write> out_data: array<u32>;
        \\
        \\@compute @workgroup_size(1)
        \\fn main(@builtin(global_invocation_id) id: vec3u) {
        \\    let mask = (id.xyxy < vec4<u32>(2u)) & vec4<bool>(true, false, true, true);
        \\    let value = select(0u, 1u, any(mask.yw)) + select(0u, 2u, all(mask.xw));
        \\    out_data[0] = value;
        \\}
    ;
    var out: [MAX_SPIRV_OUTPUT]u8 = undefined;
    const len = try translateToSpirv(allocator, source, &out);
    try testing.expect(len >= 20);
    try testing.expectEqual(spirv.MAGIC, read_u32_le(&out, 0));
    try testing.expectEqual(@as(u32, 1), count_spirv_opcode(out[0..len], spirv.Opcode.Any));
    try testing.expectEqual(@as(u32, 1), count_spirv_opcode(out[0..len], spirv.Opcode.All));
}

test "spirv builtin: coarse derivatives emit native opcodes" {
    const source =
        \\struct FragIn {
        \\    @location(0) uv: vec2f,
        \\}
        \\
        \\@fragment
        \\fn main(in: FragIn) -> @location(0) vec4f {
        \\    let dx = dpdxCoarse(in.uv);
        \\    let dy = dpdyCoarse(in.uv);
        \\    let fw = fwidthCoarse(in.uv);
        \\    let v = dx + dy + fw;
        \\    return vec4f(v.x, v.y, 0.0, 1.0);
        \\}
    ;
    var out: [MAX_SPIRV_OUTPUT]u8 = undefined;
    const len = try translateToSpirv(allocator, source, &out);
    try testing.expect(len >= 20);
    try testing.expectEqual(spirv.MAGIC, read_u32_le(&out, 0));
    try testing.expectEqual(@as(u32, 1), count_spirv_opcode(out[0..len], spirv.Opcode.DPdxCoarse));
    try testing.expectEqual(@as(u32, 1), count_spirv_opcode(out[0..len], spirv.Opcode.DPdyCoarse));
    try testing.expectEqual(@as(u32, 1), count_spirv_opcode(out[0..len], spirv.Opcode.FwidthCoarse));
}

test "spirv texture: textureNumLevels emits OpImageQueryLevels" {
    const source =
        \\@group(0) @binding(0) var tex: texture_2d<f32>;
        \\@group(0) @binding(1) var<storage, read_write> out_data: array<u32>;
        \\
        \\@compute @workgroup_size(1)
        \\fn main() {
        \\    out_data[0] = textureNumLevels(tex);
        \\}
    ;
    var out: [MAX_SPIRV_OUTPUT]u8 = undefined;
    const len = try translateToSpirv(allocator, source, &out);
    try testing.expect(len >= 20);
    try testing.expectEqual(@as(u32, 1), count_spirv_opcode(out[0..len], spirv.Opcode.ImageQueryLevels));
}

test "spirv texture: sampled textureDimensions uses OpImageQuerySizeLod" {
    const source =
        \\@group(0) @binding(0) var tex: texture_2d<f32>;
        \\@group(0) @binding(1) var<storage, read_write> out_data: array<u32>;
        \\
        \\@compute @workgroup_size(1)
        \\fn main() {
        \\    let dims = textureDimensions(tex);
        \\    out_data[0] = dims.x;
        \\}
    ;
    var out: [MAX_SPIRV_OUTPUT]u8 = undefined;
    const len = try translateToSpirv(allocator, source, &out);
    try testing.expect(len >= 20);
    try testing.expectEqual(@as(u32, 1), count_spirv_opcode(out[0..len], spirv.Opcode.ImageQuerySizeLod));
}

test "spirv texture: textureSample produces valid SPIR-V" {
    const source =
        \\@group(0) @binding(0) var tex: texture_2d<f32>;
        \\@group(0) @binding(1) var samp: sampler;
        \\@group(0) @binding(2) var<storage, read_write> out_data: array<f32>;
        \\
        \\@compute @workgroup_size(1)
        \\fn main(@builtin(global_invocation_id) id: vec3u) {
        \\    let uv = vec2f(0.5, 0.5);
        \\    out_data[id.x] = textureSample(tex, samp, uv).x;
        \\}
    ;
    var out: [MAX_SPIRV_OUTPUT]u8 = undefined;
    const len = try translateToSpirv(allocator, source, &out);
    try testing.expect(len >= 20);
    try testing.expectEqual(spirv.MAGIC, read_u32_le(&out, 0));
    try testing.expectEqual(@as(u32, 1), count_spirv_opcode(out[0..len], spirv.Opcode.ImageSampleImplicitLod));
}

test "spirv texture: textureSampleLevel produces valid SPIR-V" {
    const source =
        \\@group(0) @binding(0) var tex: texture_2d<f32>;
        \\@group(0) @binding(1) var samp: sampler;
        \\@group(0) @binding(2) var<storage, read_write> out_data: array<f32>;
        \\
        \\@compute @workgroup_size(1)
        \\fn main(@builtin(global_invocation_id) id: vec3u) {
        \\    let uv = vec2f(0.5, 0.5);
        \\    out_data[id.x] = textureSampleLevel(tex, samp, uv, 0.0).x;
        \\}
    ;
    var out: [MAX_SPIRV_OUTPUT]u8 = undefined;
    const len = try translateToSpirv(allocator, source, &out);
    try testing.expect(len >= 20);
    try testing.expectEqual(spirv.MAGIC, read_u32_le(&out, 0));
    try testing.expectEqual(@as(u32, 1), count_spirv_opcode(out[0..len], spirv.Opcode.ImageSampleExplicitLod));
}

test "spirv texture: textureSampleBias emits implicit-lod sample" {
    const source =
        \\@group(0) @binding(0) var tex: texture_2d<f32>;
        \\@group(0) @binding(1) var samp: sampler;
        \\@group(0) @binding(2) var<storage, read_write> out_data: array<f32>;
        \\
        \\@compute @workgroup_size(1)
        \\fn main(@builtin(global_invocation_id) id: vec3u) {
        \\    let uv = vec2f(0.5, 0.5);
        \\    out_data[id.x] = textureSampleBias(tex, samp, uv, 1.0).x;
        \\}
    ;
    var out: [MAX_SPIRV_OUTPUT]u8 = undefined;
    const len = try translateToSpirv(allocator, source, &out);
    try testing.expect(len >= 20);
    try testing.expectEqual(spirv.MAGIC, read_u32_le(&out, 0));
    try testing.expectEqual(@as(u32, 1), count_spirv_opcode(out[0..len], spirv.Opcode.ImageSampleImplicitLod));
}

test "spirv texture: textureSampleCompare produces valid SPIR-V" {
    const source =
        \\@group(0) @binding(0) var shadow_tex: texture_depth_2d;
        \\@group(0) @binding(1) var shadow_samp: sampler_comparison;
        \\@group(0) @binding(2) var<storage, read_write> out_data: array<f32>;
        \\
        \\@compute @workgroup_size(1)
        \\fn main(@builtin(global_invocation_id) id: vec3u) {
        \\    let uv = vec2f(0.5, 0.5);
        \\    out_data[id.x] = textureSampleCompare(shadow_tex, shadow_samp, uv, 0.5);
        \\}
    ;
    var out: [MAX_SPIRV_OUTPUT]u8 = undefined;
    const len = try translateToSpirv(allocator, source, &out);
    try testing.expect(len >= 20);
    try testing.expectEqual(spirv.MAGIC, read_u32_le(&out, 0));
    try testing.expectEqual(@as(u32, 1), count_spirv_opcode(out[0..len], spirv.Opcode.ImageSampleDrefImplicitLod));
}

test "spirv texture: textureSampleCompareLevel produces valid SPIR-V" {
    const source =
        \\@group(0) @binding(0) var shadow_tex: texture_depth_2d;
        \\@group(0) @binding(1) var shadow_samp: sampler_comparison;
        \\@group(0) @binding(2) var<storage, read_write> out_data: array<f32>;
        \\
        \\@compute @workgroup_size(1)
        \\fn main(@builtin(global_invocation_id) id: vec3u) {
        \\    let uv = vec2f(0.5, 0.5);
        \\    out_data[id.x] = textureSampleCompareLevel(shadow_tex, shadow_samp, uv, 0.5);
        \\}
    ;
    var out: [MAX_SPIRV_OUTPUT]u8 = undefined;
    const len = try translateToSpirv(allocator, source, &out);
    try testing.expect(len >= 20);
    try testing.expectEqual(spirv.MAGIC, read_u32_le(&out, 0));
    try testing.expectEqual(@as(u32, 1), count_spirv_opcode(out[0..len], spirv.Opcode.ImageSampleDrefExplicitLod));
}

test "spirv texture: textureGather produces valid SPIR-V" {
    const source =
        \\@group(0) @binding(0) var tex: texture_2d<f32>;
        \\@group(0) @binding(1) var samp: sampler;
        \\@group(0) @binding(2) var<storage, read_write> out_data: array<f32>;
        \\
        \\@compute @workgroup_size(1)
        \\fn main(@builtin(global_invocation_id) id: vec3u) {
        \\    let uv = vec2f(0.5, 0.5);
        \\    out_data[id.x] = textureGather(0, tex, samp, uv).x;
        \\}
    ;
    var out: [MAX_SPIRV_OUTPUT]u8 = undefined;
    const len = try translateToSpirv(allocator, source, &out);
    try testing.expect(len >= 20);
    try testing.expectEqual(spirv.MAGIC, read_u32_le(&out, 0));
}

test "spirv texture: textureGatherCompare produces valid SPIR-V" {
    const source =
        \\@group(0) @binding(0) var shadow_tex: texture_depth_2d;
        \\@group(0) @binding(1) var shadow_samp: sampler_comparison;
        \\@group(0) @binding(2) var<storage, read_write> out_data: array<f32>;
        \\
        \\@compute @workgroup_size(1)
        \\fn main(@builtin(global_invocation_id) id: vec3u) {
        \\    let uv = vec2f(0.5, 0.5);
        \\    out_data[id.x] = textureGatherCompare(shadow_tex, shadow_samp, uv, 0.5).x;
        \\}
    ;
    var out: [MAX_SPIRV_OUTPUT]u8 = undefined;
    const len = try translateToSpirv(allocator, source, &out);
    try testing.expect(len >= 20);
    try testing.expectEqual(spirv.MAGIC, read_u32_le(&out, 0));
}

test "spirv texture: textureSampleGrad produces valid SPIR-V" {
    const source =
        \\@group(0) @binding(0) var tex: texture_2d<f32>;
        \\@group(0) @binding(1) var samp: sampler;
        \\@group(0) @binding(2) var<storage, read_write> out_data: array<f32>;
        \\
        \\@compute @workgroup_size(1)
        \\fn main(@builtin(global_invocation_id) id: vec3u) {
        \\    let uv = vec2f(0.5, 0.5);
        \\    let ddx = vec2f(0.01, 0.0);
        \\    let ddy = vec2f(0.0, 0.01);
        \\    out_data[id.x] = textureSampleGrad(tex, samp, uv, ddx, ddy).x;
        \\}
    ;
    var out: [MAX_SPIRV_OUTPUT]u8 = undefined;
    const len = try translateToSpirv(allocator, source, &out);
    try testing.expect(len >= 20);
    try testing.expectEqual(spirv.MAGIC, read_u32_le(&out, 0));
}

test "spirv texture: textureSampleOffset produces valid SPIR-V" {
    const source =
        \\@group(0) @binding(0) var tex: texture_2d<f32>;
        \\@group(0) @binding(1) var samp: sampler;
        \\@group(0) @binding(2) var<storage, read_write> out_data: array<f32>;
        \\
        \\@compute @workgroup_size(1)
        \\fn main(@builtin(global_invocation_id) id: vec3u) {
        \\    let uv = vec2f(0.5, 0.5);
        \\    let off = vec2i(1, 0);
        \\    out_data[id.x] = textureSampleOffset(tex, samp, uv, off).x;
        \\}
    ;
    var out: [MAX_SPIRV_OUTPUT]u8 = undefined;
    const len = try translateToSpirv(allocator, source, &out);
    try testing.expect(len >= 20);
    try testing.expectEqual(spirv.MAGIC, read_u32_le(&out, 0));
}

test "spirv texture: textureSampleLevelOffset produces valid SPIR-V" {
    const source =
        \\@group(0) @binding(0) var tex: texture_2d<f32>;
        \\@group(0) @binding(1) var samp: sampler;
        \\@group(0) @binding(2) var<storage, read_write> out_data: array<f32>;
        \\
        \\@compute @workgroup_size(1)
        \\fn main(@builtin(global_invocation_id) id: vec3u) {
        \\    let uv = vec2f(0.5, 0.5);
        \\    let off = vec2i(1, 0);
        \\    out_data[id.x] = textureSampleLevelOffset(tex, samp, uv, 0.0, off).x;
        \\}
    ;
    var out: [MAX_SPIRV_OUTPUT]u8 = undefined;
    const len = try translateToSpirv(allocator, source, &out);
    try testing.expect(len >= 20);
    try testing.expectEqual(spirv.MAGIC, read_u32_le(&out, 0));
}

test "spirv builtin: countOneBits produces valid SPIR-V" {
    const source =
        \\@group(0) @binding(0) var<storage, read_write> buf: array<u32>;
        \\@compute @workgroup_size(1)
        \\fn main(@builtin(global_invocation_id) id: vec3u) {
        \\    buf[id.x] = countOneBits(buf[id.x]);
        \\}
    ;
    var out: [MAX_SPIRV_OUTPUT]u8 = undefined;
    const len = try translateToSpirv(allocator, source, &out);
    try testing.expect(len >= 20);
    try testing.expectEqual(spirv.MAGIC, read_u32_le(&out, 0));
}

test "spirv builtin: reverseBits produces valid SPIR-V" {
    const source =
        \\@group(0) @binding(0) var<storage, read_write> buf: array<u32>;
        \\@compute @workgroup_size(1)
        \\fn main(@builtin(global_invocation_id) id: vec3u) {
        \\    buf[id.x] = reverseBits(buf[id.x]);
        \\}
    ;
    var out: [MAX_SPIRV_OUTPUT]u8 = undefined;
    const len = try translateToSpirv(allocator, source, &out);
    try testing.expect(len >= 20);
    try testing.expectEqual(spirv.MAGIC, read_u32_le(&out, 0));
}

test "spirv builtin: extractBits produces valid SPIR-V" {
    const source =
        \\@group(0) @binding(0) var<storage, read_write> buf: array<u32>;
        \\@compute @workgroup_size(1)
        \\fn main(@builtin(global_invocation_id) id: vec3u) {
        \\    buf[id.x] = extractBits(buf[id.x], 4u, 8u);
        \\}
    ;
    var out: [MAX_SPIRV_OUTPUT]u8 = undefined;
    const len = try translateToSpirv(allocator, source, &out);
    try testing.expect(len >= 20);
    try testing.expectEqual(spirv.MAGIC, read_u32_le(&out, 0));
}

test "spirv builtin: insertBits produces valid SPIR-V" {
    const source =
        \\@group(0) @binding(0) var<storage, read_write> buf: array<u32>;
        \\@compute @workgroup_size(1)
        \\fn main(@builtin(global_invocation_id) id: vec3u) {
        \\    buf[id.x] = insertBits(buf[id.x], 0xFFu, 4u, 8u);
        \\}
    ;
    var out: [MAX_SPIRV_OUTPUT]u8 = undefined;
    const len = try translateToSpirv(allocator, source, &out);
    try testing.expect(len >= 20);
    try testing.expectEqual(spirv.MAGIC, read_u32_le(&out, 0));
}

test "spirv builtin: countLeadingZeros produces valid SPIR-V" {
    const source =
        \\@group(0) @binding(0) var<storage, read_write> buf: array<u32>;
        \\@compute @workgroup_size(1)
        \\fn main(@builtin(global_invocation_id) id: vec3u) {
        \\    buf[id.x] = countLeadingZeros(buf[id.x]);
        \\}
    ;
    var out: [MAX_SPIRV_OUTPUT]u8 = undefined;
    const len = try translateToSpirv(allocator, source, &out);
    try testing.expect(len >= 20);
    try testing.expectEqual(spirv.MAGIC, read_u32_le(&out, 0));
}

test "spirv builtin: countTrailingZeros produces valid SPIR-V" {
    const source =
        \\@group(0) @binding(0) var<storage, read_write> buf: array<u32>;
        \\@compute @workgroup_size(1)
        \\fn main(@builtin(global_invocation_id) id: vec3u) {
        \\    buf[id.x] = countTrailingZeros(buf[id.x]);
        \\}
    ;
    var out: [MAX_SPIRV_OUTPUT]u8 = undefined;
    const len = try translateToSpirv(allocator, source, &out);
    try testing.expect(len >= 20);
    try testing.expectEqual(spirv.MAGIC, read_u32_le(&out, 0));
}

test "spirv builtin: firstLeadingBit produces valid SPIR-V" {
    const source =
        \\@group(0) @binding(0) var<storage, read_write> buf: array<u32>;
        \\@compute @workgroup_size(1)
        \\fn main(@builtin(global_invocation_id) id: vec3u) {
        \\    buf[id.x] = firstLeadingBit(buf[id.x]);
        \\}
    ;
    var out: [MAX_SPIRV_OUTPUT]u8 = undefined;
    const len = try translateToSpirv(allocator, source, &out);
    try testing.expect(len >= 20);
    try testing.expectEqual(spirv.MAGIC, read_u32_le(&out, 0));
}

test "SPIR-V leading-bit builtins use the GLSL integer search instructions" {
    inline for (.{ "u32", "i32" }) |element| {
        const source = "@group(0) @binding(0) var<storage, read_write> buf: array<" ++ element ++ ">;" ++
            "@compute @workgroup_size(1) fn main() { buf[0] = firstLeadingBit(buf[1]); }";
        var out: [MAX_SPIRV_OUTPUT]u8 = undefined;
        const len = try translateToSpirv(allocator, source, &out);
        var offset: usize = 5;
        var found = false;
        while (offset < len / 4) {
            const instruction = read_u32_le(&out, offset * 4);
            const words = instruction >> 16;
            try testing.expect(words > 0);
            if (@as(u16, @truncate(instruction)) == spirv.Opcode.ExtInst and words == 6 and
                read_u32_le(&out, (offset + 4) * 4) == @as(u32, if (element[0] == 'i') 74 else 75))
            {
                found = true;
            }
            offset += words;
        }
        try testing.expect(found);
    }
}

test "spirv builtin: firstTrailingBit produces valid SPIR-V" {
    const source =
        \\@group(0) @binding(0) var<storage, read_write> buf: array<u32>;
        \\@compute @workgroup_size(1)
        \\fn main(@builtin(global_invocation_id) id: vec3u) {
        \\    buf[id.x] = firstTrailingBit(buf[id.x]);
        \\}
    ;
    var out: [MAX_SPIRV_OUTPUT]u8 = undefined;
    const len = try translateToSpirv(allocator, source, &out);
    try testing.expect(len >= 20);
    try testing.expectEqual(spirv.MAGIC, read_u32_le(&out, 0));
}

test "spirv builtin: saturate produces valid SPIR-V" {
    const source =
        \\@group(0) @binding(0) var<storage, read_write> buf: array<f32>;
        \\@compute @workgroup_size(1)
        \\fn main(@builtin(global_invocation_id) id: vec3u) {
        \\    buf[id.x] = saturate(buf[id.x]);
        \\}
    ;
    var out: [MAX_SPIRV_OUTPUT]u8 = undefined;
    const len = try translateToSpirv(allocator, source, &out);
    try testing.expect(len >= 20);
    try testing.expectEqual(spirv.MAGIC, read_u32_le(&out, 0));
}

test "spirv builtin: reflect produces valid SPIR-V" {
    const source =
        \\@group(0) @binding(0) var<storage, read_write> buf: array<f32>;
        \\@compute @workgroup_size(1)
        \\fn main(@builtin(global_invocation_id) id: vec3u) {
        \\    let a = vec3f(1.0, 0.0, 0.0);
        \\    let n = vec3f(0.0, 1.0, 0.0);
        \\    let r = reflect(a, n);
        \\    buf[id.x] = r.x;
        \\}
    ;
    var out: [MAX_SPIRV_OUTPUT]u8 = undefined;
    const len = try translateToSpirv(allocator, source, &out);
    try testing.expect(len >= 20);
    try testing.expectEqual(spirv.MAGIC, read_u32_le(&out, 0));
}

test "spirv builtin: refract produces valid SPIR-V" {
    const source =
        \\@group(0) @binding(0) var<storage, read_write> buf: array<f32>;
        \\@compute @workgroup_size(1)
        \\fn main(@builtin(global_invocation_id) id: vec3u) {
        \\    let a = vec3f(1.0, 0.0, 0.0);
        \\    let n = vec3f(0.0, 1.0, 0.0);
        \\    let r = refract(a, n, 1.0);
        \\    buf[id.x] = r.x;
        \\}
    ;
    var out: [MAX_SPIRV_OUTPUT]u8 = undefined;
    const len = try translateToSpirv(allocator, source, &out);
    try testing.expect(len >= 20);
    try testing.expectEqual(spirv.MAGIC, read_u32_le(&out, 0));
}

test "spirv builtin: select produces valid SPIR-V" {
    const source =
        \\@group(0) @binding(0) var<storage, read_write> buf: array<f32>;
        \\@compute @workgroup_size(1)
        \\fn main(@builtin(global_invocation_id) id: vec3u) {
        \\    buf[id.x] = select(0.0, 1.0, buf[id.x] > 0.5);
        \\}
    ;
    var out: [MAX_SPIRV_OUTPUT]u8 = undefined;
    const len = try translateToSpirv(allocator, source, &out);
    try testing.expect(len >= 20);
    try testing.expectEqual(spirv.MAGIC, read_u32_le(&out, 0));
}

test "spirv builtin: storage texture r32float format produces valid SPIR-V" {
    const source =
        \\@group(0) @binding(0) var tex: texture_storage_2d<r32float, write>;
        \\@compute @workgroup_size(1)
        \\fn main(@builtin(global_invocation_id) id: vec3u) {
        \\    textureStore(tex, vec2i(0, 0), vec4f(1.0, 0.0, 0.0, 1.0));
        \\}
    ;
    var out: [MAX_SPIRV_OUTPUT]u8 = undefined;
    const len = try translateToSpirv(allocator, source, &out);
    try testing.expect(len >= 20);
    try testing.expectEqual(spirv.MAGIC, read_u32_le(&out, 0));
}

test "spirv builtin: storage texture rgba32float format produces valid SPIR-V" {
    const source =
        \\@group(0) @binding(0) var tex: texture_storage_2d<rgba32float, write>;
        \\@compute @workgroup_size(1)
        \\fn main(@builtin(global_invocation_id) id: vec3u) {
        \\    textureStore(tex, vec2i(0, 0), vec4f(1.0, 0.0, 0.0, 1.0));
        \\}
    ;
    var out: [MAX_SPIRV_OUTPUT]u8 = undefined;
    const len = try translateToSpirv(allocator, source, &out);
    try testing.expect(len >= 20);
    try testing.expectEqual(spirv.MAGIC, read_u32_le(&out, 0));
}

test "spirv builtin: storage texture rgba8uint format produces valid SPIR-V" {
    const source =
        \\@group(0) @binding(0) var tex: texture_storage_2d<rgba8uint, write>;
        \\@compute @workgroup_size(1)
        \\fn main(@builtin(global_invocation_id) id: vec3u) {
        \\    textureStore(tex, vec2i(0, 0), vec4u(255, 0, 0, 255));
        \\}
    ;
    var out: [MAX_SPIRV_OUTPUT]u8 = undefined;
    const len = try translateToSpirv(allocator, source, &out);
    try testing.expect(len >= 20);
    try testing.expectEqual(spirv.MAGIC, read_u32_le(&out, 0));
}

test "spirv builtin: storage texture rgba8sint format produces valid SPIR-V" {
    const source =
        \\@group(0) @binding(0) var tex: texture_storage_2d<rgba8sint, write>;
        \\@compute @workgroup_size(1)
        \\fn main(@builtin(global_invocation_id) id: vec3u) {
        \\    textureStore(tex, vec2i(0, 0), vec4i(127, 0, 0, 127));
        \\}
    ;
    var out: [MAX_SPIRV_OUTPUT]u8 = undefined;
    const len = try translateToSpirv(allocator, source, &out);
    try testing.expect(len >= 20);
    try testing.expectEqual(spirv.MAGIC, read_u32_le(&out, 0));
}

test "spirv texture: textureDimensions produces valid SPIR-V" {
    const source =
        \\@group(0) @binding(0) var tex: texture_2d<f32>;
        \\@group(0) @binding(1) var<storage, read_write> out_data: array<u32>;
        \\@compute @workgroup_size(1)
        \\fn main(@builtin(global_invocation_id) id: vec3u) {
        \\    let dims = textureDimensions(tex, 0);
        \\    out_data[id.x] = dims.x;
        \\}
    ;
    var out: [MAX_SPIRV_OUTPUT]u8 = undefined;
    const len = try translateToSpirv(allocator, source, &out);
    try testing.expect(len >= 20);
    try testing.expectEqual(spirv.MAGIC, read_u32_le(&out, 0));
}

test "spirv builtin: subgroupBroadcast produces valid SPIR-V" {
    const source =
        \\enable subgroups;
        \\@group(0) @binding(0) var<storage, read_write> buf: array<f32>;
        \\@compute @workgroup_size(64)
        \\fn main(@builtin(global_invocation_id) id: vec3u) {
        \\    buf[id.x] = subgroupBroadcast(buf[id.x], 0u);
        \\}
    ;
    var out: [MAX_SPIRV_OUTPUT]u8 = undefined;
    const len = try translateToSpirv(allocator, source, &out);
    try testing.expect(len >= 20);
    try testing.expectEqual(spirv.MAGIC, read_u32_le(&out, 0));
}

test "spirv builtin: subgroupShuffle produces valid SPIR-V" {
    const source =
        \\enable subgroups;
        \\@group(0) @binding(0) var<storage, read_write> buf: array<f32>;
        \\@compute @workgroup_size(64)
        \\fn main(@builtin(global_invocation_id) id: vec3u) {
        \\    buf[id.x] = subgroupShuffle(buf[id.x], id.x ^ 1u);
        \\}
    ;
    var out: [MAX_SPIRV_OUTPUT]u8 = undefined;
    const len = try translateToSpirv(allocator, source, &out);
    try testing.expect(len >= 20);
    try testing.expectEqual(spirv.MAGIC, read_u32_le(&out, 0));
}

test "spirv builtin: mixed plain and subgroup entry points preserve inner scope" {
    const source =
        \\enable subgroups;
        \\@group(0) @binding(0) var<storage, read_write> buf: array<f32>;
        \\@compute @workgroup_size(64)
        \\fn main_plain(@builtin(local_invocation_index) lane: u32) {
        \\    buf[lane] = buf[lane] + 1.0;
        \\}
        \\@compute @workgroup_size(64)
        \\fn main_subgroup(
        \\    @builtin(local_invocation_index) lane: u32,
        \\    @builtin(subgroup_size) subgroup_size: u32,
        \\) {
        \\    let subgroup_count = (64u + subgroup_size - 1u) / subgroup_size;
        \\    let sum = subgroupAdd(buf[lane]);
        \\    if (lane == 0u) {
        \\        var total = sum;
        \\        for (var index = 1u; index < subgroup_count; index = index + 1u) {
        \\            total = total + f32(index);
        \\        }
        \\        buf[0] = total;
        \\    }
        \\}
    ;
    var out: [MAX_SPIRV_OUTPUT]u8 = undefined;
    const len = try translateToSpirv(allocator, source, &out);
    try testing.expectEqual(@as(u32, 2), count_spirv_opcode(out[0..len], spirv.Opcode.EntryPoint));
    try testing.expectEqual(@as(u32, 1), count_spirv_opcode(out[0..len], spirv.Opcode.GroupNonUniformFAdd));
}

test "spirv builtin: workgroupBarrier produces valid SPIR-V" {
    const source =
        \\var<workgroup> workgroupValues: array<f32, 64>;
        \\@group(0) @binding(0) var<storage, read_write> buf: array<f32>;
        \\@compute @workgroup_size(64)
        \\fn main(@builtin(local_invocation_index) lid: u32) {
        \\    workgroupValues[lid] = buf[lid];
        \\    workgroupBarrier();
        \\    buf[lid] = workgroupValues[63u - lid];
        \\}
    ;
    var out: [MAX_SPIRV_OUTPUT]u8 = undefined;
    const len = try translateToSpirv(allocator, source, &out);
    try testing.expect(len >= 20);
    try testing.expectEqual(spirv.MAGIC, read_u32_le(&out, 0));
}

test "spirv builtin: bitcast produces valid SPIR-V" {
    const source =
        \\@group(0) @binding(0) var<storage, read_write> buf: array<u32>;
        \\@compute @workgroup_size(1)
        \\fn main(@builtin(global_invocation_id) id: vec3u) {
        \\    let f = bitcast<f32>(buf[id.x]);
        \\    buf[id.x] = bitcast<u32>(f + 1.0);
        \\}
    ;
    var out: [MAX_SPIRV_OUTPUT]u8 = undefined;
    const len = try translateToSpirv(allocator, source, &out);
    try testing.expect(len >= 20);
    try testing.expectEqual(spirv.MAGIC, read_u32_le(&out, 0));
}

test "spirv builtin: pack2x16float and unpack2x16float produce valid SPIR-V" {
    const source =
        \\@group(0) @binding(0) var<storage, read_write> buf: array<u32>;
        \\@compute @workgroup_size(1)
        \\fn main(@builtin(global_invocation_id) id: vec3u) {
        \\    let v = unpack2x16float(buf[id.x]);
        \\    buf[id.x] = pack2x16float(v + vec2f(1.0, 1.0));
        \\}
    ;
    var out: [MAX_SPIRV_OUTPUT]u8 = undefined;
    const len = try translateToSpirv(allocator, source, &out);
    try testing.expect(len >= 20);
    try testing.expectEqual(spirv.MAGIC, read_u32_le(&out, 0));
}

test "spirv builtin: atomicAdd produces valid SPIR-V" {
    const source =
        \\@group(0) @binding(0) var<storage, read_write> counter: atomic<u32>;
        \\@compute @workgroup_size(1)
        \\fn main(@builtin(global_invocation_id) id: vec3u) {
        \\    atomicAdd(&counter, 1u);
        \\}
    ;
    var out: [MAX_SPIRV_OUTPUT]u8 = undefined;
    const len = try translateToSpirv(allocator, source, &out);
    try testing.expect(len >= 20);
    try testing.expectEqual(spirv.MAGIC, read_u32_le(&out, 0));
}

test "spirv builtin: atomic operations use relaxed memory semantics" {
    const source =
        \\@group(0) @binding(0) var<storage, read_write> counter: atomic<u32>;
        \\var<workgroup> workgroupValues: atomic<u32>;
        \\@compute @workgroup_size(1)
        \\fn main(@builtin(global_invocation_id) id: vec3u) {
        \\    atomicStore(&workgroupValues, 1u);
        \\    let loaded = atomicLoad(&workgroupValues);
        \\    let old = atomicAdd(&counter, loaded + id.x);
        \\    atomicStore(&counter, old);
        \\}
    ;
    var out: [MAX_SPIRV_OUTPUT]u8 = undefined;
    const len = try translateToSpirv(allocator, source, &out);
    try testing.expect(len >= 20);
    try testing.expectEqual(spirv.MAGIC, read_u32_le(&out, 0));
    try testing.expectEqual(
        @as(u32, 1),
        try expect_atomic_semantics_value(out[0..len], spirv.Opcode.AtomicLoad, 5, spirv.MemorySemantics.None),
    );
    try testing.expectEqual(
        @as(u32, 2),
        try expect_atomic_semantics_value(out[0..len], spirv.Opcode.AtomicStore, 3, spirv.MemorySemantics.None),
    );
    try testing.expectEqual(
        @as(u32, 1),
        try expect_atomic_semantics_value(out[0..len], spirv.Opcode.AtomicIAdd, 5, spirv.MemorySemantics.None),
    );
}

test "spirv builtin: multiple math builtins produce valid SPIR-V" {
    const source =
        \\@group(0) @binding(0) var<storage, read_write> buf: array<f32>;
        \\@compute @workgroup_size(1)
        \\fn main(@builtin(global_invocation_id) id: vec3u) {
        \\    var v = buf[id.x];
        \\    v = sin(v);
        \\    v = cos(v);
        \\    v = exp(v);
        \\    v = log(v);
        \\    v = sqrt(v);
        \\    v = abs(v);
        \\    v = floor(v);
        \\    v = ceil(v);
        \\    v = round(v);
        \\    v = trunc(v);
        \\    v = fract(v);
        \\    v = pow(v, 2.0);
        \\    v = min(v, 1.0);
        \\    v = max(v, 0.0);
        \\    v = clamp(v, 0.0, 1.0);
        \\    buf[id.x] = v;
        \\}
    ;
    var out: [MAX_SPIRV_OUTPUT]u8 = undefined;
    const len = try translateToSpirv(allocator, source, &out);
    try testing.expect(len >= 20);
    try testing.expectEqual(spirv.MAGIC, read_u32_le(&out, 0));
}

test "spirv builtin: transpose produces valid SPIR-V" {
    const source =
        \\@group(0) @binding(0) var<storage, read_write> buf: array<f32>;
        \\@compute @workgroup_size(1)
        \\fn main(@builtin(global_invocation_id) id: vec3u) {
        \\    var m: mat2x2<f32> = mat2x2<f32>(vec2f(1.0, 0.0), vec2f(0.0, 1.0));
        \\    let t: mat2x2<f32> = transpose(m);
        \\    buf[id.x] = t[0].x;
        \\}
    ;
    var out: [MAX_SPIRV_OUTPUT]u8 = undefined;
    const len = try translateToSpirv(allocator, source, &out);
    try testing.expect(len >= 20);
    try testing.expectEqual(spirv.MAGIC, read_u32_le(&out, 0));
}

test "spirv builtin: determinant produces valid SPIR-V" {
    const source =
        \\@group(0) @binding(0) var<storage, read_write> buf: array<f32>;
        \\@compute @workgroup_size(1)
        \\fn main(@builtin(global_invocation_id) id: vec3u) {
        \\    var m: mat2x2<f32> = mat2x2<f32>(vec2f(1.0, 0.0), vec2f(0.0, 1.0));
        \\    buf[id.x] = determinant(m);
        \\}
    ;
    var out: [MAX_SPIRV_OUTPUT]u8 = undefined;
    const len = try translateToSpirv(allocator, source, &out);
    try testing.expect(len >= 20);
    try testing.expectEqual(spirv.MAGIC, read_u32_le(&out, 0));
}

test "spirv builtin: textureNumLevels produces valid SPIR-V" {
    const source =
        \\@group(0) @binding(0) var tex: texture_2d<f32>;
        \\@group(0) @binding(1) var<storage, read_write> out_data: array<u32>;
        \\@compute @workgroup_size(1)
        \\fn main(@builtin(global_invocation_id) id: vec3u) {
        \\    out_data[id.x] = textureNumLevels(tex);
        \\}
    ;
    var out: [MAX_SPIRV_OUTPUT]u8 = undefined;
    const len = try translateToSpirv(allocator, source, &out);
    try testing.expect(len >= 20);
    try testing.expectEqual(spirv.MAGIC, read_u32_le(&out, 0));
}

test "spirv builtin: textureNumLayers produces valid SPIR-V" {
    const source =
        \\@group(0) @binding(0) var tex: texture_2d_array<f32>;
        \\@group(0) @binding(1) var<storage, read_write> out_data: array<u32>;
        \\@compute @workgroup_size(1)
        \\fn main(@builtin(global_invocation_id) id: vec3u) {
        \\    out_data[id.x] = textureNumLayers(tex);
        \\}
    ;
    var out: [MAX_SPIRV_OUTPUT]u8 = undefined;
    const len = try translateToSpirv(allocator, source, &out);
    try testing.expect(len >= 20);
    try testing.expectEqual(spirv.MAGIC, read_u32_le(&out, 0));
}

test "spirv builtin: textureBarrier produces valid SPIR-V" {
    const source =
        \\@group(0) @binding(0) var<storage, read_write> buf: array<f32>;
        \\@compute @workgroup_size(64)
        \\fn main(@builtin(global_invocation_id) id: vec3u) {
        \\    buf[id.x] = 1.0;
        \\    textureBarrier();
        \\    buf[id.x] = 2.0;
        \\}
    ;
    var out: [MAX_SPIRV_OUTPUT]u8 = undefined;
    const len = try translateToSpirv(allocator, source, &out);
    try testing.expect(len >= 20);
    try testing.expectEqual(spirv.MAGIC, read_u32_le(&out, 0));
}

test "spirv builtin: subgroupElect produces valid SPIR-V" {
    const source =
        \\@group(0) @binding(0) var<storage, read_write> buf: array<u32>;
        \\@compute @workgroup_size(64)
        \\fn main(@builtin(global_invocation_id) id: vec3u) {
        \\    if subgroupElect() {
        \\        buf[0u] = 1u;
        \\    }
        \\}
    ;
    var out: [MAX_SPIRV_OUTPUT]u8 = undefined;
    const len = try translateToSpirv(allocator, source, &out);
    try testing.expect(len >= 20);
    try testing.expectEqual(spirv.MAGIC, read_u32_le(&out, 0));
}

test "spirv builtin: subgroupBroadcastFirst produces valid SPIR-V" {
    const source =
        \\@group(0) @binding(0) var<storage, read_write> buf: array<f32>;
        \\@compute @workgroup_size(64)
        \\fn main(@builtin(global_invocation_id) id: vec3u) {
        \\    buf[id.x] = subgroupBroadcastFirst(buf[id.x]);
        \\}
    ;
    var out: [MAX_SPIRV_OUTPUT]u8 = undefined;
    const len = try translateToSpirv(allocator, source, &out);
    try testing.expect(len >= 20);
    try testing.expectEqual(spirv.MAGIC, read_u32_le(&out, 0));
}

test "spirv builtin: subgroupShuffleDown produces valid SPIR-V" {
    const source =
        \\@group(0) @binding(0) var<storage, read_write> buf: array<f32>;
        \\@compute @workgroup_size(64)
        \\fn main(@builtin(global_invocation_id) id: vec3u) {
        \\    buf[id.x] = subgroupShuffleDown(buf[id.x], 1u);
        \\}
    ;
    var out: [MAX_SPIRV_OUTPUT]u8 = undefined;
    const len = try translateToSpirv(allocator, source, &out);
    try testing.expect(len >= 20);
    try testing.expectEqual(spirv.MAGIC, read_u32_le(&out, 0));
    try testing.expectEqual(@as(u32, 1), count_spirv_capability(out[0..len], spirv.Capability.GroupNonUniformShuffleRelative));
    try testing.expectEqual(@as(u32, 0), count_spirv_capability(out[0..len], spirv.Capability.GroupNonUniformShuffle));
}

test "spirv builtin: subgroupInclusiveAdd produces valid SPIR-V" {
    const source =
        \\@group(0) @binding(0) var<storage, read_write> buf: array<f32>;
        \\@compute @workgroup_size(64)
        \\fn main(@builtin(global_invocation_id) id: vec3u) {
        \\    buf[id.x] = subgroupInclusiveAdd(buf[id.x]);
        \\}
    ;
    var out: [MAX_SPIRV_OUTPUT]u8 = undefined;
    const len = try translateToSpirv(allocator, source, &out);
    try testing.expect(len >= 20);
    try testing.expectEqual(spirv.MAGIC, read_u32_le(&out, 0));
}

test "spirv builtin: subgroupMul produces valid SPIR-V" {
    const source =
        \\@group(0) @binding(0) var<storage, read_write> buf: array<f32>;
        \\@compute @workgroup_size(64)
        \\fn main(@builtin(global_invocation_id) id: vec3u) {
        \\    buf[id.x] = subgroupMul(buf[id.x]);
        \\}
    ;
    var out: [MAX_SPIRV_OUTPUT]u8 = undefined;
    const len = try translateToSpirv(allocator, source, &out);
    try testing.expect(len >= 20);
    try testing.expectEqual(spirv.MAGIC, read_u32_le(&out, 0));
}

test "spirv builtin: subgroup bitwise ops produce valid SPIR-V" {
    const source =
        \\@group(0) @binding(0) var<storage, read_write> buf: array<u32>;
        \\@compute @workgroup_size(64)
        \\fn main(@builtin(global_invocation_id) id: vec3u) {
        \\    let a = subgroupAnd(buf[id.x]);
        \\    let o = subgroupOr(buf[id.x]);
        \\    let x = subgroupXor(buf[id.x]);
        \\    buf[id.x] = a + o + x;
        \\}
    ;
    var out: [MAX_SPIRV_OUTPUT]u8 = undefined;
    const len = try translateToSpirv(allocator, source, &out);
    try testing.expect(len >= 20);
    try testing.expectEqual(spirv.MAGIC, read_u32_le(&out, 0));
}

test "spirv builtin: captured atomic pointers preserve target and scope" {
    inline for (.{ "workgroup", "storage, read_write" }) |space| {
        inline for (.{ "u32", "i32" }) |element| {
            const binding = if (comptime std.mem.eql(u8, space, "workgroup")) "" else "@group(0) @binding(0) ";
            const source = binding ++ "var<" ++ space ++ "> values: array<atomic<" ++ element ++ ">, 4>;" ++
                "@compute @workgroup_size(1) fn main() {" ++
                "var index = 1u; let pointer = &values[index]; let saved_pointer = pointer; index = 2u;" ++
                "atomicStore(saved_pointer, 10); let old = atomicAdd(pointer, 3);" ++
                "let result = atomicCompareExchangeWeak(saved_pointer, 13, 17); let loaded_value = atomicLoad(pointer); }";
            var out: [MAX_SPIRV_OUTPUT]u8 = undefined;
            const len = try translateToSpirv(allocator, source, &out);
            var offset: usize = 5;
            var pointer: ?u32 = null;
            var atomic_count: usize = 0;
            const expected_scope: u32 = if (std.mem.eql(u8, space, "workgroup")) spirv.Scope.Workgroup else spirv.Scope.Device;
            const expected_class: u32 = if (std.mem.eql(u8, space, "workgroup")) spirv.StorageClass.Workgroup else spirv.StorageClass.StorageBuffer;
            while (offset < len / 4) {
                const inst = read_u32_le(&out, offset * 4);
                const words = inst >> 16;
                try testing.expect(words > 0 and offset + words <= len / 4);
                const op: u16 = @truncate(inst);
                if (op == spirv.Opcode.AtomicStore or op == spirv.Opcode.AtomicIAdd or
                    op == spirv.Opcode.AtomicCompareExchange or op == spirv.Opcode.AtomicLoad)
                {
                    const pointer_operand: usize = if (op == spirv.Opcode.AtomicStore) 1 else 3;
                    const target = read_u32_le(&out, (offset + pointer_operand) * 4);
                    try testing.expect(target != 0);
                    if (pointer) |saved| try testing.expectEqual(saved, target) else pointer = target;
                    const scope_id = read_u32_le(&out, (offset + pointer_operand + 1) * 4);
                    try testing.expectEqual(expected_scope, spirv_constant_u32(out[0..len], scope_id).?);
                    atomic_count += 1;
                }
                offset += words;
            }
            try testing.expectEqual(@as(usize, 4), atomic_count);
            // All four operations use the captured access chain, not the later
            // index value or a Function variable holding a pointer-to-pointer.
            var pointer_type: ?u32 = null;
            offset = 5;
            while (offset < len / 4) {
                const inst = read_u32_le(&out, offset * 4);
                if (@as(u16, @truncate(inst)) == spirv.Opcode.AccessChain and
                    read_u32_le(&out, (offset + 2) * 4) == pointer.?)
                    pointer_type = read_u32_le(&out, (offset + 1) * 4);
                offset += inst >> 16;
            }
            try testing.expect(pointer_type != null);
            var checked_class = false;
            offset = 5;
            while (offset < len / 4) {
                const inst = read_u32_le(&out, offset * 4);
                if (@as(u16, @truncate(inst)) == spirv.Opcode.TypePointer and
                    read_u32_le(&out, (offset + 1) * 4) == pointer_type.?)
                {
                    try testing.expectEqual(expected_class, read_u32_le(&out, (offset + 2) * 4));
                    checked_class = true;
                }
                offset += inst >> 16;
            }
            try testing.expect(checked_class);
        }
    }
}
