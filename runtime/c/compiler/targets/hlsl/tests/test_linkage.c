/* SPDX-License-Identifier: MIT
 * Typed source identities stay intact while native carrier fields are ordered. */
#include "test_support.h"
#include "../../../src/stage.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char source[] =
    "struct Out{@builtin(position) p:vec4f,@location(11) unused_tail:vec3f,"
    "@location(9) @interpolate(flat) u:u32,@location(14) @interpolate(linear,centroid) linear_value:vec3f,"
    "@location(15) @interpolate(perspective,sample) sample_value:f32,@location(3) unused_middle:vec4f,"
    "@location(7) @interpolate(flat) i:vec3i,@location(2) f:vec2f}"
    "struct Reverse{@location(2) f:vec2f,@location(7) @interpolate(flat) i:vec3i,"
    "@location(3) unused_middle:vec4f,@location(9) @interpolate(flat) u:u32,"
    "@location(15) @interpolate(perspective,sample) sample_value:f32,"
    "@location(14) @interpolate(linear,centroid) linear_value:vec3f,"
    "@location(11) unused_tail:vec3f,@builtin(position) p:vec4f}"
    "@vertex fn vertex()->Out{return Out(vec4f(0,0,0,1),vec3f(11),9u,vec3f(14),15.0,vec4f(3),vec3i(7),vec2f(2));}"
    "@vertex fn vertex_reverse()->Reverse{return Reverse(vec2f(2),vec3i(7),vec4f(3),9u,15.0,vec3f(14),vec3f(11),vec4f(0,0,0,1));}"
    "@fragment fn full(input:Out)->@location(0) vec4f{return vec4f(input.f+input.unused_middle.xy,"
    "f32(input.i.x)+f32(input.u),input.unused_tail.x+input.linear_value.x+input.sample_value);}"
    "@fragment fn subset(@location(9) @interpolate(flat) u:u32,@location(2) f:vec2f,"
    "@builtin(front_facing) face:bool,@location(7) @interpolate(flat) i:vec3i,"
    "@location(15) @interpolate(perspective,sample) sample_value:f32,"
    "@location(14) @interpolate(linear,centroid) linear_value:vec3f)->@location(0) vec4f{"
    "return vec4f(f,f32(u)+f32(i.x),select(0.0,1.0,face)+linear_value.x+sample_value);}"
    "@fragment fn subset_position(@builtin(position) p:vec4f,@location(7) @interpolate(flat) i:vec3i,"
    "@builtin(front_facing) face:bool,@location(9) @interpolate(flat) u:u32,@location(2) f:vec2f,"
    "@location(14) @interpolate(linear,centroid) linear_value:vec3f,"
    "@location(15) @interpolate(perspective,sample) sample_value:f32)"
    "->@location(0) vec4f{return vec4f(f,f32(u)+f32(i.x)+p.z,select(0.0,1.0,face)+linear_value.x+sample_value);}"
    "@fragment fn subset_unused(@location(9) @interpolate(flat) u:u32,@location(2) unused:vec2f,"
    "@builtin(front_facing) face:bool,@location(7) @interpolate(flat) i:vec3i,"
    "@location(15) @interpolate(perspective,sample) sample_value:f32,"
    "@location(14) @interpolate(linear,centroid) linear_value:vec3f)->@location(0) vec4f{"
    "return vec4f(f32(u),f32(i.x),linear_value.x+sample_value,select(0.0,1.0,face));}"
    "@fragment fn no_user(@builtin(front_facing) face:bool)->@location(0) vec4f{"
    "return vec4f(select(0.0,1.0,face));}";

static void save(const char *directory, const char *name, const char *text) {
    if (directory == NULL) return;
    char path[1024];
    const int length = snprintf(path, sizeof(path), "%s/%s.hlsl", directory, name);
    assert(length > 0 && (size_t)length < sizeof(path));
    FILE *file = fopen(path, "wb");
    assert(file != NULL);
    assert(fwrite(text, 1u, strlen(text), file) == strlen(text));
    assert(fclose(file) == 0);
}
static void ordered(const char *text, uint32_t direction, const char *const *semantics, size_t count) {
    const char *cursor = strstr(text, direction == DOE_COMPILER_INTERFACE_INPUT ?
        "struct DoeStage1" : "struct DoeStage2");
    assert(cursor != NULL);
    const char *end = strstr(cursor, "};");
    assert(end != NULL);
    for (size_t i = 0u; i < count; ++i) {
        cursor = strstr(cursor, semantics[i]);
        assert(cursor != NULL && cursor < end);
        ++cursor;
    }
}
void test_linkage(const char *directory) {
    Fixture *fixture = prepare_graphics(source);
    StageWorkspace *stages = fixture->source.stages;
    const size_t bytes = (size_t)stages->interface_count * sizeof(*stages->interfaces);
    StageInterface *before = malloc(bytes);
    assert(before != NULL);
    memcpy(before, stages->interfaces, bytes);
    static const char *const names[] = {"vertex", "vertex_reverse", "full", "subset", "subset_position", "subset_unused", "no_user"};
    static const char *const full[] = {"TEXCOORD2", "TEXCOORD3", "TEXCOORD7", "TEXCOORD9", "TEXCOORD11", "TEXCOORD14", "TEXCOORD15", "SV_Position"};
    static const char *const subset[] = {"TEXCOORD2", "TEXCOORD7", "TEXCOORD9", "TEXCOORD14", "TEXCOORD15", "SV_IsFrontFace"};
    static const char *const position[] = {"TEXCOORD2", "TEXCOORD7", "TEXCOORD9", "TEXCOORD14", "TEXCOORD15", "SV_Position", "SV_IsFrontFace"};
    for (uint32_t i = 0u; i < 7u; ++i) {
        char *text = emit(fixture, i);
        if (i < 2u) {
            ordered(text, DOE_COMPILER_INTERFACE_OUTPUT, full, 8u);
            has(text, "DOE_HLSL_INTERSTAGE_MASK & 4u");
            has(text, "DOE_HLSL_INTERSTAGE_MASK & 128u");
            has(text, "DOE_HLSL_INTERSTAGE_MASK & 512u");
            has(text, "value = doe_fn");
        } else if (i == 2u) ordered(text, DOE_COMPILER_INTERFACE_INPUT, full, 8u);
        else if (i == 3u) ordered(text, DOE_COMPILER_INTERFACE_INPUT, subset, 6u);
        else if (i == 4u) ordered(text, DOE_COMPILER_INTERFACE_INPUT, position, 7u);
        else if (i == 5u) ordered(text, DOE_COMPILER_INTERFACE_INPUT, subset, 6u);
        save(directory, names[i], text);
        free(text);
        assert(memcmp(before, stages->interfaces, bytes) == 0);
    }
    free(before);
    release(fixture);
    fixture = prepare_graphics("struct Out{@location(999) x:f32,@builtin(position) p:vec4f}"
        "@vertex fn high()->Out{return Out(7.0,vec4f(0,0,0,1));}");
    char *text = emit(fixture, 0u);
    has(text, "TEXCOORD999");
    has(text, "#error HLSL interstage projection location exceeds the 32-bit profile");
    save(directory, "high", text);
    free(text);
    release(fixture);
    puts("HLSL linkage: canonical typed carrier order, logical identities and bounded projection guards passed.");
}
