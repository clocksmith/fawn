/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "test_util.h"
#include "dxil_analyze.h"
#include "dxil_hlsl.h"

static DxilIrModule module;
static char names[65536], text[262144];
static bool emit(const char *source) {
    DxilDiagnostic diagnostic; DxilDiagKind error;
    if (!dxil_analyze(source, strlen(source), &module, names, sizeof(names), &diagnostic, &error)) {
        CHECK_MSG(false, "%s", diagnostic.message); return false;
    }
    size_t written = 0u;
    const DxilStatus status = dxil_hlsl_emit(&module, text, sizeof(text), &written);
    CHECK_MSG(status == DXIL_OK, "emit: %s", dxil_status_name(status));
    if (status != DXIL_OK) return false;
    CHECK(written == strlen(text));
    return true;
}
static void balanced(void) {
    int parentheses = 0, braces = 0, brackets = 0;
    for (size_t i = 0u; text[i] != '\0'; ++i) {
        if (text[i] == '(') ++parentheses;
        if (text[i] == ')') --parentheses;
        if (text[i] == '{') ++braces;
        if (text[i] == '}') --braces;
        if (text[i] == '[') ++brackets;
        if (text[i] == ']') --brackets;
        if (parentheses < 0 || braces < 0 || brackets < 0) { CHECK(false); return; }
    }
    CHECK(parentheses == 0 && braces == 0 && brackets == 0);
}
static void graphics(void) {
    if (!emit("struct Out{@builtin(position) position:vec4f,@location(0) uv:vec2f,}"
        "@vertex fn main(@builtin(vertex_index) index:u32)->Out{return Out(vec4f(f32(index),0,0,1),vec2f(0));}")) return;
    CHECK(strstr(text, "Out main_impl(uint index)") != NULL);
    CHECK(strstr(text, "struct main_stage_out") != NULL);
    CHECK(strstr(text, "position : SV_Position") != NULL);
    CHECK(strstr(text, "uv : TEXCOORD0") != NULL);
    CHECK(strstr(text, "_doe_out.uv = _doe_result.uv") != NULL);
    balanced();
    for (uint32_t i = 0u; i < module.structs[0].field_count; ++i) module.structs[0].fields[i].has_io = false;
    size_t absent_io_written = 17u;
    CHECK_STATUS(dxil_hlsl_emit(&module, text, sizeof(text), &absent_io_written), DXIL_INVALID_IR);
    CHECK(absent_io_written == 17u);
    if (!emit("struct In{@location(0) @interpolate(flat) number:u32,@location(1) uv:vec2f,}"
        "@fragment fn main(input:In)->@location(0) vec4f{return vec4f(input.uv,0,1);}")) return;
    CHECK(strstr(text, "nointerpolation uint _doe_input_0_0 : TEXCOORD0") != NULL);
    CHECK(strstr(text, "input.uv = _doe_input_0_1") != NULL);
    CHECK(strstr(text, "value : SV_Target0") != NULL);
    balanced();
    /* The output is a fresh view of caller IR, never cached source text. */
    module.functions[0].return_io.location = 3u;
    size_t written = 0u;
    CHECK_STATUS(dxil_hlsl_emit(&module, text, sizeof(text), &written), DXIL_OK);
    CHECK(strstr(text, "value : SV_Target3") != NULL);
}
static void atomics_and_packing(void) {
    if (!emit("var<workgroup> words:array<atomic<u32>,4>; @compute @workgroup_size(1) fn main(){"
        "let old=atomicAdd(&words[1],2u);let swapped=atomicCompareExchangeWeak(&words[2],old,3u);"
        "atomicStore(&words[0],swapped.old_value);let v=pack2x16float(vec2f(1,2));"
        "let unpacked=unpack2x16float(v);}")) return;
    CHECK(strstr(text, "InterlockedAdd(words[i0], a, result)") != NULL);
    CHECK(strstr(text, "InterlockedCompareExchange(words[i0], a, b, result.old_value)") != NULL);
    CHECK(strstr(text, "result.exchanged = result.old_value == a") != NULL);
    CHECK(strstr(text, "InterlockedExchange(words[i0], a, result)") != NULL);
    CHECK(strstr(text, "inline uint doe_pack2x16float") != NULL);
    CHECK(strstr(text, "inline float2 doe_unpack2x16float") != NULL);
    CHECK(strstr(text, "inout uint") == NULL); /* No copy-in/out atomic destination. */
    balanced();
}
static void clip_array_view(void) {
    if (!emit("@vertex fn main()->@builtin(position) vec4f{return vec4f(0,0,0,1);}")) return;
    DxilIrFunction *function = &module.functions[0];
    const DxilIrTypeId vector = function->return_type;
    const DxilIrType array = {.kind = DXIL_IR_TYPE_ARRAY,
        .elem = module.types[vector].elem, .has_len = true, .array_len = 4u};
    DxilIrTypeId result;
    CHECK_STATUS(dxil_ir_intern_type(&module, &array, &result), DXIL_OK);
    function->return_type = result;
    function->return_io.builtin = DXIL_IR_BUILTIN_CLIP_DISTANCES;
    for (uint32_t i = 0u; i < function->expr_count; ++i) {
        DxilIrExpr *expression = &function->exprs[i];
        if (expression->kind == DXIL_IR_EXPR_CONSTRUCT && expression->construct_ty == vector) {
            expression->ty = result;
            expression->construct_ty = result;
        }
    }
    size_t written = 0u;
    CHECK_STATUS(dxil_hlsl_emit(&module, text, sizeof(text), &written), DXIL_OK);
    CHECK(strstr(text, "void main_impl(out float _doe_stage_result[4])") != NULL);
    CHECK(strstr(text, "float value[4] : SV_ClipDistance") != NULL);
    CHECK(strstr(text, "_doe_stage_result[3] = _doe_stage_snapshot[3]") != NULL);
    CHECK(strstr(text, "main_impl(_doe_out.value);") != NULL);
    balanced();
}
static void runtime_atomics(void) {
    if (!emit("@group(0) @binding(0) var<storage,read_write> words:array<atomic<u32>>;"
        "@compute @workgroup_size(1) fn main(){var i=8u;let old=atomicAdd(&words[i],2u);}")) return;
    CHECK(strstr(text, "words.GetDimensions(count, stride); if (uint(i0) >= count) return result;") != NULL);
    CHECK(strstr(text, "InterlockedAdd(words[i0], a, result)") != NULL);
    balanced();
    if (!emit("@group(0) @binding(0) var<storage,read_write> words:array<atomic<u32>>;"
        "@compute @workgroup_size(1) fn main(){var i=999u;let p=&words[i];i=0u;let old=atomicAdd(p,2u);}")) return;
    CHECK(strstr(text, "doe_pointer_raw_") != NULL);
    CHECK(strstr(text, "doe_pointer_valid_") != NULL);
    const char *mutation = strstr(text, "i = 0;");
    const char *capture = strstr(text, "doe_pointer_raw_");
    CHECK(mutation != NULL && capture != NULL && capture < mutation);
    if (mutation != NULL) CHECK(strstr(mutation, "(doe_pointer_raw_") != NULL);
    balanced();
}
static void clz_and_output(void) {
    if (!emit("@compute @workgroup_size(1) fn main(){var n=7u;let leading=countLeadingZeros(n);"
        "let trailing=countTrailingZeros(n);}")) return;
    balanced();
    size_t written = 919u;
    char small[4];
    CHECK_STATUS(dxil_hlsl_emit(&module, small, sizeof(small), &written), DXIL_OUTPUT_TOO_LARGE);
    CHECK(written == 919u);
    const uint32_t count = module.function_count;
    module.function_count = DXIL_IR_MAX_FUNCTIONS + 1u;
    CHECK_STATUS(dxil_hlsl_emit(&module, text, sizeof(text), &written), DXIL_INVALID_IR);
    CHECK(written == 919u);
    module.function_count = count;
}
int main(void) {
    graphics(); atomics_and_packing(); clip_array_view(); runtime_atomics(); clz_and_output();
    TEST_SUMMARY("caller-built HLSL graphics/helper integration");
}
