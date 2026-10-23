/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "test_support.h"
#include "../../../src/stage.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void test_stage_mapping(void) {
    Fixture *f = prepare_graphics(
        "struct Output{@builtin(position) p:vec4f,"
        "@location(3) @interpolate(flat) index:u32,"
        "@location(0) @interpolate(linear,centroid) color:vec4f}"
        "@vertex fn vertex(@builtin(vertex_index) v:u32,@builtin(instance_index) i:u32,"
        "@location(2) p:vec4f)->Output{return Output(p,v+i,vec4f(0.5));}"
        "@fragment fn fragment(input:Output)->@location(0) vec4f{return input.color;}"
        "@compute @workgroup_size(1) fn compute(){}");
    char *text = emit(f, NO_ID);
    has(text, "SV_Position");
    has(text, "SV_VertexID");
    has(text, "SV_InstanceID");
    has(text, "TEXCOORD2");
    has(text, "nointerpolation uint");
    has(text, "noperspective centroid float4");
    has(text, "SV_Target0");
    has(text, "DoeStage2 vertex(");
    has(text, "DoeStage2 fragment(");
    has(text, "void compute(");
    free(text);
    text = emit(f, 0u);
    assert(strstr(text, "SV_Target") == NULL);
    assert(strstr(text, "numthreads") == NULL);
    free(text);
    text = emit(f, 1u);
    assert(strstr(text, "SV_VertexID") == NULL);
    assert(strstr(text, "doe_fn0(") == NULL);
    free(text);
    release(f);
}
static void test_fragment_builtins(void) {
    Fixture *f = prepare_graphics(
        "struct Input{@builtin(front_facing) face:bool,@builtin(sample_index) index:u32,"
        "@builtin(sample_mask) mask:u32,@builtin(position) p:vec4f,"
        "@location(0) @interpolate(perspective,sample) color:vec4f}"
        "struct Output{@builtin(frag_depth) depth:f32,@builtin(sample_mask) mask:u32,"
        "@location(0) color:vec4f}"
        "@fragment fn fragment(input:Input)->Output{"
        "if(input.face){return Output(input.p.z,input.mask,input.color);}"
        "return Output(0.5,input.index,vec4f(0.0));}");
    char *text = emit(f, 0u);
    static const char *const semantics[] = {"SV_IsFrontFace", "SV_SampleIndex", "SV_Coverage",
        "SV_Position", "SV_Depth", "SV_Target0", "linear sample float4"};
    for (size_t i = 0u; i < sizeof(semantics) / sizeof(semantics[0]); ++i) has(text, semantics[i]);
    free(text);
    release(f);
    f = prepare_graphics("@fragment fn empty(){}");
    text = emit(f, 0u);
    has(text, "void empty()");
    assert(strstr(text, "struct DoeStage") == NULL);
    free(text);
    release(f);
}
static void test_invariant_rejection(void) {
    Fixture *f = prepare_graphics(
        "@vertex fn vertex()->@builtin(position) @invariant vec4f{return vec4f(0.0);}");
    DoeHlslResult result;
    char untouched[8] = "intact";
    assert(doe_hlsl_emit(&f->source, f->module, 0u, untouched, sizeof(untouched), &result) ==
        DOE_COMPILER_UNSUPPORTED);
    assert(result.written_bytes == 0u && result.required_bytes == 0u);
    assert(strcmp(untouched, "intact") == 0);
    has(f->diagnostic.message, "invariant");
    release(f);
}
static void test_graphics_host_fixture(const char *output_path) {
    Fixture *f = prepare_graphics(
        "struct Input{@location(2) @interpolate(flat) left:u32,"
        "@location(0) @interpolate(flat) right:u32,@builtin(front_facing) face:bool}"
        "struct Output{@location(1) value:u32,@builtin(sample_mask) mask:u32}"
        "fn combine(left:u32,right:u32)->u32{return left*100u+right;}"
        "@fragment fn graphics(input:Input,@builtin(sample_index) sample:u32)->Output{"
        "let value=combine(input.left,input.right);"
        "if(input.face){return Output(value,sample);}return Output(value+1u,0u);}");
    char *text = emit(f, 0u);
    has(text, "SV_Target1");
    has(text, "SV_SampleIndex");
    if (output_path != NULL) {
        FILE *file = fopen(output_path, "wb");
        assert(file != NULL);
        assert(fwrite(text, 1u, strlen(text), file) == strlen(text));
        assert(fclose(file) == 0);
    }
    free(text);
    release(f);
}
void test_graphics(const char *output_path) {
    test_stage_mapping();
    test_fragment_builtins();
    test_invariant_rejection();
    test_graphics_host_fixture(output_path);
}
