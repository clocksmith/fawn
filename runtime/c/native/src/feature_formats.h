/* SPDX-License-Identifier: MIT */
#ifndef DOE_NATIVE_FEATURE_FORMATS_H
#define DOE_NATIVE_FEATURE_FORMATS_H
#include "internal.h"
/* Query each immutable Vulkan format once during one adapter's discovery.
 * Native format identities are bounded by the backend's final format enum. */
enum { NATIVE_FORMAT_QUERY_COUNT = DOE_VK_FORMAT_ASTC_12x12_SRGB + 1 };
typedef struct native_format_queries {
    doe_vk_context *context;
    unsigned char state[NATIVE_FORMAT_QUERY_COUNT]; /* 0=unqueried, 1=offered, 2=failed. */
    doe_vk_format_caps formats[NATIVE_FORMAT_QUERY_COUNT];
} native_format_queries;
bool native_query_format(native_format_queries *, doe_vk_texture_format, doe_vk_format_caps *);
uint32_t native_format_adapter_features(const native_backend *, native_format_queries *);
uint32_t native_feature_dependencies(uint32_t);
bool native_limits_support_core(const WGPULimits *);
bool native_d3d12_core_supported(const native_backend *);
#endif
