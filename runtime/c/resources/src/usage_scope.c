/* SPDX-License-Identifier: MIT
 * A dispatch is one WebGPU usage scope. Buffer usage identity is the whole
 * object; writable-binding alias checks additionally use effective byte ranges.
 * Texture identity is object + overlapping mip/layer/aspect subresources. */
#include "internal.h"

typedef struct dispatch_use {
    doe_resource_buffer *buffer;
    doe_resource_view *view;
    uint64_t offset, size;
    bool writable;
} dispatch_use;

enum { MAX_DISPATCH_USES = DOE_RESOURCE_MAX_GROUPS * DOE_RESOURCE_MAX_BINDINGS * 2 + 1 };

static bool overlaps(uint64_t start_a, uint64_t size_a, uint64_t start_b, uint64_t size_b)
{
    /* Resolved ranges are admitted already; subtraction avoids unchecked sums. */
    if (!size_a || !size_b) return false;
    return start_a <= start_b ? start_b - start_a < size_a : start_a - start_b < size_b;
}

static bool views_overlap(const doe_resource_view *a, const doe_resource_view *b)
{
    if (a->texture != b->texture) return false;
    if (a->desc.aspect != WGPUTextureAspect_All && b->desc.aspect != WGPUTextureAspect_All &&
        a->desc.aspect != b->desc.aspect) return false;
    return overlaps(a->desc.base_mip, a->desc.mip_count, b->desc.base_mip, b->desc.mip_count) &&
        overlaps(a->desc.base_layer, a->desc.layer_count, b->desc.base_layer, b->desc.layer_count);
}

static doe_resource_status add_use(dispatch_use *uses, size_t *count, dispatch_use next)
{
    if (*count == MAX_DISPATCH_USES) return DOE_RESOURCE_OVERFLOW;
    for (size_t i = 0; i < *count; i++) {
        const dispatch_use *previous = &uses[i];
        if (!next.writable && !previous->writable) continue;
        if (next.buffer) {
            if (next.buffer != previous->buffer) continue;
            /* Read + storage-write is incompatible for the WHOLE buffer,
             * even if their binding ranges are disjoint. */
            if (next.writable != previous->writable) return DOE_RESOURCE_USAGE;
            /* Multiple storage-write usages are permitted, but a single
             * dispatch cannot bind overlapping writable ranges. */
            if (overlaps(next.offset, next.size, previous->offset, previous->size))
                return DOE_RESOURCE_ALIAS;
        } else if (previous->view && views_overlap(next.view, previous->view)) {
            return next.writable == previous->writable ? DOE_RESOURCE_ALIAS : DOE_RESOURCE_USAGE;
        }
    }
    uses[(*count)++] = next;
    return DOE_RESOURCE_OK;
}

static doe_resource_status add_binding(dispatch_use *uses, size_t *count,
    const doe_resource_layout_entry *entry, const doe_resource_binding *binding)
{
    dispatch_use use = {0};
    switch (binding->kind) {
    case DOE_BIND_BUFFER:
        use.buffer = binding->resource.buffer.buffer;
        use.offset = binding->resource.buffer.offset;
        use.size = binding->resource.buffer.size;
        use.writable = entry->buffer_type == WGPUBufferBindingType_Storage;
        return add_use(uses, count, use);
    case DOE_BIND_TEXTURE:
    case DOE_BIND_STORAGE_TEXTURE:
        use.view = binding->resource.view;
        use.writable = binding->kind == DOE_BIND_STORAGE_TEXTURE &&
            entry->storage_access != WGPUStorageTextureAccess_ReadOnly;
        return add_use(uses, count, use);
    case DOE_BIND_EXTERNAL_TEXTURE: {
        const doe_resource_external_texture *external = binding->resource.external_texture;
        for (size_t i = 0; i < external->plane_count; i++) {
            use.view = external->planes[i];
            doe_resource_status status = add_use(uses, count, use);
            if (status != DOE_RESOURCE_OK) return status;
        }
        return DOE_RESOURCE_OK;
    }
    case DOE_BIND_SAMPLER:
        return DOE_RESOURCE_OK;
    }
    return DOE_RESOURCE_UNSUPPORTED;
}

doe_resource_status resource_dispatch_bindings_validate(const doe_resource_pipeline_layout *layout,
    const doe_resource_dispatch_group *groups, size_t group_count,
    doe_resource_buffer *indirect, bool *has_dynamic_offsets)
{
    if (!layout || !has_dynamic_offsets || (group_count && !groups)) return DOE_RESOURCE_INVALID_ARGUMENT;
    if (group_count != layout->count || group_count > DOE_RESOURCE_MAX_GROUPS) return DOE_RESOURCE_LAYOUT;
    dispatch_use uses[MAX_DISPATCH_USES];
    size_t use_count = 0;
    bool dynamic = false;
    if (indirect) {
        doe_resource_status status = add_use(uses, &use_count,
            (dispatch_use){.buffer = indirect, .size = indirect->desc.size});
        if (status != DOE_RESOURCE_OK) return status;
    }
    for (size_t i = 0; i < group_count; i++) {
        const doe_resource_bind_group *group = groups[i].group;
        if (!group || !doe_resource_layout_compatible(layout->groups[i], group->layout)) return DOE_RESOURCE_LAYOUT;
        doe_resource_binding resolved[DOE_RESOURCE_MAX_BINDINGS];
        doe_resource_status status = doe_resource_bind_group_resolve(group, groups[i].dynamic_offsets,
            groups[i].dynamic_offset_count, resolved, DOE_RESOURCE_MAX_BINDINGS);
        if (status != DOE_RESOURCE_OK) return status;
        for (size_t j = 0; j < group->count; j++) {
            status = add_binding(uses, &use_count, &group->layout->entries[j], &resolved[j]);
            if (status != DOE_RESOURCE_OK) return status;
        }
        if (groups[i].dynamic_offset_count) dynamic = true;
    }
    *has_dynamic_offsets = dynamic;
    return DOE_RESOURCE_OK;
}
