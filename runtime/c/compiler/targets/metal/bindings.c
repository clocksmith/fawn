/* SPDX-License-Identifier: MIT
 * One entry-specific native argument plan, consumed by text and ABI12 metadata. */
#include "private.h"
#include "../../src/stage.h"
#include <string.h>

static bool plan_spend(MetalEmitter *e, uint64_t *work) {
    if (*work == 0u) { metal_error(e, "Metal binding plan exceeds bounded traversal work"); return false; }
    --*work;
    return true;
}
bool metal_resource_table(MetalEmitter *e, const ResourceRecord *r, uint32_t *table) {
    switch (r->kind) {
        case DOE_COMPILER_STORAGE_BUFFER: case DOE_COMPILER_UNIFORM_BUFFER:
            if (type_handle(e->module, r->type)) { metal_error(e, "Metal buffer resource cannot have an image handle type"); return false; }
            *table = DOE_COMPILER_TEXT_TABLE_BUFFER;
            return true;
        case DOE_COMPILER_SAMPLED_TEXTURE: case DOE_COMPILER_STORAGE_TEXTURE: case DOE_COMPILER_SAMPLER:
            break;
        default: metal_error(e, "Metal resource kind has no implemented lowering"); return false;
    }
    if (r->type < IR_PRIMITIVE_COUNT || r->type - IR_PRIMITIVE_COUNT >= e->module->type_count || !type_handle(e->module, r->type)) {
        metal_error(e, "Metal image resource has no handle type"); return false;
    }
    const TypeRecord *type = &e->module->types[r->type - IR_PRIMITIVE_COUNT];
    const uint32_t kind = type->kind == TYPE_SAMPLER ? DOE_COMPILER_SAMPLER :
        type->state.image.format == 0u ? DOE_COMPILER_SAMPLED_TEXTURE : DOE_COMPILER_STORAGE_TEXTURE;
    if (kind != r->kind) { metal_error(e, "Metal image resource kind differs from its handle type"); return false; }
    *table = kind == DOE_COMPILER_SAMPLER ? DOE_COMPILER_TEXT_TABLE_SAMPLER : DOE_COMPILER_TEXT_TABLE_TEXTURE;
    return true;
}
bool metal_dense_plan(MetalEmitter *e, DoeMetalMetadata *metadata) {
    const CompilerWorkspace *c = e->module;
    metadata->binding_count = metadata->entry_count = 0u;
    uint64_t work = UINT64_C(67108864);
    for (uint32_t index = 0u; index < c->entry_count; ++index) {
        if (!plan_spend(e, &work)) return false;
        if (e->selected_entry != NO_ID && e->selected_entry != index) continue;
        const EntryRecord *entry = &c->entries[index];
        if (entry->function >= c->function_count || entry->state.resolved.resource_first > c->entry_reference_count ||
            entry->state.resolved.resource_count > c->entry_reference_count - entry->state.resolved.resource_first ||
            metadata->entry_count >= metadata->entry_capacity || metadata->binding_count > UINT32_MAX) {
            metal_error(e, "Metal entry binding plan span is invalid"); return false;
        }
        DoeCompilerMetalEntryV12 *out = &metadata->entries[metadata->entry_count++];
        *out = (DoeCompilerMetalEntryV12){.entry_index=index,.stage=stage_kind(&c->functions[entry->function]),
            .first_binding=(uint32_t)metadata->binding_count,.sizes_slot=NO_ID};
        MetalEmitter name = {.source=e->source,.module=c,.output=out->name,.capacity=sizeof(out->name)-1u};
        metal_entry_name(&name, c->functions[entry->function].name);
        if (name.failed) { e->failed = true; return false; }
        uint32_t counts[3] = {0u,0u,0u}, previous = 0u;
        for (uint32_t j = 0u; j < entry->state.resolved.resource_count; ++j) {
            if (!plan_spend(e, &work)) return false;
            uint32_t resource = c->entry_references[entry->state.resolved.resource_first + j];
            if (resource >= c->resource_count || (j != 0u && resource <= previous) ||
                metadata->binding_count >= metadata->binding_capacity) {
                metal_error(e, "Metal entry resource order or binding capacity is invalid"); return false;
            }
            previous = resource;
            const ResourceRecord *r = &c->resources[resource];
            uint32_t table;
            if (!metal_resource_table(e, r, &table)) return false;
            for (size_t k = out->first_binding; k < metadata->binding_count; ++k) {
                if (!plan_spend(e, &work)) return false;
                const ResourceRecord *other = &c->resources[metadata->bindings[k].resource_index];
                if (other->group == r->group && other->binding == r->binding) {
                    metal_error(e, "Metal entry resources have colliding binding identities"); return false;
                }
            }
            uint32_t *count = &counts[table - 1u];
            const uint32_t limit = table == DOE_COMPILER_TEXT_TABLE_BUFFER ? DOE_METAL_SIZES_SLOT :
                table == DOE_COMPILER_TEXT_TABLE_TEXTURE ? DOE_METAL_TEXTURE_SLOTS : DOE_METAL_SAMPLER_SLOTS;
            if (*count == limit) { metal_error(e, "Metal entry exceeds its native argument namespace capacity"); return false; }
            uint32_t slot = (*count)++;
            uint32_t size = (r->flags & DOE_COMPILER_RESOURCE_RUNTIME_TAIL) != 0u ? slot : NO_ID;
            metadata->bindings[metadata->binding_count++] = (DoeCompilerMetalBindingV12){index,resource,table,slot,size};
            if (size != NO_ID) {
                out->sizes_slot = DOE_METAL_SIZES_SLOT;
                out->sizes_bytes = (size + 1u) * (uint32_t)sizeof(uint32_t);
            }
        }
        out->binding_count = entry->state.resolved.resource_count;
        out->buffer_count = counts[0];
    }
    return true;
}
const DoeCompilerMetalBindingV12 *metal_dense_binding(MetalEmitter *e, uint32_t entry, uint32_t resource) {
    const DoeMetalMetadata *metadata = e->metadata;
    for (size_t i = 0u; i < metadata->entry_count; ++i) {
        if (!plan_spend(e, &e->handle_work_left)) return NULL;
        const DoeCompilerMetalEntryV12 *span = &metadata->entries[i];
        if (span->entry_index != entry) continue;
        size_t low = span->first_binding, high = low + span->binding_count;
        while (low < high) {
            if (!plan_spend(e, &e->handle_work_left)) return NULL;
            size_t middle = low + (high - low) / 2u;
            const DoeCompilerMetalBindingV12 *binding = &metadata->bindings[middle];
            if (binding->resource_index == resource) return binding;
            if (binding->resource_index < resource) low = middle + 1u;
            else high = middle;
        }
        break;
    }
    metal_error(e, "Metal argument has no selected entry binding");
    return NULL;
}

bool metal_dense_selected_resource(MetalEmitter *e, uint32_t resource) {
    for (size_t i = 0u; i < e->metadata->binding_count; ++i) {
        if (!plan_spend(e, &e->handle_work_left)) return false;
        if (e->metadata->bindings[i].resource_index == resource) return true;
    }
    return false;
}
