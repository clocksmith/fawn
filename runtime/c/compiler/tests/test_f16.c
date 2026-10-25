/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "../src/numeric.h"
#include "../include/doe_compiler_stages.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void numbers(void) {
    DoeCompilerDiagnostic diagnostic = {0};
    CompilerWorkspace compiler = {.module_flags = MODULE_F16};
    Source source = {.diagnostic = &diagnostic, .compiler = &compiler};
    NumericValue result;
    for (uint64_t bits = 0u; bits < 65536u; ++bits) {
        if ((bits & 0x7c00u) == 0x7c00u) continue;
        assert(numeric_half_pack(&source, 0u, numeric_half_value(bits), &result));
        assert(result.type == IR_F16 && result.bits == bits);
    }
    const struct { double number; uint64_t bits; } cases[] = {
        {1.00048828125, 0x3c00u}, {1.00146484375, 0x3c02u},
        {-1.00048828125, 0xbc00u}, {0x1p-24, 1u}, {0x1p-25, 0u},
        {0x1.8p-24, 2u}, {65504.0, 0x7bffu}, {-0.0, 0x8000u}
    };
    for (size_t i = 0u; i < sizeof(cases)/sizeof(cases[0]); ++i) {
        assert(numeric_half_pack(&source, 0u, cases[i].number, &result));
        assert(result.bits == cases[i].bits);
    }
    assert(!numeric_half_pack(&source, 0u, 65520.0, &result));
    assert(!numeric_half_pack(&source, 0u, INFINITY, &result));
    assert(numeric_binary(&source, 0u, EXPRESSION_ADD, COMPARE_EQUAL,
        (NumericValue){0x3c00u, IR_F16}, (NumericValue){0x1000u, IR_F16}, &result));
    assert(result.type == IR_F16 && result.bits == 0x3c00u);
    assert(numeric_binary(&source, 0u, EXPRESSION_MULTIPLY, COMPARE_EQUAL,
        (NumericValue){0x3e00u, IR_F16}, (NumericValue){0x4000u, IR_F16}, &result));
    assert(result.bits == 0x4200u);
}
static void admission(const char *directory) {
    const DoeCompilerStageCapacities caps = {
        .module = {.typed = {.source_bytes=4096u, .tokens=1024u, .statements=64u,
            .expressions=256u, .types=32u, .members=32u, .resources=8u,
            .access_steps=128u, .constants=128u, .constant_edges=256u,
            .ir_instructions=1024u, .ir_operands=1024u, .target_declarations=1024u,
            .traversal_items=128u, .words=16384u, .expression_depth=32u,
            .control_depth=8u, .type_syntax_depth=32u},
            .entries=4u, .entry_inputs=16u, .entry_references=32u},
        .interface_attributes=32u, .stage_interfaces=32u};
    DoeCompilerWorkspaceRequirements requirements;
    assert(doe_compiler_stage_requirements(8u, &caps, sizeof(caps), &requirements,
        sizeof(requirements)) == DOE_COMPILER_OK);
    void *workspace = malloc(requirements.size_bytes);
    assert(workspace != NULL);
    const char shader[] = "enable f16; @group(0) @binding(0) var<storage,read_write> a:array<vec3h>;"
        "@compute @workgroup_size(1) fn main(){a[0]=vec3h(1.5h,-2.0h,0.0h);}";
    const DoeCompilerEntrySelectionV5 select = {DOE_COMPILER_SELECT_AUTO, 0u, NULL, 0u};
    DoeCompilerStageResult result;
    assert(doe_compiler_compile_stages(8u, &caps, sizeof(caps), workspace,
        requirements.size_bytes, shader, strlen(shader), &select, sizeof(select),
        7u, NULL, 0u, &result, sizeof(result)) == DOE_COMPILER_OK);
    assert(doe_compiler_compile_stages(8u, &caps, sizeof(caps), workspace,
        requirements.size_bytes, shader, strlen(shader), &select, sizeof(select),
        7u | DOE_COMPILER_DISABLE_F16, NULL, 0u, &result, sizeof(result)) == DOE_COMPILER_UNSUPPORTED);
    assert(result.resources.module.published.words == 0u);
    assert(result.resources.module.diagnostic.offset == 7u);
    if (directory != NULL) {
        for (uint32_t count = 1u; count <= 17u; count += 2u) {
            char source_text[1024], path[4096];
            const int length = snprintf(source_text, sizeof(source_text),
                "enable f16; struct D{a:array<f16,%u>,} "
                "@group(0) @binding(0) var<uniform> data:D; "
                "@group(0) @binding(1) var<storage,read_write> out:array<u32>; "
                "@compute @workgroup_size(1) fn main(){"
                "let i=out[0] %% %uu; out[1]=u32(data.a[i]);}", count, count);
            assert(length > 0 && (size_t)length < sizeof(source_text));
            uint32_t words[16384];
            DoeCompilerStageOutputs outputs = {.resources.module = {
                .words = words, .word_capacity = sizeof(words) / sizeof(words[0]),
                .type_record_size = sizeof(DoeCompilerTypeV4),
                .member_record_size = sizeof(DoeCompilerMemberV4),
                .resource_record_size = sizeof(DoeCompilerResourceV4),
                .workgroup_record_size = sizeof(DoeCompilerWorkgroupV5),
                .entry_record_size = sizeof(DoeCompilerEntryV5)},
                .resources.handle_record_size = sizeof(DoeCompilerHandleV6),
                .interface_record_size = sizeof(DoeCompilerStageInterfaceV7)};
            const DoeCompilerStatus status = doe_compiler_compile_stages(8u, &caps, sizeof(caps),
                workspace, requirements.size_bytes, source_text, (size_t)length,
                &select, sizeof(select), DOE_COMPILER_MODULE_WORDS | DOE_COMPILER_UNIFORM_BUFFER_STANDARD_LAYOUT,
                &outputs, sizeof(outputs), &result, sizeof(result));
            if (status != DOE_COMPILER_OK) fprintf(stderr, "tail %u: %s\n", count,
                result.resources.module.diagnostic.message);
            assert(status == DOE_COMPILER_OK);
            const int written = snprintf(path, sizeof(path), "%s/tail%u.spv", directory, count);
            assert(written > 0 && (size_t)written < sizeof(path));
            FILE *file = fopen(path, "wb");
            assert(file != NULL);
            const size_t size = result.resources.module.published.words;
            assert(fwrite(words, sizeof(words[0]), size, file) == size);
            assert(fclose(file) == 0);
        }
    }
    free(workspace);
}
int main(int argc, char **argv) {
    numbers();
    admission(argc > 1 ? argv[1] : NULL);
    puts("f16: all finite binary16 payloads, tie-even arithmetic, range and device admission passed");
    return 0;
}
