/* SPDX-License-Identifier: MIT */
#include "sampling_internal.h"

static bool accessible_mask(Source *s, TypeId type, uint64_t *mask) {
    CompilerWorkspace *c = s->compiler;
    if (c->limits.traversal == 0u)
        return source_pool_error(s, DOE_COMPILER_POOL_TRAVERSAL, 1u, s->length, "immediate layout traversal requires a frame");
    c->walk_count = 1u;
    c->walk[0] = (WalkItem){.object=type,.parent=0u};
    *mask = 0u;
    uint64_t work = 0u;
    while (c->walk_count != 0u) {
        if (work++ == UINT64_C(67108864)) return source_error(s, DOE_COMPILER_UNSUPPORTED, s->length, "immediate layout work format exhausted");
        WalkItem *frame = &c->walk[c->walk_count-1u];
        if (frame->parent >= 64u) { --c->walk_count; continue; }
        if (frame->object < IR_PRIMITIVE_COUNT) {
            const uint64_t size = type_size(c,frame->object);
            for (uint32_t i = frame->parent; i < 64u && (uint64_t)i < (uint64_t)frame->parent + size; ++i)
                *mask |= UINT64_C(1) << i;
            --c->walk_count;
            continue;
        }
        const TypeRecord *record = &c->types[frame->object-IR_PRIMITIVE_COUNT];
        if (frame->cursor == record->count) { --c->walk_count; continue; }
        const uint32_t child = frame->cursor++;
        const TypeId child_type = type_child(c,frame->object,child);
        const uint64_t relative = record->kind == TYPE_STRUCT ? c->members[record->element+child].offset :
            record->kind == TYPE_MATRIX ? record->state.layout.stride * child : type_size(c,child_type) * child;
        const uint64_t offset = (uint64_t)frame->parent + relative;
        if (offset >= 64u) continue;
        if (c->walk_count == c->limits.traversal)
            return source_pool_error(s, DOE_COMPILER_POOL_TRAVERSAL, (size_t)c->walk_count+1u,
                s->length, "immediate layout traversal capacity exceeded");
        c->walk[c->walk_count++] = (WalkItem){.object=child_type,.parent=(uint32_t)offset};
    }
    return true;
}
bool translation_prepare(Source *s) {
    SamplingContext *sampling = sampling_context(s);
    if (sampling == NULL || sampling->translation == NULL) return true;
    TranslationContext *context = sampling->translation;
    const CompilerWorkspace *c = s->compiler;
    context->count = c->entry_count;
    if (context->count > context->capacity)
        return source_pool_error(s, DOE_COMPILER_POOL_TRANSLATION_ENTRIES,context->count,
            s->length,"translation entry staging capacity exceeded");
    for (uint32_t i = 0u; i < c->entry_count; ++i) {
        DoeCompilerTranslationEntryV17 record = {.entry_index=i};
        const EntryRecord *entry = &c->entries[i];
        for (uint32_t j = 0u; j < entry->state.resolved.workgroup_count; ++j) {
            const WorkgroupRoot *root = &c->workgroups[c->entry_references[entry->state.resolved.workgroup_first+j]];
            if (root->state.resolved.address_space != MODULE_IMMEDIATE) continue;
            record.immediate_bytes = (uint32_t)type_size(c,root->type);
            if (!accessible_mask(s,root->type,&record.immediate_accessible_bytes_low64)) return false;
        }
        context->staging[i] = record;
    }
    return true;
}
