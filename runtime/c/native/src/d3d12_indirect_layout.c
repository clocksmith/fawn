/* SPDX-License-Identifier: MIT */
#include "d3d12_indirect_layout.h"

bool native_d3d12_guard_layout(const doe_shader_view *view, doe_dx_binding_layout *out, uint32_t *size_rows)
{
    if (!view || !out || !size_rows || view->target != DOE_SHADER_HLSL || view->target_language_version != 600 ||
        !view->text || !view->text_size || view->counts.entries != 1 || !view->entries ||
        view->counts.resources != 1 || !view->resources || view->hlsl_binding_count != 1 ||
        !view->hlsl_bindings || view->hlsl_entry_count != 1 || !view->hlsl_entries ||
        !view->entry_references) return false;
    const DoeCompilerEntryV5 *entry = &view->entries[0];
    if (entry->stage != DOE_COMPILER_STAGE_COMPUTE || entry->workgroup_storage_bytes ||
        entry->workgroup_size[0] != 1 || entry->workgroup_size[1] != 1 || entry->workgroup_size[2] != 1 ||
        entry->resource_reference_count != 1 || entry->first_resource_reference >= view->counts.entry_references)
        return false;
    uint32_t index = view->entry_references[entry->first_resource_reference];
    if (index >= view->counts.resources) return false;
    const DoeCompilerResourceV4 *resource = &view->resources[index];
    const DoeCompilerHlslBindingV11 *binding = &view->hlsl_bindings[0];
    const DoeCompilerHlslEntryV11 *native = &view->hlsl_entries[0];
    if (resource->group || resource->binding || resource->kind != DOE_COMPILER_STORAGE_BUFFER ||
        resource->access != DOE_COMPILER_READ_WRITE || binding->resource_index != index ||
        binding->register_kind != DOE_COMPILER_HLSL_REGISTER_UAV || binding->stride != 4 ||
        binding->shader_register == UINT32_MAX || binding->register_space == UINT32_MAX ||
        native->entry_index || native->stage != DOE_COMPILER_STAGE_COMPUTE ||
        (native->flags & ~DOE_COMPILER_HLSL_ENTRY_STORAGE_SIZES) ||
        native->dispatch_bytes || native->dispatch_register != UINT32_MAX || native->dispatch_space != UINT32_MAX)
        return false;
    *out = (doe_dx_binding_layout){.shader_register=binding->shader_register,
        .register_space=binding->register_space,.stride=binding->stride,
        .access=DOE_DX_READ_WRITE,.view=DOE_DX_STRUCTURED};
    *size_rows = (native->flags & DOE_COMPILER_HLSL_ENTRY_STORAGE_SIZES) ? 1u : 0u;
    return true;
}
