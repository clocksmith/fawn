/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors
 * Stack behavior derived from runtime/zig/src/runtime/diagnostics/error_scope.zig.
 * Classification, native lifetimes and future delivery remain separate owners. */
#include "internal.h"
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

_Static_assert(WGPUErrorType_NoError == 1 && WGPUErrorType_Validation == 2 &&
               WGPUErrorType_OutOfMemory == 3 && WGPUErrorType_Internal == 4,
               "pinned error-type discriminants changed");
_Static_assert(WGPUErrorFilter_Validation == 1 && WGPUErrorFilter_OutOfMemory == 2 &&
               WGPUErrorFilter_Internal == 3, "pinned filter discriminants changed");

static doe_error_scope_result result(doe_error_scope_status status,
                                     int system_error, int saved_errno)
{
    errno = saved_errno;
    return (doe_error_scope_result){status, system_error};
}

static doe_error_scope_result enter(doe_error_scope_stack *stack)
{
    if (stack == NULL)
        return (doe_error_scope_result){DOE_ERROR_SCOPE_INVALID_ARGUMENT, 0};
    if (stack->state != DOE_ERROR_SCOPES_LIVE)
        return (doe_error_scope_result){DOE_ERROR_SCOPE_INVALID_STATE, 0};
    const int error = pthread_mutex_lock(&stack->mutex);
    if (error != 0)
        return (doe_error_scope_result){DOE_ERROR_SCOPE_SYSTEM_ERROR, error};
    return (doe_error_scope_result){DOE_ERROR_SCOPE_OK, 0};
}

static void leave(doe_error_scope_stack *stack, const char *operation)
{
    const int error = pthread_mutex_unlock(&stack->mutex);
    if (error != 0) {
        fprintf(stderr, "C error scopes %s: pthread_mutex_unlock failed: %d\n",
                operation, error);
        abort();
    }
}

static bool filter_matches(uint32_t filter, uint32_t error_type)
{
    switch (filter) {
    case WGPUErrorFilter_Validation: return error_type == WGPUErrorType_Validation;
    case WGPUErrorFilter_OutOfMemory: return error_type == WGPUErrorType_OutOfMemory;
    case WGPUErrorFilter_Internal: return error_type == WGPUErrorType_Internal;
    default: return error_type != WGPUErrorType_NoError;
    }
}

static size_t copy_message(unsigned char bytes[DOE_ERROR_SCOPE_MESSAGE_CAPACITY],
                           const unsigned char *message, size_t length)
{
    const size_t copied = length < DOE_ERROR_SCOPE_MESSAGE_CAPACITY
        ? length : DOE_ERROR_SCOPE_MESSAGE_CAPACITY;
    memset(bytes, 0, DOE_ERROR_SCOPE_MESSAGE_CAPACITY);
    if (copied != 0) memcpy(bytes, message, copied);
    return copied;
}

doe_error_scope_result doe_error_scope_storage_requirements(
    uint32_t abi_version, doe_error_scope_requirements *out)
{
    const int saved_errno = errno;
    if (out == NULL)
        return result(DOE_ERROR_SCOPE_INVALID_ARGUMENT, 0, saved_errno);
    if (abi_version != DOE_ERROR_SCOPES_ABI_VERSION)
        return result(DOE_ERROR_SCOPE_UNSUPPORTED_VERSION, 0, saved_errno);
    *out = (doe_error_scope_requirements){
        sizeof(doe_error_scope_stack), _Alignof(doe_error_scope_stack)};
    return result(DOE_ERROR_SCOPE_OK, 0, saved_errno);
}

doe_error_scope_result doe_error_scope_init(uint32_t abi_version,
    void *storage, size_t storage_size, doe_error_scope_stack **out)
{
    const int saved_errno = errno;
    if (out == NULL)
        return result(DOE_ERROR_SCOPE_INVALID_ARGUMENT, 0, saved_errno);
    *out = NULL;
    if (abi_version != DOE_ERROR_SCOPES_ABI_VERSION)
        return result(DOE_ERROR_SCOPE_UNSUPPORTED_VERSION, 0, saved_errno);
    if (storage == NULL)
        return result(DOE_ERROR_SCOPE_INVALID_ARGUMENT, 0, saved_errno);
    if (storage_size < sizeof(doe_error_scope_stack))
        return result(DOE_ERROR_SCOPE_INSUFFICIENT_STORAGE, 0, saved_errno);
    if ((uintptr_t)storage % _Alignof(doe_error_scope_stack) != 0)
        return result(DOE_ERROR_SCOPE_MISALIGNED_STORAGE, 0, saved_errno);
    doe_error_scope_stack *stack = storage;
    const int error = pthread_mutex_init(&stack->mutex, NULL);
    if (error != 0)
        return result(DOE_ERROR_SCOPE_SYSTEM_ERROR, error, saved_errno);
    stack->depth = 0;
    stack->callback = NULL;
    stack->userdata1 = NULL;
    stack->userdata2 = NULL;
    stack->state = DOE_ERROR_SCOPES_LIVE;
    *out = stack;
    return result(DOE_ERROR_SCOPE_OK, 0, saved_errno);
}

doe_error_scope_result doe_error_scope_deinit(doe_error_scope_stack *stack)
{
    const int saved_errno = errno;
    if (stack == NULL)
        return result(DOE_ERROR_SCOPE_INVALID_ARGUMENT, 0, saved_errno);
    if (stack->state != DOE_ERROR_SCOPES_LIVE &&
        stack->state != DOE_ERROR_SCOPES_CLEANUP)
        return result(DOE_ERROR_SCOPE_INVALID_STATE, 0, saved_errno);
    const int error = pthread_mutex_destroy(&stack->mutex);
    if (error != 0) {
        stack->state = DOE_ERROR_SCOPES_CLEANUP;
        return result(DOE_ERROR_SCOPE_CLEANUP_REQUIRED, error, saved_errno);
    }
    stack->state = DOE_ERROR_SCOPES_DEAD;
    return result(DOE_ERROR_SCOPE_OK, 0, saved_errno);
}

doe_error_scope_result doe_error_scope_push(doe_error_scope_stack *stack,
                                          uint32_t filter)
{
    const int saved_errno = errno;
    const doe_error_scope_result entered = enter(stack);
    if (entered.status != DOE_ERROR_SCOPE_OK)
        return result(entered.status, entered.system_error, saved_errno);
    if (stack->depth == DOE_ERROR_SCOPE_MAX_DEPTH) {
        leave(stack, "push");
        return result(DOE_ERROR_SCOPE_LIMIT, 0, saved_errno);
    }
    doe_error_scope_entry *scope = &stack->scopes[stack->depth];
    scope->filter = filter;
    scope->error_type = WGPUErrorType_NoError;
    scope->length = 0;
    memset(scope->bytes, 0, sizeof(scope->bytes));
    stack->depth += 1;
    leave(stack, "push");
    return result(DOE_ERROR_SCOPE_OK, 0, saved_errno);
}

doe_error_scope_result doe_error_scope_pop(doe_error_scope_stack *stack,
    doe_error_scope_callback callback, void *userdata1, void *userdata2)
{
    const int saved_errno = errno;
    const doe_error_scope_result entered = enter(stack);
    if (entered.status != DOE_ERROR_SCOPE_OK)
        return result(entered.status, entered.system_error, saved_errno);
    if (stack->depth == 0) {
        leave(stack, "pop");
        return result(DOE_ERROR_SCOPE_EMPTY, 0, saved_errno);
    }
    stack->depth -= 1;
    const doe_error_scope_entry scope = stack->scopes[stack->depth];
    leave(stack, "pop");
    errno = saved_errno;
    if (callback != NULL) {
        const WGPUStringView message = {
            scope.error_type == WGPUErrorType_NoError ? NULL : (const char *)scope.bytes,
            scope.length};
        callback(scope.error_type, message, userdata1, userdata2);
    }
    /* The callback may have deinitialized and freed stack. */
    return (doe_error_scope_result){DOE_ERROR_SCOPE_OK, 0};
}

doe_error_scope_result doe_error_scope_deliver(doe_error_scope_stack *stack,
    uint32_t error_type, const unsigned char *message, size_t length)
{
    const int saved_errno = errno;
    if (stack == NULL || (message == NULL && length != 0))
        return result(DOE_ERROR_SCOPE_INVALID_ARGUMENT, 0, saved_errno);
    const doe_error_scope_result entered = enter(stack);
    if (entered.status != DOE_ERROR_SCOPE_OK)
        return result(entered.status, entered.system_error, saved_errno);
    for (size_t index = stack->depth; index != 0;) {
        index -= 1;
        doe_error_scope_entry *scope = &stack->scopes[index];
        if (!filter_matches(scope->filter, error_type)) continue;
        if (scope->error_type == WGPUErrorType_NoError) {
            scope->error_type = error_type;
            scope->length = copy_message(scope->bytes, message, length);
        }
        leave(stack, "deliver");
        return result(DOE_ERROR_SCOPE_OK, 0, saved_errno);
    }
    const doe_error_scope_callback callback = stack->callback;
    void *const userdata1 = stack->userdata1;
    void *const userdata2 = stack->userdata2;
    unsigned char bytes[DOE_ERROR_SCOPE_MESSAGE_CAPACITY];
    const size_t copied = callback == NULL ? 0 : copy_message(bytes, message, length);
    leave(stack, "deliver");
    errno = saved_errno;
    if (callback != NULL) {
        const WGPUStringView view = {(const char *)bytes, copied};
        callback(error_type, view, userdata1, userdata2);
    }
    /* No stack or handler storage access after foreign dispatch. */
    return (doe_error_scope_result){DOE_ERROR_SCOPE_OK, 0};
}

doe_error_scope_result doe_error_scope_set_handler(doe_error_scope_stack *stack,
    doe_error_scope_callback callback, void *userdata1, void *userdata2)
{
    const int saved_errno = errno;
    const doe_error_scope_result entered = enter(stack);
    if (entered.status != DOE_ERROR_SCOPE_OK)
        return result(entered.status, entered.system_error, saved_errno);
    stack->callback = callback;
    stack->userdata1 = userdata1;
    stack->userdata2 = userdata2;
    leave(stack, "set_handler");
    return result(DOE_ERROR_SCOPE_OK, 0, saved_errno);
}
