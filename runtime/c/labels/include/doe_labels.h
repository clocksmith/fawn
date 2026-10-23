/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors
 * Frozen ABI 1. Independent case seal still precedes implementation.
 */
#ifndef DOE_LABELS_H
#define DOE_LABELS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DOE_LABELS_ABI_VERSION UINT32_C(1)

typedef struct doe_label_store doe_label_store;
typedef enum doe_label_status {
    DOE_LABEL_OK = 0,
    DOE_LABEL_INVALID_ARGUMENT = 1,
    DOE_LABEL_UNSUPPORTED_VERSION = 2,
    DOE_LABEL_OUT_OF_MEMORY = 3,
    DOE_LABEL_LAYOUT_OVERFLOW = 4,
    DOE_LABEL_SYNCHRONIZATION_INIT = 5,
    DOE_LABEL_CLEANUP_REQUIRED = 6
} doe_label_status;

typedef void *(*doe_label_allocate)(void *state, const void *vtable,
                                   size_t size, size_t alignment);
typedef void (*doe_label_deallocate)(void *state, const void *vtable,
                                    void *memory, size_t size,
                                    size_t alignment);
/* Copied identities, not a pointer to a temporary allocator. Hooks are mandatory;
 * state/vtable may be NULL. Allocation returns an exclusive correctly aligned
 * untyped block of the full extent, or NULL. Free receives the exact original
 * pointer/size/alignment/identities. Hooks return normally, support admitted
 * calling threads, outlive all allocations and do not reenter label-store APIs.
 * Mutation invokes these hooks under the store lock; destroy is exclusive. */
typedef struct doe_label_allocator {
    void *state;
    const void *vtable;
    doe_label_allocate allocate;
    doe_label_deallocate deallocate;
} doe_label_allocator;

typedef struct doe_label_config {
    uint32_t abi_version;
    doe_label_allocator allocator;
} doe_label_config;

typedef struct doe_label_view {
    const unsigned char *data;
    size_t length;
} doe_label_view;

/* NULL out: INVALID_ARGUMENT without reading config. Otherwise *out=NULL;
 * NULL config: INVALID_ARGUMENT; version checked before allocator fields.
 * Hooks missing: INVALID_ARGUMENT. Config is copied. No store is published
 * before successful mutex init. All ordinary setup failures fully roll back. */
doe_label_status doe_label_store_create(const doe_label_config *config,
                                         doe_label_store **out);
/* NULL store is INVALID_ARGUMENT. A closing store returns CLEANUP_REQUIRED
 * before the no-op branches, with no lock, source read or allocator hook.
 * An active store plus NULL handle, NULL data or zero length is a successful
 * no-op, including an already labelled handle.
 * Otherwise copy exactly length bytes, no terminator/encoding interpretation.
 * Any recoverable failure preserves the old value and borrowed pointer. */
doe_label_status doe_label_store_set(doe_label_store *store, const void *handle,
                                      const unsigned char *data, size_t length);
/* NULL out: INVALID_ARGUMENT before inspecting store. NULL store is invalid.
 * A closing store then returns CLEANUP_REQUIRED without locking or hooks.
 * Errors leave *out unchanged. Success writes {NULL,0} for NULL/absent handles.
 * The view BORROWS bytes until that key is replaced/removed or store destroyed.
 * The internal lock is released before return. Caller must exclude concurrent
 * invalidation through the entire dereference; object lifetime alone is not
 * sufficient. Other keys' mutations/rehash do not invalidate this view. */
doe_label_status doe_label_store_get(doe_label_store *store, const void *handle,
                                      doe_label_view *out);
/* NULL store is invalid. A closing store returns CLEANUP_REQUIRED without a
 * lock or hook before NULL/absent-handle handling. Active NULL/absent handle is OK.
 * Matching bytes are freed exactly once; no object is dereferenced or retained.
 * No allocation occurs. */
doe_label_status doe_label_store_remove(doe_label_store *store,
                                         const void *handle);
/* NULL is OK. Requires exclusive access: all operations/borrowed uses ended,
 * nobody can enter later, allocator still alive. Failure to destroy mutex
 * returns CLEANUP_REQUIRED retaining every allocation. Ordinary rejection-only
 * probes remain defined above; only exclusive destroy retry recovers storage.
 * Success frees every label, table and store exactly once. No pointer use after
 * success. Not safe to race object/store destruction. */
doe_label_status doe_label_store_destroy(doe_label_store *store);

#ifdef __cplusplus
}
#endif
#endif
