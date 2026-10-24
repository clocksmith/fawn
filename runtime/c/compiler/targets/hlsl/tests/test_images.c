/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "test_support.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void sampled_helpers(void) {
    const char *source = "@group(0) @binding(0) var image:texture_2d<f32>;"
        "@group(0) @binding(1) var sampling:sampler;"
        "@group(0) @binding(2) var<storage,read_write> output:array<vec4f>;"
        "fn fetch(tex:texture_2d<f32>,coord:vec2i,level:i32)->vec4f{return textureLoad(tex,coord,level);}"
        "fn sample(tex:texture_2d<f32>,s:sampler,coord:vec2f)->vec4f{return textureSampleLevel(tex,s,coord,0); }"
        "@compute @workgroup_size(1) fn sampled(@builtin(global_invocation_id) id:vec3u){"
        "let xy=vec2i(id.xy);let lod=i32(id.z)-1;output[0]=fetch(image,xy,lod);"
        "output[1]=sample(image,sampling,vec2f(0.5));let dims=textureDimensions(image,lod);"
        "output[2]=vec4f(f32(dims.x),f32(dims.y),f32(textureNumLevels(image)),0);}";
    Fixture *f = prepare(source);
    char *text = emit(f, 0u);
    has(text, "Texture2D<float4> r0 : register(t0, space0)");
    has(text, "SamplerState r1 : register(s1, space0)");
    has(text, "Texture2D<float4> v");
    has(text, "SamplerState v");
    has(text, ".GetDimensions(0u, image_x, image_y, image_levels)");
    has(text, ".GetDimensions(uint(");
    has(text, ".Load(int3(");
    has(text, ".SampleLevel(");
    has(text, ") < image_levels");
    has(text, ") < image_x");
    has(text, ") < image_y");
    assert(strstr(text, "Texture2D<float4> v1 =") == NULL);
    for (uint32_t i = 0u; i < f->module->instruction_count; ++i) {
        if (f->module->instructions[i].opcode != IR_IMAGE) continue;
        f->module->instructions[i].operands.composite.kind = UINT32_MAX;
        break;
    }
    DoeHlslResult result;
    char untouched[8] = "intact";
    assert(doe_hlsl_emit(&f->source, f->module, 0u, f->scratch, untouched, sizeof(untouched), &result) == DOE_COMPILER_UNSUPPORTED);
    assert(strcmp(untouched, "intact") == 0 && result.written_bytes == 0u);
    free(text);
    release(f);
}
static void storage_array(void) {
    const char *source = "@group(0) @binding(0) var image:texture_storage_2d_array<r32float,read_write>;"
        "@group(0) @binding(1) var<storage,read_write> output:array<vec4f>;"
        "@compute @workgroup_size(1) fn storage_image(@builtin(global_invocation_id) id:vec3u){"
        "let xy=vec2i(id.xy);let layer=i32(id.z)-1;let value=textureLoad(image,xy,layer);"
        "textureStore(image,xy,layer,value+vec4f(1));output[0]=value;"
        "output[1]=vec4f(f32(textureNumLayers(image)),0,0,0);}";
    Fixture *f = prepare(source);
    char *text = emit(f, 0u);
    has(text, "RWTexture2DArray<float> r0 : register(u0, space0)");
    has(text, ".GetDimensions(image_x, image_y, image_layers)");
    has(text, "float image_texel = r0[uint3(");
    has(text, "float4(image_texel, 0, 0, 1)");
    has(text, ") < image_layers");
    assert(strstr(text, "r0.Load(") == NULL);
    free(text);
    release(f);
}
static void depth_multisampling(void) {
    const char *source = "@group(0) @binding(0) var depth:texture_depth_multisampled_2d;"
        "@group(0) @binding(1) var image:texture_multisampled_2d<i32>;"
        "@group(0) @binding(2) var<storage,read_write> output:array<vec4i>;"
        "@compute @workgroup_size(1) fn multisampled(@builtin(global_invocation_id) id:vec3u){"
        "let xy=vec2i(id.xy);let sample_index=i32(id.z)-1;"
        "output[0]=textureLoad(image,xy,sample_index);"
        "output[1]=vec4i(i32(textureLoad(depth,xy,sample_index)));"
        "output[2]=vec4i(i32(textureNumSamples(image)),0,0,0);}";
    Fixture *f = prepare(source);
    char *text = emit(f, 0u);
    has(text, "Texture2DMS<float> r0");
    has(text, "Texture2DMS<int4> r1");
    has(text, ".GetDimensions(image_x, image_y, image_samples)");
    has(text, ") < image_samples");
    has(text, ".Load(int2(");
    free(text);
    release(f);
}
static void cube_sampling(void) {
    const char *source = "@group(0) @binding(0) var image:texture_cube_array<f32>;"
        "@group(0) @binding(1) var sampling:sampler;"
        "@group(0) @binding(2) var<storage,read_write> output:array<vec4f>;"
        "@compute @workgroup_size(1) fn cubes(@builtin(global_invocation_id) id:vec3u){"
        "let layer=i32(id.x)-1;output[0]=textureSampleLevel(image,sampling,vec3f(1,0,0),layer,0);"
        "let size=textureDimensions(image);output[1]=vec4f(f32(size.x),f32(size.y),f32(textureNumLayers(image)),0);}";
    Fixture *f = prepare(source);
    char *text = emit(f, 0u);
    has(text, "TextureCubeArray<float4> r0");
    has(text, ".GetDimensions(0u, image_x, image_y, image_layers, image_levels)");
    has(text, " < 0 ? 0u : min(uint(");
    has(text, "image_layers - 1u)");
    has(text, "float(image_layer)");
    has(text, "uint2(image_x, image_y)");
    free(text);
    release(f);
}
static void storage_formats(void) {
    static const struct { const char *format, *scalar, *native; } cases[] = {
        {"r32float", "f", "float"}, {"rg32float", "f", "float2"}, {"rgba32float", "f", "float4"},
        {"r32sint", "i", "int"}, {"rg32sint", "i", "int2"}, {"rgba8sint", "i", "int4"},
        {"r32uint", "u", "uint"}, {"rg32uint", "u", "uint2"}, {"rgba16uint", "u", "uint4"}
    };
    for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        char source[512], expected[64];
        const int length = snprintf(source, sizeof(source),
            "@group(0) @binding(0) var image:texture_storage_2d<%s,read_write>;"
            "@compute @workgroup_size(1) fn format_case(){"
            "let value=textureLoad(image,vec2i(0));textureStore(image,vec2i(1),value+vec4%s(1));}",
            cases[i].format, cases[i].scalar);
        assert(length > 0 && (size_t)length < sizeof(source));
        Fixture *f = prepare(source);
        char *text = emit(f, 0u);
        assert(snprintf(expected, sizeof(expected), "RWTexture2D<%s> r0", cases[i].native) > 0);
        has(text, expected);
        free(text);
        release(f);
    }
}
static void remaining_dimensions(void) {
    static const struct { const char *type, *coords, *native, *load; } cases[] = {
        {"texture_1d<u32>", "0", "Texture1D<uint4>", ".Load(int2("},
        {"texture_3d<u32>", "vec3i(0)", "Texture3D<uint4>", ".Load(int4("},
        {"texture_2d_array<u32>", "vec2i(0),0", "Texture2DArray<uint4>", ".Load(int4("},
        {"texture_depth_2d_array", "vec2i(0),0", "Texture2DArray<float>", ".Load(int4("}
    };
    for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        char source[512];
        const int length = snprintf(source, sizeof(source),
            "@group(0) @binding(0) var image:%s;"
            "@compute @workgroup_size(1) fn dimensions_case(){let value=textureLoad(image,%s,0);}",
            cases[i].type, cases[i].coords);
        assert(length > 0 && (size_t)length < sizeof(source));
        Fixture *f = prepare(source);
        char *text = emit(f, 0u);
        has(text, cases[i].native);
        has(text, cases[i].load);
        free(text);
        release(f);
    }
    const char *source = "@group(0) @binding(0) var input_image:texture_storage_1d<r32uint,read>;"
        "@group(0) @binding(1) var output_image:texture_storage_3d<rg32uint,write>;"
        "@compute @workgroup_size(1) fn write_image(){"
        "let value=textureLoad(input_image,0);textureStore(output_image,vec3i(0),value);}";
    Fixture *f = prepare(source);
    char *text = emit(f, 0u);
    has(text, "Texture1D<uint> r0");
    has(text, "RWTexture3D<uint2> r1");
    has(text, "r0.GetDimensions(0u, image_x, image_levels)");
    has(text, "r1.GetDimensions(image_x, image_y, image_z)");
    has(text, "uint4(image_texel, 0, 0, 1)");
    free(text);
    release(f);
}
static void entry_handle_identity(void) {
    const char *source = "@group(2) @binding(3) var first_image:texture_1d<u32>;"
        "@group(2) @binding(3) var second_image:texture_2d<u32>;"
        "fn ignore(tex:texture_1d<u32>)->u32{return 1;}"
        "@compute @workgroup_size(1) fn first_entry(){let value=ignore(first_image);}"
        "@compute @workgroup_size(1) fn second_entry(){let size=textureDimensions(second_image);}";
    Fixture *f = prepare(source);
    char *first = emit(f, 0u), *second = emit(f, 1u);
    has(first, "Texture1D<uint4> r0 : register(t3, space2)");
    has(second, "Texture2D<uint4> r1 : register(t3, space2)");
    assert(strstr(first, "Texture2D<") == NULL);
    assert(strstr(second, "Texture1D<") == NULL);
    has(first, "(ctx, r0)");
    free(first);
    free(second);
    release(f);
}
static void image_guard_fixture(const char *output_path) {
    const char *source = "@group(0) @binding(0) var image:texture_depth_2d;"
        "@group(0) @binding(1) var<storage,read_write> output:array<f32>;"
        "fn fetch(tex:texture_depth_2d,coord:vec2i,lod:i32)->f32{return textureLoad(tex,coord,lod);}"
        "fn width_at(lod:i32)->u32{let size=textureDimensions(image,lod);return size.x;}"
        "@compute @workgroup_size(1) fn image_guard(){let neg=-1;"
        "output[0]=fetch(image,vec2i(1,2),0);output[1]=fetch(image,vec2i(neg,2),0);"
        "output[2]=fetch(image,vec2i(1,2),neg);output[3]=fetch(image,vec2i(1,2),5);"
        "output[4]=fetch(image,vec2i(2,2),1);output[5]=f32(width_at(neg));output[6]=f32(width_at(1));}";
    Fixture *f = prepare(source);
    char *text = emit(f, 0u);
    if (output_path != NULL) {
        FILE *file = fopen(output_path, "wb");
        assert(file != NULL);
        assert(fwrite(text, 1u, strlen(text), file) == strlen(text));
        assert(fclose(file) == 0);
    }
    free(text);
    release(f);
}
void test_images(const char *output_path) {
    image_guard_fixture(output_path);
    entry_handle_identity();
    remaining_dimensions();
    sampled_helpers();
    storage_array();
    depth_multisampling();
    cube_sampling();
    storage_formats();
}
