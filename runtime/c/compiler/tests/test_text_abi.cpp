/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "../include/doe_compiler_text.h"
#include <cassert>
#include <cstdio>
#include <type_traits>
int main() {
    static_assert(std::is_standard_layout<DoeCompilerTextResult>::value, "C record layout");
    static_assert(sizeof(DoeCompilerTextBindingV10) == 16, "fixed binding record");
    static_assert(sizeof(DoeCompilerTextEntryV10) == 80, "fixed entry record");
    DoeCompilerTextCapacities caps{};
    caps.stages.module.typed.expression_depth = 1;
    caps.stages.module.typed.control_depth = 1;
    caps.stages.module.typed.type_syntax_depth = 1;
    DoeCompilerWorkspaceRequirements result{};
    assert(doe_compiler_text_requirements(DOE_COMPILER_TEXT_ABI_VERSION, DOE_COMPILER_TARGET_METAL,
        &caps, sizeof(caps), &result, sizeof(result)) == DOE_COMPILER_OK);
    assert(result.size_bytes != 0 && result.alignment_bytes != 0);
    std::puts("ABI10 ordinary C++17 linkage and fixed metadata layout passed");
}
