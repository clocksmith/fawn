/*
 * doe_napi_timeout.h — Deadline for waits on submitted GPU work.
 *
 * Queue completion and map readback have no deadline unless DOE_TIMEOUT_MS or
 * setNativeTimeoutMs set one (timeout_ns nonzero). WebGPU defines none; a long
 * valid submission is not a failure, and the driver reports real faults as
 * errors or device loss. Adapter/device requests use current_timeout_ns.
 */
#ifndef DOE_NAPI_TIMEOUT_H
#define DOE_NAPI_TIMEOUT_H
#include "doe_napi_internal.h"

static inline uint64_t gpu_work_timeout_ns(napi_env env) {
    const uint64_t explicit_ns = doe_env_state(env)->timeout_ns;
    return explicit_ns ? explicit_ns : UINT64_MAX;
}
#endif
