/* SPDX-License-Identifier: MIT
 * First-use orchestration controls at the typed compile/PSO seams; no GPU. */
#include "../src/d3d12_indirect.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct native_d3d12_guard_code { unsigned identity; };
typedef struct control {
    doe_mutex mutex;
    doe_condition changed;
    unsigned compilations, releases, creations;
    bool blocked, proceed;
    native_result compile_result, pipeline_result, device_result;
} control;
static control seen;
static _Thread_local unsigned owner_depth;

native_failure native_failure_status(native_result status)
{
    return (native_failure){.status=status};
}
void native_device_retain(void *identity)
{
    native_device *device = identity;
    assert(atomic_fetch_add(&device->references, 1) >= 1);
}
void native_device_release(void *identity)
{
    native_device *device = identity;
    assert(!owner_depth && atomic_fetch_sub(&device->references, 1) > 1);
}
void native_lock(native_device *device)
{
    assert(doe_mutex_lock(&device->mutex).status == DOE_HOST_OK);
    owner_depth++;
}
void native_unlock(native_device *device)
{
    assert(owner_depth-- == 1);
    assert(doe_mutex_unlock(&device->mutex).status == DOE_HOST_OK);
}
native_result native_device_result(native_device *device)
{
    assert(owner_depth == 1);
    return device->destroyed ? NATIVE_INVALID_STATE : seen.device_result;
}
static void await_compilers(unsigned count)
{
    while (seen.compilations < count)
        assert(doe_condition_wait_for(&seen.changed, &seen.mutex,
            UINT64_C(3000000000)).status == DOE_HOST_OK);
}
native_failure native_d3d12_guard_compile(native_device *device,
    native_d3d12_guard_code **out)
{
    assert(!owner_depth && atomic_load(&device->references) > 1);
    assert(doe_mutex_lock(&seen.mutex).status == DOE_HOST_OK);
    unsigned identity = ++seen.compilations;
    assert(doe_condition_broadcast(&seen.changed).status == DOE_HOST_OK);
    while (seen.blocked && !seen.proceed)
        assert(doe_condition_wait_for(&seen.changed, &seen.mutex,
            UINT64_C(3000000000)).status == DOE_HOST_OK);
    native_result result = seen.compile_result;
    assert(doe_mutex_unlock(&seen.mutex).status == DOE_HOST_OK);
    if (result != NATIVE_OK) return native_failure_status(result);
    *out = malloc(sizeof(**out));
    assert(*out);
    (*out)->identity = identity;
    return native_failure_status(NATIVE_OK);
}
void native_d3d12_guard_code_release(native_d3d12_guard_code *code)
{
    assert(!owner_depth);
    if (!code) return;
    assert(doe_mutex_lock(&seen.mutex).status == DOE_HOST_OK);
    seen.releases++;
    assert(doe_mutex_unlock(&seen.mutex).status == DOE_HOST_OK);
    free(code);
}
native_failure native_d3d12_guard_pipeline(native_device *device,
    const native_d3d12_guard_code *code, doe_dx_pipeline **out)
{
    assert(owner_depth == 1 && code && code->identity);
    assert(out == &device->d3d12_dispatch_guard && !*out);
    if (seen.pipeline_result != NATIVE_OK)
        return native_failure_status(seen.pipeline_result);
    seen.creations++;
    *out = (doe_dx_pipeline *)(uintptr_t)1;
    return native_failure_status(NATIVE_OK);
}
typedef struct invocation {
    native_device *device;
    native_failure result;
} invocation;
static void initialize(void *identity)
{
    invocation *call = identity;
    call->result = native_d3d12_guard_initialize(call->device);
}
static void race(native_device *device, bool cancel, bool lost)
{
    seen.compilations = seen.releases = seen.creations = 0;
    seen.blocked = true;
    seen.proceed = false;
    invocation calls[2] = {{.device=device}, {.device=device}};
    doe_thread threads[2] = {{0}, {0}};
    assert(doe_mutex_lock(&seen.mutex).status == DOE_HOST_OK);
    for (unsigned i = 0; i < 2; ++i)
        assert(doe_thread_create(&threads[i], initialize, &calls[i]).status == DOE_HOST_OK);
    await_compilers(2);
    assert(!device->d3d12_dispatch_guard && atomic_load(&device->references) == 3);
    native_lock(device);
    device->destroyed = cancel;
    seen.device_result = lost ? NATIVE_DEVICE_LOST : NATIVE_OK;
    native_unlock(device);
    seen.proceed = true;
    assert(doe_condition_broadcast(&seen.changed).status == DOE_HOST_OK);
    assert(doe_mutex_unlock(&seen.mutex).status == DOE_HOST_OK);
    for (unsigned i = 0; i < 2; ++i) {
        assert(doe_thread_join(&threads[i]).status == DOE_HOST_OK);
        assert(calls[i].result.status == (cancel ? NATIVE_INVALID_STATE :
            lost ? NATIVE_DEVICE_LOST : NATIVE_OK));
    }
    assert(seen.releases == 2 && atomic_load(&device->references) == 1);
    assert(seen.creations == (cancel || lost ? 0u : 1u));
    assert((device->d3d12_dispatch_guard != NULL) == (!cancel && !lost));
    device->d3d12_dispatch_guard = NULL;
    device->destroyed = false;
    seen.device_result = NATIVE_OK;
    seen.blocked = false;
}
int main(void)
{
    native_device device = {0};
    atomic_init(&device.references, 1);
    device.backend.kind = WGPUBackendType_D3D12;
    assert(doe_mutex_init(&device.mutex, true).status == DOE_HOST_OK);
    assert(doe_mutex_init(&seen.mutex, false).status == DOE_HOST_OK);
    assert(doe_condition_init(&seen.changed).status == DOE_HOST_OK);
    assert(native_d3d12_guard_initialize(NULL).status == NATIVE_INVALID_ARGUMENT);
    seen.compile_result = NATIVE_OUT_OF_MEMORY;
    assert(native_d3d12_guard_initialize(&device).status == NATIVE_OUT_OF_MEMORY);
    assert(!device.d3d12_dispatch_guard && !seen.creations && !seen.releases);
    seen.compile_result = NATIVE_OK;
    seen.pipeline_result = NATIVE_FAILURE;
    assert(native_d3d12_guard_initialize(&device).status == NATIVE_FAILURE);
    assert(!device.d3d12_dispatch_guard && !seen.creations && seen.releases == 1);
    seen.pipeline_result = NATIVE_OK;
    race(&device, false, false);
    assert(native_d3d12_guard_initialize(&device).status == NATIVE_OK);
    unsigned compilations = seen.compilations, creations = seen.creations;
    assert(native_d3d12_guard_initialize(&device).status == NATIVE_OK);
    assert(seen.compilations == compilations && seen.creations == creations);
    device.d3d12_dispatch_guard = NULL;
    race(&device, true, false);
    race(&device, false, true);
    assert(doe_condition_destroy(&seen.changed).status == DOE_HOST_OK);
    assert(doe_mutex_destroy(&seen.mutex).status == DOE_HOST_OK);
    assert(doe_mutex_destroy(&device.mutex).status == DOE_HOST_OK);
    puts("PASS D3D12 lazy guard preparation: unlocked compile, failure retry, concurrent first use, cancellation/loss and leases; no SDK/GPU");
    return 0;
}
