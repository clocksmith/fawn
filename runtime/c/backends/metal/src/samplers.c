/* SPDX-License-Identifier: MIT */
#include "texture_internal.h"
#include <math.h>

static bool address_valid(doe_mtl_address address) {
    return address == DOE_MTL_ADDRESS_CLAMP || address == DOE_MTL_ADDRESS_REPEAT || address == DOE_MTL_ADDRESS_MIRROR;
}

doe_mtl_status doe_mtl_sampler_create(doe_mtl_context *context,
    const doe_mtl_sampler_desc *desc, doe_mtl_sampler **out, doe_mtl_error *error) {
    doe_mtl_sampler *sampler;
    doe_mtl_status status;
    if (context == NULL || desc == NULL || out == NULL)
        return doe_mtl_fail(DOE_MTL_INVALID_ARGUMENT, "sampler create", error);
    if ((unsigned)desc->min_filter > DOE_MTL_FILTER_LINEAR ||
        (unsigned)desc->mag_filter > DOE_MTL_FILTER_LINEAR ||
        (unsigned)desc->mip_filter > DOE_MTL_FILTER_LINEAR ||
        !address_valid(desc->address_u) || !address_valid(desc->address_v) || !address_valid(desc->address_w) ||
        !isfinite(desc->min_lod) || !isfinite(desc->max_lod) || desc->min_lod < 0 ||
        desc->max_lod < desc->min_lod || desc->compare > 7 ||
        desc->max_anisotropy == 0 || desc->max_anisotropy > 16 ||
        (desc->max_anisotropy > 1 && (desc->min_filter != DOE_MTL_FILTER_LINEAR ||
         desc->mag_filter != DOE_MTL_FILTER_LINEAR || desc->mip_filter != DOE_MTL_FILTER_LINEAR)))
        return doe_mtl_fail(DOE_MTL_INVALID_ARGUMENT, "sampler descriptor", error);
    if (context->native.sampler == NULL)
        return doe_mtl_fail(DOE_MTL_UNSUPPORTED, "sampler native boundary", error);
    sampler = calloc(1, sizeof(*sampler));
    if (sampler == NULL) return doe_mtl_fail(DOE_MTL_OUT_OF_MEMORY, "sampler allocation", error);
    status = doe_mtl_context_retain(context);
    if (status != DOE_MTL_OK) {
        free(sampler);
        return doe_mtl_fail(status, "sampler context lease", error);
    }
    sampler->context = context;
    sampler->references = 1;
    status = context->native.sampler(context->native.userdata, context->device, desc, &sampler->native, error);
    if (status != DOE_MTL_OK) {
        doe_mtl_sampler_release(sampler);
        return status;
    }
    *out = sampler;
    return doe_mtl_fail(DOE_MTL_OK, "sampler create", error);
}

doe_mtl_status doe_mtl_sampler_retain(doe_mtl_sampler *sampler) {
    if (sampler == NULL) return DOE_MTL_INVALID_ARGUMENT;
    if (sampler->references == UINT32_MAX) return DOE_MTL_RANGE;
    sampler->references++;
    return DOE_MTL_OK;
}

void doe_mtl_sampler_release(doe_mtl_sampler *sampler) {
    doe_mtl_context *context;
    if (sampler == NULL || --sampler->references != 0) return;
    context = sampler->context;
    if (sampler->native != NULL) context->native.release(context->native.userdata, sampler->native);
    free(sampler);
    doe_mtl_context_release(context);
}

void *doe_mtl_sampler_native(const doe_mtl_sampler *sampler) {
    return sampler != NULL ? sampler->native : NULL;
}
