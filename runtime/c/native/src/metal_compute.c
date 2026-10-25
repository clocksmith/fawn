/* SPDX-License-Identifier: MIT */
#include "metal_binding_internal.h"
#include "doe_metal_query.h"
#include <stdlib.h>

native_failure native_metal_pipeline_create(native_pipeline *pipeline, const native_stage *stage)
{
    pipeline->metal_compute = calloc(1, sizeof(*pipeline->metal_compute));
    if (!pipeline->metal_compute) return native_failure_status(NATIVE_OUT_OF_MEMORY);
    doe_mtl_shader_stage selected = {0};
    if (!native_metal_stage_map_build(pipeline, stage, DOE_COMPILER_STAGE_COMPUTE,
        pipeline->metal_compute, &selected)) return native_failure_status(NATIVE_UNSUPPORTED);
    doe_mtl_pipeline_desc descriptor = {.msl=selected.msl, .msl_length=selected.msl_length,
        .entrypoint=selected.entrypoint, .workgroup={stage->entry->workgroup_size[0],
            stage->entry->workgroup_size[1], stage->entry->workgroup_size[2]}, .sizes_slot=selected.sizes_slot};
    doe_mtl_error error = {0};
    doe_mtl_status status = doe_mtl_pipeline_create(pipeline->device->backend.value.metal.context,
        &descriptor, &pipeline->handle.metal, &error);
    return native_failure_metal(status, &error);
}

native_failure native_metal_dispatch(doe_mtl_commands *commands, const doe_resource_command *record)
{
    native_pipeline *pipeline = doe_resource_pipeline_native(record->value.dispatch.pipeline);
    const WGPULimits *limits = &pipeline->device->backend.limits;
    if (record->value.dispatch.x > limits->maxComputeWorkgroupsPerDimension ||
        record->value.dispatch.y > limits->maxComputeWorkgroupsPerDimension ||
        record->value.dispatch.z > limits->maxComputeWorkgroupsPerDimension || !pipeline->metal_compute)
        return native_failure_status(NATIVE_INVALID_ARGUMENT);
    doe_resource_binding resolved[NATIVE_MAX_PIPELINE_BINDINGS];
    doe_resource_status status = native_resolve_resources(pipeline,
        doe_resource_pipeline_get_layout(record->value.dispatch.pipeline), record->value.dispatch.groups,
        record->value.dispatch.dynamic_offsets, resolved);
    if (status != DOE_RESOURCE_OK) return native_failure_status(NATIVE_INVALID_ARGUMENT);
    native_metal_snapshot snapshot = {0};
    native_failure failure = native_metal_binding_snapshot(commands, pipeline, pipeline->metal_compute, resolved, &snapshot);
    if (failure.status != NATIVE_OK) return failure;
    doe_mtl_dimensions groups = {record->value.dispatch.x, record->value.dispatch.y, record->value.dispatch.z};
    const doe_mtl_render_bindings *bindings = &snapshot.bindings;
    doe_mtl_error error = {0};
    const doe_mtl_compute_dispatch dispatch = {.pipeline=pipeline->handle.metal,
        .buffers=bindings->buffers,.buffer_count=bindings->buffer_count,
        .textures=bindings->textures,.texture_count=bindings->texture_count,
        .samplers=bindings->samplers,.sampler_count=bindings->sampler_count,
        .groups=groups,.repeat=1,.immediates={record->value.dispatch.immediates.words,
            pipeline->metal_compute->immediate_bytes,DOE_COMPILER_METAL_IMMEDIATE_SLOT}};
    doe_mtl_status native_status = doe_mtl_commands_compute(commands, &dispatch, &error);
    return native_failure_metal(native_status, &error);
}
