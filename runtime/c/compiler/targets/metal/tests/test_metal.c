/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "fixture.h"

static void test_residual(void) {
    const char *source = "struct Params { count:u32, }"
        "@group(0) @binding(0) var<storage,read> left:array<f32>;"
        "@group(0) @binding(1) var<storage,read> right:array<f32>;"
        "@group(0) @binding(2) var<storage,read_write> output:array<f32>;"
        "@group(0) @binding(3) var<uniform> params:Params;"
        "@compute @workgroup_size(64) fn residual(@builtin(global_invocation_id) id:vec3u){"
        "if(id.x<params.count){output[id.x]=left[id.x]+right[id.x];}}";
    Fixture *f = prepare(source);
    char *text = emit(f, NO_ID);
    has(text, "host_name(\"residual\")");
    has(text, "sizes [[buffer(30)]]");
    has(text, "constant uchar* r3 [[buffer(3)]]");
    has(text, "doe_load4(ctx.r0");
    has(text, "doe_load4(ctx.r1");
    has(text, "doe_store4(ctx.r2");
    has(text, "thread_position_in_grid");
    free(text);
    release(f);
}
static void test_layout(void) {
    const char *source = "struct Item { @align(16) a:vec3f, b:f32, @size(16) c:u32, }"
        "struct Buffer { prefix:vec4u, data:array<Item>, }"
        "@group(1) @binding(2) var<storage,read_write> storage:Buffer;"
        "@compute @workgroup_size(1) fn layout_test(){let x=storage.data[0];storage.data[1]=x;}";
    Fixture *f = prepare(source);
    char *text = emit(f, NO_ID);
    has(text, "r0 [[buffer(18)]]");
    has(text, "packed_float3");
    has(text, "value.m1 = doe_load4(p + 12ul)");
    has(text, "ulong(ctx.sizes[18]) >= 16ul");
    has(text, ") / 32ul : 0ul)");
    free(text);
    release(f);
}
static void test_workgroup(void) {
    const char *source = "var<workgroup> tile:array<u32,4>;"
        "@group(0) @binding(0) var<storage,read_write> output:array<u32>;"
        "@compute @workgroup_size(4) fn group(@builtin(local_invocation_index) lane:u32){"
        "tile[lane]=lane;workgroupBarrier();output[lane]=tile[3u-lane];}";
    Fixture *f = prepare(source);
    char *text = emit(f, NO_ID);
    has(text, "threadgroup array<uint, 4> w0;");
    has(text, "if (input4 == 0u)");
    has(text, "threadgroup_barrier(mem_flags::mem_threadgroup)");
    has(text, "doe_wstore3(&((*ctx.w0)");
    has(text, "doe_wload3(&((*ctx.w0)");
    free(text);
    release(f);
}
static void test_vectors_and_builtins(void) {
    const char *source = "@group(0) @binding(0) var<storage,read_write> output:array<f32>;"
        "fn arithmetic(x:vec4f)->vec4f{return normalize(x)+clamp(x,vec4f(1),vec4f(4));}"
        "@compute @workgroup_size(4) fn vector_test(@builtin(global_invocation_id) id:vec3u,"
        "@builtin(local_invocation_id) local:vec3u,@builtin(workgroup_id) group:vec3u,"
        "@builtin(num_workgroups) groups:vec3u,@builtin(local_invocation_index) lane:u32){"
        "let x=arithmetic(vec4f(f32(id.x),2,3,4));let y=x.wzyx;"
        "output[lane]=dot(x,y)+length(x)+distance(x,y)+exp(x.x)+tanh(x.y)+sqrt(x.z);"
        "output[local.x]=f32(group.x+groups.x);"
        "let numbers=array<vec2i,2>(vec2i(1),vec2i(2));let index=lane;"
        "let item=numbers[index];output[3]=f32(dot(item,vec2i(7)));}";
    Fixture *f = prepare(source);
    char *text = emit(f, NO_ID);
    has(text, "normalize(");
    has(text, ".wzyx");
    has(text, "threadgroups_per_grid");
    has(text, "threadgroup_position_in_grid");
    has(text, "thread_position_in_threadgroup");
    has(text, "uint(");
    free(text);
    release(f);
}
static void test_entries_and_rejection(void) {
    const char *source = "@group(0) @binding(0) var<storage,read_write> first:array<u32>;"
        "@group(0) @binding(0) var<storage,read_write> second:array<u32>;"
        "@compute @workgroup_size(1) fn alpha(){first[0]=7u;}"
        "@compute @workgroup_size(1) fn beta(){second[0]=9u;}";
    Fixture *f = prepare(source);
    char *text = emit(f, NO_ID);
    has(text, "host_name(\"alpha\")");
    has(text, "host_name(\"beta\")");
    free(text);
    text = emit(f, 1u);
    assert(strstr(text, "host_name(\"alpha\")") == NULL);
    has(text, "host_name(\"beta\")");
    free(text);
    f->module->resources[1].binding = 30u;
    char untouched[32];
    memset(untouched, 0x5a, sizeof(untouched));
    DoeMetalResult result;
    assert(doe_metal_emit(&f->source, f->module, 1u, untouched, sizeof(untouched), &result) == DOE_COMPILER_UNSUPPORTED);
    assert(result.required_bytes == 0u && result.written_bytes == 0u && untouched[0] == 0x5a);
    has(f->diagnostic.message, "valid sized resource root");
    release(f);
}
static void test_control_and_host_fixture(const char *output_path) {
    const char *source = "@group(0) @binding(0) var<storage,read_write> output:array<i32>;"
        "fn bump(x:i32)->i32{output[0]=output[0]+1;return x+1;}"
        "@compute @workgroup_size(1) fn control(){"
        "var x:i32=0;var sum:i32=0;"
        "for(var i:i32=0;i<5;i++){if(i==2){continue;}sum+=i;}"
        "loop{x+=1;if(x==2){continue;}sum+=x;continuing {break if x>=3;}}"
        "let skip=false && bump(9)>0;"
        "let call=(sum>0) && bump(sum)>0;"
        "let maximum:i32=2147483647;let minimum:i32=-2147483648;let zero:i32=0;let negative:i32=-1;output[1]=sum;output[2]=maximum+1;"
        "output[3]=minimum/negative;output[4]=(-7)/zero;"
        "output[5]=output[negative];output[6]=bump(5);}";
    Fixture *f = prepare(source);
    char *text = emit(f, NO_ID);
    has(text, "do {\n");
    has(text, "} while (false);");
    has(text, "doe_div(");
    has(text, "as_type<int>((as_type<uint>(");
    has(text, " >= 0)");
    if (output_path != NULL) {
        FILE *file = fopen(output_path, "wb");
        assert(file != NULL);
        assert(fwrite(text, 1u, strlen(text), file) == strlen(text));
        assert(fclose(file) == 0);
    }
    /* Mutation control: an obsolete but well-typed opcode must not become fake output. */
    f->module->instructions[0].opcode = IR_MAX;
    char untouched[32];
    memset(untouched, 0x5a, sizeof(untouched));
    DoeMetalResult result;
    assert(doe_metal_emit(&f->source, f->module, NO_ID, untouched, sizeof(untouched), &result) == DOE_COMPILER_UNSUPPORTED);
    assert(untouched[0] == 0x5a && result.written_bytes == 0u);
    free(text);
    release(f);
}
static void test_aggregate_host_fixture(const char *output_path) {
    const char *source = "struct Data { @align(16) first:u32, @size(16) second:u32, tail:array<u32,3>, }"
        "@group(0) @binding(0) var<storage,read_write> output:array<i32>;"
        "@group(0) @binding(1) var<storage,read_write> data:Data;"
        "@compute @workgroup_size(1) fn aggregate_test(){var value=data;"
        "output[0]=i32(value.first+value.second+value.tail[2]);"
        "value.first+=3u;value.tail[1]=8u;data=value;"
        "let negative=-1;output[1]=i32(value.tail[negative]);}";
    Fixture *f = prepare(source);
    char *text = emit(f, NO_ID);
    has(text, "p + 20ul");
    if (output_path != NULL) {
        FILE *file = fopen(output_path, "wb");
        assert(file != NULL);
        assert(fwrite(text, 1u, strlen(text), file) == strlen(text));
        assert(fclose(file) == 0);
    }
    free(text);
    release(f);
}
static void test_unimplemented_subgroups(void) {
    static const char *const operations[] = {
        "subgroupAdd", "subgroupMin", "subgroupMax", "subgroupMul", "subgroupAnd",
        "subgroupOr", "subgroupXor", "subgroupExclusiveAdd", "subgroupInclusiveAdd",
        "subgroupExclusiveMul", "subgroupInclusiveMul"
    };
    static const char *const inputs[] = {
        "subgroup_size", "subgroup_invocation_id", "num_subgroups", "subgroup_id"
    };
    for (size_t i = 0u; i < 15u; ++i) {
        char source[512];
        int length;
        if (i < 11u) {
            length = snprintf(source, sizeof(source),
                "enable subgroups; @compute @workgroup_size(4) fn main(){let value=%s(1u);}",
                operations[i]);
        } else {
            length = snprintf(source, sizeof(source),
                "enable subgroups; @group(0) @binding(0) var<storage,read_write> out:array<u32>;"
                "@compute @workgroup_size(4) fn main(@builtin(%s) value:u32){out[0]=value;}",
                inputs[i - 11u]);
        }
        assert(length > 0 && (size_t)length < sizeof(source));
        Fixture *f = prepare(source);
        char untouched[64];
        memset(untouched, 0x5a, sizeof(untouched));
        DoeMetalResult result;
        const DoeCompilerStatus status = doe_metal_emit(&f->source, f->module, NO_ID,
                                                       untouched, sizeof(untouched), &result);
        if (status != DOE_COMPILER_UNSUPPORTED)
            fprintf(stderr, "subgroup rejection %zu returned %d: %s\n", i, (int)status, f->diagnostic.message);
        assert(status == DOE_COMPILER_UNSUPPORTED);
        assert(result.required_bytes == 0u && result.written_bytes == 0u);
        for (size_t j = 0u; j < sizeof(untouched); ++j) assert(untouched[j] == 0x5a);
        has(f->diagnostic.message, "no implemented lowering");
        release(f);
    }
}
int main(int argc, char **argv) {
    uint32_t slot = NO_ID;
    assert(doe_metal_binding_slot(1u, 13u, &slot) && slot == 29u);
    assert(!doe_metal_binding_slot(1u, 14u, &slot));
    assert(!doe_metal_binding_slot(0u, 16u, &slot));
    assert(!doe_metal_binding_slot(UINT32_MAX, UINT32_MAX, &slot));
    test_unimplemented_subgroups();
    test_residual();
    test_layout();
    test_workgroup();
    test_vectors_and_builtins();
    test_entries_and_rejection();
    test_control_and_host_fixture(argc > 1 ? argv[1] : NULL);
    test_aggregate_host_fixture(argc > 2 ? argv[2] : NULL);
    puts("Metal developer checks passed: real source lowering, layout, entries, workgroups, control, atomic rejection.");
    return 0;
}
