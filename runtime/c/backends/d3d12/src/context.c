/* SPDX-License-Identifier: MIT */
#include "internal.h"

doe_dx_status doe_dx_fail(doe_dx_status status, int32_t native_code,
    const char *operation, doe_dx_error *error) {
    if (error) *error = (doe_dx_error){status, native_code, operation};
    return status;
}

bool doe_dx_range(uint64_t allocation, uint64_t offset, uint64_t size) {
    return offset <= allocation && size <= allocation - offset;
}

doe_dx_status doe_dx_context_create(const doe_dx_options *options,
    doe_dx_context **out, doe_dx_error *error) {
    if (!out) return doe_dx_fail(DOE_DX_INVALID_ARGUMENT, 0, "context.create", error);
    doe_dx_options defaults = {0};
    if (!options) options = &defaults;
    doe_dx_context *context = calloc(1, sizeof(*context));
    if (!context) return doe_dx_fail(DOE_DX_OUT_OF_MEMORY, 0, "context.allocate", error);
    doe_dx_status status = doe_dx_native_open(options, &context->native, &context->caps, error);
    if (status != DOE_DX_OK) {
        free(context);
        return status;
    }
    *out = context;
    return doe_dx_fail(DOE_DX_OK, 0, "context.create", error);
}

doe_dx_status doe_dx_context_caps(const doe_dx_context *context, doe_dx_caps *out) {
    if (!context || !out) return DOE_DX_INVALID_ARGUMENT;
    *out = context->caps;
    return DOE_DX_OK;
}

doe_dx_status doe_dx_context_destroy(doe_dx_context *context) {
    if (!context) return DOE_DX_INVALID_ARGUMENT;
    if (context->objects || context->pending) return DOE_DX_BUSY;
    doe_dx_native_close(context->native);
    free(context);
    return DOE_DX_OK;
}

static void retire(doe_dx_context *context, uint64_t completed, bool lost,
    const doe_dx_error *error) {
    doe_dx_commands **link = &context->pending;
    while (*link) {
        doe_dx_commands *commands = *link;
        if (!lost && (!commands->fence || commands->fence > completed)) {
            link = &commands->next;
            continue;
        }
        *link = commands->next;
        commands->next = NULL;
        doe_dx_commands_settle(commands, lost, error);
        if (!commands->caller_owned) doe_dx_commands_dispose(commands);
    }
}

void doe_dx_context_lose(doe_dx_context *context, const doe_dx_error *error) {
    context->lost = true;
    retire(context, 0, true, error);
}

doe_dx_status doe_dx_context_result(doe_dx_context *context, doe_dx_status status,
    const doe_dx_error *error) {
    if (status == DOE_DX_DEVICE_LOST) {
        doe_dx_error detail = {DOE_DX_DEVICE_LOST, 0, "native.device_lost"};
        if (error) detail = *error;
        doe_dx_context_lose(context, &detail);
    }
    return status;
}

static doe_dx_status observed(doe_dx_context *context, doe_dx_status status,
    const doe_dx_error *detail, doe_dx_error *error) {
    if (status == DOE_DX_DEVICE_LOST) {
        doe_dx_context_lose(context, detail);
        if (error) *error = *detail;
        return status;
    }
    return doe_dx_fail(DOE_DX_COMPLETION_UNKNOWN, detail->native_code,
        detail->operation, error);
}

doe_dx_status doe_dx_context_poll(doe_dx_context *context, uint32_t wait_ms,
    doe_dx_error *error) {
    if (!context || wait_ms == UINT32_MAX)
        return doe_dx_fail(DOE_DX_INVALID_ARGUMENT, 0, "context.poll", error);
    if (context->lost) return doe_dx_fail(DOE_DX_DEVICE_LOST, 0, "context.poll", error);
    if (!context->pending) return doe_dx_fail(DOE_DX_OK, 0, "context.poll", error);
    uint64_t completed = 0;
    doe_dx_error detail = {0};
    doe_dx_status status = doe_dx_native_completed(context->native, &completed, &detail);
    if (status != DOE_DX_OK) return observed(context, status, &detail, error);
    retire(context, completed, false, NULL);
    bool unknown = false;
    for (doe_dx_commands *item = context->pending; item; item = item->next)
        if (!item->fence) unknown = true;
    if (unknown) {
        /* No new submissions are allowed past an unknown signal. Repeating
         * the same value is safe: it cannot regress this private queue fence. */
        status = doe_dx_native_signal(context->native, context->next_fence, &detail);
        if (status != DOE_DX_OK) return observed(context, status, &detail, error);
        for (doe_dx_commands *item = context->pending; item; item = item->next)
            if (!item->fence) item->fence = context->next_fence;
    }
    if (!context->pending) return doe_dx_fail(DOE_DX_OK, 0, "context.poll", error);
    if (wait_ms) {
        status = doe_dx_native_wait(context->native, context->next_fence, wait_ms, &detail);
        if (status != DOE_DX_OK && status != DOE_DX_PENDING)
            return observed(context, status, &detail, error);
    }
    status = doe_dx_native_completed(context->native, &completed, &detail);
    if (status != DOE_DX_OK) return observed(context, status, &detail, error);
    retire(context, completed, false, NULL);
    return doe_dx_fail(context->pending ? DOE_DX_PENDING : DOE_DX_OK, 0,
        "context.poll", error);
}
