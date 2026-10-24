/* SPDX-License-Identifier: MIT */
#include "fixture.h"

#include "../../tests/matrix_source.h"

static void shapes(void) {
    for (unsigned c = 2u; c <= 4u; ++c) {
        for (unsigned r = 2u; r <= 4u; ++r) {
            char source[1024], expected[32];
            snprintf(source, sizeof(source),
                "@group(0) @binding(0) var<storage,read_write> data:array<mat%ux%uf>;"
                "@compute @workgroup_size(1) fn main(){let m=data[0];data[0]=(m+m)-m;}", c, r);
            Fixture *f = prepare(source);
            char *text = emit(f, 0u);
            snprintf(expected, sizeof(expected), "float%ux%u", c, r);
            has(text, expected);
            has(text, "value[i] = doe_load");
            has(text, "(0.0f)");
            free(text);
            release(f);
        }
    }
}
int main(int argc, char **argv) {
    shapes();
    Fixture *f = prepare(matrix_control_source);
    char *text = emit(f, 0u);
    has(text, "transpose(");
    has(text, "determinant(");
    has(text, "ceil(");
    has(text, "doe_zero");
    has(text, "8ul * i");
    if (argc > 1) {
        FILE *file = fopen(argv[1], "wb");
        assert(file != NULL);
        assert(fwrite(text, 1u, strlen(text), file) == strlen(text));
        assert(fclose(file) == 0);
    }
    free(text);
    release(f);
    f = prepare("struct S{m:mat3x3f}var<workgroup> scratch_data:array<S,2>;"
        "@compute @workgroup_size(2) fn main(){workgroupBarrier();let m=scratch_data[0].m;scratch_data[1].m=m;}");
    text = emit(f, 0u);
    has(text, "doe_zero");
    has(text, "threadgroup_barrier(mem_flags::mem_threadgroup)");
    has(text, "doe_wstore");
    free(text);
    release(f);
    f = prepare_graphics("fn stop(){discard;}@fragment fn frag()->@location(0) vec4f{stop();return vec4f(1.0);}");
    text = emit(f, 0u);
    has(text, "discard_fragment();");
    has(text, "__METAL_VERSION__ < 300");
    free(text);
    release(f);
    f = prepare("@group(0) @binding(0) var<storage,read_write> a:atomic<u32>;"
        "@compute @workgroup_size(1) fn main(){atomicStore(&a,1u);}");
    text = emit(f, 0u);
    has(text, "atomic_store_explicit(target");
    free(text);
    release(f);
    puts("Metal matrix mapping: nine shapes, native products, compact typed zero, exact stride and continuing discard passed.");
    return 0;
}
