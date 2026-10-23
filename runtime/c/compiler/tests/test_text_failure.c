/* SPDX-License-Identifier: MIT
 * Real final FP-restore fault after private target serialization, no library hook. */
#include "../include/doe_compiler_hlsl.h"
#include <assert.h>
#include <errno.h>
#include <fenv.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static bool fail_restore;
int __real_fesetenv(const fenv_t *);
int __wrap_fesetenv(const fenv_t *environment);
int __wrap_fesetenv(const fenv_t *environment) {
    if (fail_restore) { fail_restore = false; return 1; }
    return __real_fesetenv(environment);
}
int main(void) {
    const DoeCompilerTextCapacities caps = {.stages = {.module = {.typed = {
        .source_bytes = 256u, .tokens = 128u, .expressions = 32u, .statements = 16u,
        .types = 16u, .members = 16u, .constants = 32u, .constant_edges = 32u,
        .ir_instructions = 64u, .ir_operands = 64u, .access_steps = 32u,
        .traversal_items = 32u, .expression_depth = 8u, .control_depth = 4u, .type_syntax_depth = 8u},
        .entries = 1u, .entry_inputs = 1u, .entry_references = 1u}}, .text_bytes = 4096u};
    DoeCompilerWorkspaceRequirements requirements;
    assert(doe_compiler_text_requirements(10u, 1u, &caps, sizeof(caps), &requirements, sizeof(requirements)) == DOE_COMPILER_OK);
    void *workspace = malloc(requirements.size_bytes);
    assert(workspace != NULL);
    char text[4096]; memset(text, 0x5a, sizeof(text));
    const DoeCompilerTextOutputs outputs = {.stages = {.resources = {.module = {
        .type_record_size = sizeof(DoeCompilerTypeV4), .member_record_size = sizeof(DoeCompilerMemberV4),
        .resource_record_size = sizeof(DoeCompilerResourceV4), .workgroup_record_size = sizeof(DoeCompilerWorkgroupV5),
        .entry_record_size = sizeof(DoeCompilerEntryV5)}, .handle_record_size = sizeof(DoeCompilerHandleV6)},
        .interface_record_size = sizeof(DoeCompilerStageInterfaceV7)},
        .text = text, .text_capacity = sizeof(text), .binding_record_size = sizeof(DoeCompilerTextBindingV10),
        .entry_record_size = sizeof(DoeCompilerTextEntryV10)};
    const char source[] = "@compute @workgroup_size(1) fn main(){}";
    const DoeCompilerEntrySelectionV5 selection = {DOE_COMPILER_SELECT_AUTO, 0u, NULL, 0u};
    DoeCompilerTextResult result;
    errno = EDOM;
    assert(fesetround(FE_UPWARD) == 0);
    fail_restore = true;
    assert(doe_compiler_compile_text(10u, 1u, &caps, sizeof(caps), workspace, requirements.size_bytes,
        source, sizeof(source) - 1u, &selection, sizeof(selection), DOE_COMPILER_TEXT_CODE,
        &outputs, sizeof(outputs), &result, sizeof(result)) == DOE_COMPILER_INPUT_ERROR);
    assert(!fail_restore && errno == EDOM && fegetround() == FE_UPWARD);
    assert(result.required_text_bytes != 0u && result.published_text_bytes == 0u && result.target == 0u);
    assert(strstr(result.stages.resources.module.diagnostic.message, "restoration failed") != NULL);
    for (size_t i = 0u; i < sizeof(text); ++i) assert(text[i] == 0x5a);
    assert(doe_compiler_compile_text(10u, 1u, &caps, sizeof(caps), workspace, requirements.size_bytes,
        source, sizeof(source) - 1u, &selection, sizeof(selection), DOE_COMPILER_TEXT_CODE,
        &outputs, sizeof(outputs), &result, sizeof(result)) == DOE_COMPILER_OK);
    assert(result.published_text_bytes != 0u && errno == EDOM && fegetround() == FE_UPWARD);
    const DoeCompilerHlslOutputs hlsl = {.stages=outputs.stages,
        .text=text,.text_capacity=sizeof(text),
        .binding_record_size=sizeof(DoeCompilerHlslBindingV11),
        .entry_record_size=sizeof(DoeCompilerHlslEntryV11)};
    assert(doe_compiler_hlsl_requirements(11u, &caps, sizeof(caps), &requirements, sizeof(requirements)) == DOE_COMPILER_OK);
    free(workspace);
    workspace = malloc(requirements.size_bytes);
    assert(workspace != NULL);
    memset(text, 0x5a, sizeof(text));
    fail_restore = true;
    assert(doe_compiler_compile_hlsl(11u, &caps, sizeof(caps), workspace, requirements.size_bytes,
        source, sizeof(source)-1u, &selection, sizeof(selection), DOE_COMPILER_TEXT_CODE,
        &hlsl, sizeof(hlsl), &result, sizeof(result)) == DOE_COMPILER_INPUT_ERROR);
    assert(!fail_restore && errno == EDOM && fegetround() == FE_UPWARD);
    assert(result.required_text_bytes && !result.published_text_bytes && !result.target);
    assert(strstr(result.stages.resources.module.diagnostic.message, "restoration failed") != NULL);
    for (size_t i = 0; i < sizeof(text); ++i) assert(text[i] == 0x5a);
    assert(doe_compiler_compile_hlsl(11u, &caps, sizeof(caps), workspace, requirements.size_bytes,
        source, sizeof(source)-1u, &selection, sizeof(selection), DOE_COMPILER_TEXT_CODE,
        &hlsl, sizeof(hlsl), &result, sizeof(result)) == DOE_COMPILER_OK);
    assert(result.published_text_bytes && errno == EDOM && fegetround() == FE_UPWARD);
    assert(fesetround(FE_TONEAREST) == 0);
    free(workspace);
    puts("ABI10/11 final FP-restore failure preserves every caller byte after real MSL/HLSL serialization; reuse passed");
    return 0;
}
