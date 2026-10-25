const std = @import("std");
const api = @import("../../src/compiler/wgsl/mod.zig");
const csl_tests = @import("translation_test_support.zig");
const TranslateError = api.TranslateError;
const MAX_CSL_OUTPUT = api.MAX_CSL_OUTPUT;
const translateToCsl = api.translateToCsl;

test "translate element-wise compute shader to CSL" {
    try csl_tests.expectElementWiseComputeShaderCompilesToCsl(std.testing.allocator, translateToCsl, MAX_CSL_OUTPUT);
}
test "vertex shader rejected for CSL emission" {
    try csl_tests.expectVertexShaderRejectedForCsl(std.testing.allocator, translateToCsl, TranslateError.UnsupportedConstruct, MAX_CSL_OUTPUT);
}
