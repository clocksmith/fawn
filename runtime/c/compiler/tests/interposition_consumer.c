/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "../include/doe_compiler.h"
#include <assert.h>
#include <stdio.h>

/* A host may already export this ordinary name. It must not replace the
 * compiler's private lexer through ELF symbol interposition. */
static unsigned intercepted;
int lex_source(void);
int lex_source(void) {
    ++intercepted;
    return 0;
}

int main(void) {
    const char source[] =
        "@group(0) @binding(0) var<storage,read_write> data:array<u32>;"
        "@compute @workgroup_size(1) fn main(@builtin(global_invocation_id) gid:vec3u){"
        "data[gid.x]=data[gid.x]+1u;}";
    uint32_t words[DOE_COMPILER_MAX_WORDS];
    DoeCompilerResult result;
    const DoeCompilerStatus status = doe_compiler_compile(
        DOE_COMPILER_ABI_VERSION, source, sizeof(source) - 1u, words,
        DOE_COMPILER_MAX_WORDS, &result, sizeof(result));
    assert(intercepted == 0u);
    assert(status == DOE_COMPILER_OK && result.word_count > 5u);
    assert(words[0] == 0x07230203u);
    puts("ELF isolation: host lexer symbol cannot interpose on private compiler code");
    return 0;
}
