/* SPDX-License-Identifier: MIT */
#ifndef DOE_NATIVE_SHADER_IDENTITY_WORKER_H
#define DOE_NATIVE_SHADER_IDENTITY_WORKER_H
#include "doe_platform.h"
#include <stdbool.h>
#include <stdint.h>

/* One per device. D3D12 device open starts a worker computing the exact
 * native_shader_cache_identity digest before the backend opens, so the hash
 * overlaps descriptor-ring, UPLOAD-heap and compiler-module creation. The
 * worker writes only its own result; thread join publishes it. The first
 * pipeline that needs the identity claims and joins it under the device mutex
 * protocol; every other caller keeps the existing inline computation. */
typedef enum native_identity_worker_state {
    NATIVE_IDENTITY_WORKER_NONE = 0, /* Never started, start failed, or consumed. */
    NATIVE_IDENTITY_WORKER_RUNNING,
    NATIVE_IDENTITY_WORKER_CLAIMED   /* One caller is joining outside the mutex. */
} native_identity_worker_state;
typedef struct native_identity_worker {
    doe_thread thread;
    native_identity_worker_state state;
    bool valid;
    uint8_t digest[32];
} native_identity_worker;
typedef enum native_identity_take {
    NATIVE_IDENTITY_TAKE_NONE,   /* No worker result for this caller: compute inline. */
    NATIVE_IDENTITY_TAKE_READY,  /* digest holds the worker's exact identity. */
    NATIVE_IDENTITY_TAKE_FAILED  /* The worker's identity failed: no persistence now. */
} native_identity_take;

/* Starting is best effort: failure leaves NONE and the inline path unchanged. */
void native_identity_worker_start(native_identity_worker *worker);
/* Call with the device mutex held; returns true when this caller now owns the
 * join (state CLAIMED). Join and publish run outside the mutex. */
bool native_identity_worker_claim(native_identity_worker *worker);
native_identity_take native_identity_worker_join(native_identity_worker *worker, uint8_t digest[32]);
/* Device finalization and failed construction: join a never-claimed worker. */
void native_identity_worker_finish(native_identity_worker *worker);
#endif
