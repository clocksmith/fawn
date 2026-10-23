/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "../include/doe_compiler_v2.h"
#include <stdio.h>
#include <stdlib.h>
int main(int argc, char **argv) {
    if (argc != 2) return 2;
    FILE *file = fopen(argv[1], "rb");
    if (file == NULL) return 1;
    char source[DOE_COMPILER_MAX_SOURCE_BYTES + 1u];
    const size_t length = fread(source, 1u, sizeof(source), file);
    const int read_failed = ferror(file);
    const int close_failed = fclose(file);
    if (read_failed || close_failed) return 1;
    DoeCompilerWorkspaceRequirements requirements;
    if (doe_compiler_workspace_requirements(2u, &requirements, sizeof(requirements)) != DOE_COMPILER_OK) return 1;
    void *workspace = malloc(requirements.size_bytes);
    DoeCompilerResultV2 *result = malloc(sizeof(*result));
    if (workspace == NULL || result == NULL) { free(result); free(workspace); return 1; }
    uint32_t words[DOE_COMPILER_MAX_WORDS];
    const DoeCompilerStatus status = doe_compiler_compile_v2(2u, workspace, requirements.size_bytes,
        source, length, words, DOE_COMPILER_MAX_WORDS, result, sizeof(*result));
    if (status != DOE_COMPILER_OK) fprintf(stderr, "%s\n", result->diagnostic.message);
    else printf("native v2: %zu words, %u bindings, %u structs\n", result->word_count,
                result->reflection.binding_count, result->reflection.struct_count);
    free(result); free(workspace);
    return status == DOE_COMPILER_OK ? 0 : 1;
}
