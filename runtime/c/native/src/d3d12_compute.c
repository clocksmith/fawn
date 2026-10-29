/* SPDX-License-Identifier: MIT */
#include "d3d12_graphics_internal.h"
#include <stdlib.h>
#include <string.h>

native_failure native_d3d12_pipeline_create(native_pipeline *pipeline, const native_stage *stage,
    const doe_dx_shader *shader)
{
#if defined(_WIN32)
    native_d3d12_compute *map = calloc(1, sizeof(*map));
    if (!map) return native_failure_status(NATIVE_OUT_OF_MEMORY);
    native_d3d12_layout layout;
    if (!native_d3d12_stage_bindings(pipeline,stage,&layout,map) ||
        !native_d3d12_capabilities(map,&pipeline->device->backend.value.d3d12.caps)) {
        free(map);
        return native_failure_status(NATIVE_UNSUPPORTED);
    }
    size_t bytes = 0;
    const uint8_t *code = doe_dx_shader_bytes(shader, &bytes);
    if (!code || !bytes) {
        free(map);
        return native_failure_status(NATIVE_INVALID_ARGUMENT);
    }
    doe_dx_dynamic_pipeline_desc descriptor = {.resources={.buffers={.bytecode=code,.bytecode_size=bytes,
        .bindings=layout.buffers,.binding_count=map->buffer_count},.textures=layout.textures,.texture_count=map->texture_count},
        .texture_contracts=layout.contracts,.samplers=layout.samplers,.sampler_count=map->sampler_count,
        .immediate_bytes=pipeline->immediate_bytes,.immediate_required_bytes=pipeline->immediate_required_bytes,
        .logical_size_count=map->logical_size_count,.logical_size_indices=map->logical_size_indices};
    doe_dx_error error = {0};
    doe_dx_status status = doe_dx_pipeline_create_dynamic(pipeline->device->backend.value.d3d12.context,
        &descriptor, &pipeline->handle.d3d12, &error);
    native_failure failure = native_failure_d3d12(status,&error);
    if (failure.status == NATIVE_OK) pipeline->d3d12_compute = map;
    else free(map);
    return failure;
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
    if (pipeline->d3d12_graphics) {
        doe_dx_pipeline_release(pipeline->d3d12_graphics->draw_normalize);
        doe_dx_pipeline_release(pipeline->d3d12_graphics->index_scan);
    }
#endif
    free(pipeline->d3d12_compute);
    pipeline->d3d12_compute = NULL;
    free(pipeline->d3d12_graphics);
    pipeline->d3d12_graphics = NULL;
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
    native_d3d12_snapshot bindings;
    native_failure failure = native_d3d12_binding_snapshot(record,&bindings);
    if (failure.status != NATIVE_OK) return failure;
    if (!record->value.dispatch.x || !record->value.dispatch.y || !record->value.dispatch.z)
        return native_failure_status(NATIVE_OK);
    doe_dx_buffer *metadata = NULL;
    if (bindings.metadata_index != UINT32_MAX) {
        failure = dispatch_metadata(commands, record, &metadata);
        if (failure.status == NATIVE_OK)
            bindings.buffers[bindings.metadata_index] = (doe_dx_buffer_binding){metadata, 0, 256};
    }
    if (failure.status == NATIVE_OK) {
        doe_dx_error error = {0};
        doe_dx_status status = doe_dx_commands_dispatch_dynamic(commands->value.d3d12.commands,
            pipeline->handle.d3d12,&bindings.values, record->value.dispatch.x,
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
