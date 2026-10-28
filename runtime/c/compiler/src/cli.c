/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#define _POSIX_C_SOURCE 200809L
#include "cli_json.h"
#include "cli_text.h"
#include "cli_specialization.h"
#include "../include/doe_compiler_pool_growth.h"
#include "../include/doe_compiler_job.h"
#include "../include/doe_compiler_stages.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define PATH_CAPACITY 4096u
#define CLI_SOURCE_CAPACITY (2u * 1024u * 1024u)
static int write_all(int fd, const unsigned char *bytes, size_t length) {
    size_t done = 0;
    while (done < length) {
        const ssize_t count = write(fd, bytes + done, length - done);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) return -1;
        done += (size_t)count;
    }
    return 0;
}
static DoeCompilerStatus compile_binary(const DoeCompilerStageCapacities *stages,
    void *workspace, size_t workspace_size, const char *source, size_t length,
    const DoeCompilerEntrySelectionV5 *selection, uint32_t *words, size_t word_capacity,
    DoeCompilerStageResult *result) {
    const DoeCompilerStageOutputs spans = {.resources = {.module = {
        .words = words, .word_capacity = word_capacity,
        .type_record_size = sizeof(DoeCompilerTypeV4),
        .member_record_size = sizeof(DoeCompilerMemberV4),
        .resource_record_size = sizeof(DoeCompilerResourceV4),
        .workgroup_record_size = sizeof(DoeCompilerWorkgroupV5),
        .entry_record_size = sizeof(DoeCompilerEntryV5)}, .handle_record_size = sizeof(DoeCompilerHandleV6)},
        .interface_record_size = sizeof(DoeCompilerStageInterfaceV7)
    };
    return doe_compiler_compile_stages(DOE_COMPILER_STAGES_ABI_VERSION,
        stages, sizeof(*stages), workspace, workspace_size, source, length,
        selection, sizeof(*selection), DOE_COMPILER_TYPED_WORDS,
        &spans, sizeof(spans), result, sizeof(*result));
}
static int temporary(const char *path, char output[PATH_CAPACITY]) {
    const size_t length = strlen(path);
    if (length > PATH_CAPACITY - sizeof(".tmp.XXXXXX")) { errno = ENAMETOOLONG; return -1; }
    memcpy(output, path, length);
    memcpy(output + length, ".tmp.XXXXXX", sizeof(".tmp.XXXXXX"));
    return mkstemp(output);
}
static int unused_destination(const char *path) {
    struct stat state;
    if (lstat(path, &state) == 0) { errno = EEXIST; return -1; }
    return errno == ENOENT ? 0 : -1;
}
int main(int argc, char **argv) {
    const char *program = strrchr(argv[0], '/');
    program = program != NULL ? program + 1 : argv[0];
    const char *default_target = strcmp(program, "doe-emit-msl-c") == 0 ? "msl" :
        strcmp(program, "doe-emit-hlsl-c") == 0 ? "hlsl" : "spirv";
    const bool emitter = strcmp(program, "doe-emit-msl-c") == 0 ||
        strcmp(program, "doe-emit-hlsl-c") == 0 || strcmp(program, "doe-emit-spirv-c") == 0;
    if (argc == 2 && strcmp(argv[1], "--help") == 0) {
        printf("%s --shader-path INPUT %s [--target spirv|msl|hlsl]\n", program,
            emitter ? "[--out OUTPUT]" : "--out OUTPUT [--reflection JSON | --reflection-v2 JSON]");
        puts("SPIR-V: [--mode default|vulkan-compute-runtime] [--overrides NAME=VALUE,...]");
        return 0;
    }
    const char *input = NULL, *output = NULL, *reflection = NULL, *reflection_v2 = NULL;
    const char *target_name = NULL;
    const char *mode = NULL;
    CliOverrides overrides = {0};
    for (int i = 1; i < argc; i += 2) {
        if (i + 1 >= argc || strcmp(argv[i + 1], "--shader-path") == 0 ||
            strcmp(argv[i + 1], "--out") == 0 || strcmp(argv[i + 1], "--reflection") == 0 ||
            strcmp(argv[i + 1], "--reflection-v2") == 0 || strcmp(argv[i + 1], "--target") == 0 ||
            strcmp(argv[i + 1], "--mode") == 0 || strcmp(argv[i + 1], "--overrides") == 0) {
            fprintf(stderr, "each option requires a value\n"); return 2;
        }
        if (strcmp(argv[i], "--overrides") == 0) {
            if (!cli_parse_overrides(argv[i + 1], &overrides)) {
                fprintf(stderr, "invalid, duplicate or excessive override values\n"); return 2;
            }
            continue;
        }
        const char **slot = NULL;
        if (strcmp(argv[i], "--shader-path") == 0) slot = &input;
        else if (strcmp(argv[i], "--out") == 0) slot = &output;
        else if (strcmp(argv[i], "--reflection") == 0) slot = &reflection;
        else if (strcmp(argv[i], "--reflection-v2") == 0) slot = &reflection_v2;
        else if (strcmp(argv[i], "--target") == 0) slot = &target_name;
        else if (strcmp(argv[i], "--mode") == 0) slot = &mode;
        if (slot == NULL || *slot != NULL) { fprintf(stderr, "unknown or duplicate option: %s\n", argv[i]); return 2; }
        *slot = argv[i + 1];
    }
    if (input == NULL || (output == NULL && !emitter)) {
        fprintf(stderr, "usage: doe-compiler-c --shader-path INPUT --out OUTPUT [--target spirv|msl|hlsl] [--reflection JSON | --reflection-v2 JSON]\n");
        return 2;
    }
    if (target_name == NULL) target_name = default_target;
    const bool specialized = mode != NULL && strcmp(mode, "vulkan-compute-runtime") == 0;
    if (mode != NULL && !specialized && strcmp(mode, "default") != 0) {
        fprintf(stderr, "unknown compiler mode: %s\n", mode); return 2;
    }
    if ((mode != NULL && strcmp(target_name, "spirv") != 0) ||
        (overrides.count != 0u && !specialized) ||
        (specialized && (reflection != NULL || reflection_v2 != NULL))) {
        fprintf(stderr, "runtime overrides require SPIR-V runtime mode without legacy reflection\n");
        return 2;
    }
    uint32_t text_target = 0u;
    if (target_name != NULL && strcmp(target_name, "spirv") != 0) {
        if (strcmp(target_name, "msl") == 0) text_target = DOE_COMPILER_TARGET_METAL;
        else if (strcmp(target_name, "hlsl") == 0) text_target = DOE_COMPILER_TARGET_HLSL;
        else { fprintf(stderr, "unknown target: %s\n", target_name); return 2; }
    }
    if (text_target != 0u && (reflection != NULL || reflection_v2 != NULL)) {
        fprintf(stderr, "legacy reflection modes require SPIR-V\n"); return 2;
    }
    if (output == NULL && (reflection != NULL || reflection_v2 != NULL)) {
        fprintf(stderr, "reflection publication requires --out\n"); return 2;
    }
    if (reflection != NULL && reflection_v2 != NULL) {
        fprintf(stderr, "reflection modes are mutually exclusive\n"); return 2;
    }
    const int legacy = reflection != NULL;
    if (reflection_v2 != NULL) reflection = reflection_v2;
    if (reflection != NULL && reflection[0] == '\0') {
        fprintf(stderr, "reflection path must not be empty\n"); return 2;
    }
    if (reflection != NULL && strcmp(output, reflection) == 0) { fprintf(stderr, "output and reflection paths must differ\n"); return 2; }
    int status = 1, input_fd = -1, binary_fd = -1, json_fd = -1;
    int binary_published = 0, json_published = 0;
    int binary_created = 0, json_created = 0;
    char binary_temp[PATH_CAPACITY] = {0}, json_temp[PATH_CAPACITY] = {0};
    char *source = NULL;
    size_t length = 0;
    void *workspace = NULL;
    DoeCompilerResultV2 *result_v2 = NULL;
    char *json = NULL;
    uint32_t *words = NULL;
    unsigned char *binary = NULL;
    size_t binary_length = 0u;
    source = malloc(CLI_SOURCE_CAPACITY + 1u);
    if (source == NULL) { fprintf(stderr, "source allocation failed\n"); goto cleanup; }
    input_fd = open(input, O_RDONLY);
    if (input_fd < 0) { perror("open shader"); goto cleanup; }
    while (length < CLI_SOURCE_CAPACITY + 1u) {
        const ssize_t count = read(input_fd, source + length, CLI_SOURCE_CAPACITY + 1u - length);
        if (count < 0 && errno == EINTR) continue;
        if (count < 0) { perror("read shader"); goto cleanup; }
        if (count == 0) break;
        length += (size_t)count;
    }
    if (close(input_fd) < 0) { input_fd = -1; perror("close shader"); goto cleanup; }
    input_fd = -1;
    if (length > CLI_SOURCE_CAPACITY) {
        fprintf(stderr, "shader exceeds 2 MiB source limit\n"); goto cleanup;
    }
    const DoeCompilerJobCapacities job = {
        .source_bytes = CLI_SOURCE_CAPACITY, .tokens = 8192u, .statements = 1024u,
        .expressions = 4096u, .words = 65536u,
        .expression_depth = 32u, .control_depth = 16u
    };
    const DoeCompilerTypedCapacities typed = {
        .source_bytes = CLI_SOURCE_CAPACITY, .tokens = 8192u, .statements = 1024u, .expressions = 4096u,
        .types = 256u, .members = 1024u, .resources = 32u, .access_steps = 5121u,
        .constants = 1024u, .constant_edges = 4096u, .ir_instructions = 24577u,
        .ir_operands = 4096u, .target_declarations = 7744u, .traversal_items = 1281u,
        .words = 65536u, .expression_depth = 32u, .control_depth = 16u, .type_syntax_depth = 32u
    };
    DoeCompilerStageCapacities stages = {
        .module = {.typed = typed, .entries = 630u, .entry_inputs = 1024u,
            .entry_references = 665280u},
        .interface_attributes = 4096u, .stage_interfaces = 4096u
    };
    const DoeCompilerEntrySelectionV5 selection = {.mode = DOE_COMPILER_SELECT_NONE};
    size_t word_count = 0u;
    size_t word_capacity = 0u;
    if (specialized) {
        DoeCompilerDiagnostic specialization_diagnostic;
        if (!cli_compile_specialized(&stages, source, length, &overrides,
                &binary, &binary_length, &specialization_diagnostic)) {
            fprintf(stderr, "%s:%u:%u: category=%u offset=%zu: %s\n", input,
                specialization_diagnostic.line, specialization_diagnostic.column,
                (unsigned)specialization_diagnostic.status, specialization_diagnostic.offset,
                specialization_diagnostic.message);
            goto cleanup;
        }
        goto publish;
    }
    if (text_target != 0u) {
        DoeCompilerDiagnostic text_diagnostic;
        if (!cli_compile_text(text_target, &stages, source, length, &binary,
                &binary_length, &text_diagnostic)) {
            fprintf(stderr, "%s:%u:%u: category=%u offset=%zu: %s\n", input,
                text_diagnostic.line, text_diagnostic.column, (unsigned)text_diagnostic.status,
                text_diagnostic.offset, text_diagnostic.message);
            goto cleanup;
        }
        goto publish;
    }
    const bool binary_only = reflection == NULL;
    word_capacity = legacy ? DOE_COMPILER_MAX_WORDS : job.words;
    DoeCompilerStageResult stage_result;
    DoeCompilerStatus binary_compiled = DOE_COMPILER_INPUT_ERROR;
    DoeCompilerWorkspaceRequirements requirements = {0};
    if (!legacy) {
        for (;;) {
            const DoeCompilerStatus queried = binary_only ?
                doe_compiler_stage_requirements(DOE_COMPILER_STAGES_ABI_VERSION,
                    &stages, sizeof(stages), &requirements, sizeof(requirements)) :
                doe_compiler_job_requirements(DOE_COMPILER_JOB_ABI_VERSION,
                    &job, sizeof(job), &requirements, sizeof(requirements));
            if (queried != DOE_COMPILER_OK) {
                fprintf(stderr, "workspace requirements query failed\n"); goto cleanup;
            }
            workspace = malloc(requirements.size_bytes);
            if (workspace == NULL) { fprintf(stderr, "workspace allocation failed\n"); goto cleanup; }
            if (!binary_only) break;
            /* The caller already owns a bounded word pool. Publish into it on
             * the first attempt; replay only when a reported pool must grow. */
            word_capacity = stages.module.typed.words;
            if (word_capacity > SIZE_MAX / sizeof(*words)) {
                fprintf(stderr, "word capacity exceeds byte range\n"); goto cleanup;
            }
            words = malloc(word_capacity * sizeof(*words));
            if (words == NULL) { fprintf(stderr, "word allocation failed\n"); goto cleanup; }
            binary_compiled = compile_binary(&stages, workspace, requirements.size_bytes,
                source, length, &selection, words, word_capacity, &stage_result);
            if (binary_compiled == DOE_COMPILER_OK) break;
            if (doe_compiler_grow_stage_pool(&stages, binary_compiled, stage_result.resources.module.exhausted_pool,
                    stage_result.resources.module.minimum_pool_capacity)) {
                free(workspace);
                workspace = NULL;
                free(words);
                words = NULL;
                continue;
            }
            const DoeCompilerDiagnostic *d = &stage_result.resources.module.diagnostic;
            fprintf(stderr, "%s:%u:%u: category=%u offset=%zu: %s\n", input,
                d->line, d->column, (unsigned)d->status, d->offset, d->message);
            goto cleanup;
        }
        if (!binary_only) {
            result_v2 = malloc(sizeof(*result_v2));
            if (result_v2 == NULL) { fprintf(stderr, "result allocation failed\n"); goto cleanup; }
        }
    }
    if (legacy || !binary_only) {
        if (word_capacity > SIZE_MAX / sizeof(*words)) {
            fprintf(stderr, "word capacity exceeds byte range\n"); goto cleanup;
        }
        words = malloc(word_capacity * sizeof(*words));
        if (words == NULL) { fprintf(stderr, "word allocation failed\n"); goto cleanup; }
    }
    DoeCompilerResult result;
    DoeCompilerStatus compiled;
    DoeCompilerDiagnostic *diagnostic;
    if (legacy) {
        compiled = doe_compiler_compile(DOE_COMPILER_ABI_VERSION, source, length,
                                       words, word_capacity, &result, sizeof(result));
        diagnostic = &result.diagnostic;
        word_count = result.word_count;
    } else if (binary_only) {
        compiled = binary_compiled;
        diagnostic = &stage_result.resources.module.diagnostic;
        word_count = stage_result.resources.module.published.words;
    } else {
        compiled = doe_compiler_compile_job(DOE_COMPILER_JOB_ABI_VERSION,
            &job, sizeof(job), workspace, requirements.size_bytes,
            source, length, words, word_capacity, result_v2, sizeof(*result_v2));
        diagnostic = &result_v2->diagnostic;
        word_count = result_v2->word_count;
    }
    if (compiled != DOE_COMPILER_OK) {
        fprintf(stderr, "%s:%u:%u: category=%u offset=%zu: %s\n", input,
                diagnostic->line, diagnostic->column, (unsigned)diagnostic->status,
                diagnostic->offset, diagnostic->message);
        goto cleanup;
    }
    if (word_count == 0u) {
        fprintf(stderr, "module has no executable SPIR-V entry point\n");
        goto cleanup;
    }
publish:
    if ((output != NULL && unused_destination(output) < 0) ||
        (reflection != NULL && unused_destination(reflection) < 0)) {
        perror("destination unavailable (existing files are never overwritten)"); goto cleanup;
    }
    size_t json_length = 0;
    if (reflection != NULL) {
        const size_t json_capacity = legacy ? CLI_JSON_V1_CAPACITY : CLI_JSON_V2_CAPACITY;
        json = malloc(json_capacity);
        if (json == NULL) { fprintf(stderr, "reflection allocation failed\n"); goto cleanup; }
        const bool serialized = legacy ?
            cli_json_v1(&result.reflection, json, json_capacity, &json_length) :
            cli_json_v2(&result_v2->reflection, json, json_capacity, &json_length);
        if (!serialized) { fprintf(stderr, "reflection exceeds buffer\n"); goto cleanup; }
    }
    if (text_target == 0u && !specialized) {
        if (word_count > word_capacity || word_count > SIZE_MAX / 4u) {
            fprintf(stderr, "binary word count exceeds capacity\n"); goto cleanup;
        }
        binary_length = word_count * 4u;
        binary = malloc(binary_length);
        if (binary == NULL) { fprintf(stderr, "binary allocation failed\n"); goto cleanup; }
        for (size_t i = 0; i < word_count; ++i)
            for (unsigned b = 0; b < 4u; ++b) binary[i * 4u + b] = (unsigned char)(words[i] >> (b * 8u));
    }
    if (output == NULL) {
        if (write_all(STDOUT_FILENO, binary, binary_length) < 0) {
            perror("write compiler output"); goto cleanup;
        }
        status = 0;
        goto cleanup;
    }
    binary_fd = temporary(output, binary_temp);
    if (binary_fd < 0) { perror("create binary temporary"); goto cleanup; }
    binary_created = 1;
    if (reflection != NULL) {
        json_fd = temporary(reflection, json_temp);
        if (json_fd < 0) { perror("create reflection temporary"); goto cleanup; }
        json_created = 1;
    }
    if (write_all(binary_fd, binary, binary_length) < 0 ||
        (reflection != NULL && write_all(json_fd, (const unsigned char *)json, json_length) < 0)) { perror("write artifacts"); goto cleanup; }
    if (close(binary_fd) < 0) { binary_fd = -1; perror("close binary"); goto cleanup; }
    binary_fd = -1;
    if (json_fd >= 0) {
        if (close(json_fd) < 0) { json_fd = -1; perror("close reflection"); goto cleanup; }
        json_fd = -1;
    }
    /* Atomic no-clobber publication; rollback binary if requested reflection fails.
     * Not a cross-filesystem crash transaction; no existing destination is replaced. */
    if (link(binary_temp, output) < 0) { perror("publish binary"); goto cleanup; }
    binary_published = 1;
    if (reflection != NULL) {
        if (link(json_temp, reflection) < 0) { perror("publish reflection"); goto cleanup; }
        json_published = 1;
    }
    status = 0;
cleanup:
    if (input_fd >= 0) (void)close(input_fd);
    if (binary_fd >= 0) (void)close(binary_fd);
    if (json_fd >= 0) (void)close(json_fd);
    if (status != 0 && binary_published) (void)unlink(output);
    if (status != 0 && json_published) (void)unlink(reflection);
    if (binary_created) (void)unlink(binary_temp);
    if (json_created) (void)unlink(json_temp);
    free(binary);
    free(words);
    free(json);
    free(result_v2);
    free(workspace);
    free(source);
    return status;
}
