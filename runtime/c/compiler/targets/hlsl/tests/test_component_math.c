/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "test_support.h"
#include "../../tests/component_math_source.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void test_component_math(const char *path) {
    static const char *const names[][2] = {
        {"floor", "floor("},
        {"round", "round("},
        {"trunc", "trunc("},
        {"sin", "sin("},
        {"cos", "cos("},
        {"tan", "tan("},
        {"asin", "asin("},
        {"acos", "acos("},
        {"atan", "atan("},
        {"sinh", "sinh("},
        {"cosh", "cosh("},
        {"asinh", "asinh("},
        {"acosh", "acosh("},
        {"atanh", "atanh("},
        {"log", "log("},
        {"log2", "log2("},
        {"exp2", "exp2("},
        {"inverseSqrt", "rsqrt("},
    };
    for (size_t i = 0u; i < sizeof(names) / sizeof(*names); ++i) {
        char source[512];
        snprintf(source, sizeof(source),
            "@group(0) @binding(0) var<storage,read_write> data:array<vec4f>;"
            "@compute @workgroup_size(1) fn main(){data[1]=%s(data[0]);}", names[i][0]);
        Fixture *f = prepare(source);
        if (strcmp(names[i][0], "asinh") == 0 || strcmp(names[i][0], "acosh") == 0 ||
            strcmp(names[i][0], "atanh") == 0) {
            char untouched[8] = "intact";
            DoeHlslResult result;
            assert(doe_hlsl_emit(&f->source, f->module, 0u, f->scratch, untouched,
                                sizeof(untouched), &result) == DOE_COMPILER_UNSUPPORTED);
            assert(result.written_bytes == 0u && strcmp(untouched, "intact") == 0);
            release(f);
            continue;
        }
        char *text = emit(f, 0u);
        has(text, names[i][1]);
        free(text);
        release(f);
    }
    for (size_t i = 0u; i < 2u; ++i) {
        char source[512];
        const char *name = i == 0u ? "atan2" : "pow";
        snprintf(source, sizeof(source),
            "@group(0) @binding(0) var<storage,read_write> data:array<vec4f>;"
            "@compute @workgroup_size(1) fn main(){data[2]=%s(data[0],data[1]);}", name);
        Fixture *binary = prepare(source);
        char *output = emit(binary, 0u);
        has(output, i == 0u ? "atan2(" : "pow(");
        free(output);
        release(binary);
    }
    Fixture *f = prepare(component_math_source);
    char *text = emit(f, 0u);
    has(text, " - floor(");
    has(text, "asint(0u - asuint(");
    if (path != NULL) {
        FILE *file = fopen(path, "wb");
        assert(file != NULL);
        assert(fwrite(text, 1u, strlen(text), file) == strlen(text));
        assert(fclose(file) == 0);
    }
    assert(component_math_expected[6] == 0x80000000u);
    free(text);
    release(f);
    puts("Hlsl component math: closed mappings, signed minimum, ties-even and fractional lowering passed.");
}
