/* SPDX-License-Identifier: MIT */
#include "d3d12_draw_guard.h"
#include "../../common/include/doe_draw_guard.h"
#include <stdlib.h>
#include <string.h>

struct native_d3d12_kernel_code {
    doe_dx_shader *shader;
    doe_dx_binding_layout bindings[3];
    size_t count;
    native_d3d12_kernel_sizes sizes;
};
void native_d3d12_kernel_code_release(native_d3d12_kernel_code *code)
{
    if (!code) return;
    native_d3d12_compiler_release(code->shader);
    free(code);
}
bool native_d3d12_draw_layout(const doe_shader_view *view,bool scan,doe_dx_binding_layout out[3],
    native_d3d12_kernel_sizes *sizes)
{
    return native_d3d12_storage_kernel_layout(view,scan ? 64u : 1u,scan ? 3u : 1u,out,sizes);
}
native_failure native_d3d12_storage_compile(native_device *device,const char *source,size_t length,
    uint32_t workgroup,size_t count,native_d3d12_kernel_code **out)
{
    if (!device || !source || !length || !out) return native_failure_status(NATIVE_INVALID_ARGUMENT);
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
    native_d3d12_kernel_code *code=calloc(1,sizeof(*code));
    native_failure failure=native_failure_status(code ? NATIVE_INVALID_STATE : NATIVE_OUT_OF_MEMORY);
    native_stage stage={.visibility=WGPUShaderStage_Compute};
    if (code && doe_shader_borrow(shader,&stage.view)==DOE_SHADER_OK && native_d3d12_storage_kernel_layout(&stage.view,workgroup,count,code->bindings,&code->sizes)) {
        code->count=count;
        stage.entry=&stage.view.entries[0];
        failure=native_d3d12_compile_compute(&device->backend,&stage,&code->shader);
    }
    doe_shader_release(shader);
    if (failure.status==NATIVE_OK) *out=code;
    else native_d3d12_kernel_code_release(code);
    return failure;
}
native_failure native_d3d12_draw_compile(native_device *device,bool scan,native_d3d12_kernel_code **out)
{
    char source[DOE_DRAW_INDEX_SCAN_SOURCE_CAPACITY]; size_t length=0;
    doe_draw_guard_source_status generated=scan ? doe_draw_index_scan_source(source,sizeof(source),&length) :
        doe_draw_argument_source_v2(source,sizeof(source),&length);
    if (generated!=DOE_DRAW_GUARD_SOURCE_OK) return native_failure_status(NATIVE_INVALID_ARGUMENT);
    return native_d3d12_storage_compile(device,source,length,scan ? 64u : 1u,scan ? 3u : 1u,out);
}
native_failure native_d3d12_kernel_pipeline(native_device *device,const native_d3d12_kernel_code *code,doe_dx_pipeline **out)
{
#if defined(_WIN32)
    if (!device || !code || !out) return native_failure_status(NATIVE_INVALID_ARGUMENT);
    size_t size=0;
    const uint8_t *bytes=doe_dx_shader_bytes(code->shader,&size);
    if (!bytes || !size) return native_failure_status(NATIVE_INVALID_STATE);
    /* The kernel's size-table rows come from its compiled entry, like any user pipeline. */
    const doe_dx_dynamic_pipeline_desc desc={.resources={.buffers={bytes,size,code->bindings,code->count}},
        .logical_size_count=code->sizes.count,.logical_size_indices=code->sizes.count ? code->sizes.indices : NULL};
    doe_dx_error error={0};
    doe_dx_status status=doe_dx_pipeline_create_dynamic(device->backend.value.d3d12.context,&desc,out,&error);
    return native_failure_d3d12(status,&error);
#else
    (void)device; (void)code; (void)out;
    return native_failure_status(NATIVE_UNAVAILABLE);
#endif
}
