/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "fixture.h"
#include "../../tests/component_math_source.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
    const char *path = argc > 1 ? argv[1] : NULL;
    static const char *const names[][2] = {
        {"floor", "floor("},
        {"round", "rint("},
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
        char *text = emit(f, 0u);
        has(text, names[i][1]);
        free(text);
        release(f);
    }
    Fixture *f = prepare(component_math_source);
    char *text = emit(f, 0u);
    has(text, " - floor(");
    has(text, "as_type<int>(0u - as_type<uint>(");
    if (path != NULL) {
        FILE *file = fopen(path, "wb");
        assert(file != NULL);
        assert(fwrite(text, 1u, strlen(text), file) == strlen(text));
        assert(fclose(file) == 0);
    }
    assert(component_math_expected[6] == 0x80000000u);
    free(text);
    release(f);
    puts("Metal component math: closed mappings, signed minimum, ties-even and fractional lowering passed.");
    return 0;
}
