/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors
 * C owner derived from runtime/zig/src/native/support/doe_future_ids.zig.
 * See CONTRACT.md for source identity, storage and lifetime requirements.
 */
#ifndef DOE_FUTURES_H
#define DOE_FUTURES_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DOE_FUTURES_ABI_VERSION UINT32_C(1)
#define DOE_FUTURES_TIMEOUT_INFINITE UINT64_MAX

typedef struct doe_future_queue doe_future_queue;
typedef struct doe_future_completion doe_future_completion;

typedef enum doe_future_status {
    DOE_FUTURE_OK = 0,
    DOE_FUTURE_TIMED_OUT = 1,
    DOE_FUTURE_INVALID_ARGUMENT = 2,
    DOE_FUTURE_UNSUPPORTED_VERSION = 3,
    DOE_FUTURE_INSUFFICIENT_STORAGE = 4,
    DOE_FUTURE_MISALIGNED_STORAGE = 5,
    DOE_FUTURE_UNKNOWN_ID = 6,
    DOE_FUTURE_DUPLICATE = 7,
    DOE_FUTURE_BUSY = 8,
    DOE_FUTURE_EXHAUSTED = 9,
    DOE_FUTURE_SYSTEM_ERROR = 10,
    DOE_FUTURE_INVALID_STATE = 11,
    DOE_FUTURE_TIME_OVERFLOW = 12,
    DOE_FUTURE_CLEANUP_REQUIRED = 13
} doe_future_status;

typedef enum doe_future_mode {
    DOE_FUTURE_MODE_COMPAT_IMMEDIATE = 0,
    DOE_FUTURE_MODE_WAIT_ANY_ONLY = 1,
    DOE_FUTURE_MODE_ALLOW_PROCESS_EVENTS = 2,
    DOE_FUTURE_MODE_ALLOW_SPONTANEOUS = 3
} doe_future_mode;

typedef struct doe_future_storage_requirements {
    size_t size;
    size_t alignment;
} doe_future_storage_requirements;

typedef struct doe_future_wait_info {
    uint64_t id;
    uint32_t completed;
} doe_future_wait_info;

/* Called outside every queue mutex. May finish/deinit/free the completion.
 * Keep queue alive until the enclosing library call returns. */
typedef void (*doe_future_dispatch)(doe_future_completion *completion,
                                    void *userdata);

/* No allocation. Version is mandatory. Query the current library/build; storage
 * requirements may change between builds with the same API version.
 * On failure, output arguments are unchanged unless documented otherwise. */
doe_future_status doe_future_queue_requirements(
    uint32_t abi_version, doe_future_storage_requirements *out);
doe_future_status doe_future_completion_requirements(
    uint32_t abi_version, doe_future_storage_requirements *out);

/* Storage must be untyped malloc/aligned_alloc-compatible storage. A declared
 * char/byte array is NOT an admitted effective-type workaround. Do not copy or
 * move initialized storage. Init is exclusive. Ordinary init failure completes
 * cleanup, leaves reusable storage and *out=NULL (when out != NULL). Exceptional
 * CLEANUP_REQUIRED returns a cleanup-only queue in *out: retain its storage and
 * retry only queue_deinit until OK. Do not free/reinitialize it prematurely. */
doe_future_status doe_future_queue_init(uint32_t abi_version, void *storage,
                                         size_t storage_size,
                                         doe_future_queue **out);
doe_future_status doe_future_completion_init(
    uint32_t abi_version, void *storage, size_t storage_size,
    doe_future_mode mode, doe_future_dispatch dispatch, void *userdata,
    doe_future_completion **out);

/* Deinit requires caller-established quiescence/no concurrent new entry.
 * Queue returns BUSY for pending nodes or active calls/waits/callbacks, including
 * a library-dispatched callback that already finished its node. Manual external
 * callback extent after begin_delivery returns remains caller-owned. Deinit never
 * waits for quiescence. CLEANUP_REQUIRED leaves a cleanup-only queue admitting
 * only exclusive deinit retry; OK permits caller free/reinit. See CONTRACT.md. */
doe_future_status doe_future_queue_deinit(doe_future_queue *queue);
doe_future_status doe_future_completion_deinit(
    doe_future_completion *completion);

/* IDs are unique within one linked library instance, not across DSOs/processes.
 * An issued ID may be registered at most once, including after retirement. */
doe_future_status doe_future_new_id(doe_future_queue *queue, uint64_t *out_id);
doe_future_status doe_future_legacy_id(uint64_t *out_id);
doe_future_status doe_future_register(doe_future_queue *queue,
                                      doe_future_completion *completion,
                                      uint64_t id);

/* Manual-delivery compatibility: sets the delivering thread without invoking a
 * callback. Only before delivery starts; caller owns manual callback lifetime. */
doe_future_status doe_future_begin_delivery(doe_future_completion *completion);

/* Exactly one readiness publication while the completion is live. Non-NULL
 * dispatch required. No safety promise for repeated calls using stale storage.
 * Registered modes 0/3 dispatch here; 1/2 wait for wait-any / event pumping.
 * An unregistered completion dispatches immediately, matching the source owner. */
doe_future_status doe_future_mark_ready(doe_future_completion *completion);

/* Removes registered obligation; unregistered live completion is a no-op.
 * Outside the delivering callback, caller must exclude concurrent dispatch and
 * publication before retirement/free. No queue operation may touch this node
 * after invoking its dispatch callback. */
doe_future_status doe_future_finish(doe_future_completion *completion);

/* Captures issuance boundary, not a readiness snapshot. Selects lowest eligible
 * ID each time; does not impose total callback order across simultaneous pumps. */
doe_future_status doe_future_process_events(doe_future_queue *queue);

/* count == 0 succeeds (infos may be NULL). Otherwise all IDs must belong to
 * queue. Duplicate entries do not duplicate delivery. UINT64_MAX waits forever;
 * finite timeout is one monotonic deadline and includes callback/reentry time;
 * it cannot preempt a callback or bound the callback's wall time.
 * Timeout ends waiting, never retires a node or cancels work. */
doe_future_status doe_future_wait_any(doe_future_queue *queue,
                                      doe_future_wait_info *infos,
                                      size_t count, uint64_t timeout_ns);

#ifdef __cplusplus
}
#endif
#endif
