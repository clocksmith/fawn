/* SPDX-License-Identifier: MIT */
#include "artifact_internal.h"

static bool span(size_t first, size_t count, size_t total)
{
    return first <= total && count <= total-first;
}
static bool name_valid(const doe_shader_view *view, DoeCompilerNameV4 name)
{
    if (!name.length && !name.offset) return true; /* Canonical unnamed type. */
    return name.offset < view->counts.name_bytes &&
        name.length < view->counts.name_bytes-name.offset &&
        view->names[name.offset+name.length] == 0 && !memchr(view->names+name.offset,0,name.length);
}
static bool type_index(uint32_t index, size_t count)
{
    return index == DOE_COMPILER_TYPED_NONE || index < count;
}
/* Structural admission, not a second WGSL semantic engine. These checks bound
 * every persisted reference before downstream typed consumers may dereference it.
 * A conservative cache miss is always preferable to admitting an ambiguous graph. */
bool shader_artifact_valid(const doe_shader_view *view)
{
    const DoeCompilerModuleOutputCounts *n = &view->counts;
    if (n->entries != 1 || view->module.selected_entry != 0 ||
        view->module.schema_version != DOE_COMPILER_ATOMIC_SCHEMA_VERSION ||
        view->translation_entry_count != 1 || !name_valid(view,view->entries[0].name)) return false;
    for (size_t i = 0; i < n->types; ++i) {
        const DoeCompilerTypeV4 *type = &view->types[i];
        if (!name_valid(view,type->name) || type->kind < DOE_COMPILER_TYPE_SCALAR || type->kind > DOE_COMPILER_TYPE_ATOMIC ||
            !span(type->first_member,type->member_count,n->members) || !type_index(type->element_type,n->types)) return false;
    }
    for (size_t i = 0; i < n->members; ++i)
        if (!name_valid(view,view->members[i].name) || view->members[i].type_index >= n->types) return false;
    for (size_t i = 0; i < n->resources; ++i) {
        const DoeCompilerResourceV4 *resource = &view->resources[i];
        if (!name_valid(view,resource->name) || !type_index(resource->type_index,n->types) ||
            !type_index(resource->runtime_array_type,n->types) || resource->kind < 1 || resource->kind > DOE_COMPILER_SAMPLER ||
            resource->reserved) return false;
    }
    for (size_t i = 0; i < n->workgroups; ++i)
        if (!name_valid(view,view->workgroups[i].name) || view->workgroups[i].type_index >= n->types || view->workgroups[i].reserved) return false;
    const DoeCompilerEntryV5 *entry = view->entries;
    if (!entry->name.length || entry->stage < DOE_COMPILER_STAGE_COMPUTE || entry->stage > DOE_COMPILER_STAGE_FRAGMENT ||
        !span(entry->first_resource_reference,entry->resource_reference_count,n->entry_references) ||
        !span(entry->first_workgroup_reference,entry->workgroup_reference_count,n->entry_references)) return false;
    for (size_t j = 0; j < entry->resource_reference_count; ++j)
        if (view->entry_references[entry->first_resource_reference+j] >= n->resources) return false;
    for (size_t j = 0; j < entry->workgroup_reference_count; ++j)
        if (view->entry_references[entry->first_workgroup_reference+j] >= n->workgroups) return false;
    for (size_t i = 0; i < view->handle_count; ++i)
        if (view->handles[i].resource_index >= n->resources || view->handles[i].reserved[0] ||
            view->handles[i].reserved[1] || view->handles[i].reserved[2]) return false;
    for (size_t i = 0; i < view->interface_count; ++i)
        if (view->interfaces[i].entry_index || !name_valid(view,view->interfaces[i].name) || view->interfaces[i].reserved) return false;
    for (size_t i = 0; i < view->sampling_pair_count; ++i)
        if (view->sampling_pairs[i].entry_index || view->sampling_pairs[i].texture_resource_index >= n->resources ||
            view->sampling_pairs[i].sampler_resource_index >= n->resources || view->sampling_pairs[i].reserved) return false;
    const DoeCompilerTranslationEntryV17 *translation = view->translation_entries;
    if (translation->entry_index || translation->flags || translation->reserved) return false;
    if (view->target == DOE_SHADER_METAL) {
        if (n->words || !view->text_size || view->metal_entry_count != 1 || view->hlsl_entry_count || view->hlsl_binding_count) return false;
        const DoeCompilerMetalEntryV12 *metal = view->metal_entries;
        if (metal->entry_index || metal->stage != entry->stage || !memchr(metal->name,0,sizeof(metal->name)) ||
            !span(metal->first_binding,metal->binding_count,view->metal_binding_count)) return false;
        for (size_t i = 0; i < view->metal_binding_count; ++i)
            if (view->metal_bindings[i].entry_index || view->metal_bindings[i].resource_index >= n->resources) return false;
    } else if (view->target == DOE_SHADER_HLSL) {
        if (n->words || !view->text_size || view->hlsl_entry_count != 1 || view->metal_entry_count || view->metal_binding_count) return false;
        const DoeCompilerHlslEntryV11 *hlsl = view->hlsl_entries;
        if (hlsl->entry_index || hlsl->stage != entry->stage || !memchr(hlsl->name,0,sizeof(hlsl->name))) return false;
        for (size_t i = 0; i < view->hlsl_binding_count; ++i)
            if (view->hlsl_bindings[i].resource_index >= n->resources) return false;
    } else if (view->target == DOE_SHADER_SPIRV) {
        if (view->text_size || view->metal_entry_count || view->metal_binding_count || view->hlsl_entry_count || view->hlsl_binding_count ||
            n->words < 5 || view->words[0] != 0x07230203u || !view->words[3] || view->words[4]) return false;
        for (size_t i = 5; i < n->words;) {
            const uint32_t width = view->words[i] >> 16;
            if (!width || width > n->words-i) return false;
            i += width;
        }
    } else return false;
    return true;
}

/* Explicit bounded graph walk: declaration order can put a structure before
 * its scalar/member types. Never recurse through untrusted cache indices. */
bool shader_artifact_acyclic(const doe_shader_view *view, uint8_t *colors, shader_artifact_walk *stack)
{
    size_t edges = 0;
    for (size_t i = 0; i < view->counts.types; ++i) {
        size_t count = view->types[i].member_count + (view->types[i].element_type != DOE_COMPILER_TYPED_NONE ? 1u : 0u);
        if (count > SIZE_MAX-edges) return false;
        edges += count;
    }
    if (edges > view->counts.types + view->counts.members) return false;
    for (size_t root = 0; root < view->counts.types; ++root) {
        if (colors[root]) continue;
        size_t depth = 1;
        stack[0] = (shader_artifact_walk){root,0}; colors[root] = 1;
        while (depth) {
            shader_artifact_walk *frame = &stack[depth-1];
            const DoeCompilerTypeV4 *type = &view->types[frame->type];
            const size_t element = type->element_type != DOE_COMPILER_TYPED_NONE ? 1u : 0u;
            if (frame->next >= element+type->member_count) { colors[frame->type] = 2; --depth; continue; }
            size_t child = element && !frame->next ? type->element_type :
                view->members[type->first_member+frame->next-element].type_index;
            ++frame->next;
            if (colors[child] == 1) return false;
            if (colors[child] == 2) continue;
            if (depth >= view->counts.types) return false;
            stack[depth++] = (shader_artifact_walk){child,0}; colors[child] = 1;
        }
    }
    return true;
}
