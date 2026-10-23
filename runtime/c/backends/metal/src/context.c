/* SPDX-License-Identifier: MIT */
#include "internal.h"

const char *doe_mtl_status_name(doe_mtl_status status) {
    switch (status) {
    case DOE_MTL_OK: return "ok";
    case DOE_MTL_PENDING: return "pending";
    case DOE_MTL_COMPLETION_UNKNOWN: return "completion unknown";
    case DOE_MTL_INVALID_ARGUMENT: return "invalid argument";
    case DOE_MTL_OUT_OF_MEMORY: return "out of memory";
    case DOE_MTL_LOADER_UNAVAILABLE: return "loader unavailable";
    case DOE_MTL_UNSUPPORTED: return "unsupported";
    case DOE_MTL_NATIVE_FAILURE: return "native failure";
    case DOE_MTL_INVALID_STATE: return "invalid state";
    case DOE_MTL_BUSY: return "busy";
    case DOE_MTL_RANGE: return "range";
    case DOE_MTL_ALIGNMENT: return "alignment";
    case DOE_MTL_DEVICE_MISMATCH: return "device mismatch";
    case DOE_MTL_UNINITIALIZED: return "uninitialized";
    case DOE_MTL_EXECUTION_FAILED: return "execution failed";
    }
    return "unknown status";
}

doe_mtl_status doe_mtl_fail(doe_mtl_status status, const char *operation,
    doe_mtl_error *error) {
    if (error != NULL) {
        memset(error, 0, sizeof(*error));
        error->status = status;
        error->operation = operation;
    }
    return status;
}

bool doe_mtl_range(size_t allocation, uint64_t offset, uint64_t size) {
    return offset <= allocation && size <= allocation - offset;
}

static bool native_complete(const doe_mtl_native *native) {
    return native->close != NULL && native->release != NULL &&
        native->device != NULL && native->queue != NULL &&
        native->buffer != NULL && native->contents != NULL &&
        native->pipeline != NULL && native->command != NULL &&
        native->copy != NULL && native->fill != NULL &&
        native->dispatch != NULL && native->commit != NULL &&
        native->observe != NULL;
}

doe_mtl_status doe_mtl_context_open(doe_mtl_native native,
    doe_mtl_context **out, doe_mtl_error *error) {
    doe_mtl_context *context = NULL;
    doe_mtl_status status;
    if (out == NULL || !native_complete(&native)) {
        if (native.close != NULL) native.close(native.userdata);
        return doe_mtl_fail(DOE_MTL_INVALID_ARGUMENT, "context create", error);
    }
    context = calloc(1, sizeof(*context));
    if (context == NULL) {
        native.close(native.userdata);
        return doe_mtl_fail(DOE_MTL_OUT_OF_MEMORY, "context allocation", error);
    }
    context->native = native;
    context->references = 1;
    status = native.device(native.userdata, &context->device, &context->caps, error);
    if (status == DOE_MTL_OK)
        status = native.queue(native.userdata, context->device, &context->queue, error);
    if (status != DOE_MTL_OK) {
        doe_mtl_context_release(context);
        return status;
    }
    *out = context;
    return doe_mtl_fail(DOE_MTL_OK, "context create", error);
}

doe_mtl_status doe_mtl_context_create(doe_mtl_context **out, doe_mtl_error *error) {
    doe_mtl_native native = {0};
    doe_mtl_status status;
    if (out == NULL)
        return doe_mtl_fail(DOE_MTL_INVALID_ARGUMENT, "context create", error);
    status = doe_mtl_native_open(&native, error);
    if (status != DOE_MTL_OK) return status;
    return doe_mtl_context_open(native, out, error);
}

doe_mtl_status doe_mtl_context_retain(doe_mtl_context *context) {
    if (context == NULL) return DOE_MTL_INVALID_ARGUMENT;
    if (context->references == UINT32_MAX) return DOE_MTL_RANGE;
    context->references++;
    return DOE_MTL_OK;
}

void doe_mtl_context_release(doe_mtl_context *context) {
    if (context == NULL || --context->references != 0) return;
    if (context->queue != NULL)
        context->native.release(context->native.userdata, context->queue);
    if (context->device != NULL)
        context->native.release(context->native.userdata, context->device);
    context->native.close(context->native.userdata);
    free(context);
}

doe_mtl_status doe_mtl_context_caps(const doe_mtl_context *context, doe_mtl_caps *out) {
    if (context == NULL || out == NULL) return DOE_MTL_INVALID_ARGUMENT;
    *out = context->caps;
    return DOE_MTL_OK;
}

void *doe_mtl_context_device(const doe_mtl_context *context) {
    return context != NULL ? context->device : NULL;
}
