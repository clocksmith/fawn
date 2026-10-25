/* SPDX-License-Identifier: MIT */
#include "../include/doe_compiler_module_specialization.h"
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <cstdio>

int main() {
    DoeCompilerSpecializationCapacities caps{};
    auto &typed = caps.target.stages.module.typed;
    typed.source_bytes = 1024u;
    typed.tokens = 512u;
    typed.statements = 16u;
    typed.expressions = 64u;
    typed.types = 16u;
    typed.resources = 4u;
    typed.members = 16u;
    typed.access_steps = 32u;
    typed.constants = 32u;
    typed.constant_edges = 64u;
    typed.ir_instructions = 128u;
    typed.ir_operands = 128u;
    typed.target_declarations = 1024u;
    typed.traversal_items = 32u;
    typed.words = 8192u;
    typed.expression_depth = 8u;
    typed.control_depth = 8u;
    typed.type_syntax_depth = 8u;
    caps.target.stages.module.entries = 2u;
    caps.target.stages.module.entry_inputs = 8u;
    caps.target.stages.module.entry_references = 8u;
    caps.overrides = 4u;
    DoeCompilerWorkspaceRequirements requirements{};
    assert(doe_compiler_specialization_requirements(13u, DOE_COMPILER_TARGET_SPIRV,
        &caps, sizeof(caps), &requirements, sizeof(requirements)) == DOE_COMPILER_OK);
    DoeCompilerWorkspaceRequirements module_requirements{};
    assert(doe_compiler_module_specialization_requirements(14u, DOE_COMPILER_TARGET_SPIRV,
        &caps, sizeof(caps), &module_requirements, sizeof(module_requirements)) == DOE_COMPILER_OK);
    assert(module_requirements.size_bytes == requirements.size_bytes &&
        module_requirements.alignment_bytes == requirements.alignment_bytes);
    void *workspace = std::malloc(requirements.size_bytes);
    assert(workspace != nullptr);
    const char source[] = "override width:u32; @compute @workgroup_size(width) fn main(){}";
    DoeCompilerValidationResult validation{};
    assert(doe_compiler_validate_module(13u, &caps, sizeof(caps), workspace, requirements.size_bytes,
        source, sizeof(source) - 1u, 0u, nullptr, 0u, &validation, sizeof(validation)) == DOE_COMPILER_OK);
    assert(validation.required_overrides == 1u && validation.required_entries == 1u);
    DoeCompilerEntrySelectionV5 selection{DOE_COMPILER_SELECT_NAME, 0u, "main", 4u};
    DoeCompilerOverrideValueV13 value{"width", 5u, 2.0};
    DoeCompilerTextResult result{};
    assert(doe_compiler_specialize(13u, DOE_COMPILER_TARGET_SPIRV, &caps, sizeof(caps),
        workspace, requirements.size_bytes, source, sizeof(source) - 1u, &selection, sizeof(selection),
        &value, 1u, sizeof(value), DOE_COMPILER_MODULE_WORDS | DOE_COMPILER_MODULE_MEASURE,
        nullptr, 0u, &result, sizeof(result)) == DOE_COMPILER_OK);
    assert(result.stages.resources.module.required.words > 0u);
    selection.mode = DOE_COMPILER_SELECT_NONE;
    selection.name = nullptr;
    selection.name_length = 0u;
    assert(doe_compiler_specialize(13u, DOE_COMPILER_TARGET_SPIRV, &caps, sizeof(caps),
        workspace, requirements.size_bytes, source, sizeof(source) - 1u, &selection, sizeof(selection),
        &value, 1u, sizeof(value), DOE_COMPILER_MODULE_WORDS | DOE_COMPILER_MODULE_MEASURE,
        nullptr, 0u, &result, sizeof(result)) == DOE_COMPILER_INPUT_ERROR);
    const char bundle[] = "override width:u32; @compute @workgroup_size(width) fn first(){}"
        "@compute @workgroup_size(width+1u) fn second(){}";
    assert(doe_compiler_specialize_module(14u, DOE_COMPILER_TARGET_SPIRV, &caps, sizeof(caps),
        workspace, requirements.size_bytes, bundle, sizeof(bundle) - 1u, &value, 1u, sizeof(value),
        DOE_COMPILER_MODULE_WORDS | DOE_COMPILER_MODULE_REFLECTION | DOE_COMPILER_MODULE_MEASURE,
        nullptr, 0u, &result, sizeof(result)) == DOE_COMPILER_OK);
    assert(result.stages.resources.module.required.entries == 2u &&
        result.stages.resources.module.required.words > 0u);
    std::memset(&result, 0x5a, sizeof(result));
    const DoeCompilerTextResult untouched = result;
    assert(doe_compiler_specialize_module(13u, DOE_COMPILER_TARGET_SPIRV, &caps, sizeof(caps),
        workspace, requirements.size_bytes, bundle, sizeof(bundle) - 1u, &value, 1u, sizeof(value),
        DOE_COMPILER_MODULE_WORDS | DOE_COMPILER_MODULE_MEASURE,
        nullptr, 0u, &result, sizeof(result)) == DOE_COMPILER_ABI_MISMATCH);
    assert(std::memcmp(&untouched, &result, sizeof(result)) == 0);
    assert(doe_compiler_specialize_module(14u, DOE_COMPILER_TARGET_SPIRV, &caps, sizeof(caps),
        workspace, requirements.size_bytes, bundle, sizeof(bundle) - 1u, &value, 1u, sizeof(value),
        DOE_COMPILER_MODULE_WORDS | DOE_COMPILER_MODULE_MEASURE,
        nullptr, 0u, &result, sizeof(result) - 1u) == DOE_COMPILER_ABI_MISMATCH);
    assert(std::memcmp(&untouched, &result, sizeof(result)) == 0);
    std::free(workspace);
    std::puts("C++17 ABI13/14 selected/all-entry shared consumers and frozen ABI13 NONE rejection passed");
}
