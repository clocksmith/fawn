/* SPDX-License-Identifier: MIT */
#include "d3d12_frontend.h"
#include <stdlib.h>
#include <string.h>

typedef struct native_d3d12_compute {
    size_t public_indices[DOE_DX_MAX_BINDINGS];
    bool uniform[DOE_DX_MAX_BINDINGS];
    size_t count;
    uint32_t dispatch_index;
} native_d3d12_compute;

#if defined(_WIN32)
static bool stage_bindings(const native_pipeline *pipeline, const native_stage *stage,
    doe_dx_binding_layout *layouts, native_d3d12_compute *map)
{
    const doe_shader_view *view = &stage->view;
    const DoeCompilerEntryV5 *entry = stage->entry;
    if (pipeline->binding_count > DOE_DX_MAX_BINDINGS ||
        entry->first_resource_reference > view->counts.entry_references ||
        entry->resource_reference_count > view->counts.entry_references - entry->first_resource_reference ||
        entry->resource_reference_count != pipeline->binding_count) return false;
    for (size_t i = 0; i < pipeline->binding_count; ++i) {
        const native_binding *public = &pipeline->bindings[i];
        if (public->kind != DOE_BIND_BUFFER) return false;
        const DoeCompilerResourceV4 *resource = NULL;
        uint32_t resource_index = UINT32_MAX;
        for (uint32_t j = 0; j < entry->resource_reference_count; ++j) {
            uint32_t index = view->entry_references[entry->first_resource_reference + j];
            if (index >= view->counts.resources) return false;
            const DoeCompilerResourceV4 *candidate = &view->resources[index];
            if (candidate->group != public->group || candidate->binding != public->binding) continue;
            if (resource) return false;
            resource = candidate;
            resource_index = index;
        }
        const DoeCompilerHlslBindingV11 *target = NULL;
        for (size_t j = 0; j < view->hlsl_binding_count; ++j) {
            if (view->hlsl_bindings[j].resource_index != resource_index) continue;
            if (target) return false;
            target = &view->hlsl_bindings[j];
        }
        if (!resource || !target) return false;
        bool uniform = resource->kind == DOE_COMPILER_UNIFORM_BUFFER;
        if (!uniform && resource->kind != DOE_COMPILER_STORAGE_BUFFER) return false;
        uint32_t expected = uniform ? DOE_COMPILER_HLSL_REGISTER_CBV :
            resource->access == DOE_COMPILER_READ ? DOE_COMPILER_HLSL_REGISTER_SRV :
            resource->access == DOE_COMPILER_READ_WRITE ? DOE_COMPILER_HLSL_REGISTER_UAV : 0;
        if (!expected || target->register_kind != expected || target->stride != (uniform ? 0u : 4u) ||
            target->shader_register == UINT32_MAX || target->register_space == UINT32_MAX) return false;
        layouts[i] = (doe_dx_binding_layout){.shader_register = target->shader_register,
            .register_space = target->register_space, .stride = target->stride,
            .access = expected == DOE_COMPILER_HLSL_REGISTER_UAV ? DOE_DX_READ_WRITE : DOE_DX_READ_ONLY,
            .view = uniform ? DOE_DX_UNIFORM : DOE_DX_STRUCTURED};
        map->public_indices[i] = i;
        map->uniform[i] = uniform;
    }
    map->count = pipeline->binding_count;
    map->dispatch_index = UINT32_MAX;
    size_t selected = (size_t)(entry - view->entries);
    const DoeCompilerHlslEntryV11 *native = NULL;
    for (size_t i = 0; i < view->hlsl_entry_count; ++i) {
        if (view->hlsl_entries[i].entry_index != selected) continue;
        if (native) return false;
        native = &view->hlsl_entries[i];
    }
    if (!native || native->stage != DOE_COMPILER_STAGE_COMPUTE || native->flags) return false;
    if (!native->dispatch_bytes)
        return native->dispatch_register == UINT32_MAX && native->dispatch_space == UINT32_MAX;
    if (native->dispatch_bytes != 16 || map->count == DOE_DX_MAX_BINDINGS ||
        native->dispatch_register == UINT32_MAX || native->dispatch_space == UINT32_MAX) return false;
    map->dispatch_index = (uint32_t)map->count;
    layouts[map->count++] = (doe_dx_binding_layout){.shader_register = native->dispatch_register,
        .register_space = native->dispatch_space, .access = DOE_DX_READ_ONLY, .view = DOE_DX_UNIFORM};
    return true;
}
#endif

native_failure native_d3d12_pipeline_create(native_pipeline *pipeline, const native_stage *stage,
    const doe_dx_shader *shader)
{
#if defined(_WIN32)
    native_d3d12_compute *map = calloc(1, sizeof(*map));
    if (!map) return native_failure_status(NATIVE_OUT_OF_MEMORY);
    doe_dx_binding_layout bindings[DOE_DX_MAX_BINDINGS];
    if (!stage_bindings(pipeline, stage, bindings, map)) {
        free(map);
        return native_failure_status(NATIVE_UNSUPPORTED);
    }
    size_t bytes = 0;
    const uint8_t *code = doe_dx_shader_bytes(shader, &bytes);
    if (!code || !bytes) {
        free(map);
        return native_failure_status(NATIVE_INVALID_ARGUMENT);
    }
    doe_dx_pipeline_desc descriptor = {.bytecode = code, .bytecode_size = bytes,
        .bindings = bindings, .binding_count = map->count};
    doe_dx_error error = {0};
    doe_dx_status status = doe_dx_pipeline_create(pipeline->device->backend.value.d3d12.context,
        &descriptor, &pipeline->handle.d3d12, &error);
    if (status == DOE_DX_OK) pipeline->d3d12_compute = map;
    else free(map);
    return native_failure_d3d12(status, &error);
#else
    (void)pipeline;
    (void)stage;
    (void)shader;
    return native_failure_status(NATIVE_UNAVAILABLE);
#endif
}

void native_d3d12_pipeline_close(native_pipeline *pipeline)
{
#if defined(_WIN32)
    doe_dx_pipeline_release(pipeline->handle.d3d12);
#endif
    free(pipeline->d3d12_compute);
    pipeline->d3d12_compute = NULL;
}

#if defined(_WIN32)
static native_failure dispatch_metadata(native_command_handle *commands, const doe_resource_command *record,
    doe_dx_buffer **out)
{
    doe_dx_buffer *upload = NULL, *native = NULL;
    doe_dx_error error = {0};
    doe_dx_status status = doe_dx_buffer_create(commands->value.d3d12.context, 256,
        DOE_DX_UPLOAD, &upload, &error);
    if (status == DOE_DX_OK)
        status = doe_dx_buffer_create(commands->value.d3d12.context, 256, DOE_DX_DEFAULT, &native, &error);
    const uint32_t groups[3] = {record->value.dispatch.x, record->value.dispatch.y, record->value.dispatch.z};
    if (status == DOE_DX_OK) status = doe_dx_buffer_write(upload, 0, groups, sizeof(groups), &error);
    if (status == DOE_DX_OK)
        status = doe_dx_commands_copy(commands->value.d3d12.commands, upload, 0, native, 0, 256, &error);
    native_failure failure = native_failure_d3d12(status, &error);
    doe_dx_buffer_release(upload);
    if (status == DOE_DX_OK) *out = native;
    else doe_dx_buffer_release(native);
    return failure;
}
#endif

native_failure native_d3d12_dispatch(native_command_handle *commands, const doe_resource_command *record)
{
#if defined(_WIN32)
    native_pipeline *pipeline = doe_resource_pipeline_native(record->value.dispatch.pipeline);
    const native_d3d12_compute *map = pipeline->d3d12_compute;
    uint32_t maximum = pipeline->device->backend.limits.maxComputeWorkgroupsPerDimension;
    if (!map || record->value.dispatch.x > maximum || record->value.dispatch.y > maximum ||
        record->value.dispatch.z > maximum) return native_failure_status(NATIVE_INVALID_ARGUMENT);
    doe_resource_binding resolved[NATIVE_MAX_PIPELINE_BINDINGS];
    doe_resource_status resource = native_resolve_resources(pipeline,
        doe_resource_pipeline_get_layout(record->value.dispatch.pipeline), record->value.dispatch.groups,
        record->value.dispatch.dynamic_offsets, resolved);
    if (resource != DOE_RESOURCE_OK) return native_failure_status(NATIVE_INVALID_ARGUMENT);
    if (!record->value.dispatch.x || !record->value.dispatch.y || !record->value.dispatch.z)
        return native_failure_status(NATIVE_OK);
    doe_dx_buffer_binding bindings[DOE_DX_MAX_BINDINGS];
    for (size_t i = 0; i < pipeline->binding_count; ++i) {
        size_t index = map->public_indices[i];
        if (index >= pipeline->binding_count || resolved[index].kind != DOE_BIND_BUFFER)
            return native_failure_status(NATIVE_INVALID_ARGUMENT);
        native_buffer *buffer = doe_resource_buffer_native(resolved[index].resource.buffer.buffer);
        uint64_t offset = resolved[index].resource.buffer.offset;
        uint64_t size = resolved[index].resource.buffer.size;
        if (map->uniform[i]) {
            if (size > UINT64_MAX - 255) return native_failure_status(NATIVE_INVALID_ARGUMENT);
            size = (size + 255) & ~UINT64_C(255);
        }
        native_d3d12_buffer *native = buffer->handle.d3d12;
        if (!native || offset > native->allocation_size || size > native->allocation_size - offset)
            return native_failure_status(NATIVE_INVALID_ARGUMENT);
        bindings[i] = (doe_dx_buffer_binding){native->allocation, offset, size};
    }
    doe_dx_buffer *metadata = NULL;
    native_failure failure = native_failure_status(NATIVE_OK);
    if (map->dispatch_index != UINT32_MAX) {
        failure = dispatch_metadata(commands, record, &metadata);
        if (failure.status == NATIVE_OK)
            bindings[map->dispatch_index] = (doe_dx_buffer_binding){metadata, 0, 256};
    }
    if (failure.status == NATIVE_OK) {
        doe_dx_error error = {0};
        doe_dx_status status = doe_dx_commands_dispatch(commands->value.d3d12.commands,
            pipeline->handle.d3d12, bindings, map->count, record->value.dispatch.x,
            record->value.dispatch.y, record->value.dispatch.z, &error);
        failure = native_failure_d3d12(status, &error);
    }
    doe_dx_buffer_release(metadata);
    return failure;
#else
    (void)commands;
    (void)record;
    return native_failure_status(NATIVE_UNAVAILABLE);
#endif
}
