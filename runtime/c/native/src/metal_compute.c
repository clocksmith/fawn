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

native_failure native_metal_dispatch_timed(doe_mtl_commands *commands,
    const doe_resource_command *record, const doe_mtl_pass_timestamps *timestamps)
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
        .groups=groups,.repeat=1,.timestamps=timestamps ? timestamps->set : NULL,
        .timestamp_begin=timestamps ? timestamps->begin : UINT32_MAX,
        .timestamp_end=timestamps ? timestamps->end : UINT32_MAX,
        .immediates={record->value.dispatch.immediates.words,
            pipeline->metal_compute->immediate_bytes}};
    doe_mtl_status native_status = doe_mtl_commands_compute(commands, &dispatch, &error);
    return native_failure_metal(native_status, &error);
}

native_failure native_metal_dispatch(doe_mtl_commands *commands, const doe_resource_command *record)
{
    return native_metal_dispatch_timed(commands, record, NULL);
}

static bool same_dispatch_bindings(const doe_resource_command *a, const doe_resource_command *b)
{
    if (a->value.dispatch.pipeline != b->value.dispatch.pipeline ||
        a->value.dispatch.group_count != b->value.dispatch.group_count ||
        memcmp(a->value.dispatch.groups, b->value.dispatch.groups, sizeof(a->value.dispatch.groups)) != 0)
        return false;
    if (a->value.dispatch.dynamic_offsets == b->value.dispatch.dynamic_offsets) return true;
    return a->value.dispatch.dynamic_offsets != NULL && b->value.dispatch.dynamic_offsets != NULL &&
        memcmp(a->value.dispatch.dynamic_offsets, b->value.dispatch.dynamic_offsets,
            sizeof(*a->value.dispatch.dynamic_offsets)) == 0;
}

native_failure native_metal_dispatch_batch(doe_mtl_commands *commands,
    const doe_resource_command *records, size_t count)
{
    size_t total_bindings = 0;
    for (size_t d = 0; d < count; ++d) {
        native_pipeline *pipeline = doe_resource_pipeline_native(records[d].value.dispatch.pipeline);
        const WGPULimits *limits = &pipeline->device->backend.limits;
        if (records[d].value.dispatch.x > limits->maxComputeWorkgroupsPerDimension ||
            records[d].value.dispatch.y > limits->maxComputeWorkgroupsPerDimension ||
            records[d].value.dispatch.z > limits->maxComputeWorkgroupsPerDimension || !pipeline->metal_compute)
            return native_failure_status(NATIVE_INVALID_ARGUMENT);
        if (pipeline->binding_count > SIZE_MAX - total_bindings)
            return native_failure_status(NATIVE_OUT_OF_MEMORY);
        total_bindings += pipeline->binding_count;
    }
    if (count > SIZE_MAX / sizeof(doe_mtl_compute_dispatch) ||
        total_bindings > SIZE_MAX / sizeof(doe_mtl_buffer_binding))
        return native_failure_status(NATIVE_OUT_OF_MEMORY);
    doe_mtl_compute_dispatch *dispatches = calloc(count, sizeof(*dispatches));
    doe_mtl_buffer_binding *buffers = total_bindings ? calloc(total_bindings, sizeof(*buffers)) : NULL;
    doe_mtl_texture_binding *textures = total_bindings ? calloc(total_bindings, sizeof(*textures)) : NULL;
    doe_mtl_sampler_binding *samplers = total_bindings ? calloc(total_bindings, sizeof(*samplers)) : NULL;
    if (!dispatches || (total_bindings && (!buffers || !textures || !samplers))) {
        free(samplers); free(textures); free(buffers); free(dispatches);
        return native_failure_status(NATIVE_OUT_OF_MEMORY);
    }
    size_t buffer_used = 0, texture_used = 0, sampler_used = 0;
    native_failure failure = native_failure_status(NATIVE_OK);
    for (size_t d = 0; d < count; ++d) {
        const doe_resource_command *record = &records[d];
        native_pipeline *pipeline = doe_resource_pipeline_native(record->value.dispatch.pipeline);
        doe_mtl_render_bindings bindings = {0};
        if (d != 0 && same_dispatch_bindings(&records[d - 1], record)) {
            bindings = (doe_mtl_render_bindings){.buffers=dispatches[d - 1].buffers,
                .buffer_count=dispatches[d - 1].buffer_count, .textures=dispatches[d - 1].textures,
                .texture_count=dispatches[d - 1].texture_count, .samplers=dispatches[d - 1].samplers,
                .sampler_count=dispatches[d - 1].sampler_count};
        } else {
            doe_resource_binding resolved[NATIVE_MAX_PIPELINE_BINDINGS];
            doe_resource_status status = native_resolve_resources(pipeline,
                doe_resource_pipeline_get_layout(record->value.dispatch.pipeline),
                record->value.dispatch.groups, record->value.dispatch.dynamic_offsets, resolved);
            if (status != DOE_RESOURCE_OK) { failure = native_failure_status(NATIVE_INVALID_ARGUMENT); break; }
            native_metal_binding_storage storage = {
                .buffers=buffers ? buffers + buffer_used : NULL,
                .textures=textures ? textures + texture_used : NULL,
                .samplers=samplers ? samplers + sampler_used : NULL,
                .buffer_capacity=total_bindings - buffer_used,
                .texture_capacity=total_bindings - texture_used,
                .sampler_capacity=total_bindings - sampler_used};
            failure = native_metal_binding_snapshot_into(commands, pipeline, pipeline->metal_compute, resolved, &storage);
            if (failure.status != NATIVE_OK) break;
            bindings = storage.bindings;
            buffer_used += bindings.buffer_count;
            texture_used += bindings.texture_count;
            sampler_used += bindings.sampler_count;
        }
        dispatches[d] = (doe_mtl_compute_dispatch){.pipeline=pipeline->handle.metal,
            .buffers=bindings.buffers, .buffer_count=bindings.buffer_count,
            .textures=bindings.textures, .texture_count=bindings.texture_count,
            .samplers=bindings.samplers, .sampler_count=bindings.sampler_count,
            .groups={record->value.dispatch.x, record->value.dispatch.y, record->value.dispatch.z},
            .repeat=1, .timestamps=NULL, .timestamp_begin=UINT32_MAX, .timestamp_end=UINT32_MAX,
            .immediates={record->value.dispatch.immediates.words, pipeline->metal_compute->immediate_bytes}};
    }
    if (failure.status == NATIVE_OK) {
        doe_mtl_error error = {0};
        doe_mtl_status native_status = doe_mtl_commands_compute_batch(commands, dispatches, count, &error);
        failure = native_failure_metal(native_status, &error);
    }
    free(samplers); free(textures); free(buffers); free(dispatches);
    return failure;
}
