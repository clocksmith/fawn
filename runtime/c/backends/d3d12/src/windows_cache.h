/* SPDX-License-Identifier: MIT */
#ifndef DOE_DX_WINDOWS_CACHE_H
#define DOE_DX_WINDOWS_CACHE_H
#include "windows_internal.h"
#include "../include/doe_d3d12_cache.h"
#ifdef _WIN32
/* Caller context serialization also protects same-name pipeline-library loads. */
void doe_dx_cache_adapter(doe_dx_native_context *, IDXGIAdapter1 *);
void doe_dx_cache_close(doe_dx_native_context *);
void doe_dx_cache_root_key(const void *, size_t, uint8_t [32]);
doe_dx_status doe_dx_cached_compute(doe_dx_native_context *, const D3D12_COMPUTE_PIPELINE_STATE_DESC *,
    const uint8_t [32], ID3D12PipelineState **, doe_dx_error *);
doe_dx_status doe_dx_cached_graphics(doe_dx_native_context *, const D3D12_GRAPHICS_PIPELINE_STATE_DESC *,
    const uint8_t [32], ID3D12PipelineState **, doe_dx_error *);
doe_dx_status doe_dx_native_cache_configure(doe_dx_native_context *,const doe_dx_caps *,const char *,size_t,doe_dx_error *);
doe_dx_status doe_dx_native_cache_flush(doe_dx_native_context *,doe_dx_error *);
void doe_dx_native_cache_stats(const doe_dx_native_context *,doe_dx_pipeline_cache_stats *);
bool doe_dx_cache_compute_key(const D3D12_COMPUTE_PIPELINE_STATE_DESC *,const uint8_t [32],WCHAR [65]);
bool doe_dx_cache_graphics_key(const D3D12_GRAPHICS_PIPELINE_STATE_DESC *,const uint8_t [32],WCHAR [65]);
#endif
#endif
