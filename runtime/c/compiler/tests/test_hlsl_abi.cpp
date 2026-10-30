/* SPDX-License-Identifier: MIT */
#include "hlsl_fixture.h"
#include "../include/doe_compiler_hlsl_profile.h"
#include <cassert>
#include <cstdlib>
#include <cstdio>
#include <type_traits>
int main() {
    static_assert(std::is_standard_layout<DoeCompilerHlslOutputs>::value, "C descriptor layout");
    static_assert(sizeof(DoeCompilerHlslBindingV11) == 20, "fixed register record");
    static_assert(sizeof(DoeCompilerHlslEntryV11) == 88, "fixed entry record");
    DoeCompilerTextCapacities caps = hlsl_capacities();
    static_assert(std::is_standard_layout<DoeCompilerHlslProfileResultV19>::value,"V19 C result layout");
    static_assert(sizeof(DoeCompilerHlslProfileV19) == 16,"V19 pointer-free profile");
    const DoeCompilerTranslationCapacitiesV17 profile_caps{{{caps,32u},0u},8u};
    DoeCompilerWorkspaceRequirements profile_required{};
    assert(doe_compiler_hlsl_profile_requirements(19u,&profile_caps,sizeof(profile_caps),
        &profile_required,sizeof(profile_required)) == DOE_COMPILER_OK);
    assert(profile_required.size_bytes && profile_required.alignment_bytes);
    DoeCompilerWorkspaceRequirements required{};
    assert(doe_compiler_hlsl_requirements(11u, &caps, sizeof(caps), &required, sizeof(required)) == DOE_COMPILER_OK);
    void *workspace = std::malloc(required.size_bytes);
    auto *storage = static_cast<HlslFixture *>(std::malloc(sizeof(HlslFixture)));
    assert(workspace && storage);
    DoeCompilerHlslOutputs outputs = hlsl_outputs(storage);
    DoeCompilerTextResult result{};
    DoeCompilerEntrySelectionV5 selection{DOE_COMPILER_SELECT_NAME, 0u, "selected", 8};
    assert(doe_compiler_compile_hlsl(11u, &caps, sizeof(caps), workspace, required.size_bytes,
        hlsl_source, sizeof(hlsl_source)-1, &selection, sizeof(selection), 3u,
        &outputs, sizeof(outputs), &result, sizeof(result)) == DOE_COMPILER_OK);
    assert(result.target == DOE_COMPILER_TARGET_HLSL && result.published_entries == 1);
    assert(result.stages.resources.module.module.selected_entry == 1 && result.published_bindings == 6);
    assert(outputs.entries[0].dispatch_space == 7 && outputs.entries[0].flags == (DOE_COMPILER_HLSL_ENTRY_WAVES |
        DOE_COMPILER_HLSL_ENTRY_ARRAY_VIEWS | DOE_COMPILER_HLSL_ENTRY_STORAGE_SIZES));
    assert(outputs.bindings[2].register_kind == DOE_COMPILER_HLSL_REGISTER_CBV);
    assert(outputs.bindings[2].register_space == 1 && outputs.bindings[2].shader_register == 2);
    assert(outputs.bindings[0].stride == 4 && outputs.bindings[0].shader_register == 5);
    std::free(storage);
    std::free(workspace);
    std::puts("ABI11 ordinary C++17 HLSL source/metadata roundtrip passed");
}
