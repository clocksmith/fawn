/* SPDX-License-Identifier: MIT */
#include "metal_draw_guard.h"
#include "../../common/include/doe_draw_guard.h"
#include <string.h>

static bool description(const doe_shader_view *view,bool scan,doe_mtl_pipeline_desc *out,uint32_t slots[3])
{
    size_t count=scan ? 3u : 1u;
    if (!view || view->counts.entries!=1 || !view->entries ||
        view->counts.resources!=count || !view->resources) return false;
    const DoeCompilerEntryV5 *entry=&view->entries[0];
    uint32_t threads=scan ? 64u : 1u;
    if (entry->workgroup_storage_bytes || entry->workgroup_size[0]!=threads ||
        entry->workgroup_size[1]!=1 || entry->workgroup_size[2]!=1) return false;
    native_pipeline expected={.binding_count=count};
    for (size_t i=0;i<count;++i) {
        const DoeCompilerResourceV4 *resource=&view->resources[i];
        if (resource->group || resource->binding!=i || resource->kind!=DOE_COMPILER_STORAGE_BUFFER ||
            resource->access!=(i ? DOE_COMPILER_READ : DOE_COMPILER_READ_WRITE)) return false;
        expected.bindings[i]=(native_binding){.group=0,.binding=(uint32_t)i,.kind=DOE_BIND_BUFFER,
            .buffer_type=i ? WGPUBufferBindingType_ReadOnlyStorage : WGPUBufferBindingType_Storage};
    }
    const native_stage stage={.view=*view,.entry=entry};
    native_metal_stage_map map;
    doe_mtl_shader_stage native;
    if (!native_metal_stage_map_build(&expected,&stage,DOE_COMPILER_STAGE_COMPUTE,&map,&native)) return false;
    *out=(doe_mtl_pipeline_desc){native.msl,native.msl_length,native.entrypoint,{threads,1,1},native.sizes_slot};
    for (size_t i=0;i<count;++i) slots[i]=map.slots[i];
    return true;
}

static native_failure compile(bool scan,doe_shader **out,doe_mtl_pipeline_desc *desc,uint32_t slots[3])
{
    char source[DOE_DRAW_INDEX_SCAN_SOURCE_CAPACITY];
    size_t length=0;
    doe_draw_guard_source_status generated=scan ? doe_draw_index_scan_source(source,sizeof(source),&length) :
        doe_draw_argument_source_v2(source,sizeof(source),&length);
    if (generated!=DOE_DRAW_GUARD_SOURCE_OK) return native_failure_status(NATIVE_INVALID_ARGUMENT);
    doe_shader *shader=NULL;
    doe_shader_error error={0};
    doe_shader_status status=native_compile_shader_target(DOE_SHADER_METAL,source,length,&shader,&error);
    if (status!=DOE_SHADER_OK) {
        native_failure failure=native_failure_status(status==DOE_SHADER_OUT_OF_MEMORY ? NATIVE_OUT_OF_MEMORY : NATIVE_FAILURE);
        failure.backend=WGPUBackendType_Metal;
        memcpy(failure.operation,"WGSL-to-MSL draw guard",sizeof("WGSL-to-MSL draw guard"));
        size_t bytes=0;
        while (bytes<sizeof(failure.message)-1 && bytes<sizeof(error.diagnostic.message) && error.diagnostic.message[bytes]) ++bytes;
        memcpy(failure.message,error.diagnostic.message,bytes);
        failure.message[bytes]=0;
        return failure;
    }
    doe_shader_view view;
    if (doe_shader_borrow(shader,&view)!=DOE_SHADER_OK || !description(&view,scan,desc,slots)) {
        doe_shader_release(shader);
        return native_failure_status(NATIVE_INVALID_STATE);
    }
    *out=shader;
    return native_failure_status(NATIVE_OK);
}

native_failure native_metal_draw_guard_initialize(native_device *device)
{
    if (!device || device->backend.kind!=WGPUBackendType_Metal) return native_failure_status(NATIVE_INVALID_ARGUMENT);
    native_device_retain(device);
    native_lock(device);
    native_failure failure=native_failure_status(native_device_result(device));
    bool ready=device->metal_draw_guard!=NULL;
    native_unlock(device);
    doe_shader *shaders[2]={0};
    doe_mtl_draw_guard_desc desc={.source_version=DOE_MTL_DRAW_GUARD_SOURCE_VERSION,
        .max_groups_x=device->backend.limits.maxComputeWorkgroupsPerDimension};
    uint32_t normal_slots[3]={0};
    if (failure.status==NATIVE_OK && !ready) failure=compile(false,&shaders[0],&desc.normalize,normal_slots);
    if (failure.status==NATIVE_OK && !ready) failure=compile(true,&shaders[1],&desc.scan,desc.scan_slots);
    desc.record_slot=normal_slots[0];
    if (failure.status==NATIVE_OK && shaders[0]) {
        native_lock(device);
        failure=native_failure_status(native_device_result(device));
        if (failure.status==NATIVE_OK && !device->metal_draw_guard) {
            doe_mtl_error detail={0};
            failure=native_failure_metal(doe_mtl_draw_guard_create(device->backend.value.metal.context,
                &desc,&device->metal_draw_guard,&detail),&detail);
        }
        native_unlock(device);
    }
    for (size_t i=0;i<2;++i) doe_shader_release(shaders[i]);
    native_device_release(device);
    return failure;
}

void native_metal_draw_guard_finish(native_device *device,doe_resource_render_pass *pass,
    doe_resource_bundle_encoder *bundle,doe_resource_status status,bool ended,const char *message)
{
    native_failure failure=native_failure_status(NATIVE_OK);
    if (status==DOE_RESOURCE_OK && device->backend.kind==WGPUBackendType_Metal)
        failure=native_metal_draw_guard_initialize(device);
    if (failure.status==NATIVE_OK) { native_encoding_error(device,ended,status,message); return; }
    doe_resource_status rejection=failure.status==NATIVE_OUT_OF_MEMORY ? DOE_RESOURCE_OUT_OF_MEMORY :
        native_result_error(failure.status)==WGPUErrorType_Internal ? DOE_RESOURCE_EXECUTION_FAILED : DOE_RESOURCE_INVALID_ARGUMENT;
    native_lock(device);
    if (pass) (void)doe_resource_render_pass_invalidate(pass,rejection);
    else (void)doe_resource_bundle_encoder_invalidate(bundle,rejection);
    native_unlock(device);
    char detail[NATIVE_FAILURE_MESSAGE_CAPACITY];
    native_failure_format(&failure,"Metal draw guard preparation failed",detail);
    native_error(device,native_result_error(failure.status),detail);
}
