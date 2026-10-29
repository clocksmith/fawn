/* SPDX-License-Identifier: MIT */
#include "d3d12_indirect.h"

native_failure native_d3d12_guard_initialize(native_device *device)
{
    if (!device || device->backend.kind != WGPUBackendType_D3D12)
        return native_failure_status(NATIVE_INVALID_ARGUMENT);
    native_device_retain(device);
    native_lock(device);
    native_failure failure = native_failure_status(native_device_result(device));
    bool ready = device->d3d12_dispatch_guard != NULL;
    native_unlock(device);
    native_d3d12_guard_code *compiled = NULL;
    if (failure.status == NATIVE_OK && !ready)
        failure = native_d3d12_guard_compile(device, &compiled);
    if (failure.status == NATIVE_OK && compiled) {
        native_lock(device);
        failure = native_failure_status(native_device_result(device));
        if (failure.status == NATIVE_OK && !device->d3d12_dispatch_guard) {
            /* DXC and its loader finished outside the mutex. The existing
             * context serialization owns actual PSO creation and publication. */
            failure = native_d3d12_guard_pipeline(device, compiled,
                &device->d3d12_dispatch_guard);
        }
        native_unlock(device);
    }
    /* Another initializer can win publication while this one compiles. Its
     * immutable compiler result still releases outside all owner locks. */
    native_d3d12_guard_code_release(compiled);
    native_device_release(device);
    return failure;
}
