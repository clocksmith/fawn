/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors
 * Reviewed contract-only declaration. Derived from runtime/zig/src/runtime/diagnostics/error_scope.zig.
 * CONTRACT.md owns semantics; this header does not claim an implementation. */
#ifndef DOE_ERROR_SCOPES_H
#define DOE_ERROR_SCOPES_H

#include <stddef.h>
#include <stdint.h>
#include "webgpu.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DOE_ERROR_SCOPES_ABI_VERSION UINT32_C(1)
#define DOE_ERROR_SCOPE_MAX_DEPTH 64u
#define DOE_ERROR_SCOPE_MESSAGE_CAPACITY 512u

typedef struct doe_error_scope_stack doe_error_scope_stack;

typedef enum doe_error_scope_status {
    DOE_ERROR_SCOPE_OK = 0,
    DOE_ERROR_SCOPE_EMPTY = 1,
    DOE_ERROR_SCOPE_LIMIT = 2,
    DOE_ERROR_SCOPE_INVALID_ARGUMENT = 3,
    DOE_ERROR_SCOPE_UNSUPPORTED_VERSION = 4,
    DOE_ERROR_SCOPE_INSUFFICIENT_STORAGE = 5,
    DOE_ERROR_SCOPE_MISALIGNED_STORAGE = 6,
    DOE_ERROR_SCOPE_INVALID_STATE = 7,
    DOE_ERROR_SCOPE_SYSTEM_ERROR = 8,
    DOE_ERROR_SCOPE_CLEANUP_REQUIRED = 9
} doe_error_scope_status;

/* system_error is the original POSIX return code or Win32 error bits only for SYSTEM_ERROR or
 * CLEANUP_REQUIRED; it is zero for every other status. No global last-error. */
typedef struct doe_error_scope_result {
    doe_error_scope_status status;
    int system_error;
} doe_error_scope_result;

typedef struct doe_error_scope_requirements {
    size_t size;
    size_t alignment;
} doe_error_scope_requirements;

/* Doe's INTERNAL synchronous ABI. NOT WGPUPopErrorScopeCallback (has status),
 * and NOT WGPUUncapturedErrorCallback (has device argument). Exact uint32_t
 * preserves the native API's unknown numeric error values. Message is borrowed
 * only through callback return. Either userdata may be NULL. */
typedef void (*doe_error_scope_callback)(uint32_t error_type,
                                       WGPUStringView message,
                                       void *userdata1,
                                       void *userdata2);

/* No allocation by this library. Requery requirements for this library build.
 * Failed query leaves *out unchanged. */
doe_error_scope_result doe_error_scope_storage_requirements(
    uint32_t abi_version, doe_error_scope_requirements *out);

/* Init requires exclusive untyped allocated storage, not a declared byte array.
 * After out validation, *out is NULL on failure. Ordinary failure leaves storage
 * reusable. Never move/copy initialized storage. Private native bridge supports
 * an actual imported C typed object, not an aliased byte-array approximation. */
doe_error_scope_result doe_error_scope_init(uint32_t abi_version,
    void *storage, size_t storage_size, doe_error_scope_stack **out);

/* Caller excludes new/concurrent stack access; no implicit waiting/refcount.
 * Discards pending scopes without callbacks. CLEANUP_REQUIRED preserves owned
 * synchronization/storage and permits only exclusive deinit retry. A callback
 * can deinit/free when no other call can access the stack, because its invoking
 * pop/deliver never accesses the stack after dispatch begins. */
doe_error_scope_result doe_error_scope_deinit(doe_error_scope_stack *stack);

/* Ordinary operation validation: NULL stack -> INVALID_ARGUMENT, then deliver's
 * NULL/nonzero message -> INVALID_ARGUMENT, then known DEAD/cleanup-only state
 * -> INVALID_STATE. These results have system_error=0 and never access a mutex,
 * message bytes or callback. Only deinit retry recovers cleanup-only state.
 * Every callback-free nonterminal return preserves entry errno, including errors;
 * after dispatch the callback's resulting errno remains visible.
 * All filter/type numbers are accepted. Full push returns LIMIT without change;
 * native compatibility deliberately maps LIMIT to the retained silent no-op. */
doe_error_scope_result doe_error_scope_push(doe_error_scope_stack *stack,
                                          uint32_t filter);

/* EMPTY invokes no callback. OK consumes one scope even if callback is NULL.
 * Dispatch is synchronous, outside the mutex; no stack access after dispatch. */
doe_error_scope_result doe_error_scope_pop(doe_error_scope_stack *stack,
    doe_error_scope_callback callback, void *userdata1, void *userdata2);

/* Message is length-delimited, truncated to 512 bytes, never strlen-decoded.
 * NULL/zero is empty. NULL/nonzero is INVALID_ARGUMENT before state mutation. */
doe_error_scope_result doe_error_scope_deliver(doe_error_scope_stack *stack,
    uint32_t error_type, const unsigned char *message, size_t length);

/* Atomically replaces the callback+two-userdata tuple under the stack mutex.
 * Clearing is not a barrier for callbacks already snapshotted by another call. */
doe_error_scope_result doe_error_scope_set_handler(doe_error_scope_stack *stack,
    doe_error_scope_callback callback, void *userdata1, void *userdata2);

#ifdef __cplusplus
}
#endif
#endif
