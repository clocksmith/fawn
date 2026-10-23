/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "fixture.h"

static void fail_untouched(Fixture *f, const char *diagnostic) {
    char output[128];
    memset(output, 0x5a, sizeof(output));
    DoeMetalResult result;
    assert(doe_metal_emit(&f->source, f->module, NO_ID, output, sizeof(output), &result) == DOE_COMPILER_UNSUPPORTED);
    assert(result.required_bytes == 0u && result.written_bytes == 0u);
    for (size_t i = 0u; i < sizeof(output); ++i) assert(output[i] == 0x5a);
    has(f->diagnostic.message, diagnostic);
}
static void test_shapes(void) {
    static const char *const wgsl[] = {"1d", "2d", "2d_array", "cube", "cube_array", "3d"};
    static const char *const msl[] = {"texture1d", "texture2d", "texture2d_array", "texturecube", "texturecube_array", "texture3d"};
    static const char *const scalar[] = {"f32", "i32", "u32"};
    static const char *const loads[] = {"0", "vec2i(0)", "vec2i(0),0", "", "", "vec3i(0)"};
    static const char *const samples[] = {"0.5", "vec2f(0.5)", "vec2f(0.5),0", "vec3f(0.5)", "vec3f(0.5),0", "vec3f(0.5)"};
    for (size_t dimension = 0u; dimension < 6u; ++dimension) {
        for (size_t type = 0u; type < 3u; ++type) {
            char source[1024], load[128] = "", sample[128] = "";
            if (dimension != 3u && dimension != 4u)
                assert(snprintf(load, sizeof(load), "let v=textureLoad(t,%s,0);", loads[dimension]) > 0);
            if (type == 0u)
                assert(snprintf(sample, sizeof(sample), "let q=textureSampleLevel(t,s,%s,1.5);", samples[dimension]) > 0);
            int length = snprintf(source, sizeof(source),
                "@group(0) @binding(0) var t:texture_%s<%s>;@group(0) @binding(1) var s:sampler;"
                "@compute @workgroup_size(1) fn main(){let d=textureDimensions(t,0);let n=textureNumLevels(t);%s%s%s}",
                wgsl[dimension], scalar[type], dimension == 2u || dimension == 4u ? "let a=textureNumLayers(t);" : "", load, sample);
            assert(length > 0 && (size_t)length < sizeof(source));
            Fixture *f = prepare(source);
            char *text = emit(f, NO_ID);
            has(text, msl[dimension]);
            has(text, "[[texture(0)]]");
            has(text, "image_levels");
            if (dimension == 0u) {
                has(text, "uint image_levels = 1u;");
                assert(strstr(text, ".get_num_mip_levels()") == NULL);
            }
            free(text);
            release(f);
        }
    }
    static const char *const depth[] = {"2d", "2d_array", "cube", "cube_array", "multisampled_2d"};
    for (size_t i = 0u; i < 5u; ++i) {
        char source[1024];
        int length = snprintf(source, sizeof(source),
            "@group(0) @binding(0) var t:texture_depth_%s;"
            "@compute @workgroup_size(1) fn main(){let d=textureDimensions(t);%s}", depth[i],
            i == 4u ? "let n=textureNumSamples(t);let v=textureLoad(t,vec2i(0),0);" :
            i == 0u ? "let v=textureLoad(t,vec2i(0),0);" :
            i == 1u ? "let v=textureLoad(t,vec2i(0),0,0);" : "let n=textureNumLevels(t);");
        assert(length > 0 && (size_t)length < sizeof(source));
        Fixture *f = prepare(source);
        char *text = emit(f, NO_ID);
        has(text, "depth");
        free(text);
        release(f);
    }
}
static void test_storage_formats(void) {
    static const char *const formats[] = {"rgba32float", "rgba16float", "r32float", "rgba8unorm", "rgba8snorm", "rg32float",
        "rgba32sint", "rgba16sint", "rgba8sint", "r32sint", "rg32sint", "rgba32uint", "rgba16uint", "rgba8uint", "r32uint", "rg32uint"};
    static const char *const dimensions[] = {"1d", "2d", "2d_array", "3d"};
    static const char *const coordinates[] = {"0", "vec2i(0)", "vec2i(0),0", "vec3i(0)"};
    static const char *const access[] = {"read", "write", "read_write"};
    for (size_t f = 0u; f < 16u; ++f) for (size_t d = 0u; d < 4u; ++d) for (size_t a = 0u; a < 3u; ++a) {
        char source[1024], operation[256];
        const char *value = f < 6u ? "vec4f(1)" : f < 11u ? "vec4i(1)" : "vec4u(1)";
        int length = snprintf(operation, sizeof(operation), a == 0u ? "let v=textureLoad(t,%s);" :
            "textureStore(t,%s,%s);", coordinates[d], value);
        assert(length > 0 && (size_t)length < sizeof(operation));
        length = snprintf(source, sizeof(source),
            "@group(0) @binding(0) var t:texture_storage_%s<%s,%s>;"
            "@compute @workgroup_size(1) fn main(){%s let d=textureDimensions(t);}",
            dimensions[d], formats[f], access[a], operation);
        assert(length > 0 && (size_t)length < sizeof(source));
        Fixture *fixture = prepare(source);
        char *text = emit(fixture, NO_ID);
        char expected[40];
        length = snprintf(expected, sizeof(expected), "access::%s>", access[a]);
        assert(length > 0 && (size_t)length < sizeof(expected));
        has(text, expected);
        has(text, a == 0u ? ".read(" : ".write(");
        /* Writable handles are function parameters, never local zero values. */
        assert(strstr(text, "ctx.r0") == NULL);
        free(text);
        release(fixture);
    }
}
static void test_bindings_and_handles(void) {
    Fixture *f = prepare("@group(0) @binding(2) var a:texture_2d<f32>;"
        "@group(0) @binding(2) var b:texture_3d<u32>;"
        "@group(1) @binding(14) var far:texture_1d<i32>;"
        "fn leaf(t:texture_2d<f32>)->vec4f{return textureLoad(t,vec2i(0),0);}"
        "fn nested()->vec4f{return leaf(a);}"
        "@compute @workgroup_size(1) fn first(){let x=nested();}"
        "@compute @workgroup_size(1) fn second(){let x=textureLoad(b,vec3i(0),0);let q=textureDimensions(far);}");
    char *text = emit(f, NO_ID);
    has(text, "texture2d<float, access::sample> r0 [[texture(2)]]");
    has(text, "texture3d<uint, access::sample> r1 [[texture(2)]]");
    has(text, "texture1d<int, access::sample> r2 [[texture(30)]]");
    assert(strstr(text, "ctx.r0") == NULL && strstr(text, "ctx.r1") == NULL);
    free(text);
    text = emit(f, 0u);
    has(text, "host_name(\"first\")");
    assert(strstr(text, "host_name(\"second\")") == NULL);
    assert(strstr(text, "[[texture(30)]]") == NULL);
    free(text);
    release(f);
    f = prepare("@group(0) @binding(15) var s:sampler;"
        "@group(0) @binding(0) var t:texture_2d<f32>;"
        "fn sample(t:texture_2d<f32>,s:sampler)->vec4f{return textureSampleLevel(t,s,vec2f(0.5),0.0);}"
        "@compute @workgroup_size(1) fn main(){let q=sample(t,s);}");
    text = emit(f, NO_ID);
    has(text, "sampler r0 [[sampler(15)]]");
    free(text);
    f->module->resources[0].group = 1u;
    f->module->resources[0].binding = 0u;
    fail_untouched(f, "sampler binding exceeds");
    release(f);
    /* An unused declaration has no native binding; an empty selected entry is legal. */
    f = prepare("@group(3) @binding(31) var unused:sampler_comparison;"
        "@compute @workgroup_size(1) fn empty(){}");
    text = emit(f, NO_ID);
    assert(strstr(text, "[[sampler(") == NULL);
    free(text);
    f->module->functions[0].flags |= FUNCTION_VERTEX;
    fail_untouched(f, "graphics stages");
    release(f);
}
static void test_mutations(void) {
    static const char *const source = "@group(0) @binding(0) var t:texture_2d<f32>;"
        "@compute @workgroup_size(1) fn main(){let v=textureLoad(t,vec2i(0),0);}";
    Fixture *f = prepare(source);
    uint32_t image = NO_ID, handle = NO_ID;
    for (uint32_t i = 0u; i < f->module->instruction_count; ++i) {
        if (f->module->instructions[i].opcode == IR_IMAGE) image = i;
        if (f->module->instructions[i].opcode == IR_HANDLE) handle = i;
    }
    assert(image != NO_ID && handle != NO_ID);
    const IrInstruction original = f->module->instructions[image];
    f->module->instructions[image].operands.composite.count = 1u;
    fail_untouched(f, "operand count");
    f->module->instructions[image] = original;
    f->module->instructions[image].operands.composite.reserved = IR_F32;
    fail_untouched(f, "type or operand range");
    f->module->instructions[image] = original;
    f->module->instructions[handle].operands.scalar.binding = NO_ID;
    fail_untouched(f, "declared typed resource");
    release(f);
}
static void host_fixture(const char *path) {
    const char *source =
        "struct Params{x:i32,y:i32,z:i32,layer:i32,lod:i32,sample:i32,coord:f32,level:f32,}"
        "@group(0) @binding(0) var<storage,read_write> out:array<i32>;"
        "@group(0) @binding(1) var<uniform> p:Params;"
        "@group(0) @binding(2) var tex:texture_2d_array<f32>;"
        "@group(0) @binding(3) var smp:sampler;"
        "@group(0) @binding(4) var dst:texture_storage_2d_array<r32float,read_write>;"
        "@group(0) @binding(5) var ms:texture_multisampled_2d<u32>;"
        "@group(0) @binding(6) var dep:texture_depth_2d;"
        "@group(0) @binding(7) var one:texture_1d<f32>;"
        "@group(0) @binding(8) var volume:texture_3d<i32>;"
        "@group(0) @binding(9) var cube:texture_cube<f32>;"
        "@group(0) @binding(10) var cubes:texture_cube_array<f32>;"
        "fn effect(value:i32)->i32{out[20]=out[20]+1;out[21]=out[21]*10+value;return value;}"
        "fn read_helper(t:texture_2d_array<f32>,c:vec2i,a:i32,l:i32)->vec4f{return textureLoad(t,c,a,l);}"
        "fn write_helper(t:texture_storage_2d_array<r32float,read_write>,c:vec2i,a:i32,v:vec4f){textureStore(t,c,a,v);}"
        "@compute @workgroup_size(1) fn image_control(){"
        "out[0]=i32(read_helper(tex,vec2i(p.x,p.y),p.layer,p.lod)[0]);"
        "out[1]=i32(textureDimensions(tex,p.lod)[0]);out[2]=i32(textureDimensions(tex,p.lod)[1]);"
        "out[3]=i32(textureNumLevels(tex));out[4]=i32(textureNumLayers(tex));out[5]=i32(textureNumSamples(ms));"
        "out[6]=i32(textureLoad(ms,vec2i(p.x,p.y),p.sample)[0]);"
        "out[7]=i32(textureLoad(dep,vec2i(p.x,p.y),p.lod));"
        "write_helper(dst,vec2i(p.x,p.y),p.layer,vec4f(7,8,9,10));"
        "out[8]=i32(textureLoad(dst,vec2i(p.x,p.y),p.layer)[0]);"
        "out[9]=i32(textureSampleLevel(tex,smp,vec2f(p.coord),p.layer,p.level)[0]);"
        "out[10]=i32(textureLoad(one,p.x,p.lod)[0]);out[11]=i32(textureNumLevels(one));"
        "out[12]=i32(textureSampleLevel(one,smp,p.coord,p.level)[0]);"
        "out[13]=textureLoad(volume,vec3i(p.x,p.y,p.z),p.lod)[0];"
        "out[14]=i32(textureDimensions(volume,p.lod)[2]);"
        "out[15]=i32(textureSampleLevel(cube,smp,vec3f(p.coord),p.level)[0]);"
        "out[16]=i32(textureSampleLevel(cubes,smp,vec3f(p.coord),p.layer,p.level)[0]);"
        "out[17]=i32(textureDimensions(cube,p.lod)[0]);"
        "out[22]=i32(textureLoad(tex,vec2i(p.x,p.y),p.layer,p.lod)[3]);"
        "out[23]=i32(textureLoad(dst,vec2i(p.x,p.y),p.layer)[3]);"
        "let ordered=textureLoad(tex,vec2i(effect(1),effect(2)),effect(3),effect(4));"
        "textureStore(dst,vec2i(effect(1),effect(2)),effect(3),vec4f(f32(effect(4))));"
        "for(var i:i32=0;i<2;i++){let value=textureLoad(tex,vec2i(0),0,i*99);out[18+i]=i32(value[0]);}"
        "}"
        "@compute @workgroup_size(1) fn image_unsigned(){"
        "out[0]=i32(textureLoad(tex,vec2u(bitcast<u32>(p.x),bitcast<u32>(p.y)),bitcast<u32>(p.layer),bitcast<u32>(p.lod))[0]);"
        "out[1]=i32(textureDimensions(tex,bitcast<u32>(p.lod))[0]);"
        "out[2]=i32(textureLoad(ms,vec2u(bitcast<u32>(p.x),bitcast<u32>(p.y)),bitcast<u32>(p.sample))[0]);"
        "out[3]=i32(textureSampleLevel(tex,smp,vec2f(p.coord),bitcast<u32>(p.layer),p.level)[0]);"
        "}"
        "@group(0) @binding(2) var alias_tex:texture_2d_array<f32>;"
        "@compute @workgroup_size(1) fn image_alias(){out[0]=i32(textureLoad(alias_tex,vec2i(0),0,0)[0]);}";
    Fixture *f = prepare(source);
    char *text = emit(f, NO_ID);
    has(text, "image_layers - 1u");
    has(text, ".sample(");
    has(text, "access::read_write");
    has(text, "if (");
    if (path != NULL) {
        FILE *file = fopen(path, "wb");
        assert(file != NULL);
        assert(fwrite(text, 1u, strlen(text), file) == strlen(text));
        assert(fclose(file) == 0);
    }
    free(text);
    release(f);
}
int main(int argc, char **argv) {
    test_shapes();
    test_storage_formats();
    test_bindings_and_handles();
    test_mutations();
    host_fixture(argc > 1 ? argv[1] : NULL);
    puts("Metal image developer controls passed: seven typed operations, 16 formats, dimensions, closures, atomic rejection.");
    return 0;
}
