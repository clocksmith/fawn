/* SPDX-License-Identifier: MIT
 * Shared WebGPU usage semantics. Buffers are whole usage identities; writable
 * binding aliases additionally compare effective byte ranges within one draw
 * or dispatch. Render scopes merge usages across all their state setters. */
#include "internal.h"

static bool overlaps(uint64_t a, uint64_t count_a, uint64_t b, uint64_t count_b)
{
    if (!count_a || !count_b) return false;
    return a <= b ? b - a < count_a : a - b < count_b;
}

unsigned resource_view_aspects(const doe_resource_view *view)
{
    unsigned aspects = resource_format_aspects(view->texture->desc.format);
    if (view->desc.aspect == WGPUTextureAspect_DepthOnly) aspects &= 2;
    if (view->desc.aspect == WGPUTextureAspect_StencilOnly) aspects &= 4;
    return aspects;
}

doe_resource_status resource_use_conflict(const resource_use *a, const resource_use *b, bool binding_aliases)
{
    /* Whole-scope compatibility is unconditional; only shader binding alias
     * checks are restricted to stages shared by both entries. */
    if (a->usage == RESOURCE_READ && b->usage == RESOURCE_READ) return DOE_RESOURCE_OK;
    if (a->buffer) {
        if (a->buffer != b->buffer) return DOE_RESOURCE_OK;
        if (a->usage != b->usage) return DOE_RESOURCE_USAGE;
        if (binding_aliases && (a->visibility & b->visibility) && overlaps(a->offset, a->size, b->offset, b->size)) return DOE_RESOURCE_ALIAS;
    } else if (b->view && a->view->texture == b->view->texture && (a->aspects & b->aspects) &&
        overlaps(a->view->desc.base_mip, a->view->desc.mip_count, b->view->desc.base_mip, b->view->desc.mip_count) &&
        overlaps(a->view->desc.base_layer, a->view->desc.layer_count, b->view->desc.base_layer, b->view->desc.layer_count)) {
        if (a->usage != b->usage) return DOE_RESOURCE_USAGE;
        if (binding_aliases && (a->visibility & b->visibility)) return DOE_RESOURCE_ALIAS;
    }
    return DOE_RESOURCE_OK;
}

/* Usage of one resolved binding; external textures contribute one use per plane. */
static doe_resource_status binding_uses(const doe_resource_binding *binding, const doe_resource_layout_entry *entry,
    resource_use *uses, size_t *n)
{
    resource_use use = {.visibility = entry->visibility};
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
            uses[(*n)++] = use;
        }
        return DOE_RESOURCE_OK;
    case DOE_BIND_SAMPLER:
        return DOE_RESOURCE_OK;
    default:
        return DOE_RESOURCE_UNSUPPORTED;
    }
    uses[(*n)++] = use;
    return DOE_RESOURCE_OK;
}

/* Appends a group's uses to uses[*count..capacity). */
static doe_resource_status append_group_uses(const doe_resource_dispatch_group *input,
    resource_use *uses, size_t *count, size_t capacity)
{
    const doe_resource_bind_group *group = input->group;
    if (capacity - *count < group->count * RESOURCE_USES_PER_BINDING) return DOE_RESOURCE_OVERFLOW;
    doe_resource_status status = resource_group_check(group, input->dynamic_offsets, input->dynamic_offset_count);
    if (status != DOE_RESOURCE_OK) return status;
    size_t next = 0;
    for (size_t i = 0; i < group->count; i++) {
        doe_resource_binding resolved;
        status = resource_group_resolve_at(group, input->dynamic_offsets, input->dynamic_offset_count, i, &next,
            &resolved);
        if (status == DOE_RESOURCE_OK) status = binding_uses(&resolved, &group->layout->entries[i], uses, count);
        if (status != DOE_RESOURCE_OK) return status;
    }
    return DOE_RESOURCE_OK;
}

/* Scratch use storage: the caller's stack array when it fits, so ordinary
 * validation and its failures never touch the allocator. */
static resource_use *uses_allocate(const doe_resource_config *config, size_t capacity, resource_use *stack)
{
    if (capacity <= RESOURCE_STACK_USES) return stack;
    if (capacity > SIZE_MAX / sizeof(resource_use)) return NULL;
    return resource_allocate(config, capacity * sizeof(resource_use), _Alignof(resource_use));
}

void resource_group_uses_free(const doe_resource_config *config, resource_use *uses, size_t capacity,
    resource_use *stack)
{
    if (uses != stack) resource_free(config, uses, capacity * sizeof(resource_use), _Alignof(resource_use));
}

doe_resource_status resource_group_uses(const doe_resource_config *config, const doe_resource_dispatch_group *input,
    resource_use stack[RESOURCE_STACK_USES], resource_use **uses, size_t *count, size_t *capacity)
{
    if (!config || !input || !input->group || !stack || !uses || !count || !capacity)
        return DOE_RESOURCE_INVALID_ARGUMENT;
    /* Binding counts are below DOE_RESOURCE_MAX_BINDING_NUMBER, so this cannot wrap. */
    size_t storage_capacity = input->group->count * RESOURCE_USES_PER_BINDING;
    resource_use *storage = uses_allocate(config, storage_capacity, stack);
    if (!storage) return DOE_RESOURCE_OUT_OF_MEMORY;
    size_t n = 0;
    doe_resource_status status = append_group_uses(input, storage, &n, storage_capacity);
    if (status != DOE_RESOURCE_OK) {
        resource_group_uses_free(config, storage, storage_capacity, stack);
        return status;
    }
    *uses = storage;
    *count = n;
    *capacity = storage_capacity;
    return DOE_RESOURCE_OK;
}

doe_resource_status resource_shader_bindings_validate(const doe_resource_pipeline_layout *layout,
    const doe_resource_dispatch_group *groups, size_t group_count,
    doe_resource_buffer *indirect, bool *has_dynamic_offsets)
{
    if (!layout || !has_dynamic_offsets || (group_count && !groups)) return DOE_RESOURCE_INVALID_ARGUMENT;
    if (group_count != layout->count || group_count > DOE_RESOURCE_MAX_GROUPS) return DOE_RESOURCE_LAYOUT;
    size_t capacity = 1;
    bool dynamic = false;
    for (size_t i = 0; i < group_count; i++) {
        if (!doe_resource_pipeline_layout_group_required(layout, i)) continue;
        if (!groups[i].group || !doe_resource_layout_compatible(layout->groups[i], groups[i].group->layout))
            return DOE_RESOURCE_LAYOUT;
        /* Binding counts are below DOE_RESOURCE_MAX_BINDING_NUMBER, so this cannot wrap. */
        capacity += groups[i].group->count * RESOURCE_USES_PER_BINDING;
        if (groups[i].dynamic_offset_count) dynamic = true;
    }
    const doe_resource_config *config = &layout->base.config;
    resource_use stack[RESOURCE_STACK_USES];
    resource_use *uses = uses_allocate(config, capacity, stack);
    if (!uses) return DOE_RESOURCE_OUT_OF_MEMORY;
    size_t count = 0;
    if (indirect) uses[count++] = (resource_use){.buffer = indirect, .size = indirect->desc.size};
    doe_resource_status status = DOE_RESOURCE_OK;
    for (size_t i = 0; status == DOE_RESOURCE_OK && i < group_count; i++) {
        if (!doe_resource_pipeline_layout_group_required(layout, i)) continue;
        size_t first = count;
        status = append_group_uses(&groups[i], uses, &count, capacity);
        /* Each new use is checked against every earlier use, including
         * earlier uses of the same group. */
        for (size_t j = first; status == DOE_RESOURCE_OK && j < count; j++)
            for (size_t k = 0; status == DOE_RESOURCE_OK && k < j; k++)
                status = resource_use_conflict(&uses[k], &uses[j], true);
    }
    resource_group_uses_free(config, uses, capacity, stack);
    if (status != DOE_RESOURCE_OK) return status;
    *has_dynamic_offsets = dynamic;
    return DOE_RESOURCE_OK;
}
