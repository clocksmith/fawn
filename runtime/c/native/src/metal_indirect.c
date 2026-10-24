/* SPDX-License-Identifier: MIT */
#include "metal_indirect.h"
#include "../../common/include/doe_dispatch_guard.h"
#include <string.h>

static bool guard_description(const doe_shader_view *view, uint32_t bound,
    doe_mtl_dispatch_guard_desc *out)
{
    if (!view || !out || !bound || view->counts.entries != 1 || !view->entries ||
        view->counts.resources != 1 || !view->resources) return false;
    const DoeCompilerEntryV5 *entry = &view->entries[0];
    const DoeCompilerResourceV4 *resource = &view->resources[0];
    if (entry->workgroup_storage_bytes || entry->workgroup_size[0] != 1 ||
        entry->workgroup_size[1] != 1 || entry->workgroup_size[2] != 1 ||
        resource->group || resource->binding || resource->kind != DOE_COMPILER_STORAGE_BUFFER ||
        resource->access != DOE_COMPILER_READ_WRITE) return false;
    /* This is private typed conversion data for the fixed shared guard, not a
     * published pipeline/resource owner. Reuse the common selected-entry map
     * to validate all ABI12 spans, namespaces, runtime-tail indices and slots. */
    const native_pipeline expected = {.binding_count=1,
        .bindings={{.group=0,.binding=0,.kind=DOE_BIND_BUFFER,.buffer_type=WGPUBufferBindingType_Storage}}};
    const native_stage stage = {.view=*view,.entry=entry};
    native_metal_stage_map map;
    doe_mtl_shader_stage native;
    if (!native_metal_stage_map_build(&expected,&stage,DOE_COMPILER_STAGE_COMPUTE,&map,&native)) return false;
    *out = (doe_mtl_dispatch_guard_desc){.pipeline={native.msl,native.msl_length,native.entrypoint,
        {1,1,1},native.sizes_slot},.binding_slot=map.slots[0],.max_groups={bound,bound,bound}};
    return true;
}

static native_failure compile_guard(native_device *device, doe_shader **out,
    doe_mtl_dispatch_guard_desc *desc)
{
    uint32_t bound = device->backend.limits.maxComputeWorkgroupsPerDimension;
    const uint32_t limits[3] = {bound,bound,bound};
    char source[DOE_DISPATCH_GUARD_SOURCE_CAPACITY];
    size_t length = 0;
    if (doe_dispatch_guard_source(limits,source,sizeof(source),&length) != DOE_DISPATCH_GUARD_SOURCE_OK)
        return native_failure_status(NATIVE_INVALID_ARGUMENT);
    doe_shader *compiled = NULL;
    doe_shader_error error = {0};
    doe_shader_status status = native_compile_shader_target(DOE_SHADER_METAL,source,length,&compiled,&error);
    if (status != DOE_SHADER_OK) {
        native_failure failure = native_failure_status(status == DOE_SHADER_OUT_OF_MEMORY ?
            NATIVE_OUT_OF_MEMORY : NATIVE_FAILURE);
        failure.backend = WGPUBackendType_Metal;
        memcpy(failure.operation,"WGSL-to-MSL indirect guard compilation",sizeof("WGSL-to-MSL indirect guard compilation"));
        size_t size = 0;
        while (size < sizeof(failure.message) - 1 && size < sizeof(error.diagnostic.message) &&
            error.diagnostic.message[size]) ++size;
        memcpy(failure.message,error.diagnostic.message,size);
        failure.message[size] = 0;
        return failure;
    }
    doe_shader_view view;
    if (doe_shader_borrow(compiled,&view) != DOE_SHADER_OK || !guard_description(&view,bound,desc)) {
        doe_shader_release(compiled);
        return native_failure_status(NATIVE_INVALID_STATE);
    }
    *out = compiled;
    return native_failure_status(NATIVE_OK);
}

native_failure native_metal_guard_initialize(native_device *device)
{
    if (!device || device->backend.kind != WGPUBackendType_Metal)
        return native_failure_status(NATIVE_INVALID_ARGUMENT);
    native_device_retain(device);
    native_lock(device);
    native_failure failure = native_failure_status(native_device_result(device));
    bool ready = device->metal_dispatch_guard != NULL;
    native_unlock(device);
    doe_shader *compiled = NULL;
    doe_mtl_dispatch_guard_desc desc = {0};
    if (failure.status == NATIVE_OK && !ready) failure = compile_guard(device,&compiled,&desc);
    if (failure.status == NATIVE_OK && compiled) {
        native_lock(device);
        failure = native_failure_status(native_device_result(device));
        if (failure.status == NATIVE_OK && !device->metal_dispatch_guard) {
            /* Canonical source compilation finished above. Actual MTLLibrary /
             * PSO creation retains the existing private context serialization;
             * its loader is already live and no user callback runs here. */
            doe_mtl_error error = {0};
            doe_mtl_status status = doe_mtl_dispatch_guard_create(device->backend.value.metal.context,
                &desc,&device->metal_dispatch_guard,&error);
            failure = native_failure_metal(status,&error);
        }
        native_unlock(device);
    }
    /* A racing initializer may have published already; only its immutable code
     * is duplicated. No duplicate PSO is created or released outside serialization. */
    doe_shader_release(compiled);
    native_device_release(device);
    return failure;
}

native_failure native_metal_dispatch_indirect(doe_mtl_commands *commands,
    const doe_resource_command *record)
{
    native_pipeline *pipeline = doe_resource_pipeline_native(record->value.dispatch.pipeline);
    native_device *device = pipeline->device;
    if (!pipeline->metal_compute || !device->metal_dispatch_guard)
        return native_failure_status(NATIVE_INVALID_STATE);
    doe_resource_binding resolved[NATIVE_MAX_PIPELINE_BINDINGS];
    doe_resource_status status = native_resolve_resources(pipeline,
        doe_resource_pipeline_get_layout(record->value.dispatch.pipeline),record->value.dispatch.groups,
        record->value.dispatch.dynamic_offsets,resolved);
    if (status != DOE_RESOURCE_OK) return native_failure_status(NATIVE_INVALID_ARGUMENT);
    native_metal_snapshot snapshot = {0};
    native_failure failure = native_metal_binding_snapshot(commands,pipeline,pipeline->metal_compute,resolved,&snapshot);
    if (failure.status != NATIVE_OK) return failure;
    native_buffer *arguments = doe_resource_buffer_native(record->value.dispatch.indirect);
    if (!arguments || !arguments->handle.metal) return native_failure_status(NATIVE_INVALID_ARGUMENT);
    const doe_mtl_render_bindings *bindings = &snapshot.bindings;
    const doe_mtl_indirect_dispatch dispatch = {.guard=device->metal_dispatch_guard,.pipeline=pipeline->handle.metal,
        .buffers=bindings->buffers,.buffer_count=bindings->buffer_count,
        .textures=bindings->textures,.texture_count=bindings->texture_count,
        .samplers=bindings->samplers,.sampler_count=bindings->sampler_count,
        .arguments=arguments->handle.metal,.argument_offset=record->value.dispatch.indirect_offset};
    doe_mtl_error error = {0};
    doe_mtl_status result = doe_mtl_commands_dispatch_indirect(commands,&dispatch,&error);
    return native_failure_metal(result,&error);
}
