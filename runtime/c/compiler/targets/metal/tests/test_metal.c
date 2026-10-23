/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "../metal.h"
#include "../../../src/numeric.h"
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
typedef struct {
    void *storage;
    CompilerWorkspace *module;
    Source source;
    DoeCompilerDiagnostic diagnostic;
} Fixture;
static Fixture *prepare(const char *text) {
    Fixture *f = calloc(1u, sizeof(*f));
    assert(f != NULL);
    CompilerLimits limits;
    size_t bytes = 0u;
    assert(compiler_module_layout(&capacities, &limits, &bytes));
    f->storage = malloc(bytes);
    assert(f->storage != NULL);
    f->module = f->storage;
    compiler_job_bind(f->module, f->storage, &limits);
    f->source = (Source){.source = text, .length = strlen(text), .diagnostic = &f->diagnostic,
        .limits = &f->module->limits, .compiler = f->module};
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
    return f;
}
static void release(Fixture *f) {
    free(f->storage);
    free(f);
}
static char *emit(Fixture *f, uint32_t selected) {
    DoeMetalResult measure;
    errno = EDOM;
    const DoeCompilerStatus status = doe_metal_emit(&f->source, f->module, selected, NULL, 0u, &measure);
    if (status != DOE_COMPILER_OK) fprintf(stderr, "Metal rejected at %zu: %s\n", f->diagnostic.offset, f->diagnostic.message);
    assert(status == DOE_COMPILER_OK);
    assert(errno == EDOM);
    assert(measure.required_bytes != 0u && measure.written_bytes == 0u);
    char *text = malloc(measure.required_bytes + 1u);
    assert(text != NULL);
    memset(text, 0xa5, measure.required_bytes + 1u);
    DoeMetalResult result;
    assert(doe_metal_emit(&f->source, f->module, selected, text, measure.required_bytes - 1u, &result) == DOE_COMPILER_OUTPUT_TOO_SMALL);
    assert(result.written_bytes == 0u && (unsigned char)text[0] == 0xa5u);
    assert(doe_metal_emit(&f->source, f->module, selected, text, measure.required_bytes, &result) == DOE_COMPILER_OK);
    assert(result.required_bytes == measure.required_bytes && result.written_bytes == measure.required_bytes);
    assert((unsigned char)text[measure.required_bytes] == 0xa5u);
    text[measure.required_bytes] = '\0';
    return text;
}
static void has(const char *text, const char *needle) {
    if (strstr(text, needle) == NULL) fprintf(stderr, "missing emission: %s\n", needle);
    assert(strstr(text, needle) != NULL);
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
int main(int argc, char **argv) {
    uint32_t slot = NO_ID;
    assert(doe_metal_binding_slot(1u, 13u, &slot) && slot == 29u);
    assert(!doe_metal_binding_slot(1u, 14u, &slot));
    assert(!doe_metal_binding_slot(0u, 16u, &slot));
    assert(!doe_metal_binding_slot(UINT32_MAX, UINT32_MAX, &slot));
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
