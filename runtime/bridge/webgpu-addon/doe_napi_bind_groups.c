/* SPDX-License-Identifier: MIT
 * setBindGroup for compute passes, render passes and render bundle encoders.
 * A null bind group unbinds the slot; dynamic offsets arrive as an optional
 * Uint32Array the JS surface has already sliced to the caller's range. */
#include "doe_napi_internal.h"

/* Absent, undefined or null offsets mean none. Anything else must be a
 * Uint32Array; the provider validates the count against the layout. */
static bool dynamic_offsets(napi_env env, napi_value value, size_t* count, const uint32_t** data) {
    *count = 0;
    *data = NULL;
    napi_valuetype type = napi_undefined;
    if (!value || napi_typeof(env, value, &type) != napi_ok || type == napi_undefined || type == napi_null)
        return true;
    bool typed = false;
    napi_is_typedarray(env, value, &typed);
    if (!typed) return false;
    napi_typedarray_type element = napi_int8_array;
    size_t length = 0;
    void* bytes = NULL;
    if (napi_get_typedarray_info(env, value, &element, &length, &bytes, NULL, NULL) != napi_ok ||
        element != napi_uint32_array) return false;
    *count = length;
    *data = length ? (const uint32_t*)bytes : NULL;
    return true;
}

typedef struct {
    void* encoder;
    uint32_t index;
    void* group;
    size_t offset_count;
    const uint32_t* offsets;
} bind_group_call;

/* (encoder, index, bindGroup | null, offsets?) */
static bool read_bind_group_call(napi_env env, napi_callback_info info, const char* name, bind_group_call* call) {
    size_t argc = 4;
    napi_value args[4] = {NULL, NULL, NULL, NULL};
    char message[96];
    if (napi_get_cb_info(env, info, &argc, args, NULL, NULL) != napi_ok || argc < 3) {
        snprintf(message, sizeof(message), "%s requires an encoder, index and bind group", name);
        napi_throw_error(env, NULL, message);
        return false;
    }
    call->encoder = unwrap_ptr(env, args[0]);
    call->index = 0;
    napi_get_value_uint32(env, args[1], &call->index);
    call->group = unwrap_ptr(env, args[2]);
    if (!call->encoder) {
        snprintf(message, sizeof(message), "%s: invalid encoder", name);
        napi_throw_error(env, NULL, message);
        return false;
    }
    if (!dynamic_offsets(env, argc > 3 ? args[3] : NULL, &call->offset_count, &call->offsets)) {
        snprintf(message, sizeof(message), "%s: dynamic offsets must be a Uint32Array", name);
        napi_throw_type_error(env, NULL, message);
        return false;
    }
    return true;
}

napi_value doe_compute_pass_set_bind_group(napi_env env, napi_callback_info info) {
    bind_group_call call;
    if (!read_bind_group_call(env, info, "computePassSetBindGroup", &call)) return NULL;
    pfn_wgpuComputePassEncoderSetBindGroup(call.encoder, call.index, call.group, call.offset_count, call.offsets);
    return NULL;
}

napi_value doe_render_pass_set_bind_group(napi_env env, napi_callback_info info) {
    bind_group_call call;
    if (!read_bind_group_call(env, info, "renderPassSetBindGroup", &call)) return NULL;
    pfn_wgpuRenderPassEncoderSetBindGroup(call.encoder, call.index, call.group, call.offset_count, call.offsets);
    return NULL;
}

napi_value doe_render_bundle_encoder_set_bind_group(napi_env env, napi_callback_info info) {
    bind_group_call call;
    if (!read_bind_group_call(env, info, "renderBundleEncoderSetBindGroup", &call)) return NULL;
    if (!pfn_doeNativeRenderBundleEncoderSetBindGroup) NAPI_THROW(env, "renderBundleEncoderSetBindGroup not available");
    pfn_doeNativeRenderBundleEncoderSetBindGroup(call.encoder, call.index, call.group, call.offset_count, call.offsets);
    return NULL;
}

/* GPUComputePassEncoder#setBindGroup(index, bindGroup, dynamicOffsets?) on the
 * native-direct surface: offsets are a Uint32Array or omitted. */
napi_value native_direct_compute_pass_set_bind_group(napi_env env, napi_callback_info info) {
    size_t argc = 3;
    napi_value argv[3] = {NULL, NULL, NULL};
    napi_value this_arg;
    napi_get_cb_info(env, info, &argc, argv, &this_arg, NULL);
    if (argc < 2) NAPI_THROW(env, "setBindGroup requires an index and bind group");
    NativeDirectHandleCache* pass_cache = native_direct_get_handle_cache(env, this_arg);
    WGPUComputePassEncoder pass = pass_cache ? (WGPUComputePassEncoder)pass_cache->native : native_direct_unwrap_external_prop(env, this_arg, DOE_DIRECT_NATIVE);
    napi_valuetype group_type = napi_undefined;
    napi_typeof(env, argv[1], &group_type);
    WGPUBindGroup bind_group = NULL;
    if (group_type != napi_null && group_type != napi_undefined) {
        NativeDirectHandleCache* bind_group_cache = native_direct_get_handle_cache(env, argv[1]);
        bind_group = bind_group_cache ? (WGPUBindGroup)bind_group_cache->native : native_direct_unwrap_external_prop(env, argv[1], DOE_DIRECT_NATIVE);
    }
    uint32_t index = 0;
    napi_get_value_uint32(env, argv[0], &index);
    size_t offset_count = 0; const uint32_t* offsets = NULL;
    if (!dynamic_offsets(env, argc > 2 ? argv[2] : NULL, &offset_count, &offsets))
        NAPI_THROW(env, "setBindGroup: dynamic offsets must be a Uint32Array");
    pfn_wgpuComputePassEncoderSetBindGroup(pass, index, bind_group, offset_count, offsets);
    return NULL;
}
