/* SPDX-License-Identifier: MIT */
#include "d3d12_frontend.h"
#include <stdlib.h>
#if defined(_WIN32)
#include <d3d12.h>

static native_result finish_open(native_backend *backend, doe_dx_status status, native_backend *out)
{
    native_result result = native_from_d3d12(status);
    if (result == NATIVE_OK)
        result = native_from_d3d12(doe_dx_context_caps(backend->value.d3d12.context,
            &backend->value.d3d12.caps));
    /* The public compiler target is DXIL Shader Model6.0. Bytecode compilation
     * cannot qualify a device which did not positively offer that profile. */
    if (result == NATIVE_OK && backend->value.d3d12.caps.shader_model < D3D_SHADER_MODEL_6_0)
        result = NATIVE_UNAVAILABLE;
    if (result == NATIVE_OK && native_d3d12_limits(&backend->value.d3d12.caps,
        &backend->limits) != WGPUStatus_Success) result = NATIVE_UNAVAILABLE;
    if (result != NATIVE_OK) {
        native_d3d12_close(backend);
        return result;
    }
    *out = *backend;
    return NATIVE_OK;
}

static native_result open_selected(const doe_dx_options *options, native_backend *out)
{
    native_backend backend = {.kind = WGPUBackendType_D3D12};
    backend.value.d3d12.options = *options;
    return finish_open(&backend,
        doe_dx_context_create(options, &backend.value.d3d12.context, NULL), out);
}
#endif

native_result native_d3d12_open(bool fallback, native_backend *out)
{
#if defined(_WIN32)
    doe_dx_options options = {.allow_software_adapter = fallback,
        .adapter_selection = fallback ? DOE_DX_SELECT_WARP : DOE_DX_SELECT_HARDWARE};
    return open_selected(&options, out);
#else
    (void)fallback;
    (void)out;
    return NATIVE_UNAVAILABLE;
#endif
}

native_result native_d3d12_open_luid(uint32_t low, int32_t high, native_backend *out)
{
#if defined(_WIN32)
    native_backend backend = {.kind = WGPUBackendType_D3D12};
    doe_dx_status status = doe_dx_context_create_from_luid(low, high,
        &backend.value.d3d12.context, NULL);
    native_result result = finish_open(&backend, status, out);
    if (result == NATIVE_OK) {
        const doe_dx_caps *identity = &out->value.d3d12.caps;
        out->value.d3d12.options = (doe_dx_options){.adapter_selection = DOE_DX_SELECT_LUID,
            .allow_software_adapter = identity->software_adapter,
            .adapter_identity = {identity->luid_low,identity->luid_high,identity->software_adapter}};
    }
    return result;
#else
    (void)low;
    (void)high;
    (void)out;
    return NATIVE_UNAVAILABLE;
#endif
}

native_result native_d3d12_reopen(const native_backend *adapter, native_backend *out)
{
#if defined(_WIN32)
    const doe_dx_caps *expected = &adapter->value.d3d12.caps;
    doe_dx_options options = {.allow_software_adapter = expected->software_adapter,
        .adapter_selection = DOE_DX_SELECT_LUID,
        .adapter_identity = {expected->luid_low, expected->luid_high, expected->software_adapter}};
    native_backend result = {0};
    native_result status = open_selected(&options, &result);
    if (status != NATIVE_OK) return status;
    const doe_dx_caps *actual = &result.value.d3d12.caps;
    if (actual->luid_low == expected->luid_low && actual->luid_high == expected->luid_high &&
        actual->software_adapter == expected->software_adapter) {
        /* Device creation, not the first submission, owns the shader-visible
         * descriptor heap (Dawn creates its equivalent with the device too). */
        doe_dx_error error = {0};
        status = native_from_d3d12(doe_dx_context_prepare_descriptors(result.value.d3d12.context, &error));
        if (status != NATIVE_OK) {
            native_d3d12_close(&result);
            return status;
        }
        /* Load FXC with the device, not the first compute pipeline. A load
         * failure is not a device failure: the first compile retries the same
         * explicit module and reports that failure; nothing is substituted. */
        (void)native_d3d12_retain_compute_compiler(&result);
        *out = result;
        return NATIVE_OK;
    }
    native_d3d12_close(&result);
    return NATIVE_UNAVAILABLE;
#else
    (void)adapter;
    (void)out;
    return NATIVE_UNAVAILABLE;
#endif
}

void native_d3d12_close(native_backend *backend)
{
#if defined(_WIN32)
    /* Existing resource and pending device leases prevent this final close
     * until every private allocation/command is terminal and released. */
    if (backend->value.d3d12.context &&
        doe_dx_context_destroy(backend->value.d3d12.context) != DOE_DX_OK) abort();
    /* Every shader and pipeline compiled through these modules is gone. */
    for (size_t i = 0; i < 2; ++i)
        doe_dx_shader_compiler_release(atomic_exchange(&backend->value.d3d12.compilers[i], NULL));
#endif
    backend->value.d3d12.context = NULL;
}

native_result native_d3d12_status(const native_backend *backend)
{
#if defined(_WIN32)
    return native_from_d3d12(doe_dx_context_status(backend->value.d3d12.context));
#else
    (void)backend;
    return NATIVE_UNAVAILABLE;
#endif
}
