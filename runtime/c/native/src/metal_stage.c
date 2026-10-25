/* SPDX-License-Identifier: MIT */
#include "metal_binding_internal.h"
#include <string.h>

bool native_metal_stage_map_build(const native_pipeline *pipeline, const native_stage *stage,
    uint32_t expected_stage, native_metal_stage_map *map, doe_mtl_shader_stage *out)
{
    const doe_shader_view *view = &stage->view;
    if (view->target != DOE_SHADER_METAL || view->target_language_version != 300 ||
        !view->text || !view->text_size || pipeline->binding_count > NATIVE_MAX_PIPELINE_BINDINGS) return false;
    if (stage->imported) {
        if (!stage->entry || stage->entry->stage != expected_stage || pipeline->immediate_bytes) return false;
        native_metal_stage_map result={0};
        for (size_t i=0;i<pipeline->binding_count;++i) {
            if (!(pipeline->imported_binding_visibility[i] & stage->visibility)) continue;
            const native_binding *binding=&pipeline->bindings[i];
            /* Retained native MSL ABI: one flat slot per actual public binding,
             * independently in each Metal argument table, with MAX_BIND=32. */
            if (binding->binding>=DOE_NATIVE_MSL_BINDINGS_PER_GROUP || binding->group>UINT32_MAX/DOE_NATIVE_MSL_BINDINGS_PER_GROUP) return false;
            uint32_t slot=binding->group*DOE_NATIVE_MSL_BINDINGS_PER_GROUP+binding->binding, table, limit;
            switch (binding->kind) {
            case DOE_BIND_BUFFER: table=DOE_COMPILER_TEXT_TABLE_BUFFER; limit=DOE_MTL_BUFFER_SLOTS; break;
            case DOE_BIND_TEXTURE: case DOE_BIND_EXTERNAL_TEXTURE: case DOE_BIND_STORAGE_TEXTURE:
                table=DOE_COMPILER_TEXT_TABLE_TEXTURE; limit=DOE_MTL_TEXTURE_SLOTS; break;
            case DOE_BIND_SAMPLER: table=DOE_COMPILER_TEXT_TABLE_SAMPLER; limit=DOE_MTL_SAMPLER_SLOTS; break;
            default: return false;
            }
            if (slot>=limit) return false;
            result.tables[i]=table; result.slots[i]=slot;
        }
        *map=result;
        *out=(doe_mtl_shader_stage){view->text,view->text_size,stage->name,UINT32_MAX};
        return true;
    }
    size_t selected = (size_t)(stage->entry - view->entries);
    const DoeCompilerMetalEntryV12 *entry = NULL;
    for (size_t i = 0; i < view->metal_entry_count; ++i) {
        if (view->metal_entries[i].entry_index != selected) continue;
        if (entry) return false;
        entry = &view->metal_entries[i];
    }
    if (!entry || entry->stage != expected_stage || !memchr(entry->name, 0, sizeof(entry->name)) ||
        entry->first_binding > view->metal_binding_count ||
        entry->binding_count > view->metal_binding_count - entry->first_binding ||
        entry->binding_count != stage->entry->resource_reference_count ||
        entry->binding_count > NATIVE_MAX_PIPELINE_BINDINGS || entry->buffer_count > DOE_COMPILER_METAL_SIZES_SLOT)
        return false;
    if (entry->sizes_slot == UINT32_MAX) {
        if (entry->sizes_bytes) return false;
    } else if (entry->sizes_slot >= DOE_MTL_BUFFER_SLOTS || !entry->sizes_bytes ||
        entry->sizes_bytes % sizeof(uint32_t) || entry->sizes_bytes > DOE_MTL_BUFFER_SLOTS * sizeof(uint32_t)) return false;
    uint32_t texture_count = 0, sampler_count = 0;
    for (size_t i = 0; i < entry->binding_count; ++i) {
        const DoeCompilerMetalBindingV12 *binding = &view->metal_bindings[entry->first_binding + i];
        if (binding->entry_index != selected || binding->resource_index >= view->counts.resources) return false;
        if (binding->table == DOE_COMPILER_TEXT_TABLE_TEXTURE) ++texture_count;
        else if (binding->table == DOE_COMPILER_TEXT_TABLE_SAMPLER) ++sampler_count;
        else if (binding->table != DOE_COMPILER_TEXT_TABLE_BUFFER) return false;
    }
    *map = (native_metal_stage_map){0};
    for (size_t i = 0; i < view->translation_entry_count; ++i)
        if (view->translation_entries[i].entry_index == selected)
            map->immediate_bytes = (view->translation_entries[i].immediate_bytes + 3u) & ~3u;
    uint32_t buffers = 0, required_sizes_bytes = 0;
    for (size_t i = 0; i < stage->entry->resource_reference_count; ++i) {
        uint32_t resource_index = view->entry_references[stage->entry->first_resource_reference + i];
        if (resource_index >= view->counts.resources) return false;
        const DoeCompilerResourceV4 *resource = &view->resources[resource_index];
        size_t binding = 0;
        while (binding < pipeline->binding_count && (pipeline->bindings[binding].group != resource->group ||
            pipeline->bindings[binding].binding != resource->binding)) ++binding;
        if (binding == pipeline->binding_count || map->tables[binding]) return false;
        const DoeCompilerMetalBindingV12 *target = NULL;
        for (size_t j = 0; j < entry->binding_count; ++j) {
            const DoeCompilerMetalBindingV12 *candidate = &view->metal_bindings[entry->first_binding + j];
            if (candidate->entry_index != selected || candidate->resource_index >= view->counts.resources) return false;
            if (candidate->resource_index != resource_index) continue;
            if (target) return false;
            target = candidate;
        }
        uint32_t table;
        switch (pipeline->bindings[binding].kind) {
        case DOE_BIND_BUFFER: table = DOE_COMPILER_TEXT_TABLE_BUFFER; break;
        case DOE_BIND_TEXTURE:
        case DOE_BIND_EXTERNAL_TEXTURE:
        case DOE_BIND_STORAGE_TEXTURE: table = DOE_COMPILER_TEXT_TABLE_TEXTURE; break;
        case DOE_BIND_SAMPLER: table = DOE_COMPILER_TEXT_TABLE_SAMPLER; break;
        default: return false;
        }
        uint32_t limit = table == DOE_COMPILER_TEXT_TABLE_BUFFER ? DOE_MTL_BUFFER_SLOTS :
            table == DOE_COMPILER_TEXT_TABLE_TEXTURE ? DOE_MTL_TEXTURE_SLOTS : DOE_MTL_SAMPLER_SLOTS;
        uint32_t prefix = table == DOE_COMPILER_TEXT_TABLE_BUFFER ? entry->buffer_count :
            table == DOE_COMPILER_TEXT_TABLE_TEXTURE ? texture_count : sampler_count;
        if (!target || target->table != table || target->slot >= limit || target->slot >= prefix ||
            (table == DOE_COMPILER_TEXT_TABLE_BUFFER && (target->slot == entry->sizes_slot ||
                (map->immediate_bytes && target->slot == DOE_COMPILER_METAL_IMMEDIATE_SLOT)))) return false;
        /* Reflection owns runtime-tail identity; the selected compiler row
         * must agree exactly. The private size table uses the physical buffer
         * slot, never a logical binding number or another packed index. */
        bool runtime_tail = (resource->flags & DOE_COMPILER_RESOURCE_RUNTIME_TAIL) != 0;
        if (runtime_tail != (target->size_index != UINT32_MAX)) return false;
        if (runtime_tail) {
            if (table != DOE_COMPILER_TEXT_TABLE_BUFFER || target->size_index != target->slot ||
                entry->sizes_slot != DOE_COMPILER_METAL_SIZES_SLOT) return false;
            uint32_t bytes = (target->slot + 1) * (uint32_t)sizeof(uint32_t);
            if (required_sizes_bytes < bytes) required_sizes_bytes = bytes;
        }
        for (size_t j = 0; j < pipeline->binding_count; ++j)
            if (map->tables[j] == table && map->slots[j] == target->slot) return false;
        map->tables[binding] = table;
        map->slots[binding] = target->slot;
        if (table == DOE_COMPILER_TEXT_TABLE_BUFFER) ++buffers;
    }
    if (buffers != entry->buffer_count || entry->sizes_bytes != required_sizes_bytes ||
        entry->sizes_slot != (required_sizes_bytes ? DOE_COMPILER_METAL_SIZES_SLOT : UINT32_MAX)) return false;
    *out = (doe_mtl_shader_stage){view->text, view->text_size, entry->name, entry->sizes_slot};
    return true;
}
