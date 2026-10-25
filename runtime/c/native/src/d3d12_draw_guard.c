/* SPDX-License-Identifier: MIT */
#include "d3d12_draw_guard.h"
#include "../../common/include/doe_draw_guard.h"
#include <stdlib.h>
#include <string.h>

struct native_d3d12_draw_code {
    doe_dx_shader *shader;
    doe_dx_binding_layout bindings[3];
    size_t count;
};
void native_d3d12_draw_code_release(native_d3d12_draw_code *code)
{
    if (!code) return;
    native_d3d12_compiler_release(code->shader);
    free(code);
}
bool native_d3d12_draw_layout(const doe_shader_view *view,bool scan,doe_dx_binding_layout out[3])
{
    size_t count=scan ? 3u : 1u;
    if (!view || !out || view->counts.entries!=1 || !view->entries || view->counts.resources!=count || !view->resources)
        return false;
    const DoeCompilerEntryV5 *entry=&view->entries[0];
    if (entry->workgroup_storage_bytes || entry->workgroup_size[0]!=(scan ? 64u : 1u) ||
        entry->workgroup_size[1]!=1 || entry->workgroup_size[2]!=1) return false;
    native_pipeline expected={.binding_count=count};
    for (size_t i=0;i<count;++i) {
        expected.bindings[i]=(native_binding){.group=0,.binding=(uint32_t)i,.kind=DOE_BIND_BUFFER,
            .buffer_type=i ? WGPUBufferBindingType_ReadOnlyStorage : WGPUBufferBindingType_Storage};
    }
    native_stage stage={.view=*view,.entry=entry,.visibility=WGPUShaderStage_Compute};
    native_d3d12_layout layout;
    native_d3d12_compute map;
    if (!native_d3d12_stage_bindings(&expected,&stage,&layout,&map) || map.buffer_count!=count ||
        map.texture_count || map.sampler_count || map.dispatch_index!=UINT32_MAX) return false;
    for (size_t i=0;i<count;++i) {
        if (map.buffer_indices[i]!=i || map.uniform[i]) return false;
    }
    memcpy(out,layout.buffers,count*sizeof(*out));
    return true;
}
native_failure native_d3d12_draw_compile(bool scan,native_d3d12_draw_code **out)
{
    if (!out) return native_failure_status(NATIVE_INVALID_ARGUMENT);
    char source[DOE_DRAW_INDEX_SCAN_SOURCE_CAPACITY];
    size_t length=0;
    doe_draw_guard_source_status generated=scan ? doe_draw_index_scan_source(source,sizeof(source),&length) :
        doe_draw_argument_source_v2(source,sizeof(source),&length);
    if (generated!=DOE_DRAW_GUARD_SOURCE_OK) return native_failure_status(NATIVE_INVALID_ARGUMENT);
    doe_shader *shader=NULL;
    doe_shader_error error={0};
    doe_shader_status status=native_compile_shader_target(DOE_SHADER_HLSL,source,length,&shader,&error);
    if (status!=DOE_SHADER_OK) {
        native_failure failure=native_failure_status(status==DOE_SHADER_OUT_OF_MEMORY ? NATIVE_OUT_OF_MEMORY : NATIVE_FAILURE);
        size_t n=0;
        while (n<sizeof(failure.message)-1 && n<sizeof(error.diagnostic.message) && error.diagnostic.message[n]) ++n;
        memcpy(failure.message,error.diagnostic.message,n); failure.message[n]=0;
        return failure;
    }
    native_d3d12_draw_code *code=calloc(1,sizeof(*code));
    native_failure failure=native_failure_status(code ? NATIVE_INVALID_STATE : NATIVE_OUT_OF_MEMORY);
    native_stage stage={.visibility=WGPUShaderStage_Compute};
    if (code && doe_shader_borrow(shader,&stage.view)==DOE_SHADER_OK && native_d3d12_draw_layout(&stage.view,scan,code->bindings)) {
        code->count=scan ? 3u : 1u;
        stage.entry=&stage.view.entries[0];
        failure=native_d3d12_compile_compute(&stage,&code->shader);
    }
    doe_shader_release(shader);
    if (failure.status==NATIVE_OK) *out=code;
    else native_d3d12_draw_code_release(code);
    return failure;
}
native_failure native_d3d12_draw_pipeline(native_device *device,const native_d3d12_draw_code *code,doe_dx_pipeline **out)
{
#if defined(_WIN32)
    if (!device || !code || !out) return native_failure_status(NATIVE_INVALID_ARGUMENT);
    size_t size=0;
    const uint8_t *bytes=doe_dx_shader_bytes(code->shader,&size);
    if (!bytes || !size) return native_failure_status(NATIVE_INVALID_STATE);
    const doe_dx_pipeline_desc desc={bytes,size,code->bindings,code->count};
    doe_dx_error error={0};
    doe_dx_status status=doe_dx_pipeline_create(device->backend.value.d3d12.context,&desc,out,&error);
    return native_failure_d3d12(status,&error);
#else
    (void)device; (void)code; (void)out;
    return native_failure_status(NATIVE_UNAVAILABLE);
#endif
}
