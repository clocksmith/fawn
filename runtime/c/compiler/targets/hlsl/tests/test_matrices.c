/* SPDX-License-Identifier: MIT */
#include "test_support.h"
#include "../../tests/matrix_source.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void test_matrices(const char *path) {
    for (unsigned columns = 2u; columns <= 4u; ++columns) {
        for (unsigned rows = 2u; rows <= 4u; ++rows) {
            char source[1024], expected[32];
            snprintf(source, sizeof(source),
                "@group(0) @binding(0) var<storage,read_write> data:array<mat%ux%uf>;"
                "@compute @workgroup_size(1) fn main(){let m=data[0];data[0]=(m+m)-m;}", columns, rows);
            Fixture *f = prepare(source);
            char *text = emit(f, 0u);
            snprintf(expected, sizeof(expected), "float%ux%u", columns, rows);
            has(text, expected);
            has(text, "value[i] = doe_r0_load");
            free(text);
            release(f);
        }
    }
    Fixture *f = prepare(matrix_control_source);
    char *text = emit(f, 0u);
    has(text, "mul(");
    has(text, "transpose(");
    has(text, "determinant(");
    has(text, "ceil(");
    has(text, "8u * i");
    has(text, "16u * i");
    if (path != NULL) {
        FILE *file = fopen(path, "wb");
        assert(file != NULL);
        assert(fwrite(text, 1u, strlen(text), file) == strlen(text));
        assert(fclose(file) == 0);
    }
    free(text);
    release(f);
    f = prepare("struct S{m:mat3x3f}var<workgroup> scratch_data:array<S,2>;"
        "@compute @workgroup_size(2) fn main(){workgroupBarrier();let m=scratch_data[0].m;scratch_data[1].m=m;}");
    text = emit(f, 0u);
    has(text, "GroupMemoryBarrierWithGroupSync()");
    has(text, "float3x3");
    free(text);
    release(f);
    f = prepare("@group(0) @binding(0) var<storage,read_write> a:atomic<u32>;"
        "@compute @workgroup_size(1) fn main(){atomicStore(&a,1u);}");
    char intact[8] = "intact";
    DoeHlslResult result;
    assert(doe_hlsl_emit(&f->source, f->module, 0u, f->scratch, intact, sizeof(intact), &result) == DOE_COMPILER_UNSUPPORTED);
    assert(strcmp(intact, "intact") == 0 && result.written_bytes == 0u);
    release(f);
    puts("HLSL matrix mapping: nine shapes, reversed products, exact stride and atomic rejection passed.");
}
