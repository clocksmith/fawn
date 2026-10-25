/* SPDX-License-Identifier: MIT */
#include "internal.h"
#include <string.h>

typedef struct render_delivery {
    WGPUCreateRenderPipelineAsyncCallbackInfo callback;
    WGPURenderPipeline pipeline;
    WGPUCreatePipelineAsyncStatus status;
    char message[NATIVE_FAILURE_MESSAGE_CAPACITY];
} render_delivery;
static void copy_render(void *destination, const void *source)
{
    *(render_delivery *)destination = *(const render_delivery *)source;
}
static void deliver_render(void *context)
{
    render_delivery value = *(render_delivery *)context;
    if (value.callback.callback)
        value.callback.callback(value.status,value.pipeline,native_string(value.message),
            value.callback.userdata1,value.callback.userdata2);
    else wgpuRenderPipelineRelease(value.pipeline);
}
WGPUFuture wgpuDeviceCreateRenderPipelineAsync(WGPUDevice handle,
    const WGPURenderPipelineDescriptor *descriptor, WGPUCreateRenderPipelineAsyncCallbackInfo callback)
{
    native_device *device = (native_device *)handle;
    if (!device || callback.nextInChain) return (WGPUFuture){0};
    native_device_retain(device);
    native_pipeline_error error;
    WGPURenderPipeline pipeline = native_create_render_pipeline(device,descriptor,&error);
    native_lock(device);
    bool lost = native_backend_status(&device->backend) == NATIVE_DEVICE_LOST;
    native_unlock(device);
    if (lost) native_loss_complete(device->loss,WGPUDeviceLostReason_Unknown,"native device lost");
    render_delivery delivery = {.callback=callback,.pipeline=pipeline,
        .status=pipeline ? WGPUCreatePipelineAsyncStatus_Success :
            error.type == WGPUErrorType_Validation ? WGPUCreatePipelineAsyncStatus_ValidationError : WGPUCreatePipelineAsyncStatus_InternalError};
    if (!pipeline) memcpy(delivery.message, error.message, sizeof(delivery.message));
    doe_callback_status admitted;
    WGPUFuture future = native_callback_admit(device->instance,callback.mode,&delivery,
        sizeof(delivery),_Alignof(render_delivery),copy_render,deliver_render, &admitted);
    if (!future.id) {
        if (!pipeline) native_error(device, error.type, error.message);
        else {
            wgpuRenderPipelineRelease(pipeline);
            native_callback_refused(device, admitted);
        }
    }
    native_device_release(device);
    return future;
}
