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

static uint32_t *compile_matrix(const char *path, size_t *word_count) {
    FILE *file = fopen(path, "rb");
    if (file == NULL || fseek(file, 0, SEEK_END) != 0) exit(2);
    long length = ftell(file);
    if (length <= 0 || length > 8192 || fseek(file, 0, SEEK_SET) != 0) exit(2);
    char *source = malloc((size_t)length);
    if (source == NULL || fread(source, 1, (size_t)length, file) != (size_t)length) exit(2);
    fclose(file);
    const DoeCompilerStageCapacities capacities = {
        .module = {.typed = {.source_bytes = 8192, .tokens = 2048, .statements = 64, .expressions = 512,
            .types = 64, .members = 32, .resources = 8, .access_steps = 128, .constants = 128,
            .constant_edges = 256, .ir_instructions = 1024, .ir_operands = 1024,
            .target_declarations = 1024, .traversal_items = 128, .words = 32768,
            .expression_depth = 32, .control_depth = 8, .type_syntax_depth = 32},
            .entries = 4, .entry_inputs = 16, .entry_references = 32},
        .interface_attributes = 32, .stage_interfaces = 32
    };
    DoeCompilerWorkspaceRequirements requirements;
    DoeCompilerStatus status = doe_compiler_stage_requirements(DOE_COMPILER_MATRIX_ABI_VERSION,
        &capacities, sizeof(capacities), &requirements, sizeof(requirements));
    assert(status == DOE_COMPILER_OK && requirements.alignment_bytes <= _Alignof(max_align_t));
    assert(requirements.size_bytes <= 64u * 1024u * 1024u);
    void *workspace = malloc(requirements.size_bytes);
    uint32_t *words = malloc(32768 * sizeof(*words));
    assert(workspace != NULL && words != NULL);
    uint32_t references[32];
    char names[2048];
    DoeCompilerTypeV4 types[64];
    DoeCompilerMemberV4 members[32];
    DoeCompilerResourceV4 resources[8];
    DoeCompilerWorkgroupV5 workgroups[4];
    DoeCompilerEntryV5 entries[4];
    const DoeCompilerStageOutputs outputs = {.resources = {.module = {
        .words = words, .word_capacity = 32768,
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
    status = doe_compiler_compile_stages(DOE_COMPILER_MATRIX_ABI_VERSION, &capacities, sizeof(capacities),
        workspace, requirements.size_bytes, source, (size_t)length, &selection, sizeof(selection),
        DOE_COMPILER_MODULE_WORDS | DOE_COMPILER_MODULE_REFLECTION, &outputs, sizeof(outputs), &result, sizeof(result));
    if (status != DOE_COMPILER_OK) {
        fprintf(stderr, "matrix ABI8 compiler status=%u: %s\n", (unsigned)status, result.resources.module.diagnostic.message);
        exit(1);
    }
    const DoeCompilerModuleResult *module = &result.resources.module;
    assert(module->module.schema_version == DOE_COMPILER_MATRIX_SCHEMA_VERSION);
    assert(module->published.resources == 2 && module->published.entries == 1);
    assert(result.required_interfaces == 0 && result.resources.required_handles == 0);
    assert(entries[0].stage == DOE_COMPILER_STAGE_COMPUTE && entries[0].workgroup_size[0] == 1 &&
        entries[0].workgroup_size[1] == 1 && entries[0].workgroup_size[2] == 1);
    assert(resources[0].group == 0 && resources[0].binding == 0 && resources[0].kind == DOE_COMPILER_UNIFORM_BUFFER);
    assert(resources[0].minimum_binding_size_bytes == 24 && resources[0].type_index < module->published.types);
    const DoeCompilerTypeV4 *matrix = &types[resources[0].type_index];
    assert(matrix->kind == DOE_COMPILER_TYPE_MATRIX && matrix->array_count == 3 && matrix->vector_width == 2);
    assert(matrix->size_bytes == 24 && matrix->alignment_bytes == 8 && matrix->stride_bytes == 8);
    assert(resources[1].group == 0 && resources[1].binding == 1 && resources[1].kind == DOE_COMPILER_STORAGE_BUFFER);
    assert(resources[1].access == DOE_COMPILER_READ_WRITE && resources[1].runtime_array_stride_bytes == 4);
    *word_count = module->published.words;
    free(source);
    free(workspace);
    return words;
}

int main(int argc, char **argv) {
    if (argc < 2 || argc > 3) return 2;
    size_t word_count = 0;
    uint32_t *words = compile_matrix(argv[1], &word_count);
    const doe_vk_options options = {.allow_cpu_adapter = argc == 3 && strcmp(argv[2], "--allow-software") == 0};
    doe_vk_context *context = NULL;
    doe_vk_error error;
    doe_vk_status status = doe_vk_context_create(&options, &context, &error);
    if (status != DOE_VK_OK) {
        fprintf(stderr, "SKIP native matrix: %s at %s native=%d\n", doe_vk_status_name(status), error.operation, error.native_result);
        free(words);
        return status == DOE_VK_UNSUPPORTED || status == DOE_VK_LOADER_UNAVAILABLE ? 77 : 1;
    }
    doe_vk_caps caps;
    require(doe_vk_context_caps(context, &caps), "caps");
    printf("Matrix adapter: %s software=%u; ABI8 direct, not old shader projection or hardware qualification\n",
        caps.device_name, caps.software_adapter);
    doe_vk_buffer *uniform = NULL, *storage = NULL, *readback = NULL;
    require(doe_vk_buffer_create(context, 24, DOE_VK_BUFFER_UNIFORM, DOE_VK_MEMORY_HOST, &uniform, &error), "uniform24");
    require(doe_vk_buffer_create(context, 140, DOE_VK_BUFFER_STORAGE | DOE_VK_BUFFER_COPY_SRC,
        DOE_VK_MEMORY_HOST, &storage, &error), "storage140");
    require(doe_vk_buffer_create(context, 160, DOE_VK_BUFFER_COPY_DST, DOE_VK_MEMORY_HOST, &readback, &error), "readback160");
    const float input[6] = {1, 2, 3, 4, 5, 6};
    require(doe_vk_buffer_write(uniform, 0, input, sizeof(input)), "matrix bytes");
    const doe_vk_binding_layout layout[2] = {{0, 0, DOE_VK_BINDING_UNIFORM}, {0, 1, DOE_VK_BINDING_STORAGE}};
    doe_vk_pipeline *pipeline = NULL;
    require(doe_vk_pipeline_create(context, words, word_count, "main", layout, 2, &pipeline, &error), "matrix pipeline");
    free(words);
    doe_vk_commands *commands = NULL;
    require(doe_vk_commands_create(context, &commands, &error), "commands");
    const doe_vk_buffer_binding bindings[2] = {{uniform, 0, 24}, {storage, 0, 140}};
    require(doe_vk_commands_dispatch(commands, pipeline, bindings, 2, 5, 1, 1), "five matrix invocations");
    require(doe_vk_commands_fill(commands, readback, 0, 160, UINT32_C(0xdeadbeef)), "readback sentinels");
    require(doe_vk_commands_copy(commands, storage, 0, readback, 8, 140), "matrix readback copy");
    doe_vk_pipeline_release(pipeline);
    doe_vk_buffer_release(uniform);
    doe_vk_buffer_release(storage);
    require(doe_vk_commands_submit(commands, &error), "submit");
    assert(doe_vk_commands_release(commands) == DOE_VK_BUSY);
    uint32_t actual[40];
    assert(doe_vk_buffer_read(readback, 0, actual, sizeof(actual)) == DOE_VK_BUSY);
    require(doe_vk_commands_wait(commands, UINT64_C(5000000000), &error), "wait");
    require(doe_vk_buffer_read(readback, 0, actual, sizeof(actual)), "readback");
    for (unsigned invocation = 0; invocation < 5; ++invocation) {
        int column = (int)invocation - 1;
        const float expected[7] = {
            column >= 0 && column < 3 ? input[2 * column] : 0,
            column >= 0 && column < 3 ? input[2 * column + 1] : 0,
            input[4 + invocation % 2], invocation < 2 ? input[2 + invocation] : 0,
            input[4] * 7 + input[5] * 8, input[0] * 2 + input[2] * 3 + input[4] * 4,
            input[0] * input[5] - input[1] * input[4]
        };
        uint32_t bits[7];
        memcpy(bits, expected, sizeof(bits));
        for (unsigned lane = 0; lane < 7; ++lane) if (actual[2 + invocation * 7 + lane] != bits[lane]) {
            fprintf(stderr, "matrix mismatch invocation=%u lane=%u got=%08x expected=%08x\n",
                invocation, lane, actual[2 + invocation * 7 + lane], bits[lane]);
            return 1;
        }
    }
    for (unsigned i = 0; i < 40; ++i) if ((i < 2 || i >= 37) && actual[i] != UINT32_C(0xdeadbeef)) return 1;
    require(doe_vk_commands_release(commands), "retire");
    doe_vk_buffer_release(readback);
    doe_vk_context_release(context);
    puts("PASS direct ABI8 WGSL/matrix reflection -> real Vulkan: exact24-byte mat3x2f uniform/final8-byte tail, dynamic column/row OOB zeros, whole reconstruction, transpose/products/determinant, exact35-float oracle and untouched padding; early caller release");
    return 0;
}
