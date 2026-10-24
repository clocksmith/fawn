/* SPDX-License-Identifier: MIT */
#include "d3d12_indirect.h"
#include "d3d12_binding_internal.h"
#include "../../common/include/doe_dispatch_guard.h"
#include <stdlib.h>
#include <string.h>

struct native_d3d12_guard_code {
    doe_dx_shader *shader;
    doe_dx_binding_layout binding;
};

void native_d3d12_guard_code_release(native_d3d12_guard_code *code)
{
    if (!code) return;
    native_d3d12_compiler_release(code->shader);
    free(code);
}

native_failure native_d3d12_guard_compile(native_device *device, native_d3d12_guard_code **out)
{
    if (!device || !out || device->backend.kind != WGPUBackendType_D3D12)
        return native_failure_status(NATIVE_INVALID_ARGUMENT);
    uint32_t bound = device->backend.limits.maxComputeWorkgroupsPerDimension;
    uint32_t limits[3] = {bound,bound,bound};
    char source[DOE_DISPATCH_GUARD_SOURCE_CAPACITY];
    size_t length = 0;
    if (doe_dispatch_guard_source(limits,source,sizeof(source),&length) != DOE_DISPATCH_GUARD_SOURCE_OK)
        return native_failure_status(NATIVE_INVALID_ARGUMENT);
    doe_shader *compiled = NULL;
    doe_shader_error error = {0};
    doe_shader_status status = native_compile_shader_target(DOE_SHADER_HLSL,source,length,&compiled,&error);
    if (status != DOE_SHADER_OK) {
        native_failure failure = native_failure_status(status == DOE_SHADER_OUT_OF_MEMORY ?
            NATIVE_OUT_OF_MEMORY : NATIVE_FAILURE);
        size_t capacity = sizeof(error.diagnostic.message) < sizeof(failure.message) - 1 ?
            sizeof(error.diagnostic.message) : sizeof(failure.message) - 1;
        size_t size = 0;
        while (size < capacity && error.diagnostic.message[size]) ++size;
        memcpy(failure.message,error.diagnostic.message,size);
        failure.message[size] = 0;
        return failure;
    }
    native_d3d12_guard_code *code = calloc(1,sizeof(*code));
    native_failure failure = native_failure_status(code ? NATIVE_INVALID_STATE : NATIVE_OUT_OF_MEMORY);
    native_stage stage = {0};
    if (code && doe_shader_borrow(compiled,&stage.view) == DOE_SHADER_OK &&
        native_d3d12_guard_layout(&stage.view,&code->binding)) {
        stage.entry = &stage.view.entries[0];
        stage.visibility = WGPUShaderStage_Compute;
        failure = native_d3d12_compile_compute(&stage,&code->shader);
    }
    doe_shader_release(compiled);
    if (failure.status != NATIVE_OK) native_d3d12_guard_code_release(code);
    else *out = code;
    return failure;
}

native_failure native_d3d12_guard_pipeline(native_device *device,
    const native_d3d12_guard_code *code, doe_dx_pipeline **out)
{
#if defined(_WIN32)
    if (!device || !code || !out) return native_failure_status(NATIVE_INVALID_ARGUMENT);
    size_t size = 0;
    const uint8_t *bytes = doe_dx_shader_bytes(code->shader,&size);
    if (!bytes || !size) return native_failure_status(NATIVE_INVALID_ARGUMENT);
    const doe_dx_pipeline_desc desc = {.bytecode=bytes,.bytecode_size=size,
        .bindings=&code->binding,.binding_count=1};
    doe_dx_error error = {0};
    doe_dx_status status = doe_dx_pipeline_create(device->backend.value.d3d12.context,&desc,out,&error);
    return native_failure_d3d12(status,&error);
#else
    (void)device;
    (void)code;
    (void)out;
    return native_failure_status(NATIVE_UNAVAILABLE);
#endif
}

#if defined(_WIN32)
static native_failure copy_metadata(native_command_handle *commands, doe_dx_buffer *scratch,
    doe_dx_buffer **out)
{
    doe_dx_buffer *upload = NULL, *metadata = NULL;
    doe_dx_error error = {0};
    doe_dx_status status = doe_dx_buffer_create(commands->value.d3d12.context,256,DOE_DX_UPLOAD,&upload,&error);
    if (status == DOE_DX_OK)
        status = doe_dx_buffer_create(commands->value.d3d12.context,256,DOE_DX_DEFAULT,&metadata,&error);
    if (status == DOE_DX_OK)
        status = doe_dx_commands_copy(commands->value.d3d12.commands,upload,0,metadata,0,256,&error);
    if (status == DOE_DX_OK)
        status = doe_dx_commands_copy(commands->value.d3d12.commands,scratch,0,metadata,0,12,&error);
    native_failure failure = native_failure_d3d12(status,&error);
    doe_dx_buffer_release(upload);
    if (status == DOE_DX_OK) *out = metadata;
    else doe_dx_buffer_release(metadata);
    return failure;
}
#endif

native_failure native_d3d12_dispatch_indirect(native_command_handle *commands,
    const doe_resource_command *record)
{
#if defined(_WIN32)
    native_pipeline *pipeline = doe_resource_pipeline_native(record->value.dispatch.pipeline);
    native_d3d12_snapshot bindings;
    native_failure failure = native_d3d12_binding_snapshot(record,&bindings);
    if (failure.status != NATIVE_OK) return failure;
    if (!bindings.guard || (bindings.metadata_index != UINT32_MAX &&
        bindings.metadata_index >= bindings.values.resources.buffer_count))
        return native_failure_status(NATIVE_INVALID_STATE);
    native_buffer *arguments = doe_resource_buffer_native(record->value.dispatch.indirect);
    uint64_t offset = record->value.dispatch.indirect_offset;
    if (!arguments || !arguments->handle.d3d12 || (offset & 3) ||
        offset > arguments->handle.d3d12->allocation_size || 12 > arguments->handle.d3d12->allocation_size - offset)
        return native_failure_status(NATIVE_INVALID_ARGUMENT);
    doe_dx_buffer *scratch = NULL, *metadata = NULL;
    doe_dx_error error = {0};
    doe_dx_status status = doe_dx_buffer_create(commands->value.d3d12.context,12,DOE_DX_DEFAULT,&scratch,&error);
    if (status == DOE_DX_OK)
        status = doe_dx_commands_copy(commands->value.d3d12.commands,
            arguments->handle.d3d12->allocation,offset,scratch,0,12,&error);
    const doe_dx_buffer_binding scratch_binding = {scratch,0,12};
    if (status == DOE_DX_OK)
        status = doe_dx_commands_dispatch(commands->value.d3d12.commands,bindings.guard,&scratch_binding,1,1,1,1,&error);
    failure = native_failure_d3d12(status,&error);
    if (failure.status == NATIVE_OK && bindings.metadata_index != UINT32_MAX) {
        failure = copy_metadata(commands,scratch,&metadata);
        if (failure.status == NATIVE_OK)
            bindings.buffers[bindings.metadata_index] = (doe_dx_buffer_binding){metadata,0,256};
    }
    if (failure.status == NATIVE_OK) {
        status = doe_dx_commands_dispatch_indirect_dynamic(commands->value.d3d12.commands,
            pipeline->handle.d3d12,&bindings.values,scratch,0,&error);
        failure = native_failure_d3d12(status,&error);
    }
    /* Successful recording already acquired private command references. Unknown
     * submissions keep those references plus the common pending device lease. */
    doe_dx_buffer_release(scratch);
    doe_dx_buffer_release(metadata);
    return failure;
#else
    (void)commands;
    (void)record;
    return native_failure_status(NATIVE_UNAVAILABLE);
#endif
}
