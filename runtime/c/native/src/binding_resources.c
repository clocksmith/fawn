/* SPDX-License-Identifier: MIT */
#include "internal.h"

/* Resource resolution is canonical and shared. Target-specific conversion below
 * does not recreate dynamic offsets, empty slots or cross-binding usage policy. */
doe_resource_status native_resolve_resources(const native_pipeline *pipeline,
    doe_resource_pipeline_layout *layout_owner, doe_resource_bind_group *const *groups,
    const doe_resource_dispatch_offsets *offsets, doe_resource_binding *out)
{
    doe_resource_binding resolved[DOE_RESOURCE_MAX_GROUPS][DOE_RESOURCE_MAX_BINDINGS];
    size_t counts[DOE_RESOURCE_MAX_GROUPS] = {0};
    for (size_t i = 0; i < DOE_RESOURCE_MAX_GROUPS; ++i) {
        if (!doe_resource_pipeline_layout_group_required(layout_owner, (uint32_t)i)) continue;
        (void)doe_resource_bind_group_bindings(groups[i], &counts[i]);
        doe_resource_status status = doe_resource_bind_group_resolve(groups[i], offsets ? offsets->values[i] : NULL,
            offsets ? offsets->counts[i] : 0, resolved[i], DOE_RESOURCE_MAX_BINDINGS);
        if (status != DOE_RESOURCE_OK) return status;
    }
    for (size_t i = 0; i < pipeline->binding_count; ++i) {
        native_binding layout = pipeline->bindings[i];
        if (layout.group >= DOE_RESOURCE_MAX_GROUPS) return DOE_RESOURCE_RANGE;
        const doe_resource_binding *binding = NULL;
        for (size_t j = 0; j < counts[layout.group]; ++j)
            if (resolved[layout.group][j].binding == layout.binding) binding = &resolved[layout.group][j];
        if (!binding || binding->kind != layout.kind) return DOE_RESOURCE_LAYOUT;
        if (binding->kind == DOE_BIND_BUFFER && binding->resource.buffer.size < pipeline->minimum_sizes[i])
            return DOE_RESOURCE_RANGE;
        out[i] = *binding;
    }
    return DOE_RESOURCE_OK;
}

bool native_vulkan_binding_layout(const native_pipeline *pipeline, doe_vk_binding_layout *out)
{
    for (size_t i = 0; i < pipeline->binding_count; ++i) {
        native_binding binding = pipeline->bindings[i];
        doe_vk_binding_kind kind;
        switch (binding.kind) {
        case DOE_BIND_BUFFER:
            kind = binding.buffer_type == WGPUBufferBindingType_Uniform ? DOE_VK_BINDING_UNIFORM : DOE_VK_BINDING_STORAGE;
            break;
        case DOE_BIND_TEXTURE: kind = DOE_VK_BINDING_SAMPLED_TEXTURE; break;
        case DOE_BIND_STORAGE_TEXTURE: kind = DOE_VK_BINDING_STORAGE_TEXTURE; break;
        case DOE_BIND_SAMPLER: kind = DOE_VK_BINDING_SAMPLER; break;
        default: return false;
        }
        out[i] = (doe_vk_binding_layout){binding.group,binding.binding,kind};
    }
    return true;
}

doe_vk_status native_resolve_bindings(const native_pipeline *pipeline, doe_resource_pipeline_layout *layout,
    doe_resource_bind_group *const *groups, const doe_resource_dispatch_offsets *offsets,
    doe_vk_binding_resource *bindings)
{
    doe_resource_binding resolved[DOE_RESOURCE_MAX_GROUPS * DOE_RESOURCE_MAX_BINDINGS];
    doe_resource_status status = native_resolve_resources(pipeline,layout,groups,offsets,resolved);
    if (status != DOE_RESOURCE_OK) return DOE_VK_INVALID_ARGUMENT;
    doe_vk_binding_layout native_layout[DOE_RESOURCE_MAX_GROUPS * DOE_RESOURCE_MAX_BINDINGS];
    if (!native_vulkan_binding_layout(pipeline,native_layout)) return DOE_VK_UNSUPPORTED;
    for (size_t i = 0; i < pipeline->binding_count; ++i) {
        const doe_resource_binding *binding = &resolved[i];
        bindings[i].kind = native_layout[i].kind;
        if (binding->kind == DOE_BIND_BUFFER) {
            native_buffer *buffer = doe_resource_buffer_native(binding->resource.buffer.buffer);
            bindings[i].value.buffer = (doe_vk_buffer_binding){buffer->handle.vulkan,
                binding->resource.buffer.offset,binding->resource.buffer.size};
        } else if (binding->kind == DOE_BIND_SAMPLER) {
            native_sampler *sampler = doe_resource_sampler_native(binding->resource.sampler);
            bindings[i].value.sampler = sampler->sampler;
        } else {
            native_view *view = doe_resource_view_native(binding->resource.view);
            bindings[i].value.view = view->view;
        }
    }
    return DOE_VK_OK;
}
