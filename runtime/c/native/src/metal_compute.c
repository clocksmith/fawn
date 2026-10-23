/* SPDX-License-Identifier: MIT */
#include "internal.h"
#include <string.h>

native_result native_metal_pipeline_create(native_pipeline *pipeline, const native_stage *stage)
{
    const doe_shader_view *view = &stage->view;
    if (view->target != DOE_SHADER_METAL || view->target_language_version != 300 ||
        !view->text || !view->text_size || pipeline->binding_count > DOE_MTL_BUFFER_SLOTS)
        return NATIVE_UNSUPPORTED;
    size_t selected = (size_t)(stage->entry - view->entries);
    const DoeCompilerTextEntryV10 *entry = NULL;
    for (size_t i = 0; i < view->target_entry_count; ++i) {
        if (view->target_entries[i].entry_index != selected) continue;
        if (entry) return NATIVE_INVALID_ARGUMENT;
        entry = &view->target_entries[i];
    }
    if (!entry || entry->stage != DOE_COMPILER_STAGE_COMPUTE ||
        !memchr(entry->name,0,sizeof(entry->name))) return NATIVE_INVALID_ARGUMENT;
    if (entry->sizes_slot != UINT32_MAX && (entry->sizes_slot >= DOE_MTL_BUFFER_SLOTS ||
        entry->sizes_bytes > DOE_MTL_BUFFER_SLOTS * sizeof(uint32_t))) return NATIVE_UNSUPPORTED;
    for (size_t i = 0; i < pipeline->binding_count; ++i) {
        const native_binding *binding = &pipeline->bindings[i];
        if (binding->kind != DOE_BIND_BUFFER) return NATIVE_UNSUPPORTED;
        const DoeCompilerTextBindingV10 *target = NULL;
        for (size_t j = 0; j < view->target_binding_count; ++j) {
            const DoeCompilerTextBindingV10 *candidate = &view->target_bindings[j];
            if (candidate->resource_index >= view->counts.resources) return NATIVE_INVALID_ARGUMENT;
            const DoeCompilerResourceV4 *resource = &view->resources[candidate->resource_index];
            if (resource->group == binding->group && resource->binding == binding->binding) {
                if (target) return NATIVE_INVALID_ARGUMENT;
                target = candidate;
            }
        }
        if (!target || target->table != DOE_COMPILER_TEXT_TABLE_BUFFER ||
            target->slot >= DOE_MTL_BUFFER_SLOTS || target->slot == entry->sizes_slot)
            return NATIVE_UNSUPPORTED;
        /* The backend's existing size payload is indexed by buffer slot. An
         * incompatible compiler layout rejects, never guesses a second mapping. */
        if (target->size_index != UINT32_MAX && (target->size_index != target->slot ||
            entry->sizes_slot == UINT32_MAX || target->size_index >= entry->sizes_bytes / sizeof(uint32_t)))
            return NATIVE_UNSUPPORTED;
        for (size_t j = 0; j < i; ++j)
            if (pipeline->metal_slots[j] == target->slot) return NATIVE_INVALID_ARGUMENT;
        pipeline->metal_slots[i] = target->slot;
    }
    doe_mtl_pipeline_desc descriptor = {.msl=view->text,.msl_length=view->text_size,
        .entrypoint=entry->name,.workgroup={stage->entry->workgroup_size[0],
            stage->entry->workgroup_size[1],stage->entry->workgroup_size[2]},
        .sizes_slot=entry->sizes_slot};
    return native_from_metal(doe_mtl_pipeline_create(pipeline->device->backend.value.metal.context,
        &descriptor,&pipeline->handle.metal,NULL));
}

native_result native_metal_dispatch(doe_mtl_commands *commands, const doe_resource_command *record)
{
    native_pipeline *pipeline = doe_resource_pipeline_native(record->value.dispatch.pipeline);
    const WGPULimits *limits = &pipeline->device->backend.limits;
    if (record->value.dispatch.x > limits->maxComputeWorkgroupsPerDimension ||
        record->value.dispatch.y > limits->maxComputeWorkgroupsPerDimension ||
        record->value.dispatch.z > limits->maxComputeWorkgroupsPerDimension) return NATIVE_INVALID_ARGUMENT;
    doe_resource_binding resolved[DOE_RESOURCE_MAX_GROUPS * DOE_RESOURCE_MAX_BINDINGS];
    doe_resource_status status = native_resolve_resources(pipeline,
        doe_resource_pipeline_get_layout(record->value.dispatch.pipeline),record->value.dispatch.groups,
        record->value.dispatch.dynamic_offsets,resolved);
    if (status != DOE_RESOURCE_OK) return NATIVE_INVALID_ARGUMENT;
    if (pipeline->binding_count > DOE_MTL_BUFFER_SLOTS) return NATIVE_UNSUPPORTED;
    doe_mtl_buffer_binding bindings[DOE_MTL_BUFFER_SLOTS];
    for (size_t i = 0; i < pipeline->binding_count; ++i) {
        if (resolved[i].kind != DOE_BIND_BUFFER) return NATIVE_UNSUPPORTED;
        native_buffer *buffer = doe_resource_buffer_native(resolved[i].resource.buffer.buffer);
        bindings[i] = (doe_mtl_buffer_binding){pipeline->metal_slots[i],buffer->handle.metal,
            resolved[i].resource.buffer.offset,resolved[i].resource.buffer.size};
    }
    doe_mtl_dimensions groups = {record->value.dispatch.x,record->value.dispatch.y,record->value.dispatch.z};
    return native_from_metal(doe_mtl_commands_dispatch(commands,pipeline->handle.metal,
        bindings,pipeline->binding_count,groups,1,NULL));
}
