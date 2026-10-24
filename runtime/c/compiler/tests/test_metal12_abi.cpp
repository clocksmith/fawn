/* SPDX-License-Identifier: MIT */
#include "metal12_fixture.h"
#include <cassert>
#include <cstdlib>
#include <cstring>
static_assert(sizeof(DoeCompilerTextBindingV10) == 16, "legacy ABI10 binding unchanged");
static_assert(sizeof(DoeCompilerTextEntryV10) == 80, "legacy ABI10 entry unchanged");
static_assert(sizeof(DoeCompilerMetalBindingV12) == 20, "ABI12 binding");
static_assert(sizeof(DoeCompilerMetalEntryV12) == 92, "ABI12 entry");
int main() {
    auto caps = metal12_capacities();
    DoeCompilerWorkspaceRequirements required{};
    assert(doe_compiler_metal_requirements(12, &caps, sizeof(caps), &required, sizeof(required)) == DOE_COMPILER_OK);
    void *workspace = std::malloc(required.size_bytes);
    assert(workspace);
    DoeCompilerEntrySelectionV5 selection{DOE_COMPILER_SELECT_NAME, 0, "second", 6};
    DoeCompilerTextResult result{};
    assert(doe_compiler_compile_metal(12, &caps, sizeof(caps), workspace, required.size_bytes,
        metal12_mixed, std::strlen(metal12_mixed), &selection, sizeof(selection), 7 | DOE_COMPILER_CORE_BINDING_NUMBERS, nullptr, 0,
        &result, sizeof(result)) == DOE_COMPILER_OK);
    assert(result.required_bindings == 1 && result.required_entries == 1);
    std::free(workspace);
}
