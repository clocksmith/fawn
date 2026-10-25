/* SPDX-License-Identifier: MIT */
#include "internal.h"
#include "../include/doe_d3d12_cache.h"
#ifdef _WIN32
#include "windows_cache.h"
#endif

doe_dx_status doe_dx_context_pipeline_cache(doe_dx_context *context, const char *directory,
    size_t length, doe_dx_error *error)
{
    if (!context || (!directory && length) || (directory && (!length || length > 16384 || memchr(directory,0,length))))
        return doe_dx_fail(DOE_DX_INVALID_ARGUMENT,0,"pipeline cache arguments",error);
    if (context->lost) return doe_dx_fail(DOE_DX_DEVICE_LOST,0,"pipeline cache device",error);
#ifdef _WIN32
    doe_dx_error detail={0};
    doe_dx_status status=doe_dx_native_cache_configure(context->native,&context->caps,directory,length,&detail);
    if (error) *error=detail;
    return doe_dx_context_result(context,status,&detail);
#else
    return doe_dx_fail(directory ? DOE_DX_UNSUPPORTED : DOE_DX_OK,0,"pipeline cache platform",error);
#endif
}
doe_dx_status doe_dx_context_pipeline_cache_flush(doe_dx_context *context, doe_dx_error *error)
{
    if (!context) return doe_dx_fail(DOE_DX_INVALID_ARGUMENT,0,"pipeline cache context",error);
    if (context->lost) return doe_dx_fail(DOE_DX_DEVICE_LOST,0,"pipeline cache device",error);
#ifdef _WIN32
    doe_dx_error detail={0};
    doe_dx_status status=doe_dx_native_cache_flush(context->native,&detail);
    if (error) *error=detail;
    return doe_dx_context_result(context,status,&detail);
#else
    return doe_dx_fail(DOE_DX_UNSUPPORTED,0,"pipeline cache platform",error);
#endif
}
doe_dx_status doe_dx_context_pipeline_cache_stats(const doe_dx_context *context, doe_dx_pipeline_cache_stats *out)
{
    if (!context || !out) return DOE_DX_INVALID_ARGUMENT;
#ifdef _WIN32
    doe_dx_native_cache_stats(context->native,out);
    return DOE_DX_OK;
#else
    *out = (doe_dx_pipeline_cache_stats){0};
    return DOE_DX_OK;
#endif
}
