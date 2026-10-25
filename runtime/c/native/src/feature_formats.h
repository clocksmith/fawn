/* SPDX-License-Identifier: MIT */
#ifndef DOE_NATIVE_FEATURE_FORMATS_H
#define DOE_NATIVE_FEATURE_FORMATS_H
#include "internal.h"
uint32_t native_format_adapter_features(const native_backend *);
uint32_t native_feature_dependencies(uint32_t);
bool native_limits_support_core(const WGPULimits *);
bool native_d3d12_core_supported(const native_backend *);
#endif
