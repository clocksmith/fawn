/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "../include/doe_compiler.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
    assert(argc == 2);
    FILE *input = fopen(argv[1], "rb");
    assert(input != NULL);
    char source[DOE_COMPILER_MAX_SOURCE_BYTES + 1u];
    const size_t length = fread(source, 1u, sizeof(source), input);
    assert(ferror(input) == 0 && length <= DOE_COMPILER_MAX_SOURCE_BYTES);
    assert(fclose(input) == 0);
    uint32_t words[DOE_COMPILER_MAX_WORDS];
    DoeCompilerResult result;
    const DoeCompilerStatus status = doe_compiler_compile(
        DOE_COMPILER_ABI_VERSION, source, length, words, DOE_COMPILER_MAX_WORDS,
        &result, sizeof(result));
    if (status != DOE_COMPILER_OK)
        fprintf(stderr, "native consumer: %u:%u: %s\n",
                result.diagnostic.line, result.diagnostic.column,
                result.diagnostic.message);
    assert(status == DOE_COMPILER_OK);
    assert(words[0] == 0x07230203u && words[1] == 0x00010300u);
    assert(result.word_count > 5u && result.word_count <= DOE_COMPILER_MAX_WORDS);
    assert(strcmp(result.reflection.entry_point, "main") == 0);
    assert(result.reflection.binding_count == 3u);
    assert(result.reflection.workgroup_size[0] == 256u);
    const char *const names[] = {"a", "b", "out"};
    for (uint32_t i = 0; i < result.reflection.binding_count; ++i) {
        const DoeCompilerBinding *binding = &result.reflection.bindings[i];
        assert(strcmp(binding->name, names[i]) == 0);
        assert(binding->group == 0u && binding->binding == i);
        assert(binding->scalar == DOE_COMPILER_F32);
        assert(binding->access == (i == 2u ? DOE_COMPILER_READ_WRITE : DOE_COMPILER_READ));
        assert(binding->element_stride == 4u && binding->runtime_sized == 1u);
    }
    puts("Native C consumer: unchanged residual compiled through loaded shared library");
    return 0;
}
