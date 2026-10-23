/* SPDX-License-Identifier: MIT
 * Shared WebGPU usage semantics. Buffers are whole usage identities; writable
 * binding aliases additionally compare effective byte ranges within one draw
 * or dispatch. Render scopes merge usages across all their state setters. */
#include "internal.h"

enum { MAX_SHADER_USES = DOE_RESOURCE_MAX_GROUPS * RESOURCE_GROUP_USES + 1 };

static bool overlaps(uint64_t a, uint64_t count_a, uint64_t b, uint64_t count_b)
{
    if (!count_a || !count_b) return false;
    return a <= b ? b - a < count_a : a - b < count_b;
}

unsigned resource_view_aspects(const doe_resource_view *view)
{
    if (!resource_format_depth(view->desc.format)) return 1;
    unsigned aspects = 0;
    if (view->desc.aspect != WGPUTextureAspect_StencilOnly &&
        resource_format_aspect(view->desc.format, WGPUTextureAspect_DepthOnly)) aspects |= 2;
    if (view->desc.aspect != WGPUTextureAspect_DepthOnly &&
        resource_format_aspect(view->desc.format, WGPUTextureAspect_StencilOnly)) aspects |= 4;
    return aspects;
}

doe_resource_status resource_use_conflict(const resource_use *a, const resource_use *b, bool binding_aliases)
{
    if (a->usage == RESOURCE_READ && b->usage == RESOURCE_READ) return DOE_RESOURCE_OK;
    if (a->buffer) {
        if (a->buffer != b->buffer) return DOE_RESOURCE_OK;
        if (a->usage != b->usage) return DOE_RESOURCE_USAGE;
        if (binding_aliases && overlaps(a->offset, a->size, b->offset, b->size)) return DOE_RESOURCE_ALIAS;
    } else if (b->view && a->view->texture == b->view->texture && (a->aspects & b->aspects) &&
        overlaps(a->view->desc.base_mip, a->view->desc.mip_count, b->view->desc.base_mip, b->view->desc.mip_count) &&
        overlaps(a->view->desc.base_layer, a->view->desc.layer_count, b->view->desc.base_layer, b->view->desc.layer_count)) {
        if (a->usage != b->usage) return DOE_RESOURCE_USAGE;
        if (binding_aliases) return DOE_RESOURCE_ALIAS;
    }
    return DOE_RESOURCE_OK;
}

doe_resource_status resource_group_uses(const doe_resource_dispatch_group *input,
    resource_use uses[RESOURCE_GROUP_USES], size_t *count)
{
    if (!input || !count) return DOE_RESOURCE_INVALID_ARGUMENT;
    doe_resource_binding resolved[DOE_RESOURCE_MAX_BINDINGS];
    doe_resource_status status = doe_resource_bind_group_resolve(input->group,
        input->dynamic_offsets, input->dynamic_offset_count, resolved, DOE_RESOURCE_MAX_BINDINGS);
    if (status != DOE_RESOURCE_OK) return status;
    const doe_resource_bind_group *group = input->group;
    size_t n = 0;
    for (size_t i = 0; i < group->count; i++) {
        const doe_resource_binding *binding = &resolved[i];
        const doe_resource_layout_entry *entry = &group->layout->entries[i];
        resource_use use = {0};
        switch (binding->kind) {
        case DOE_BIND_BUFFER:
            use.buffer = binding->resource.buffer.buffer;
            use.offset = binding->resource.buffer.offset;
            use.size = binding->resource.buffer.size;
            use.usage = entry->buffer_type == WGPUBufferBindingType_Storage ? RESOURCE_STORAGE : RESOURCE_READ;
            break;
        case DOE_BIND_TEXTURE:
        case DOE_BIND_STORAGE_TEXTURE:
            use.view = binding->resource.view;
            use.aspects = resource_view_aspects(use.view);
            use.usage = binding->kind == DOE_BIND_STORAGE_TEXTURE && entry->storage_access != WGPUStorageTextureAccess_ReadOnly ?
                RESOURCE_STORAGE : RESOURCE_READ;
            break;
        case DOE_BIND_EXTERNAL_TEXTURE:
            use.external = binding->resource.external_texture;
            for (size_t plane = 0; plane < use.external->plane_count; plane++) {
                use.view = use.external->planes[plane];
                use.aspects = resource_view_aspects(use.view);
                uses[n++] = use;
            }
            continue;
        case DOE_BIND_SAMPLER:
            continue;
        default:
            return DOE_RESOURCE_UNSUPPORTED;
        }
        uses[n++] = use;
    }
    *count = n;
    return DOE_RESOURCE_OK;
}

doe_resource_status resource_shader_bindings_validate(const doe_resource_pipeline_layout *layout,
    const doe_resource_dispatch_group *groups, size_t group_count,
    doe_resource_buffer *indirect, bool *has_dynamic_offsets)
{
    if (!layout || !has_dynamic_offsets || (group_count && !groups)) return DOE_RESOURCE_INVALID_ARGUMENT;
    if (group_count != layout->count || group_count > DOE_RESOURCE_MAX_GROUPS) return DOE_RESOURCE_LAYOUT;
    resource_use uses[MAX_SHADER_USES];
    size_t count = 0;
    bool dynamic = false;
    if (indirect) uses[count++] = (resource_use){.buffer = indirect, .size = indirect->desc.size};
    for (size_t i = 0; i < group_count; i++) {
        if (!doe_resource_pipeline_layout_group_required(layout, i)) continue;
        if (!groups[i].group || !doe_resource_layout_compatible(layout->groups[i], groups[i].group->layout))
            return DOE_RESOURCE_LAYOUT;
        resource_use group_uses[RESOURCE_GROUP_USES];
        size_t group_use_count;
        doe_resource_status status = resource_group_uses(&groups[i], group_uses, &group_use_count);
        if (status != DOE_RESOURCE_OK) return status;
        for (size_t j = 0; j < group_use_count; j++) {
            for (size_t k = 0; k < count; k++) {
                status = resource_use_conflict(&uses[k], &group_uses[j], true);
                if (status != DOE_RESOURCE_OK) return status;
            }
            if (count == MAX_SHADER_USES) return DOE_RESOURCE_OVERFLOW;
            uses[count++] = group_uses[j];
        }
        if (groups[i].dynamic_offset_count) dynamic = true;
    }
    *has_dynamic_offsets = dynamic;
    return DOE_RESOURCE_OK;
}
