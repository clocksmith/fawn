/* SPDX-License-Identifier: MIT
 * Persistent render-pass/bundle usages retain even overwritten, unused setters.
 * No queue or object registry: storage moves to the final immutable record. */
#include "internal.h"
#include <string.h>

/* Stage visibility is deliberately irrelevant to whole-pass usage history. */
static bool same_use(const resource_use *a, const resource_use *b)
{
    return a->buffer == b->buffer && a->view == b->view && a->external == b->external &&
        a->aspects == b->aspects && a->usage == b->usage;
}

static bool already_present(const resource_use *uses, size_t count, const resource_use *use)
{
    for (size_t i = 0; i < count; i++) if (same_use(&uses[i], use)) return true;
    return false;
}

doe_resource_status resource_scope_add(const doe_resource_config *config, resource_usage_scope *scope,
    const resource_use *uses, size_t count)
{
    size_t unique = 0;
    for (size_t i = 0; i < count; i++) {
        for (size_t j = 0; j < scope->count; j++) {
            doe_resource_status status = resource_use_conflict(&scope->uses[j], &uses[i], false);
            if (status != DOE_RESOURCE_OK) return status;
        }
        for (size_t j = 0; j < i; j++) {
            doe_resource_status status = resource_use_conflict(&uses[j], &uses[i], false);
            if (status != DOE_RESOURCE_OK) return status;
        }
        if (!already_present(scope->uses, scope->count, &uses[i]) && !already_present(uses, i, &uses[i])) unique++;
    }
    if (unique > SIZE_MAX - scope->count) return DOE_RESOURCE_OVERFLOW;
    size_t required = scope->count + unique;
    if (required > scope->capacity) {
        size_t capacity = scope->capacity ? scope->capacity : 8;
        while (capacity < required) {
            if (capacity > SIZE_MAX / 2) { capacity = required; break; }
            capacity *= 2;
        }
        if (capacity > SIZE_MAX / sizeof(*scope->uses)) return DOE_RESOURCE_OVERFLOW;
        resource_use *storage = resource_allocate(config, capacity * sizeof(*storage), _Alignof(resource_use));
        if (!storage) return DOE_RESOURCE_OUT_OF_MEMORY;
        if (scope->count) memcpy(storage, scope->uses, scope->count * sizeof(*storage));
        resource_free(config, scope->uses, scope->capacity * sizeof(*storage), _Alignof(resource_use));
        scope->uses = storage;
        scope->capacity = capacity;
    }
    for (size_t i = 0; i < count; i++) {
        if (already_present(scope->uses, scope->count, &uses[i])) continue;
        doe_resource_buffer_retain(uses[i].buffer);
        doe_resource_view_retain(uses[i].view);
        doe_resource_external_texture_retain(uses[i].external);
        scope->uses[scope->count++] = uses[i];
    }
    return DOE_RESOURCE_OK;
}

doe_resource_status resource_scope_group(const doe_resource_config *config, resource_usage_scope *scope,
    const doe_resource_dispatch_group *group)
{
    resource_use stack[RESOURCE_STACK_USES], *uses;
    size_t count, capacity;
    doe_resource_status status = resource_group_uses(config, group, stack, &uses, &count, &capacity);
    if (status != DOE_RESOURCE_OK) return status;
    status = resource_scope_add(config, scope, uses, count);
    resource_group_uses_free(config, uses, capacity, stack);
    return status;
}

doe_resource_status resource_scope_input(const doe_resource_config *config, resource_usage_scope *scope,
    doe_resource_buffer *buffer)
{
    resource_use use = {.buffer = buffer, .size = buffer->desc.size};
    return resource_scope_add(config, scope, &use, 1);
}

doe_resource_status resource_scope_targets(const doe_resource_config *config, resource_usage_scope *scope,
    const doe_resource_render_targets *targets)
{
    resource_use uses[2 * DOE_RESOURCE_MAX_COLOR_ATTACHMENTS + 2];
    size_t count = 0;
    for (uint32_t i = 0; i < targets->color_count; i++) {
        const doe_resource_color_attachment *color = &targets->colors[i];
        if (color->view) uses[count++] = (resource_use){.view = color->view, .aspects = 1, .usage = RESOURCE_ATTACHMENT};
        if (color->resolve_target) uses[count++] = (resource_use){.view = color->resolve_target,
            .aspects = 1, .usage = RESOURCE_ATTACHMENT};
    }
    if (targets->depth_stencil) {
        unsigned aspects = resource_view_aspects(targets->depth_stencil);
        if (aspects & 2) uses[count++] = (resource_use){.view = targets->depth_stencil, .aspects = 2,
            .usage = targets->depth_read_only ? RESOURCE_READ : RESOURCE_ATTACHMENT};
        if (aspects & 4) uses[count++] = (resource_use){.view = targets->depth_stencil, .aspects = 4,
            .usage = targets->stencil_read_only ? RESOURCE_READ : RESOURCE_ATTACHMENT};
    }
    return resource_scope_add(config, scope, uses, count);
}

/* executeBundles admits a destroyed texture like setBindGroup; `submit` rejects it. */
doe_resource_status resource_scope_validate(const resource_usage_scope *scope, bool submit)
{
    for (size_t i = 0; i < scope->count; i++) {
        const resource_use *use = &scope->uses[i];
        if (use->buffer) {
            if (!resource_buffer_available(use->buffer)) return DOE_RESOURCE_UNAVAILABLE;
            if (use->buffer->mapped || use->buffer->map_pending) return DOE_RESOURCE_STATE;
        }
        if (use->view && !(submit ? resource_texture_available(use->view->texture) :
                resource_texture_live(use->view->texture))) return DOE_RESOURCE_UNAVAILABLE;
        if (use->external && !resource_external_available(use->external)) return DOE_RESOURCE_UNAVAILABLE;
    }
    return DOE_RESOURCE_OK;
}

void resource_scope_release(const doe_resource_config *config, resource_usage_scope *scope)
{
    for (size_t i = 0; i < scope->count; i++) {
        doe_resource_buffer_release(scope->uses[i].buffer);
        doe_resource_view_release(scope->uses[i].view);
        doe_resource_external_texture_release(scope->uses[i].external);
    }
    resource_free(config, scope->uses, scope->capacity * sizeof(*scope->uses), _Alignof(resource_use));
    *scope = (resource_usage_scope){0};
}
