/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "test_support.h"
#include "../../tests/array_length_source.h"
#include "../../tests/closure_source.h"
#include "../../../src/numeric.h"
#include "../../../src/stage.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const DoeCompilerModuleCapacities capacities = {
    .typed = {.source_bytes = 16384u, .tokens = 2048u, .statements = 256u, .expressions = 1024u,
        .types = 64u, .members = 128u, .resources = 32u, .access_steps = 1024u,
        .constants = 256u, .constant_edges = 512u, .ir_instructions = 4096u,
        .ir_operands = 1024u, .target_declarations = 2048u, .traversal_items = 193u,
        .words = 32768u, .expression_depth = 32u, .control_depth = 16u, .type_syntax_depth = 32u},
    .entries = 8u, .entry_inputs = 32u, .entry_references = 256u
};
static Fixture *prepare_mode(const char *text, bool graphics) {
    Fixture *f = calloc(1u, sizeof(*f));
    assert(f != NULL);
    CompilerLimits limits;
    size_t bytes = 0u, base = 0u;
    const DoeCompilerStageCapacities stage_caps = {.module = capacities,
        .interface_attributes = 256u, .stage_interfaces = 256u};
    if (graphics) assert(stage_layout(&stage_caps, &limits, &base, &bytes));
    else assert(compiler_module_layout(&capacities, &limits, &bytes));
    f->storage = malloc(bytes);
    assert(f->storage != NULL);
    f->module = f->storage;
    compiler_job_bind(f->module, f->storage, &limits);
    f->source = (Source){.source = text, .length = strlen(text), .diagnostic = &f->diagnostic,
        .limits = &f->module->limits, .compiler = f->module};
    if (graphics) stage_bind(&f->source, f->storage, &stage_caps, base);
    bool okay = lex_source(&f->source, f->module) && parse_tokens(&f->source, f->module);
    if (okay) {
        for (uint32_t i = 0u; i < f->module->expression_count; ++i)
            f->module->expression_references[i] = (SourceReference){SOURCE_NONE, NO_ID};
        for (uint32_t i = 0u; i < f->module->statement_count; ++i)
            f->module->statement_references[i] = (SourceReference){SOURCE_NONE, NO_ID};
        NumericEnvironment environment;
        okay = numeric_environment_begin(&f->source, &environment);
        if (okay) {
            okay = validate_syntax(&f->source, f->module);
            okay = numeric_environment_end(&f->source, &environment) && okay;
        }
    }
    if (!okay) fprintf(stderr, "source failed at %zu: %s\n%s\n", f->diagnostic.offset, f->diagnostic.message, text);
    assert(okay);
    assert(doe_target_scratch_bytes(f->module, &f->scratch.capacity));
    f->scratch.data = malloc(f->scratch.capacity);
    assert(f->scratch.data != NULL);
    return f;
}
Fixture *prepare(const char *text) { return prepare_mode(text, false); }
Fixture *prepare_graphics(const char *text) { return prepare_mode(text, true); }
void release(Fixture *f) {
    free(f->scratch.data);
    free(f->storage);
    free(f);
}
char *emit(Fixture *f, uint32_t selected) {
    DoeHlslResult measure;
    errno = EDOM;
    const DoeCompilerStatus status = doe_hlsl_emit(&f->source, f->module, selected, f->scratch, NULL, 0u, &measure);
    if (status != DOE_COMPILER_OK) fprintf(stderr, "Hlsl rejected at %zu: %s\n", f->diagnostic.offset, f->diagnostic.message);
    assert(status == DOE_COMPILER_OK);
    assert(errno == EDOM);
    assert(measure.required_bytes != 0u && measure.written_bytes == 0u);
    char *text = malloc(measure.required_bytes + 1u);
    assert(text != NULL);
    memset(text, 0xa5, measure.required_bytes + 1u);
    DoeHlslResult result;
    assert(doe_hlsl_emit(&f->source, f->module, selected, f->scratch, text, measure.required_bytes - 1u, &result) == DOE_COMPILER_OUTPUT_TOO_SMALL);
    assert(result.written_bytes == 0u && (unsigned char)text[0] == 0xa5u);
    assert(doe_hlsl_emit(&f->source, f->module, selected, f->scratch, text, measure.required_bytes, &result) == DOE_COMPILER_OK);
    assert(result.required_bytes == measure.required_bytes && result.written_bytes == measure.required_bytes);
    assert((unsigned char)text[measure.required_bytes] == 0xa5u);
    text[measure.required_bytes] = '\0';
    return text;
}
void has(const char *text, const char *needle) {
    if (strstr(text, needle) == NULL) fprintf(stderr, "missing emission: %s\n", needle);
    assert(strstr(text, needle) != NULL);
}
static void test_shared_call_dag(void) {
    char source[2048];
    assert(target_type_dag_source(source, sizeof(source)));
    Fixture *types = prepare(source);
    char *ordered = emit(types, 0u);
    has(ordered, "struct DoeT");
    free(ordered);
    release(types);
    assert(target_dag_source(source, sizeof(source)));
    Fixture *f = prepare(source);
    char *text = emit(f, 0u);
    has(text, "doe_fn24(");
    assert(strstr(text, "doe_fn25(") == NULL); /* unrelated function */
    free(text);
    DoeHlslResult result;
    unsigned char untouched[16];
    memset(untouched, 0xa5, sizeof(untouched));
    DoeTargetScratch short_scratch = f->scratch;
    --short_scratch.capacity;
    assert(doe_hlsl_emit(&f->source, f->module, 0u, short_scratch,
        (char *)untouched, sizeof(untouched), &result) == DOE_COMPILER_INPUT_ERROR);
    for (size_t i = 0u; i < sizeof(untouched); ++i) assert(untouched[i] == 0xa5u);
    text = emit(f, 0u); /* failure permits scratch reuse without reset */
    free(text);
    release(f);
}
static void test_helper_closure(void) {
    Fixture *f = prepare(target_closure_source);
    char *text = emit(f, 0u);
    has(text, "doe_r0_store3(uint offset");
    assert(strstr(text, "doe_r0_load3(uint offset") == NULL);
    assert(strstr(text, "doe_r0_store4(uint offset") == NULL);
    assert(strstr(text, "doe_r1_load") == NULL);
    assert(strstr(text, "doe_fn1(") == NULL);
    free(text);
    text = emit(f, 1u);
    has(text, "doe_r1_load3(uint offset");
    has(text, "doe_r1_load4(uint offset");
    has(text, "doe_r2_store4(uint offset");
    assert(strstr(text, "doe_r2_store3(uint offset") == NULL);
    assert(strstr(text, "doe_r0_") == NULL);
    free(text);
    release(f);
}
static void test_residual(void) {
    const char *source = "struct Params { count:u32, }"
        "@group(0) @binding(0) var<storage,read> left:array<f32>;"
        "@group(0) @binding(1) var<storage,read> right:array<f32>;"
        "@group(0) @binding(2) var<storage,read_write> output:array<f32>;"
        "@group(0) @binding(3) var<uniform> params:Params;"
        "@compute @workgroup_size(64) fn residual(@builtin(global_invocation_id) id:vec3u){"
        "if(id.x<params.count){output[id.x]=left[id.x]+right[id.x];}}";
    Fixture *f = prepare(source);
    char *text = emit(f, 0u);
    has(text, "void residual(");
    has(text, "r0.GetDimensions(count, stride)");
    has(text, "cbuffer DoeUniform3 : register(b3, space0)");
    has(text, "doe_r0_load4(offset)");
    has(text, "doe_r1_load4(offset)");
    has(text, "doe_r2_store4(offset,");
    has(text, "SV_DispatchThreadID");
    free(text);
    release(f);
}
static void test_layout(void) {
    const char *source = "struct Item { @align(16) a:vec3f, b:f32, @size(16) c:u32, }"
        "struct Buffer { prefix:vec4u, data:array<Item>, }"
        "@group(1) @binding(2) var<storage,read_write> storage:Buffer;"
        "@compute @workgroup_size(1) fn layout_test(){let x=storage.data[0];storage.data[1]=x;}";
    Fixture *f = prepare(source);
    char *text = emit(f, 0u);
    has(text, "r0 : register(u2, space1)");
    has(text, "asfloat(r0[(offset + 8u) / 4u])");
    has(text, "value.m1 = doe_r0_load4(offset + 12u)");
    has(text, "bytes >= 16u");
    has(text, ") / 32u : 0u)");
    free(text);
    release(f);
}
static void test_workgroup(void) {
    const char *source = "var<workgroup> tile:array<u32,4>;"
        "@group(0) @binding(0) var<storage,read_write> output:array<u32>;"
        "@compute @workgroup_size(4) fn group(@builtin(local_invocation_index) lane:u32){"
        "tile[lane]=lane;workgroupBarrier();output[lane]=tile[3u-lane];}";
    Fixture *f = prepare(source);
    char *text = emit(f, 0u);
    has(text, "groupshared DoeT");
    has(text, "if (input4 == 0u)");
    has(text, "GroupMemoryBarrierWithGroupSync()");
    has(text, "w0.elements[uint(");
    has(text, "w0.elements[uint(");
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
    char *text = emit(f, 0u);
    has(text, "normalize(");
    has(text, ".wzyx");
    has(text, "doe_num_workgroups");
    has(text, "SV_GroupID");
    has(text, "SV_GroupThreadID");
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
    has(text, "void alpha(");
    has(text, "void beta(");
    free(text);
    text = emit(f, 1u);
    assert(strstr(text, "void alpha(") == NULL);
    has(text, "void beta(");
    assert(strstr(text, "r0 : register(") == NULL);
    assert(strstr(text, "doe_fn0(") == NULL);
    free(text);
    f->module->resources[1].tail_stride = UINT64_MAX;
    char untouched[32];
    memset(untouched, 0x5a, sizeof(untouched));
    DoeHlslResult result;
    assert(doe_hlsl_emit(&f->source, f->module, 1u, f->scratch, untouched, sizeof(untouched), &result) == DOE_COMPILER_UNSUPPORTED);
    assert(result.required_bytes == 0u && result.written_bytes == 0u && untouched[0] == 0x5a);
    has(f->diagnostic.message, "32-bit byte addressing");
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
    char *text = emit(f, 0u);
    has(text, "do {\n");
    has(text, "} while (false);");
    has(text, "doe_div(");
    has(text, "a % (b == 0u ? 1u : b)");
    has(text, "asint((asuint(");
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
    DoeHlslResult result;
    assert(doe_hlsl_emit(&f->source, f->module, NO_ID, f->scratch, untouched, sizeof(untouched), &result) == DOE_COMPILER_UNSUPPORTED);
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
    char *text = emit(f, 0u);
    has(text, "offset + 20u");
    if (output_path != NULL) {
        FILE *file = fopen(output_path, "wb");
        assert(file != NULL);
        assert(fwrite(text, 1u, strlen(text), file) == strlen(text));
        assert(fclose(file) == 0);
    }
    free(text);
    release(f);
}
static void test_effects_host_fixture(const char *output_path) {
    const char *source = "@group(0) @binding(0) var<storage,read_write> output:array<i32>;"
        "@group(0) @binding(1) var<storage,read> indices:array<i32>;"
        "fn tick()->i32{let n=output[0];output[0]=n+1;return n;}"
        "fn pair(a:i32,b:i32)->i32{return a*100+b;}"
        "@compute @workgroup_size(1) fn effects(){output[0]=1;"
        "let pair_value=pair(tick(),tick());output[1]=pair_value;"
        "output[2]=tick()+tick();output[tick()]+=tick();"
        "let indexed=output[tick()];output[3]=indexed;"
        "let negative=-1;output[4]=output[indices[negative]];output[10]=tick();output[6]=output[10];}";
    Fixture *f = prepare(source);
    char *text = emit(f, 0u);
    has(text, "doe_r1_bytes()");
    has(text, "doe_fn0(ctx)");
    if (output_path != NULL) {
        FILE *file = fopen(output_path, "wb");
        assert(file != NULL);
        assert(fwrite(text, 1u, strlen(text), file) == strlen(text));
        assert(fclose(file) == 0);
    }
    free(text);
    release(f);
}
static void test_target_rejection(void) {
    static const char *const sources[] = {
        "@group(0) @binding(0) var<storage,read_write> output:array<u32>;"
        "@compute @workgroup_size(1) fn r0(){output[0]=1u;}",
        "struct Params{count:u32,} @group(1) @binding(0) var<uniform> params:Params;"
        "@group(0) @binding(0) var<storage,read_write> output:array<u32>;"
        "@compute @workgroup_size(1) fn dispatch(@builtin(num_workgroups) groups:vec3u){output[0]=groups.x+params.count;}",
        "@compute @workgroup_size(1,1,65) fn oversized(){}"
    };
    for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i) {
        Fixture *f = prepare(sources[i]);
        if (i == 1u) f->module->resources[0].group = 7u;
        char output[32] = "intact";
        DoeHlslResult result;
        assert(doe_hlsl_emit(&f->source, f->module, 0u, f->scratch, output, sizeof(output), &result) == DOE_COMPILER_UNSUPPORTED);
        assert(strcmp(output, "intact") == 0 && result.required_bytes == 0u);
        release(f);
    }
}
static void test_subgroup_inputs(void) {
    const char *source = "enable subgroups;"
        "@group(0) @binding(0) var<storage,read_write> output:array<u32>;"
        "@compute @workgroup_size(32) fn wave_inputs(@builtin(subgroup_size) size:u32,"
        "@builtin(subgroup_invocation_id) lane:u32){output[lane]=size;}";
    Fixture *f = prepare(source);
    char *text = emit(f, 0u);
    has(text, "ctx.input5 = WaveGetLaneCount();");
    has(text, "ctx.input6 = WaveGetLaneIndex();");
    has(text, "uint input5;");
    assert(strstr(text, "ctx.input5[") == NULL);
    free(text);
    f->module->entry_inputs[0].kind = INPUT_NUM_SUBGROUPS;
    DoeHlslResult result;
    char output[8] = "intact";
    assert(doe_hlsl_emit(&f->source, f->module, 0u, f->scratch, output, sizeof(output), &result) == DOE_COMPILER_UNSUPPORTED);
    assert(strcmp(output, "intact") == 0);
    f->module->entry_inputs[0].kind = INPUT_SUBGROUP_ID;
    assert(doe_hlsl_emit(&f->source, f->module, 0u, f->scratch, output, sizeof(output), &result) == DOE_COMPILER_UNSUPPORTED);
    release(f);
}
static void test_subgroups(void) {
    const char *source = "enable subgroups;"
        "@group(0) @binding(0) var<storage,read_write> output:array<u32>;"
        "@compute @workgroup_size(32) fn wave(@builtin(local_invocation_index) lane:u32){"
        "output[0]=subgroupAdd(lane);output[1]=subgroupMin(lane);output[2]=subgroupMax(lane);"
        "output[3]=subgroupMul(lane);output[4]=subgroupAnd(lane);output[5]=subgroupOr(lane);"
        "output[6]=subgroupXor(lane);output[7]=subgroupExclusiveAdd(lane);"
        "output[8]=subgroupInclusiveAdd(lane);output[9]=subgroupExclusiveMul(lane);"
        "output[10]=subgroupInclusiveMul(lane);output[11]=u32(subgroupAdd(i32(lane)));}";
    Fixture *f = prepare(source);
    char *text = emit(f, 0u);
    static const char *const intrinsics[] = {"WaveActiveSum(", "WaveActiveMin(", "WaveActiveMax(",
        "WaveActiveProduct(", "WaveActiveBitAnd(", "WaveActiveBitOr(", "WaveActiveBitXor(",
        "WavePrefixSum(", "WavePrefixProduct(", "asint(WaveActiveSum(asuint("};
    for (size_t i = 0u; i < sizeof(intrinsics) / sizeof(intrinsics[0]); ++i) has(text, intrinsics[i]);
    assert(strstr(text, "subgroupAdd(") == NULL);
    DoeHlslResult result;
    assert(doe_hlsl_emit(&f->source, f->module, 0u, f->scratch, NULL, 0u, &result) == DOE_COMPILER_OK);
    assert(result.uses_subgroups);
    for (uint32_t i = 0u; i < f->module->instruction_count; ++i) {
        if (f->module->instructions[i].opcode == IR_BUILTIN) {
            f->module->instructions[i].operands.scalar.literal = UINT32_MAX;
            break;
        }
    }
    char untouched[8] = "intact";
    assert(doe_hlsl_emit(&f->source, f->module, 0u, f->scratch, untouched, sizeof(untouched), &result) == DOE_COMPILER_UNSUPPORTED);
    assert(strcmp(untouched, "intact") == 0 && result.written_bytes == 0u);
    free(text);
    release(f);
}
static void test_graphics_stage_rejection(void) {
    Fixture *f = prepare("@compute @workgroup_size(1) fn main(){}");
    const uint32_t function = f->module->entries[0].function;
    for (unsigned stage = 0u; stage < 2u; ++stage) {
        f->module->functions[function].flags = FUNCTION_ENTRY |
            (stage == 0u ? FUNCTION_VERTEX : FUNCTION_FRAGMENT);
        char untouched[8] = "intact";
        DoeHlslResult result;
        assert(doe_hlsl_emit(&f->source, f->module, 0u, f->scratch, untouched,
                            sizeof(untouched), &result) == DOE_COMPILER_UNSUPPORTED);
        assert(result.required_bytes == 0u && result.written_bytes == 0u);
        assert(strcmp(untouched, "intact") == 0);
    }
    release(f);
}
static void test_array_length(const char *path) {
    Fixture *f = prepare(array_length_source);
    char *text = emit(f, 0u);
    if (path != NULL) {
        FILE *file = fopen(path, "wb");
        assert(file != NULL);
        assert(fwrite(text, 1u, strlen(text), file) == strlen(text));
        assert(fclose(file) == 0);
    }
    free(text);
    release(f);
}
static void test_pointer_rejection(void) {
    static const char *const sources[] = {
        "fn write(p:ptr<function,u32>){*p=7u;}"
        "@compute @workgroup_size(1) fn main(){var value=0u;write(&value);}",
        "@compute @workgroup_size(1) fn main(){var value=0u;let p=&value;*p=7u;}"
    };
    for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i) {
        Fixture *f = prepare(sources[i]);
        char output[16] = "intact";
        DoeHlslResult result;
        assert(doe_hlsl_emit(&f->source, f->module, 0u, f->scratch,
            output, sizeof(output), &result) == DOE_COMPILER_UNSUPPORTED);
        assert(result.written_bytes == 0u && result.required_bytes == 0u);
        assert(strcmp(output, "intact") == 0);
        release(f);
    }
}
int main(int argc, char **argv) {
    test_linkage(argc > 11 ? argv[11] : NULL);
    test_readonly_images(argc > 10 ? argv[10] : NULL);
    test_atomics(argc > 9 ? argv[9] : NULL);
    test_component_math(argc > 8 ? argv[8] : NULL);
    test_pointer_rejection();
    test_matrices(argc > 7 ? argv[7] : NULL);
    test_array_length(argc > 6 ? argv[6] : NULL);
    test_graphics(argc > 5 ? argv[5] : NULL);
    test_images(argc > 4 ? argv[4] : NULL);
    test_effects_host_fixture(argc > 3 ? argv[3] : NULL);
    test_target_rejection();
    test_graphics_stage_rejection();
    test_subgroup_inputs();
    test_subgroups();
    test_shared_call_dag();
    test_helper_closure();
    test_residual();
    test_layout();
    test_workgroup();
    test_vectors_and_builtins();
    test_entries_and_rejection();
    test_control_and_host_fixture(argc > 1 ? argv[1] : NULL);
    test_aggregate_host_fixture(argc > 2 ? argv[2] : NULL);
    puts("HLSL developer checks passed: typed source, layout, entry closures, workgroups, control and atomics.");
    return 0;
}
