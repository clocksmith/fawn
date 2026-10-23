/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "../include/doe_compiler_stages.h"
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <iostream>

int main() {
    static_assert(DOE_COMPILER_MATRIX_ABI_VERSION == 8u);
    DoeCompilerStageCapacities caps{};
    auto &typed = caps.module.typed;
    typed.source_bytes = 4096;
    typed.tokens = 512;
    typed.statements = 16;
    typed.expressions = 128;
    typed.types = 32;
    typed.members = 16;
    typed.resources = 4;
    typed.access_steps = 32;
    typed.constants = 64;
    typed.constant_edges = 128;
    typed.ir_instructions = 256;
    typed.ir_operands = 256;
    typed.target_declarations = 256;
    typed.traversal_items = 64;
    typed.words = 4096;
    typed.expression_depth = 16;
    typed.control_depth = 4;
    typed.type_syntax_depth = 16;
    caps.module.entries = 1;
    caps.module.entry_inputs = 1;
    caps.module.entry_references = 4;
    DoeCompilerWorkspaceRequirements requirement{};
    assert(doe_compiler_stage_requirements(8, &caps, sizeof(caps), &requirement, sizeof(requirement)) == DOE_COMPILER_OK);
    void *workspace = std::malloc(requirement.size_bytes);
    assert(workspace != nullptr);
    const char source[] = "@group(0) @binding(0) var<uniform> a:mat3x2f;"
        "@compute @workgroup_size(1) fn main(){let value=a;}";
    const DoeCompilerEntrySelectionV5 selection{DOE_COMPILER_SELECT_AUTO, 0, nullptr, 0};
    DoeCompilerStageResult result{};
    assert(doe_compiler_compile_stages(8, &caps, sizeof(caps), workspace, requirement.size_bytes,
        source, std::strlen(source), &selection, sizeof(selection), 7, nullptr, 0,
        &result, sizeof(result)) == DOE_COMPILER_OK);
    assert(result.resources.module.required_is_exact == 1 && result.resources.module.required.resources == 1);
    std::free(workspace);
    std::cout << "C++17 ABI8 shared-library matrix measurement passed\n";
}
