/* SPDX-License-Identifier: MIT */
#include "internal.h"

doe_vk_status native_resolve_bindings(const native_pipeline *pipeline, doe_resource_pipeline_layout *layout_owner,
    doe_resource_bind_group *const *groups, const doe_resource_dispatch_offsets *offsets,
    doe_vk_binding_resource *bindings)
{
    doe_resource_binding resolved[DOE_RESOURCE_MAX_GROUPS][DOE_RESOURCE_MAX_BINDINGS];
    size_t counts[DOE_RESOURCE_MAX_GROUPS] = {0};
    for (size_t i = 0; i < DOE_RESOURCE_MAX_GROUPS; ++i) {
        if (!doe_resource_pipeline_layout_group_required(layout_owner, (uint32_t)i)) continue;
        doe_resource_bind_group *group = groups[i];
        (void)doe_resource_bind_group_bindings(group, &counts[i]);
        if (doe_resource_bind_group_resolve(group, offsets ? offsets->values[i] : NULL,
                offsets ? offsets->counts[i] : 0, resolved[i], DOE_RESOURCE_MAX_BINDINGS) != DOE_RESOURCE_OK)
            return DOE_VK_INVALID_ARGUMENT;
    }
    for (size_t i = 0; i < pipeline->binding_count; ++i) {
        doe_vk_binding_layout layout = pipeline->bindings[i];
        if (layout.group >= DOE_RESOURCE_MAX_GROUPS) return DOE_VK_INVALID_ARGUMENT;
        const doe_resource_binding *binding = NULL;
        for (size_t j = 0; j < counts[layout.group]; ++j)
            if (resolved[layout.group][j].binding == layout.binding) binding = &resolved[layout.group][j];
        if (!binding) return DOE_VK_INVALID_ARGUMENT;
        bindings[i].kind = layout.kind;
        if (layout.kind == DOE_VK_BINDING_UNIFORM || layout.kind == DOE_VK_BINDING_STORAGE) {
            if (binding->kind != DOE_BIND_BUFFER || binding->resource.buffer.size < pipeline->minimum_sizes[i])
                return DOE_VK_INVALID_ARGUMENT;
            native_buffer *buffer = doe_resource_buffer_native(binding->resource.buffer.buffer);
            bindings[i].value.buffer = (doe_vk_buffer_binding){buffer->buffer,
                binding->resource.buffer.offset, binding->resource.buffer.size};
        } else if (layout.kind == DOE_VK_BINDING_SAMPLER) {
            if (binding->kind != DOE_BIND_SAMPLER) return DOE_VK_INVALID_ARGUMENT;
            native_sampler *sampler = doe_resource_sampler_native(binding->resource.sampler);
            bindings[i].value.sampler = sampler->sampler;
        } else {
            doe_resource_binding_kind expected = layout.kind == DOE_VK_BINDING_SAMPLED_TEXTURE ?
                DOE_BIND_TEXTURE : DOE_BIND_STORAGE_TEXTURE;
            if (binding->kind != expected) return DOE_VK_INVALID_ARGUMENT;
            native_view *view = doe_resource_view_native(binding->resource.view);
            bindings[i].value.view = view->view;
        }
    }
    return DOE_VK_OK;
}
