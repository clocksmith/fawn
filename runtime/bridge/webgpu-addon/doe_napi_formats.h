/*
 * doe_napi_formats.h — WebGPU name/value converters (doe_napi_formats.c and the
 * pipeline/binding tables). Values are the pinned webgpu.h values; see AGENTS.md.
 */
#ifndef DOE_NAPI_FORMATS_H
#define DOE_NAPI_FORMATS_H
#include <node_api.h>
#include <stdbool.h>
#include <stdint.h>

/* True when the loaded provider is the Zig runtime, whose ABI numbers some
 * texture formats and stencil operations differently (doe_napi_formats.c). */
extern bool g_zig_enum_order;

uint32_t texture_format_from_string(napi_env env, napi_value val);
uint32_t primitive_topology_from_string(napi_env env, napi_value val);
uint32_t front_face_from_string(napi_env env, napi_value val);
uint32_t cull_mode_from_string(napi_env env, napi_value val);
uint32_t buffer_binding_type_from_string(napi_env env, napi_value val);
uint32_t sampler_binding_type_from_string(napi_env env, napi_value val);
uint32_t texture_sample_type_from_string(napi_env env, napi_value val);
uint32_t texture_view_dimension_from_string(napi_env env, napi_value val);
uint32_t storage_texture_access_from_string(napi_env env, napi_value val);
uint32_t filter_mode_from_string(napi_env env, napi_value val);
uint32_t address_mode_from_string(napi_env env, napi_value val);
uint32_t compare_func_from_value(napi_env env, napi_value val);
uint32_t vertex_step_mode_from_value(napi_env env, napi_value val);
uint32_t vertex_format_from_value(napi_env env, napi_value val);
uint32_t index_format_from_value(napi_env env, napi_value val);
uint32_t blend_factor_from_string(napi_env env, napi_value val);
uint32_t blend_operation_from_string(napi_env env, napi_value val);
const char* texture_format_u32_to_string(uint32_t fmt);
uint32_t texture_aspect_from_value(napi_env env, napi_value val);

#endif
