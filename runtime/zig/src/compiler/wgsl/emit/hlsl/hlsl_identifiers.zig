const std = @import("std");

// WGSL reserves double-underscore identifiers. Reserve this namespace as well
// for IR-created names so encoding is injective even for synthetic modules.
const ESCAPE_PREFIX = "__doe_hlsl_user_";

// HLSL keywords and reserved C++ words. Numeric types additionally reserve their
// vector/matrix expansions. Keep source identifiers separate from HLSL tokens.
// https://learn.microsoft.com/windows/win32/direct3dhlsl/dx-graphics-hlsl-appendix-keywords
const KEYWORDS = std.StaticStringMap(void).initComptime(.{
    .{ "AppendStructuredBuffer", {} },
    .{ "asm", {} },
    .{ "asm_fragment", {} },
    .{ "BlendState", {} },
    .{ "bool", {} },
    .{ "break", {} },
    .{ "Buffer", {} },
    .{ "ByteAddressBuffer", {} },
    .{ "case", {} },
    .{ "cbuffer", {} },
    .{ "centroid", {} },
    .{ "class", {} },
    .{ "column_major", {} },
    .{ "compile", {} },
    .{ "compile_fragment", {} },
    .{ "CompileShader", {} },
    .{ "const", {} },
    .{ "continue", {} },
    .{ "ComputeShader", {} },
    .{ "ConsumeStructuredBuffer", {} },
    .{ "default", {} },
    .{ "DepthStencilState", {} },
    .{ "DepthStencilView", {} },
    .{ "discard", {} },
    .{ "do", {} },
    .{ "double", {} },
    .{ "DomainShader", {} },
    .{ "dword", {} },
    .{ "else", {} },
    .{ "export", {} },
    .{ "extern", {} },
    .{ "false", {} },
    .{ "float", {} },
    .{ "for", {} },
    .{ "fxgroup", {} },
    .{ "GeometryShader", {} },
    .{ "groupshared", {} },
    .{ "half", {} },
    .{ "HullShader", {} },
    .{ "Hullshader", {} },
    .{ "if", {} },
    .{ "in", {} },
    .{ "inline", {} },
    .{ "inout", {} },
    .{ "InputPatch", {} },
    .{ "int", {} },
    .{ "interface", {} },
    .{ "line", {} },
    .{ "lineadj", {} },
    .{ "linear", {} },
    .{ "LineStream", {} },
    .{ "matrix", {} },
    .{ "min16float", {} },
    .{ "min10float", {} },
    .{ "min16int", {} },
    .{ "min12int", {} },
    .{ "min16uint", {} },
    .{ "namespace", {} },
    .{ "nointerpolation", {} },
    .{ "noperspective", {} },
    .{ "NULL", {} },
    .{ "out", {} },
    .{ "OutputPatch", {} },
    .{ "packoffset", {} },
    .{ "pass", {} },
    .{ "pixelfragment", {} },
    .{ "PixelShader", {} },
    .{ "point", {} },
    .{ "PointStream", {} },
    .{ "precise", {} },
    .{ "RasterizerState", {} },
    .{ "RenderTargetView", {} },
    .{ "return", {} },
    .{ "register", {} },
    .{ "row_major", {} },
    .{ "RWBuffer", {} },
    .{ "RWByteAddressBuffer", {} },
    .{ "RWStructuredBuffer", {} },
    .{ "RWTexture1D", {} },
    .{ "RWTexture1DArray", {} },
    .{ "RWTexture2D", {} },
    .{ "RWTexture2DArray", {} },
    .{ "RWTexture3D", {} },
    .{ "sample", {} },
    .{ "sampler", {} },
    .{ "SamplerState", {} },
    .{ "SamplerComparisonState", {} },
    .{ "shared", {} },
    .{ "snorm", {} },
    .{ "stateblock", {} },
    .{ "stateblock_state", {} },
    .{ "static", {} },
    .{ "string", {} },
    .{ "struct", {} },
    .{ "switch", {} },
    .{ "StructuredBuffer", {} },
    .{ "tbuffer", {} },
    .{ "technique", {} },
    .{ "technique10", {} },
    .{ "technique11", {} },
    .{ "texture", {} },
    .{ "Texture1D", {} },
    .{ "Texture1DArray", {} },
    .{ "Texture2D", {} },
    .{ "Texture2DArray", {} },
    .{ "Texture2DMS", {} },
    .{ "Texture2DMSArray", {} },
    .{ "Texture3D", {} },
    .{ "TextureCube", {} },
    .{ "TextureCubeArray", {} },
    .{ "true", {} },
    .{ "typedef", {} },
    .{ "triangle", {} },
    .{ "triangleadj", {} },
    .{ "TriangleStream", {} },
    .{ "uint", {} },
    .{ "uniform", {} },
    .{ "unorm", {} },
    .{ "unsigned", {} },
    .{ "vector", {} },
    .{ "vertexfragment", {} },
    .{ "VertexShader", {} },
    .{ "void", {} },
    .{ "volatile", {} },
    .{ "while", {} },
    .{ "auto", {} },
    .{ "catch", {} },
    .{ "char", {} },
    .{ "const_cast", {} },
    .{ "delete", {} },
    .{ "dynamic_cast", {} },
    .{ "enum", {} },
    .{ "explicit", {} },
    .{ "friend", {} },
    .{ "goto", {} },
    .{ "long", {} },
    .{ "mutable", {} },
    .{ "new", {} },
    .{ "operator", {} },
    .{ "private", {} },
    .{ "protected", {} },
    .{ "public", {} },
    .{ "reinterpret_cast", {} },
    .{ "short", {} },
    .{ "signed", {} },
    .{ "sizeof", {} },
    .{ "static_cast", {} },
    .{ "template", {} },
    .{ "this", {} },
    .{ "throw", {} },
    .{ "try", {} },
    .{ "typename", {} },
    .{ "union", {} },
    .{ "using", {} },
    .{ "virtual", {} },
    .{ "float16_t", {} },
    .{ "float32_t", {} },
    .{ "float64_t", {} },
    .{ "int16_t", {} },
    .{ "int32_t", {} },
    .{ "int64_t", {} },
    .{ "uint16_t", {} },
    .{ "uint32_t", {} },
    .{ "uint64_t", {} },
});

// User symbols cannot occupy generated helper namespaces or shadow resource
// references in helper bodies. This is shared by definitions and every use.
const HELPER_LOCALS = std.StaticStringMap(void).initComptime(.{
    .{ "count", {} },    .{ "stride", {} },  .{ "index", {} },
    .{ "result", {} },   .{ "next", {} },    .{ "ignored", {} },
    .{ "expected", {} }, .{ "desired", {} }, .{ "level", {} },
    .{ "w", {} },        .{ "h", {} },       .{ "d", {} },
    .{ "lvls", {} },     .{ "elems", {} },
});

pub fn needsEscape(name: []const u8) bool {
    if (std.mem.startsWith(u8, name, "doe_") or
        std.mem.endsWith(u8, name, "_impl") or
        std.mem.endsWith(u8, name, "_stage_out") or
        HELPER_LOCALS.has(name)) return true;
    if (std.mem.startsWith(u8, name, "__doe_hlsl_") or KEYWORDS.has(name)) return true;
    const numeric = [_][]const u8{ "bool", "int", "uint", "float", "double", "half", "min10float", "min16float", "min12int", "min16int", "min16uint", "int16_t", "int32_t", "int64_t", "uint16_t", "uint32_t", "uint64_t", "float16_t", "float32_t", "float64_t" };
    for (numeric) |base| {
        if (!std.mem.startsWith(u8, name, base)) continue;
        const suffix = name[base.len..];
        if (suffix.len == 1 and suffix[0] >= '1' and suffix[0] <= '4') return true;
        if (suffix.len == 3 and suffix[0] >= '1' and suffix[0] <= '4' and suffix[1] == 'x' and suffix[2] >= '1' and suffix[2] <= '4') return true;
    }
    return false;
}

pub fn emit(name: []const u8, buffer: []u8, position: *usize) error{OutputTooLarge}!void {
    if (position.* > buffer.len) return error.OutputTooLarge;
    if (!needsEscape(name)) {
        if (name.len > buffer.len - position.*) return error.OutputTooLarge;
        @memcpy(buffer[position.*..][0..name.len], name);
        position.* += name.len;
        return;
    }
    const encoded_len = std.math.mul(usize, name.len, 2) catch return error.OutputTooLarge;
    const len = std.math.add(usize, ESCAPE_PREFIX.len, encoded_len) catch return error.OutputTooLarge;
    if (len > buffer.len - position.*) return error.OutputTooLarge;
    @memcpy(buffer[position.*..][0..ESCAPE_PREFIX.len], ESCAPE_PREFIX);
    position.* += ESCAPE_PREFIX.len;
    const hex = "0123456789abcdef";
    for (name) |byte| {
        buffer[position.*] = hex[byte >> 4];
        buffer[position.* + 1] = hex[byte & 15];
        position.* += 2;
    }
}

/// DXC consumers select the same emitted entry name without changing WGSL identity.
pub fn allocName(allocator: std.mem.Allocator, name: []const u8) error{OutOfMemory}![]u8 {
    if (!needsEscape(name)) return allocator.dupe(u8, name);
    const encoded_len = std.math.mul(usize, name.len, 2) catch return error.OutOfMemory;
    const len = std.math.add(usize, ESCAPE_PREFIX.len, encoded_len) catch return error.OutOfMemory;
    const result = try allocator.alloc(u8, len);
    var position: usize = 0;
    emit(name, result, &position) catch unreachable; // Exact checked allocation above.
    return result;
}

test "HLSL numeric keyword expansions and case remain distinct" {
    for ([_][]const u8{ "out", "matrix", "vector", "float4", "uint4x4", "min16float2x3", "int64_t4" }) |name| {
        try std.testing.expect(needsEscape(name));
    }
    for ([_][]const u8{ "output", "Out", "Matrix", "float5", "float4x5", "data" }) |name| {
        try std.testing.expect(!needsEscape(name));
    }
}

test "HLSL escape prefix cannot alias a source identifier and short output stays untouched" {
    const escaped = try allocName(std.testing.allocator, "out");
    defer std.testing.allocator.free(escaped);
    const prefixed = try allocName(std.testing.allocator, escaped);
    defer std.testing.allocator.free(prefixed);
    try std.testing.expect(!std.mem.eql(u8, escaped, prefixed));
    var buffer = [_]u8{0xaa} ** 4;
    var position: usize = 1;
    try std.testing.expectError(error.OutputTooLarge, emit("out", &buffer, &position));
    try std.testing.expectEqual(@as(usize, 1), position);
    try std.testing.expectEqualSlices(u8, &.{ 0xaa, 0xaa, 0xaa, 0xaa }, &buffer);
}
