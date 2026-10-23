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

/* Independent finite integer oracle. No compiler IR or source-pattern replay. */
static void input_words(bool all_ops, size_t binding, uint32_t *words, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        if (!all_ops) words[i] = (uint32_t)(17u * i + 5u);
        else if (binding == 0) words[i] = (uint32_t)((int32_t)i - 8);
        else if (binding == 1) words[i] = (uint32_t)(10u + i);
        else words[i] = UINT32_C(0xdeadbeef);
    }
}

static void expected_words(bool all_ops, size_t binding, uint32_t *words, size_t count) {
    if (!all_ops) {
        uint32_t shared[256];
        for (uint32_t i = 0; i < 256; ++i) shared[i] = 18u * i + 5u;
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t accum = 17u * i + 5u;
            for (uint32_t j = 0; j < 256; ++j) accum = shared[(j + accum) & 255u];
            words[i] = accum;
        }
        assert(count == 256);
        return;
    }
    if (binding < 2) {
        assert(count == 4);
        for (size_t i = 0; i < count; ++i) words[i] = 11;
        return;
    }
    assert(count == 336);
    const int32_t signed_values[13] = {-7, -4, -6, 5, -5, 3, 7, 1, 9, 1, 11, 0, 11};
    const uint32_t unsigned_values[13] = {UINT32_MAX - 2u, 2, UINT32_MAX,
        UINT32_MAX, 9, 0, 3, 5, 9, 1, 11, 0, 11};
    for (size_t lane = 0; lane < 6; ++lane) {
        for (size_t root = 0; root < 4; ++root) {
            size_t base = lane * 56 + root * 14;
            for (size_t item = 0; item < 14; ++item) words[base + item] = 0;
            if (lane == 0 || lane == 5) continue;
            if (root == 0) words[base] = (uint32_t)((int32_t)lane - 9);
            if (root == 1) words[base] = (uint32_t)(lane + 9);
            for (size_t item = 0; item < 13; ++item)
                words[base + 1 + item] = root % 2 == 0 ? (uint32_t)signed_values[item] : unsigned_values[item];
        }
    }
}

static uint32_t *compile_source(const char *path, bool all_ops, size_t *word_count) {
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
    assert(module->published.resources == (all_ops ? 3u : 1u) && module->published.entries == 1);
    assert(result.required_interfaces == 0 && result.resources.required_handles == 0);
    assert(entries[0].stage == DOE_COMPILER_STAGE_COMPUTE);
    assert(entries[0].workgroup_size[0] == (all_ops ? 6u : 256u));
    assert(entries[0].workgroup_size[1] == 1 && entries[0].workgroup_size[2] == 1);
    assert(entries[0].workgroup_storage_bytes == (all_ops ? 32u : 1024u));
    assert(entries[0].workgroup_reference_count == (all_ops ? 2u : 1u));
    assert(entries[0].resource_reference_count == (all_ops ? 3u : 1u));
    for (size_t i = 0; i < module->published.resources; ++i) {
        assert(resources[i].group == 0 && resources[i].binding == i);
        assert(resources[i].kind == DOE_COMPILER_STORAGE_BUFFER && resources[i].access == DOE_COMPILER_READ_WRITE);
        assert(resources[i].runtime_array_stride_bytes == 4 && resources[i].minimum_binding_size_bytes == 4);
        assert(resources[i].type_index < module->published.types);
        const DoeCompilerTypeV4 *array = &types[resources[i].type_index];
        assert(array->element_type < module->published.types);
        if (all_ops && i < 2) {
            const DoeCompilerTypeV4 *atomic = &types[array->element_type];
            assert(atomic->kind == DOE_COMPILER_TYPE_ATOMIC && atomic->size_bytes == 4 && atomic->alignment_bytes == 4);
        }
    }
    *word_count = module->published.words;
    free(source);
    free(workspace);
    return words;
}

static void execute(doe_vk_context *context, const char *path, bool all_ops) {
    size_t word_count;
    uint32_t *words = compile_source(path, all_ops, &word_count);
    doe_vk_error error;
    const size_t count = all_ops ? 3u : 1u;
    const size_t sizes[3] = {all_ops ? 16u : 1024u, 16u, 1344u};
    const size_t total = all_ops ? 1376u : 1024u;
    doe_vk_buffer *buffers[3] = {NULL, NULL, NULL}, *readback = NULL;
    doe_vk_buffer_binding bindings[3];
    doe_vk_binding_layout layouts[3];
    for (size_t i = 0; i < count; ++i) {
        require(doe_vk_buffer_create(context, sizes[i], DOE_VK_BUFFER_STORAGE | DOE_VK_BUFFER_COPY_SRC,
            DOE_VK_MEMORY_HOST, &buffers[i], &error), "atomic buffer");
        uint32_t seeds[336];
        assert(sizes[i] <= sizeof(seeds));
        input_words(all_ops, i, seeds, sizes[i] / 4);
        require(doe_vk_buffer_write(buffers[i], 0, seeds, sizes[i]), "atomic seeds");
        bindings[i] = (doe_vk_buffer_binding){buffers[i], 0, sizes[i]};
        layouts[i] = (doe_vk_binding_layout){0, (uint32_t)i, DOE_VK_BINDING_STORAGE};
    }
    require(doe_vk_buffer_create(context, total + 16, DOE_VK_BUFFER_COPY_DST, DOE_VK_MEMORY_HOST,
        &readback, &error), "atomic readback");
    doe_vk_pipeline *pipeline = NULL;
    require(doe_vk_pipeline_create(context, words, word_count, "main", layouts, count, &pipeline, &error), "atomic pipeline");
    free(words);
    doe_vk_commands *commands = NULL;
    require(doe_vk_commands_create(context, &commands, &error), "commands");
    require(doe_vk_commands_dispatch(commands, pipeline, bindings, count, 1, 1, 1), "one workgroup");
    require(doe_vk_commands_fill(commands, readback, 0, total + 16, UINT32_C(0xdeadbeef)), "readback sentinels");
    size_t offset = 8;
    for (size_t i = 0; i < count; ++i) {
        require(doe_vk_commands_copy(commands, buffers[i], 0, readback, offset, sizes[i]), "atomic copy");
        offset += sizes[i];
        doe_vk_buffer_release(buffers[i]);
    }
    doe_vk_pipeline_release(pipeline);
    require(doe_vk_commands_submit(commands, &error), "submit");
    assert(doe_vk_commands_release(commands) == DOE_VK_BUSY);
    uint32_t actual[348];
    assert(total + 16 <= sizeof(actual));
    assert(doe_vk_buffer_read(readback, 0, actual, total + 16) == DOE_VK_BUSY);
    require(doe_vk_commands_wait(commands, UINT64_C(30000000000), &error), "wait");
    require(doe_vk_buffer_read(readback, 0, actual, total + 16), "readback");
    offset = 2;
    for (size_t i = 0; i < count; ++i) {
        uint32_t expected[336];
        expected_words(all_ops, i, expected, sizes[i] / 4);
        for (size_t j = 0; j < sizes[i] / 4; ++j) {
            uint32_t want = expected[j];
            if (actual[offset + j] != want) {
                fprintf(stderr, "%s buffer%zu word%zu got=%08x expected=%08x\n",
                    all_ops ? "all ops" : "retained", i, j, actual[offset + j], want);
                exit(1);
            }
        }
        offset += sizes[i] / 4;
    }
    assert(actual[0] == UINT32_C(0xdeadbeef) && actual[1] == UINT32_C(0xdeadbeef));
    assert(actual[offset] == UINT32_C(0xdeadbeef) && actual[offset + 1] == UINT32_C(0xdeadbeef));
    require(doe_vk_commands_release(commands), "retire");
    doe_vk_buffer_release(readback);
    printf("PASS %s: %zu exact words, untouched padding, early release/pending guards\n",
        all_ops ? "all11 signed/unsigned storage/workgroup atomics + per-access OOB" : "unchanged workgroup_atomic.wgsl", total / 4);
}

int main(int argc, char **argv) {
    if (argc != 4 || strcmp(argv[3], "--allow-software") != 0) return 2;
    const doe_vk_options options = {.allow_cpu_adapter = true};
    doe_vk_context *context = NULL;
    doe_vk_error error;
    doe_vk_status status = doe_vk_context_create(&options, &context, &error);
    if (status != DOE_VK_OK) {
        fprintf(stderr, "context: %s at %s native=%d\n", doe_vk_status_name(status), error.operation, error.native_result);
        return 1;
    }
    doe_vk_caps caps;
    require(doe_vk_context_caps(context, &caps), "caps");
    assert(caps.software_adapter);
    printf("Atomic adapter: %s software=%u; ABI9 direct, no physical qualification\n", caps.device_name, caps.software_adapter);
    execute(context, argv[1], true);
    execute(context, argv[2], false);
    doe_vk_context_release(context);
    return 0;
}
