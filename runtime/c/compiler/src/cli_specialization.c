/* SPDX-License-Identifier: MIT */
#include "cli_specialization.h"
#include "../include/doe_compiler_module_specialization.h"
#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

bool cli_parse_overrides(const char *argument, CliOverrides *overrides) {
    const char *cursor = argument;
    for (;;) {
        const char *end = strchr(cursor, ',');
        if (end == NULL) end = cursor + strlen(cursor);
        const char *equals = memchr(cursor, '=', (size_t)(end - cursor));
        if (equals == NULL || equals == cursor || equals + 1 == end ||
            overrides->count == CLI_MAX_OVERRIDES) return false;
        const size_t key_length = (size_t)(equals - cursor);
        for (size_t i = 0u; i < overrides->count; ++i)
            if (overrides->values[i].key_length == key_length &&
                memcmp(overrides->values[i].key, cursor, key_length) == 0) return false;
        for (const char *p = equals + 1; p != end; ++p)
            if (isspace((unsigned char)*p)) return false;
        char *number_end = NULL;
        const double value = strtod(equals + 1, &number_end);
        if (number_end != end || !isfinite(value)) return false;
        overrides->values[overrides->count++] = (DoeCompilerOverrideValueV13){
            .key = cursor, .key_length = key_length, .value = value};
        if (*end == '\0') return true;
        cursor = end + 1;
    }
}

bool cli_compile_specialized(const DoeCompilerStageCapacities *stages,
    const char *source, size_t length, const CliOverrides *overrides,
    unsigned char **code, size_t *code_length, DoeCompilerDiagnostic *diagnostic) {
    *code = NULL;
    *code_length = 0u;
    *diagnostic = (DoeCompilerDiagnostic){.status = DOE_COMPILER_INPUT_ERROR};
    memcpy(diagnostic->message, "specialization allocation or requirements failed",
        sizeof("specialization allocation or requirements failed"));
    const DoeCompilerSpecializationCapacities capacities = {
        .target = {.stages = *stages}, .overrides = stages->module.typed.constants};
    DoeCompilerWorkspaceRequirements size = {0};
    if (doe_compiler_module_specialization_requirements(
            DOE_COMPILER_MODULE_SPECIALIZATION_ABI_VERSION, DOE_COMPILER_TARGET_SPIRV,
            &capacities, sizeof(capacities), &size, sizeof(size)) != DOE_COMPILER_OK) return false;
    void *workspace = malloc(size.size_bytes);
    if (workspace == NULL) return false;
    bool success = false;
    uint32_t *words = NULL;
    unsigned char *binary = NULL;
    DoeCompilerTextResult result = {0};
    if (doe_compiler_specialize_module(DOE_COMPILER_MODULE_SPECIALIZATION_ABI_VERSION,
            DOE_COMPILER_TARGET_SPIRV, &capacities, sizeof(capacities), workspace,
            size.size_bytes, source, length, overrides->values, overrides->count,
            sizeof(overrides->values[0]), DOE_COMPILER_TYPED_WORDS | DOE_COMPILER_TYPED_MEASURE,
            NULL, 0u, &result, sizeof(result)) != DOE_COMPILER_OK) {
        *diagnostic = result.stages.resources.module.diagnostic;
        goto finish;
    }
    const size_t word_count = result.stages.resources.module.required.words;
    if (word_count == 0u || word_count > SIZE_MAX / sizeof(*words)) goto finish;
    words = malloc(word_count * sizeof(*words));
    binary = malloc(word_count * sizeof(*words));
    if (words == NULL || binary == NULL) goto finish;
    const DoeCompilerSpecializationOutputs outputs = {.spirv = {
        .resources = {.module = {.words = words, .word_capacity = word_count,
            .type_record_size = sizeof(DoeCompilerTypeV4),
            .member_record_size = sizeof(DoeCompilerMemberV4),
            .resource_record_size = sizeof(DoeCompilerResourceV4),
            .workgroup_record_size = sizeof(DoeCompilerWorkgroupV5),
            .entry_record_size = sizeof(DoeCompilerEntryV5)},
            .handle_record_size = sizeof(DoeCompilerHandleV6)},
        .interface_record_size = sizeof(DoeCompilerStageInterfaceV7)}};
    if (doe_compiler_specialize_module(DOE_COMPILER_MODULE_SPECIALIZATION_ABI_VERSION,
            DOE_COMPILER_TARGET_SPIRV, &capacities, sizeof(capacities), workspace,
            size.size_bytes, source, length, overrides->values, overrides->count,
            sizeof(overrides->values[0]), DOE_COMPILER_TYPED_WORDS,
            &outputs, sizeof(outputs), &result, sizeof(result)) != DOE_COMPILER_OK) {
        *diagnostic = result.stages.resources.module.diagnostic;
        goto finish;
    }
    if (result.stages.resources.module.published.words != word_count) goto finish;
    for (size_t i = 0u; i < word_count; ++i)
        for (unsigned b = 0u; b < 4u; ++b)
            binary[i * 4u + b] = (unsigned char)(words[i] >> (b * 8u));
    *code = binary;
    *code_length = word_count * sizeof(*words);
    *diagnostic = result.stages.resources.module.diagnostic;
    binary = NULL;
    success = true;
finish:
    free(binary);
    free(words);
    free(workspace);
    return success;
}
