/* SPDX-License-Identifier: MIT */
#include "metal_binding_internal.h"
#include "../../common/include/doe_timestamp.h"
#include <string.h>

enum { TIMESTAMP_WORKGROUP = 64 };
bool native_metal_timestamp_supported(const doe_mtl_caps *caps)
{
    return caps && (caps->timestamp_stage || caps->timestamp_blit) &&
        isfinite(caps->timestamp_period_ns) && caps->timestamp_period_ns > 0 &&
        caps->max_threads_per_workgroup >= TIMESTAMP_WORKGROUP &&
        caps->max_threads_per_dimension[0] >= TIMESTAMP_WORKGROUP &&
        caps->max_buffer_length >= (uint64_t)DOE_RESOURCE_MAX_QUERIES * 8;
}

/* Called only before device publication. WGSL compilation owns semantics and
 * reflection; the existing selected-entry map supplies actual MSL slots. */
native_failure native_metal_timestamp_initialize(native_device *device)
{
    const doe_mtl_caps *caps = &device->backend.value.metal.caps;
    if (!native_metal_timestamp_supported(caps)) return native_failure_status(NATIVE_UNSUPPORTED);
    char source[DOE_TIMESTAMP_SOURCE_CAPACITY];
    size_t length = 0;
    if (doe_timestamp_source_mode(caps->timestamp_period_ns, 64, TIMESTAMP_WORKGROUP, true,
        source, sizeof(source), &length) != DOE_TIMESTAMP_SOURCE_OK) return native_failure_status(NATIVE_INVALID_ARGUMENT);
    doe_shader *shader = NULL;
    doe_shader_error error = {0};
    doe_shader_status compiled = native_compile_shader_target(DOE_SHADER_METAL, source, length, &shader, &error);
    if (compiled != DOE_SHADER_OK) return native_failure_status(compiled == DOE_SHADER_OUT_OF_MEMORY ?
        NATIVE_OUT_OF_MEMORY : NATIVE_FAILURE);
    native_failure failure = native_failure_status(NATIVE_INVALID_STATE);
    doe_shader_view view;
    if (doe_shader_borrow(shader, &view) == DOE_SHADER_OK && view.counts.entries == 1 &&
        view.counts.resources == 1 && view.resources[0].group == 0 && view.resources[0].binding == 0 &&
        view.resources[0].kind == DOE_COMPILER_STORAGE_BUFFER && view.resources[0].access == DOE_COMPILER_READ_WRITE) {
        const native_pipeline expected = {.binding_count=1,
            .bindings={{.group=0,.binding=0,.kind=DOE_BIND_BUFFER,.buffer_type=WGPUBufferBindingType_Storage}}};
        const native_stage stage = {.view=view, .entry=&view.entries[0]};
        native_metal_stage_map map;
        doe_mtl_shader_stage native;
        if (stage.entry->workgroup_size[0] == TIMESTAMP_WORKGROUP && stage.entry->workgroup_size[1] == 1 &&
            stage.entry->workgroup_size[2] == 1 &&
            native_metal_stage_map_build(&expected, &stage, DOE_COMPILER_STAGE_COMPUTE, &map, &native)) {
            doe_mtl_pipeline_desc desc = {native.msl, native.msl_length, native.entrypoint,
                {TIMESTAMP_WORKGROUP,1,1}, native.sizes_slot};
            doe_mtl_error detail = {0};
            failure = native_failure_metal(doe_mtl_pipeline_create(device->backend.value.metal.context,
                &desc, &device->metal_timestamp_pipeline, &detail), &detail);
            if (failure.status == NATIVE_OK) device->metal_timestamp_slot = map.slots[0];
        }
    }
    doe_shader_release(shader);
    return failure;
}

static doe_mtl_status resolve_timestamps(doe_mtl_commands *commands, native_queries *queries,
    uint32_t first, uint32_t count, doe_mtl_buffer *destination, uint64_t offset, doe_mtl_error *error)
{
    native_device *device = queries->device;
    if (!device->metal_timestamp_pipeline) return DOE_MTL_INVALID_STATE;
    for (uint32_t i = 0; i < count;) {
        bool available;
        doe_mtl_status status = doe_mtl_timestamps_available(commands, queries->metal_timestamps, first + i, &available);
        if (status != DOE_MTL_OK) return status;
        uint32_t run = 1;
        while (run < count - i) {
            bool next;
            status = doe_mtl_timestamps_available(commands, queries->metal_timestamps, first + i + run, &next);
            if (status != DOE_MTL_OK) return status;
            if (next != available) break;
            ++run;
        }
        uint64_t size = (uint64_t)run * 8;
        if (!available) status = doe_mtl_commands_fill(commands, destination, offset + (uint64_t)i * 8, size, 0, error);
        else {
            doe_mtl_buffer *scratch = NULL;
            status = doe_mtl_buffer_create(device->backend.value.metal.context, size, DOE_MTL_SHARED, &scratch, error);
            if (status == DOE_MTL_OK) status = doe_mtl_commands_resolve_timestamps(commands,
                queries->metal_timestamps, first + i, run, scratch, 0, error);
            doe_mtl_buffer_binding binding = {device->metal_timestamp_slot, scratch, 0, size};
            if (status == DOE_MTL_OK) status = doe_mtl_commands_dispatch(commands, device->metal_timestamp_pipeline,
                &binding, 1, (doe_mtl_dimensions){(run - 1) / TIMESTAMP_WORKGROUP + 1,1,1}, 1, error);
            if (status == DOE_MTL_OK) status = doe_mtl_commands_copy(commands, scratch, 0, destination,
                offset + (uint64_t)i * 8, size, error);
            doe_mtl_buffer_release(scratch);
        }
        if (status != DOE_MTL_OK) return status;
        i += run;
    }
    return DOE_MTL_OK;
}

native_failure native_metal_record_query(doe_mtl_commands *commands, const doe_resource_command *record)
{
    doe_mtl_error detail = {0};
    if (record->kind == DOE_COMMAND_WRITE_TIMESTAMP) {
        native_queries *queries = doe_resource_query_set_native(record->value.timestamp.set);
        if (!queries || !queries->metal_timestamps) return native_failure_status(NATIVE_INVALID_ARGUMENT);
        doe_mtl_status status = doe_mtl_commands_ordered_timestamp(commands,
            queries->metal_timestamps, record->value.timestamp.index, &detail);
        return native_failure_metal(status, &detail);
    }
    if (record->kind != DOE_COMMAND_RESOLVE_QUERY) return native_failure_status(NATIVE_UNSUPPORTED);
    if (!record->value.resolve.count) return native_failure_status(NATIVE_OK);
    native_queries *queries = doe_resource_query_set_native(record->value.resolve.set);
    native_buffer *destination = doe_resource_buffer_native(record->value.resolve.destination);
    if (!queries || !destination) return native_failure_status(NATIVE_INVALID_ARGUMENT);
    doe_mtl_status status = queries->metal_timestamps ?
        resolve_timestamps(commands, queries, record->value.resolve.first, record->value.resolve.count,
            destination->handle.metal, record->value.resolve.offset, &detail) :
        doe_mtl_commands_copy(commands, queries->metal_results, (uint64_t)record->value.resolve.first * 8,
            destination->handle.metal, record->value.resolve.offset, (uint64_t)record->value.resolve.count * 8, &detail);
    return native_failure_metal(status, &detail);
}
