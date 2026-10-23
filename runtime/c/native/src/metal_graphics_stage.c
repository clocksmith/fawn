/* SPDX-License-Identifier: MIT */
#include "metal_graphics_internal.h"
#include <string.h>

bool native_metal_stage(native_pipeline *pipeline, const native_stage *stage,
    size_t index, doe_mtl_shader_stage *out)
{
    const doe_shader_view *view = &stage->view;
    if (index > 1 || view->target != DOE_SHADER_METAL || view->target_language_version != 300 ||
        !view->text || !view->text_size) return false;
    const DoeCompilerTextEntryV10 *entry = NULL;
    for (size_t i = 0; i < view->target_entry_count; ++i) {
        if (view->target_entries[i].entry_index != (size_t)(stage->entry - view->entries)) continue;
        if (entry) return false;
        entry = &view->target_entries[i];
    }
    if (!entry || entry->stage != (index ? DOE_COMPILER_STAGE_FRAGMENT : DOE_COMPILER_STAGE_VERTEX) ||
        !memchr(entry->name, 0, sizeof(entry->name)) ||
        (entry->sizes_slot != UINT32_MAX && (entry->sizes_slot >= DOE_MTL_BUFFER_SLOTS ||
            entry->sizes_bytes > DOE_MTL_BUFFER_SLOTS * sizeof(uint32_t)))) return false;
    *out = (doe_mtl_shader_stage){view->text, view->text_size, entry->name, entry->sizes_slot};
    native_metal_graphics *graphics = pipeline->metal_graphics;
    for (size_t i = 0; i < stage->entry->resource_reference_count; ++i) {
        uint32_t resource_index = view->entry_references[stage->entry->first_resource_reference + i];
        if (resource_index >= view->counts.resources) return false;
        const DoeCompilerResourceV4 *resource = &view->resources[resource_index];
        size_t binding = 0;
        while (binding < pipeline->binding_count && (pipeline->bindings[binding].group != resource->group ||
            pipeline->bindings[binding].binding != resource->binding)) ++binding;
        if (binding == pipeline->binding_count) return false;
        const DoeCompilerTextBindingV10 *target = NULL;
        for (size_t j = 0; j < view->target_binding_count; ++j)
            if (view->target_bindings[j].resource_index == resource_index) {
                if (target) return false;
                target = &view->target_bindings[j];
            }
        uint32_t table = pipeline->bindings[binding].kind == DOE_BIND_BUFFER ? DOE_COMPILER_TEXT_TABLE_BUFFER :
            pipeline->bindings[binding].kind == DOE_BIND_SAMPLER ? DOE_COMPILER_TEXT_TABLE_SAMPLER : DOE_COMPILER_TEXT_TABLE_TEXTURE;
        uint32_t limit = table == DOE_COMPILER_TEXT_TABLE_BUFFER ? DOE_MTL_BUFFER_SLOTS :
            table == DOE_COMPILER_TEXT_TABLE_TEXTURE ? DOE_MTL_TEXTURE_SLOTS : DOE_MTL_SAMPLER_SLOTS;
        if (!target || target->table != table || target->slot >= limit ||
            (table == DOE_COMPILER_TEXT_TABLE_BUFFER && target->slot == entry->sizes_slot)) return false;
        if (target->size_index != UINT32_MAX && (table != DOE_COMPILER_TEXT_TABLE_BUFFER ||
            target->size_index != target->slot || entry->sizes_slot == UINT32_MAX ||
            target->size_index >= entry->sizes_bytes / sizeof(uint32_t))) return false;
        for (size_t j = 0; j < pipeline->binding_count; ++j)
            if (j != binding && graphics->tables[index][j] == table &&
                graphics->slots[index][j] == target->slot) return false;
        graphics->tables[index][binding] = table;
        graphics->slots[index][binding] = target->slot;
    }
    return true;
}
