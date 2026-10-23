/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors
 * Frozen experimental C instance boundary; see CONTRACT.md and PROVENANCE.md.
 * No implementation, production selection or physical qualification is implied.
 */
#ifndef DOE_INSTANCE_H
#define DOE_INSTANCE_H

#include <stddef.h>
#include <stdint.h>
#include "webgpu.h"
#include "doe_futures.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DOE_INSTANCE_ABI_VERSION UINT32_C(1)
typedef struct doe_instance doe_instance;
typedef enum doe_instance_status {
    DOE_INSTANCE_OK = 0,
    DOE_INSTANCE_INVALID_ARGUMENT = 1,
    DOE_INSTANCE_UNSUPPORTED_VERSION = 2,
    DOE_INSTANCE_OUT_OF_MEMORY = 3,
    DOE_INSTANCE_LAYOUT_OVERFLOW = 4,
    DOE_INSTANCE_QUEUE_INITIALIZATION = 5
} doe_instance_status;

typedef void *(*doe_instance_allocate)(void *state, const void *vtable,
                                      size_t size, size_t alignment);
typedef void (*doe_instance_deallocate)(void *state, const void *vtable,
                                      void *memory, size_t size,
                                      size_t alignment);
/* Copied allocator identities, not pointers to a temporary allocator record.
 * Both hooks are mandatory. Unused state/vtable tokens may be NULL. allocate
 * returns one untyped allocation of the requested extent/alignment or NULL;
 * deallocate receives the identical identity, pointer, extent and alignment.
 * Hook state must support the calling threads and outlive every allocation. */
typedef struct doe_instance_allocator {
    void *state;
    const void *vtable;
    doe_instance_allocate allocate;
    doe_instance_deallocate deallocate;
} doe_instance_allocator;

typedef void (*doe_instance_allocated)(void *userdata, doe_instance *instance,
                                     size_t allocation_size,
                                     size_t allocation_alignment);
typedef uint32_t (*doe_instance_external_count)(void *userdata,
                                              doe_instance *instance);
typedef void (*doe_instance_remove_label)(void *userdata,
                                        doe_instance *instance);
/* Configuration is copied by value, never retained by address. Optional hooks
 * mean: allocated/remove_label no-op; external_texture_count zero. Native
 * integration supplies all three. Hooks return normally and acquire no implicit
 * lease. allocated observes an unpublished/uninitialized allocation only.
 * remove_label observes cleanup identity after zero count and queue teardown;
 * neither it nor final deallocate may reenter/resurrect the dying instance.
 * No allocated hook may retain, borrow or reenter the unpublished instance.
 * Ordinary delivery callback reentry remains valid under its actual leases. */
typedef struct doe_instance_config {
    uint32_t abi_version;
    doe_instance_allocator object_allocator;
    doe_instance_allocator wait_allocator;
    void *hook_userdata;
    doe_instance_allocated allocated;
    doe_instance_external_count external_texture_count;
    doe_instance_remove_label remove_label;
} doe_instance_config;

typedef void (*doe_instance_lease_hook)(void *userdata);
/* NON-OWNING value: copying this record does not acquire a reference. queue is
 * the actual initialized accepted queue, never a wrapper or copied storage.
 * userdata and hooks denote its exact C instance lease. Caller already holds
 * lifetime through use; acquire an explicit reference before it escapes that
 * scope. Hooks cannot fail; final release may destroy instance and queue. */
typedef struct doe_instance_queue_lease {
    doe_future_queue *queue;
    void *userdata;
    doe_instance_lease_hook retain;
    doe_instance_lease_hook release;
} doe_instance_queue_lease;

/* Validation: NULL out -> INVALID_ARGUMENT without reading config. Otherwise
 * set *out=NULL, reject NULL config, check abi_version before nested fields,
 * then validate allocator pairs and copy configuration. Config/out are valid,
 * nonoverlapping storage through return. Ordinary failure returns NULL with
 * complete rollback; CLEANUP_REQUIRED and impossible queue state terminate
 * diagnostically without freeing still-owned synchronization. */
doe_instance_status doe_instance_create(const doe_instance_config *config,
                                        doe_instance **out);
/* Reads a C-defined magic prefix using memcpy. NULL/mismatching readable
 * marker -> NULL. Requires live/readable storage; not validation of arbitrary,
 * forged, inaccessible or stale pointers and not an old Zig layout overlay. */
doe_instance *doe_instance_from_handle(void *readable_handle);
/* NULL is a no-op. Live authentic instances only; retain rejects zero/max.
 * Ordinary release preserves the retained external-texture guard and C alone
 * decides/finalizes the final count. Cleanup failure terminates before free. */
void doe_instance_retain(doe_instance *instance);
void doe_instance_release(doe_instance *instance);
/* NULL is a no-op. Undo only an unregistered speculative retain while an
 * independent original owner lives: acq_rel nonfinal decrement, ignoring the
 * external guard. Count <=1 is fatal. Never a general release/finalization API. */
void doe_instance_undo_retain(doe_instance *instance);
/* Requires a live non-NULL instance. Does not retain or issue a future ID. */
doe_instance_queue_lease doe_instance_borrow_queue(doe_instance *instance);
/* count0 succeeds before inspecting instance/infos. Otherwise NULL instance
 * returns Error3; readable infos[count] is required after checked size bounds.
 * Copy into distinct accepted-future records using wait_allocator, copy flags
 * back and free before releasing a full-call lease. Overflow/OOM leaves flags
 * untouched. OK/timeout/other map to Success1/TimedOut2/Error3. Timeout never
 * retires work. Duplicate IDs preserve separate flags and single delivery. */
WGPUWaitStatus doe_instance_wait_any(doe_instance *instance, size_t count,
                                    WGPUFutureWaitInfo *infos,
                                    uint64_t timeout_ns);
/* Standalone NULL is a no-op. For live instance, hold a full-call lease until
 * accepted queue return. Native NULL/invalid fallback remains the separate
 * retained global registry. Every explicit recursive pump gets its own fresh
 * issuance boundary; outer-pump deferral does not suppress nested delivery. */
void doe_instance_process_events(doe_instance *instance);
/* Conservative unchanged capability contract: empty/NULL features, all feature
 * queries false, limits nextInChain=NULL and timedWaitAnyMaxCount=0. Native
 * GetInstanceLimits returns Success1. FreeMembers is always a no-op. */
WGPUSupportedInstanceFeatures doe_instance_features(void);
WGPUInstanceLimits doe_instance_limits(void);
WGPUBool doe_instance_has_feature(WGPUInstanceFeatureName feature);
void doe_instance_features_free_members(WGPUSupportedInstanceFeatures features);

#ifdef __cplusplus
}
#endif
#endif
