/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "../include/doe_compiler_v2.h"
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <type_traits>
int main() {
    static_assert(std::is_standard_layout<DoeCompilerResultV2>::value, "native C++ layout");
    static_assert(offsetof(DoeCompilerResultV2, reflection) == 8u, "reflection offset");
    static_assert(offsetof(DoeCompilerResultV2, diagnostic) == 6688u, "diagnostic offset");
    DoeCompilerWorkspaceRequirements requirements{};
    assert(doe_compiler_workspace_requirements(2, &requirements, sizeof(requirements)) == DOE_COMPILER_OK);
    void *workspace = std::malloc(requirements.size_bytes);
    assert(workspace != nullptr);
    const char source[] = "struct U{u:u32,}@group(0) @binding(0) var<uniform> a:U;"
        "@compute @workgroup_size(1) fn run(@builtin(global_invocation_id) g:vec3u){let x=a.u;return;}";
    uint32_t words[DOE_COMPILER_MAX_WORDS];
    DoeCompilerResultV2 result{};
    assert(doe_compiler_compile_v2(2, workspace, requirements.size_bytes, source, std::strlen(source),
        words, DOE_COMPILER_MAX_WORDS, &result, sizeof(result)) == DOE_COMPILER_OK);
    assert(result.reflection.struct_count == 1 && result.reflection.bindings[0].minimum_binding_size_bytes == 4);
    std::free(workspace);
}
