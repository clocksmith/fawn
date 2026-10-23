/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "../include/doe_compiler_stages.h"
#include <cassert>
#include <type_traits>
static_assert(std::is_standard_layout<DoeCompilerStageCapacities>::value, "capacity layout");
static_assert(std::is_standard_layout<DoeCompilerStageOutputs>::value, "output layout");
static_assert(sizeof(DoeCompilerStageInterfaceV7) == 48u, "interface record");
int main() {
    DoeCompilerWorkspaceRequirements requirements{123u, 456u};
    assert(doe_compiler_stage_requirements(7u, nullptr, sizeof(DoeCompilerStageCapacities),
        &requirements, sizeof(requirements)) == DOE_COMPILER_INPUT_ERROR);
    assert(requirements.size_bytes == 123u && requirements.alignment_bytes == 456u);
}
