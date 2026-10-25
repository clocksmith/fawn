/* SPDX-License-Identifier: MIT */
#include "internal.h"
#include "doe_metal_cache.h"

doe_mtl_status doe_mtl_context_pipeline_cache(doe_mtl_context *context,
    const char *directory, size_t length, doe_mtl_error *error) {
    if (!context || (!directory && length) || (directory && (!length || memchr(directory, 0, length))))
        return doe_mtl_fail(DOE_MTL_INVALID_ARGUMENT, "pipeline cache directory", error);
    if (!context->native.pipeline_cache)
        return doe_mtl_fail(directory ? DOE_MTL_UNSUPPORTED : DOE_MTL_OK, "pipeline cache capability", error);
    return context->native.pipeline_cache(context->native.userdata, context->device, directory, length, error);
}
doe_mtl_status doe_mtl_context_pipeline_cache_flush(doe_mtl_context *context, doe_mtl_error *error) {
    if (!context) return doe_mtl_fail(DOE_MTL_INVALID_ARGUMENT, "pipeline cache flush", error);
    return context->native.pipeline_cache_flush ? context->native.pipeline_cache_flush(context->native.userdata, error) :
        doe_mtl_fail(DOE_MTL_OK, "pipeline cache disabled", error);
}
doe_mtl_status doe_mtl_context_pipeline_cache_stats(const doe_mtl_context *context, doe_mtl_pipeline_cache_stats *out) {
    if (!context || !out) return DOE_MTL_INVALID_ARGUMENT;
    if (context->native.pipeline_cache_stats) context->native.pipeline_cache_stats(context->native.userdata, out);
    else *out = (doe_mtl_pipeline_cache_stats){0};
    return DOE_MTL_OK;
}
