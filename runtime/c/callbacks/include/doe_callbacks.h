/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors
 * Frozen callback payload/instance lease boundary; acceptance is separate.
 * Derived from native/support/doe_callback_delivery.zig; see CONTRACT.md.
 */
#ifndef DOE_CALLBACKS_H
#define DOE_CALLBACKS_H

#include "doe_futures.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DOE_CALLBACKS_ABI_VERSION UINT32_C(1)

typedef struct doe_callback_event doe_callback_event;

typedef enum doe_callback_status {
    DOE_CALLBACK_OK = 0,
    DOE_CALLBACK_INVALID_ARGUMENT = 1,
    DOE_CALLBACK_UNSUPPORTED_VERSION = 2,
    DOE_CALLBACK_UNSUPPORTED_MODE = 3,
    DOE_CALLBACK_INSTANCE_UNAVAILABLE = 4,
    DOE_CALLBACK_OUT_OF_MEMORY = 5,
    DOE_CALLBACK_LAYOUT_OVERFLOW = 6
} doe_callback_status;

/* These two identities are allocator callback userdata, not erased resources.
 * The binding is copied by value; never pass the address of a stack allocator.
 * allocate returns NULL without an allocation or context/resource transfer,
 * or one untyped allocation meeting
 * the exact byte extent/alignment. deallocate receives the identical arguments.
 * Underlying state/vtable and hooks must outlive every allocated event and support
 * calls from producer, wait/pump or worker threads. No originating-thread free
 * guarantee or extra serialization of hook state is provided. */
typedef void *(*doe_callback_allocate)(void *state, const void *vtable,
                                      size_t size, size_t alignment);
typedef void (*doe_callback_deallocate)(void *state, const void *vtable,
                                       void *allocation, size_t size,
                                       size_t alignment);
typedef struct doe_callback_allocator {
    void *state;
    const void *vtable;
    doe_callback_allocate allocate;
    doe_callback_deallocate deallocate;
} doe_callback_allocator;

/* A live initialized accepted-C queue and its lifetime lease. Hooks cannot fail
 * and the lease holds that exact queue alive. Its final release may destroy the
 * owner/queue. The record itself is borrowed only during publish, then copied.
 * Both hooks are required for an owned request, even for caller-owned fixtures;
 * shared hook state must support concurrent producer, wait/pump and worker calls. */
typedef void (*doe_callback_lease_hook)(void *userdata);
typedef struct doe_callback_owner {
    doe_future_queue *queue;
    void *userdata;
    doe_callback_lease_hook retain;
    doe_callback_lease_hook release;
} doe_callback_owner;

/* Narrow typed-context boundary. copy initializes the declared Context at the
 * final aligned destination with an infallible value copy; it does not acquire
 * leases, allocate, retain source storage or deep-copy pointer/slice referents.
 * deliver includes foreign callback and all per-API resource cleanup. It must
 * return normally. The context is mutable and cannot escape that delivery. */
typedef void (*doe_callback_copy_context)(void *destination, const void *source);
typedef void (*doe_callback_deliver_context)(void *context);

typedef struct doe_callback_request {
    uint32_t abi_version;
    uint32_t mode; /* Exact accepted future values 0, 1, 2, 3 only. */
    const doe_callback_owner *owner; /* NULL selects the ownerless path. */
    doe_callback_allocator allocator;
    void *source_context; /* Mutable adapter-local value; borrowed during call. */
    size_t context_size;
    size_t context_alignment;
    doe_callback_copy_context copy;
    doe_callback_deliver_context deliver;
} doe_callback_request;

/* Called exactly once only for an owned request, after transfer/registration.
 * Publish readiness now or hand the token to one guaranteed eventual publisher.
 * Submission failure must fall back before transfer to a worker, not abandon it.
 * userdata is borrowed only for this call; it is not captured by the event.
 * mark_ready may free event and its owner before returning: never reuse token. */
typedef void (*doe_callback_publisher)(doe_callback_event *event, void *userdata);

/* Return errors occur ONLY before transfer/ID issuance; *out_future_id remains
 * unchanged. Once admitted, context cleanup belongs to deliver, even when the
 * foreign callback is NULL. Unexpected queue/exhaustion/platform failures abort
 * with a diagnostic, never return false rollback. Inputs/out must remain valid
 * through return and cannot overlap event/owner storage freed by a callback.
 * A valid ownerless 0/3 request delivers source_context synchronously, with no
 * allocation/copy/publisher/lease/registration, then issues its legacy ID.
 * source_context must therefore already be a separate mutable local value. */
doe_callback_status doe_callback_publish(const doe_callback_request *request,
                                          doe_callback_publisher publisher,
                                          void *publisher_userdata,
                                          uint64_t *out_future_id);

/* Same contract, with the built-in immediate readiness publisher. */
doe_callback_status doe_callback_ready(const doe_callback_request *request,
                                        uint64_t *out_future_id);

/* Exactly one call on a still-live admitted event. Takes an independent owner
 * lease around accepted-C readiness; snapshots all required data before entry.
 * The event and its context may be freed before this function returns. No
 * cancellation, repeated/stale publication, inspection or manual retirement. */
void doe_callback_mark_ready(doe_callback_event *event);

#ifdef __cplusplus
}
#endif
#endif
