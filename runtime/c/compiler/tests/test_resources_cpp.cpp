/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "../include/doe_compiler_resources.h"
#include <cassert>
#include <cerrno>
#include <cstddef>
#include <type_traits>
static_assert(std::is_standard_layout<DoeCompilerResourceOutputs>::value, "C output layout");
static_assert(std::is_standard_layout<DoeCompilerResourceResult>::value, "C result layout");
static_assert(sizeof(DoeCompilerHandleV6) == 32u, "fixed-width resource descriptor");
static_assert(offsetof(DoeCompilerResourceOutputs, module) == 0u, "module descriptor owns old spans");
int main() {
    DoeCompilerWorkspaceRequirements requirements{123u, 16u};
    errno = EDOM;
    assert(doe_compiler_resource_requirements(5u, nullptr, 0u, &requirements, sizeof(requirements)) == DOE_COMPILER_ABI_MISMATCH);
    assert(requirements.size_bytes == 123u && errno == EDOM);
    assert(doe_compiler_compile_resources(6u, nullptr, 0u, nullptr, 0u, nullptr, 0u,
        nullptr, 0u, 0u, nullptr, 0u, nullptr, 0u) == DOE_COMPILER_INPUT_ERROR);
    return 0;
}
