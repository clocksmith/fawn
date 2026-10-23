/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "../include/doe_compiler.h"
#include <cassert>
#include <cstdio>
#include <cstring>

int main() {
    const char source[] =
        "@group(0) @binding(0) var<storage,read_write> data:array<u32>;"
        "@compute @workgroup_size(1) fn main(@builtin(global_invocation_id) gid:vec3u){"
        "data[gid.x]=data[gid.x]+1u;}";
    uint32_t words[DOE_COMPILER_MAX_WORDS]{};
    DoeCompilerResult result{};
    const DoeCompilerStatus status = doe_compiler_compile(
        DOE_COMPILER_ABI_VERSION, source, sizeof(source) - 1u,
        words, DOE_COMPILER_MAX_WORDS, &result, sizeof(result));
    assert(status == DOE_COMPILER_OK);
    assert(words[0] == 0x07230203u);
    assert(std::strcmp(result.reflection.entry_point, "main") == 0);
    assert(result.reflection.binding_count == 1u);
    assert(result.reflection.bindings[0].scalar == DOE_COMPILER_U32);
    std::puts("C++17 consumer: public C ABI included, shared library linked and called");
    return 0;
}
