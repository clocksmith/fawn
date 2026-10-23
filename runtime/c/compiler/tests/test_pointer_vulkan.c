/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "doe_vk.h"
#include "doe_compiler_stages.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void require(doe_vk_status status, const char *operation) {
    if (status == DOE_VK_OK) return;
    fprintf(stderr, "%s: %s\n", operation, doe_vk_status_name(status));
    exit(1);
}

static unsigned char *read_file(const char *path, size_t *size) {
    FILE *file = fopen(path, "rb");
    if (file == NULL || fseek(file, 0, SEEK_END) != 0) exit(2);
    long length = ftell(file);
    if (length <= 0 || length > 65536 || fseek(file, 0, SEEK_SET) != 0) exit(2);
    unsigned char *bytes = malloc((size_t)length);
    if (bytes == NULL || fread(bytes, 1, (size_t)length, file) != (size_t)length) exit(2);
    assert(fclose(file) == 0);
    *size = (size_t)length;
    return bytes;
}

static uint32_t *compile_source(const char *path, size_t *word_count) {
    size_t length;
    unsigned char *source = read_file(path, &length);
    const DoeCompilerStageCapacities capacities = {
        .module = {.typed = {.source_bytes = 65536, .tokens = 8192, .statements = 256, .expressions = 2048,
            .types = 64, .members = 32, .resources = 8, .access_steps = 1024, .constants = 512,
            .constant_edges = 1024, .ir_instructions = 8192, .ir_operands = 2048,
            .target_declarations = 4096, .traversal_items = 512, .words = 65536,
            .expression_depth = 32, .control_depth = 16, .type_syntax_depth = 32},
            .entries = 4, .entry_inputs = 16, .entry_references = 32},
        .interface_attributes = 32, .stage_interfaces = 32
    };
    DoeCompilerWorkspaceRequirements requirements;
    DoeCompilerStatus status = doe_compiler_stage_requirements(DOE_COMPILER_ATOMIC_ABI_VERSION,
        &capacities, sizeof(capacities), &requirements, sizeof(requirements));
    assert(status == DOE_COMPILER_OK && requirements.alignment_bytes <= _Alignof(max_align_t));
    assert(requirements.size_bytes <= 64u * 1024u * 1024u);
    void *workspace = malloc(requirements.size_bytes);
    uint32_t *words = malloc(65536 * sizeof(*words));
    assert(workspace != NULL && words != NULL);
    uint32_t references[32];
    char names[2048];
    DoeCompilerTypeV4 types[64];
    DoeCompilerMemberV4 members[32];
    DoeCompilerResourceV4 resources[8];
    DoeCompilerWorkgroupV5 workgroups[4];
    DoeCompilerEntryV5 entries[4];
    const DoeCompilerStageOutputs outputs = {.resources = {.module = {
        .words = words, .word_capacity = 65536,
        .types = types, .type_capacity = 64, .type_record_size = sizeof(types[0]),
        .members = members, .member_capacity = 32, .member_record_size = sizeof(members[0]),
        .resources = resources, .resource_capacity = 8, .resource_record_size = sizeof(resources[0]),
        .workgroups = workgroups, .workgroup_capacity = 4, .workgroup_record_size = sizeof(workgroups[0]),
        .entries = entries, .entry_capacity = 4, .entry_record_size = sizeof(entries[0]),
        .entry_references = references, .entry_reference_capacity = 32,
        .names = names, .name_capacity = sizeof(names)},
        .handle_record_size = sizeof(DoeCompilerHandleV6)},
        .interface_record_size = sizeof(DoeCompilerStageInterfaceV7)};
    const DoeCompilerEntrySelectionV5 selection = {DOE_COMPILER_SELECT_NAME, 0, "main", 4};
    DoeCompilerStageResult result;
    status = doe_compiler_compile_stages(DOE_COMPILER_ATOMIC_ABI_VERSION, &capacities, sizeof(capacities),
        workspace, requirements.size_bytes, (const char *)source, length, &selection, sizeof(selection),
        DOE_COMPILER_MODULE_WORDS | DOE_COMPILER_MODULE_REFLECTION, &outputs, sizeof(outputs), &result, sizeof(result));
    if (status != DOE_COMPILER_OK) {
        fprintf(stderr, "ABI9 status=%u: %s\n", (unsigned)status, result.resources.module.diagnostic.message);
        exit(1);
    }
    const DoeCompilerModuleResult *module = &result.resources.module;
    assert(module->module.schema_version == DOE_COMPILER_ATOMIC_SCHEMA_VERSION);
    assert(module->published.resources == 1u && module->published.entries == 1);
    assert(result.required_interfaces == 0 && result.resources.required_handles == 0);
    assert(entries[0].stage == DOE_COMPILER_STAGE_COMPUTE);
    assert(entries[0].workgroup_size[0] == 5u);
    assert(entries[0].workgroup_size[1] == 1 && entries[0].workgroup_size[2] == 1);
    assert(entries[0].workgroup_storage_bytes == 0u);
    assert(entries[0].workgroup_reference_count == 0u);
    assert(entries[0].resource_reference_count == 1u);
    for (size_t i = 0; i < module->published.resources; ++i) {
        assert(resources[i].group == 0 && resources[i].binding == i);
        assert(resources[i].kind == DOE_COMPILER_STORAGE_BUFFER && resources[i].access == DOE_COMPILER_READ_WRITE);
        assert(resources[i].runtime_array_stride_bytes == 4 && resources[i].minimum_binding_size_bytes == 4);
        assert(resources[i].type_index < module->published.types);
        const DoeCompilerTypeV4 *array = &types[resources[i].type_index];
        assert(array->element_type < module->published.types);

    }
    *word_count = module->published.words;
    free(source);
    free(workspace);
    return words;
}

int main(int argc, char **argv) {
    if (argc != 3 || strcmp(argv[2], "--allow-software") != 0) return 2;
    size_t word_count;
    uint32_t *words = compile_source(argv[1], &word_count);
    const doe_vk_options options = {.allow_cpu_adapter = true};
    doe_vk_context *context = NULL;
    doe_vk_error error;
    require(doe_vk_context_create(&options, &context, &error), "context");
    doe_vk_caps caps;
    require(doe_vk_context_caps(context, &caps), "caps");
    assert(caps.software_adapter);
    printf("Pointer adapter: %s software=%u; no physical qualification\n", caps.device_name, caps.software_adapter);
    doe_vk_buffer *output = NULL, *readback = NULL;
    require(doe_vk_buffer_create(context, 220, DOE_VK_BUFFER_STORAGE | DOE_VK_BUFFER_COPY_SRC,
        DOE_VK_MEMORY_HOST, &output, &error), "output");
    require(doe_vk_buffer_create(context, 236, DOE_VK_BUFFER_COPY_DST,
        DOE_VK_MEMORY_HOST, &readback, &error), "readback");
    uint32_t initial[55];
    for (size_t i = 0; i < 55; ++i) initial[i] = UINT32_C(0xdeadbeef);
    require(doe_vk_buffer_write(output, 0, initial, sizeof(initial)), "sentinel output");
    const doe_vk_binding_layout layout = {0, 0, DOE_VK_BINDING_STORAGE};
    const doe_vk_buffer_binding binding = {output, 0, 220};
    doe_vk_pipeline *pipeline = NULL;
    require(doe_vk_pipeline_create(context, words, word_count, "main", &layout, 1, &pipeline, &error), "pipeline");
    free(words);
    doe_vk_commands *commands = NULL;
    require(doe_vk_commands_create(context, &commands, &error), "commands");
    require(doe_vk_commands_dispatch(commands, pipeline, &binding, 1, 1, 1, 1), "five invocations");
    require(doe_vk_commands_fill(commands, readback, 0, 236, UINT32_C(0xdeadbeef)), "readback sentinel");
    require(doe_vk_commands_copy(commands, output, 0, readback, 8, 220), "copy");
    doe_vk_pipeline_release(pipeline);
    doe_vk_buffer_release(output);
    require(doe_vk_commands_submit(commands, &error), "submit");
    assert(doe_vk_commands_release(commands) == DOE_VK_BUSY);
    uint32_t actual[59];
    assert(doe_vk_buffer_read(readback, 0, actual, sizeof(actual)) == DOE_VK_BUSY);
    require(doe_vk_commands_wait(commands, UINT64_C(30000000000), &error), "wait");
    require(doe_vk_buffer_read(readback, 0, actual, sizeof(actual)), "readback");
    for (uint32_t lane = 0; lane < 5; ++lane) {
        const int32_t index = (int32_t)lane - 1;
        const uint32_t expected[11] = {
            index >= 0 && index < 3 ? 10u * (uint32_t)(index + 1) + 7u : 0u,
            index == 0 ? 117u : 10u, index == 1 ? 127u : 20u, index == 2 ? 137u : 30u,
            41u, 34u, 16u, 6u, 9u, 18u, 12u
        };
        for (uint32_t i = 0; i < 11; ++i) if (actual[2 + lane * 11 + i] != expected[i]) {
            fprintf(stderr, "pointer lane%u item%u got=%u expected=%u\n", lane, i,
                actual[2 + lane * 11 + i], expected[i]);
            return 1;
        }
    }
    assert(actual[0] == UINT32_C(0xdeadbeef) && actual[1] == UINT32_C(0xdeadbeef));
    assert(actual[57] == UINT32_C(0xdeadbeef) && actual[58] == UINT32_C(0xdeadbeef));
    require(doe_vk_commands_release(commands), "retire");
    doe_vk_buffer_release(readback);
    doe_vk_context_release(context);
    puts("PASS real function pointers: exact55 words, caller mutation/forwarding, ordered snapshots, aggregate path/index snapshots, negative/upper OOB, readonly aliasing, fresh locals, padding and pending lifetimes");
    return 0;
}
