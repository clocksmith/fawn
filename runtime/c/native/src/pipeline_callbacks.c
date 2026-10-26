/* SPDX-License-Identifier: MIT */
#include "internal.h"
#include <string.h>

typedef struct pipeline_delivery {
    WGPUCreateComputePipelineAsyncCallbackInfo callback;
    WGPUComputePipeline pipeline;
    WGPUCreatePipelineAsyncStatus status;
    char message[NATIVE_FAILURE_MESSAGE_CAPACITY];
} pipeline_delivery;
static void copy_pipeline(void *destination, const void *source)
{
    *(pipeline_delivery *)destination = *(const pipeline_delivery *)source;
}
static void deliver_pipeline(void *context)
{
    pipeline_delivery value = *(pipeline_delivery *)context;
    if (value.callback.callback)
        value.callback.callback(value.status, value.pipeline, native_string(value.message),
            value.callback.userdata1, value.callback.userdata2);
    else wgpuComputePipelineRelease(value.pipeline);
}
WGPUFuture wgpuDeviceCreateComputePipelineAsync(WGPUDevice handle,
    const WGPUComputePipelineDescriptor *descriptor, WGPUCreateComputePipelineAsyncCallbackInfo callback)
{
    native_device *device = (native_device *)handle;
    if (!device || callback.nextInChain) return (WGPUFuture){0};
    native_device_retain(device);
    native_pipeline_error error = {WGPUErrorType_Internal, "compute pipeline creation failed"};
    /* A lost device resolves with its invalid pipeline, never an error. */
    WGPUComputePipeline pipeline = native_device_dead(device) ?
        native_lost_compute_pipeline(device, descriptor ? descriptor->label : (WGPUStringView){NULL, 0}) :
        native_create_compute_pipeline(device, descriptor, &error);
    native_lock(device);
    bool lost = native_backend_status(&device->backend) == NATIVE_DEVICE_LOST;
    native_unlock(device);
    if (lost) native_loss_complete(device->loss, WGPUDeviceLostReason_Unknown, "native device lost");
    pipeline_delivery delivery = {.callback=callback, .pipeline=pipeline,
        .status=pipeline ? WGPUCreatePipelineAsyncStatus_Success :
            error.type == WGPUErrorType_Validation ? WGPUCreatePipelineAsyncStatus_ValidationError : WGPUCreatePipelineAsyncStatus_InternalError};
    if (!pipeline) memcpy(delivery.message, error.message, sizeof(delivery.message));
    doe_callback_status admitted;
    WGPUFuture future = native_callback_admit(device->instance, callback.mode, &delivery,
        sizeof(delivery), _Alignof(pipeline_delivery), copy_pipeline, deliver_pipeline, &admitted);
    if (!future.id) {
        if (!pipeline) native_error(device, error.type, error.message);
        else {
            wgpuComputePipelineRelease(pipeline);
            native_callback_refused(device, admitted);
        }
    }
    native_device_release(device);
    return future;
}

typedef struct compilation_delivery {
    WGPUCompilationInfoCallbackInfo callback;
    doe_resource_shader *shader;
} compilation_delivery;
static void copy_compilation(void *destination, const void *source)
{
    *(compilation_delivery *)destination = *(const compilation_delivery *)source;
}
static void deliver_compilation(void *context)
{
    compilation_delivery value = *(compilation_delivery *)context;
    native_shader *shader = doe_resource_shader_native(value.shader);
    WGPUCompilationMessage message = {0};
    WGPUCompilationInfo info = {0};
    if (shader && shader->module) {
        info.messageCount = shader->message_count;
        info.messages = shader->messages;
    }
    /* A module of a lost device has no compilation and reports no messages. */
    if (shader && !shader->module && !shader->imported.data) {
        const DoeCompilerDiagnostic *diagnostic = &shader->error.diagnostic;
        message = (WGPUCompilationMessage){.message=native_string(diagnostic->message),
            .type=WGPUCompilationMessageType_Error, .lineNum=diagnostic->line,
            .linePos=diagnostic->column, .offset=diagnostic->offset};
        info.messageCount = 1;
        info.messages = &message;
    }
    if (value.callback.callback)
        value.callback.callback(WGPUCompilationInfoRequestStatus_Success, &info,
            value.callback.userdata1, value.callback.userdata2);
    doe_resource_shader_release(value.shader);
}
WGPUFuture wgpuShaderModuleGetCompilationInfo(WGPUShaderModule handle, WGPUCompilationInfoCallbackInfo callback)
{
    doe_resource_shader *shader = (doe_resource_shader *)handle;
    native_device *device = doe_resource_shader_device(shader);
    if (!device || callback.nextInChain) return (WGPUFuture){0};
    doe_resource_shader_retain(shader);
    compilation_delivery delivery = {callback, shader};
    doe_callback_status admitted;
    WGPUFuture future = native_callback_admit(device->instance, callback.mode, &delivery,
        sizeof(delivery), _Alignof(compilation_delivery), copy_compilation, deliver_compilation, &admitted);
    if (!future.id) {
        doe_resource_shader_release(shader);
        native_callback_refused(device, admitted);
    }
    return future;
}
