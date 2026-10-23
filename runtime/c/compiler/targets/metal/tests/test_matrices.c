/* SPDX-License-Identifier: MIT */
#include "fixture.h"

static const char control[] =
    "struct Data{m:mat3x2f, guard:u32}struct Padded{m:mat2x3f,guard:u32}"
    "@group(0) @binding(0) var<storage,read_write> output:array<f32>;"
    "@group(0) @binding(1) var<uniform> input:Data;"
    "@group(0) @binding(2) var<storage,read_write> matrices:array<mat3x2f>;"
    "@group(0) @binding(3) var<uniform> padded:Padded;"
    "@group(0) @binding(4) var<storage,read_write> padded_output:array<mat2x3f>;"
    "@compute @workgroup_size(1) fn main(){"
    "let a=input.m; let v=vec3f(1.0,2.0,3.0); let b=a*v;"
    "output[0]=b.x; output[1]=b.y; let c=vec2f(2.0,3.0)*a;"
    "output[2]=c.x; output[3]=c.y; output[4]=c.z;"
    "let product=a*transpose(a); output[5]=product[0][0];"
    "output[6]=product[0][1]; output[7]=product[1][1];"
    "let square=mat2x2f(1.0,2.0,3.0,5.0); output[8]=determinant(square);"
    "let doubled=a*2.0; let added=doubled-a; matrices[0]=added;"
    "let missing=matrices[2]; output[9]=missing[0][0];"
    "var nested:array<mat3x2f,2>; output[10]=nested[1][2][1];"
    "output[11]=ceil(a[0][0]);output[12]=padded.m[0][2];"
    "output[13]=padded.m[1][2];padded_output[0]=padded.m;}";
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
    Fixture *f = prepare(control);
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
    char intact[8] = "intact";
    DoeMetalResult result;
    assert(doe_metal_emit(&f->source, f->module, 0u, f->scratch, intact, sizeof(intact), &result) == DOE_COMPILER_UNSUPPORTED);
    assert(strcmp(intact, "intact") == 0 && result.written_bytes == 0u);
    release(f);
    puts("Metal matrix mapping: nine shapes, native products, compact typed zero, exact stride and continuing discard passed.");
    return 0;
}
