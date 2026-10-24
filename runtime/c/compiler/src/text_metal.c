/* SPDX-License-Identifier: MIT
 * Derive the public target metadata from the same Metal binding/name owners. */
#include "text_internal.h"
#include "../targets/metal/private.h"
#include <string.h>

bool compiler_text_metal(Source *source, uint32_t selected, DoeTargetScratch scratch,
    DoeCompilerTextBindingV10 *bindings, DoeCompilerTextEntryV10 *entries, TextTargetResult *result) {
    const CompilerWorkspace *c = source->compiler;
    *result = (TextTargetResult){0};
    if (c->entry_count == 0u) return true;
    DoeMetalResult measured;
    if (doe_metal_emit(source, c, selected, scratch, NULL, 0u, &measured) != DOE_COMPILER_OK)
        return false;
    result->required_bytes = measured.required_bytes;
    /* Capacity-sized private metadata staging is explicitly charged by ABI10.
     * table zero is an unvisited source resource, not a second closure graph. */
    const uint64_t limit = UINT64_C(67108864);
    if (c->resource_count > limit)
        return source_error(source, DOE_COMPILER_UNSUPPORTED, source->length,
            "text metadata traversal work limit exceeded");
    memset(bindings, 0, (size_t)c->resource_count * sizeof(*bindings));
    MetalEmitter emitter = {.source = source, .module = c, .selected_entry = selected};
    uint64_t work = limit - c->resource_count;
    for (uint32_t i = 0u; i < c->entry_count; ++i) {
        if (work == 0u) return source_error(source, DOE_COMPILER_UNSUPPORTED, source->length,
            "text metadata traversal work limit exceeded");
        --work;
        if (selected != NO_ID && selected != i) continue;
        const EntryRecord *entry = &c->entries[i];
        const FunctionRecord *function = &c->functions[entry->function];
        DoeCompilerTextEntryV10 *out = &entries[result->entries++];
        *out = (DoeCompilerTextEntryV10){.entry_index = i, .stage = stage_kind(function), .sizes_slot = NO_ID};
        emitter.output = out->name;
        emitter.capacity = sizeof(out->name) - 1u;
        emitter.position = 0u;
        metal_entry_name(&emitter, function->name);
        for (uint32_t j = 0u; j < entry->state.resolved.resource_count; ++j) {
            if (work == 0u) return source_error(source, DOE_COMPILER_UNSUPPORTED, source->length,
                "text metadata traversal work limit exceeded");
            --work;
            const uint32_t resource = c->entry_references[entry->state.resolved.resource_first + j];
            const ResourceRecord *r = &c->resources[resource];
            DoeCompilerTextBindingV10 *binding = &bindings[resource];
            if (binding->table == 0u) {
                uint32_t slot;
                if (!metal_resource_slot(&emitter, r, &slot)) return false;
                const uint32_t table = r->kind == DOE_COMPILER_SAMPLER ? DOE_COMPILER_TEXT_TABLE_SAMPLER :
                    type_handle(c, r->type) ? DOE_COMPILER_TEXT_TABLE_TEXTURE : DOE_COMPILER_TEXT_TABLE_BUFFER;
                *binding = (DoeCompilerTextBindingV10){resource, table, slot,
                    (r->flags & DOE_COMPILER_RESOURCE_RUNTIME_TAIL) != 0u ? slot : NO_ID};
            }
            if (binding->size_index != NO_ID) {
                out->sizes_slot = DOE_METAL_SIZES_SLOT;
                const uint32_t bytes = (binding->size_index + 1u) * (uint32_t)sizeof(uint32_t);
                if (out->sizes_bytes < bytes) out->sizes_bytes = bytes;
            }
        }
        if (emitter.failed) return false;
    }
    if ((uint64_t)c->resource_count > work)
        return source_error(source, DOE_COMPILER_UNSUPPORTED, source->length,
            "text metadata traversal work limit exceeded");
    for (uint32_t i = 0u; i < c->resource_count; ++i)
        if (bindings[i].table != 0u) bindings[result->bindings++] = bindings[i];
    return true;
}

bool compiler_text_metal_entry(Source *source, uint32_t selected, DoeTargetScratch scratch,
    DoeCompilerMetalBindingV12 *bindings, DoeCompilerMetalEntryV12 *entries, TextTargetResult *result) {
    const CompilerWorkspace *c = source->compiler;
    *result = (TextTargetResult){0};
    if (c->entry_count == 0u) return true;
    DoeMetalMetadata metadata = {.bindings=bindings,.binding_capacity=c->limits.entry_references,
        .entries=entries,.entry_capacity=c->limits.entries};
    MetalEmitter planner = {.source=source,.module=c,.selected_entry=selected};
    if (!metal_dense_plan(&planner, &metadata)) return false;
    DoeMetalResult measured;
    if (doe_metal_emit_dense(source, c, selected, scratch, &metadata, NULL, 0u, &measured) != DOE_COMPILER_OK) return false;
    *result = (TextTargetResult){measured.required_bytes,metadata.binding_count,metadata.entry_count};
    return true;
}
