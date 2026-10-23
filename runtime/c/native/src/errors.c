/* SPDX-License-Identifier: MIT */
#include "internal.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

void wgpuDevicePushErrorScope(WGPUDevice handle, WGPUErrorFilter filter)
{
    native_device *device = (native_device *)handle;
    if (!device) return;
    doe_error_scope_result result = doe_error_scope_push(device->errors, (uint32_t)filter);
    if (result.status != DOE_ERROR_SCOPE_OK && result.status != DOE_ERROR_SCOPE_LIMIT)
        native_error(device, WGPUErrorType_Internal, "error scope push failed");
}

typedef struct error_delivery {
    WGPUPopErrorScopeCallbackInfo callback;
    WGPUPopErrorScopeStatus status;
    WGPUErrorType type;
    size_t length;
    char message[DOE_ERROR_SCOPE_MESSAGE_CAPACITY];
} error_delivery;
static void capture_error(uint32_t type, WGPUStringView message, void *userdata1, void *userdata2)
{
    (void)userdata2;
    error_delivery *delivery = userdata1;
    delivery->type = (WGPUErrorType)type;
    delivery->length = message.length;
    if (message.length > sizeof(delivery->message)) abort();
    if (message.length) memcpy(delivery->message, message.data, message.length);
}
static void copy_error(void *destination, const void *source)
{
    *(error_delivery *)destination = *(const error_delivery *)source;
}
static void deliver_error(void *context)
{
    error_delivery *value = context;
    if (value->callback.callback)
        value->callback.callback(value->status, value->type,
            (WGPUStringView){value->message, value->length}, value->callback.userdata1, value->callback.userdata2);
}
WGPUFuture wgpuDevicePopErrorScope(WGPUDevice handle, WGPUPopErrorScopeCallbackInfo callback)
{
    native_device *device = (native_device *)handle;
    if (!device || callback.nextInChain) return (WGPUFuture){0};
    native_device_retain(device);
    error_delivery delivery = {.callback=callback, .status=WGPUPopErrorScopeStatus_Success,
        .type=WGPUErrorType_NoError};
    doe_error_scope_result result = doe_error_scope_pop(device->errors, capture_error, &delivery, NULL);
    if (result.status != DOE_ERROR_SCOPE_OK) delivery.status = WGPUPopErrorScopeStatus_Error;
    WGPUFuture future = native_callback(device->instance, callback.mode, &delivery,
        sizeof(delivery), _Alignof(error_delivery), copy_error, deliver_error);
    native_device_release(device);
    return future;
}

void native_encoding_error(native_device *device, bool ended, doe_resource_status status, const char *message)
{
    if (!ended || status == DOE_RESOURCE_OK) return;
    char text[256];
    snprintf(text, sizeof(text), "%s: the encoder or pass already ended", message);
    native_error(device, native_resource_error(status), text);
}
