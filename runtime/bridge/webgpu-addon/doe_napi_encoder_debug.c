/* SPDX-License-Identifier: MIT
 * GPUCommandEncoder pushDebugGroup, popDebugGroup and insertDebugMarker through
 * the provider's standard procedures, which validate group balance. */
#include "doe_napi_internal.h"

static WGPUCommandEncoder encoder_argument(napi_env env, napi_value value, const char* name) {
    WGPUCommandEncoder encoder = unwrap_ptr(env, value);
    if (!encoder) {
        char message[96];
        snprintf(message, sizeof(message), "%s: invalid command encoder", name);
        napi_throw_error(env, NULL, message);
    }
    return encoder;
}

/* (encoder, label) -> procedure(encoder, label view) */
static napi_value with_label(napi_env env, napi_callback_info info, const char* name,
    void (*procedure)(WGPUCommandEncoder, WGPUStringView)) {
    NAPI_ASSERT_ARGC(env, info, 2);
    WGPUCommandEncoder encoder = encoder_argument(env, _args[0], name);
    if (!encoder) return NULL;
    if (!procedure) NAPI_THROW(env, "command encoder debug markers are not available in the loaded library");
    size_t length = 0;
    if (napi_get_value_string_utf8(env, _args[1], NULL, 0, &length) != napi_ok) NAPI_THROW(env, "debug label must be a string");
    char* label = (char*)malloc(length + 1);
    if (!label) NAPI_THROW(env, "debug label: out of memory");
    napi_get_value_string_utf8(env, _args[1], label, length + 1, &length);
    procedure(encoder, (WGPUStringView){label, length});
    free(label);
    return NULL;
}

napi_value doe_command_encoder_push_debug_group(napi_env env, napi_callback_info info) {
    return with_label(env, info, "commandEncoderPushDebugGroup", pfn_wgpuCommandEncoderPushDebugGroup);
}

napi_value doe_command_encoder_insert_debug_marker(napi_env env, napi_callback_info info) {
    return with_label(env, info, "commandEncoderInsertDebugMarker", pfn_wgpuCommandEncoderInsertDebugMarker);
}

napi_value doe_command_encoder_pop_debug_group(napi_env env, napi_callback_info info) {
    NAPI_ASSERT_ARGC(env, info, 1);
    WGPUCommandEncoder encoder = encoder_argument(env, _args[0], "commandEncoderPopDebugGroup");
    if (!encoder) return NULL;
    if (!pfn_wgpuCommandEncoderPopDebugGroup) NAPI_THROW(env, "command encoder debug markers are not available in the loaded library");
    pfn_wgpuCommandEncoderPopDebugGroup(encoder);
    return NULL;
}
