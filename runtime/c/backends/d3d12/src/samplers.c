/* SPDX-License-Identifier: MIT */
#include "internal.h"
#include <math.h>

doe_dx_status doe_dx_sampler_validate(const doe_dx_sampler_desc *sampler)
{
    if (!sampler || sampler->min_filter < DOE_DX_NEAREST || sampler->min_filter > DOE_DX_LINEAR ||
        sampler->mag_filter < DOE_DX_NEAREST || sampler->mag_filter > DOE_DX_LINEAR ||
        sampler->mip_filter < DOE_DX_NEAREST || sampler->mip_filter > DOE_DX_LINEAR ||
        sampler->address_u < DOE_DX_CLAMP || sampler->address_u > DOE_DX_MIRROR ||
        sampler->address_v < DOE_DX_CLAMP || sampler->address_v > DOE_DX_MIRROR ||
        sampler->address_w < DOE_DX_CLAMP || sampler->address_w > DOE_DX_MIRROR ||
        !isfinite(sampler->min_lod) || !isfinite(sampler->max_lod) || sampler->min_lod < 0 ||
        sampler->max_lod < sampler->min_lod || !sampler->max_anisotropy || sampler->max_anisotropy > 16 ||
        sampler->compare < DOE_DX_SAMPLER_NO_COMPARE || sampler->compare > DOE_DX_SAMPLER_ALWAYS)
        return DOE_DX_INVALID_ARGUMENT;
    if (sampler->max_anisotropy > 1 && (sampler->min_filter != DOE_DX_LINEAR ||
        sampler->mag_filter != DOE_DX_LINEAR || sampler->mip_filter != DOE_DX_LINEAR)) return DOE_DX_INVALID_ARGUMENT;
    return DOE_DX_OK;
}
