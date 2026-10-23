/* SPDX-License-Identifier: MIT
 * Pinned ABI default/enum conversion. Public depth-state and writesStencil rules:
 * https://gpuweb.github.io/gpuweb/#depth-stencil-state
 * https://gpuweb.github.io/gpuweb/#dom-gpudevice-createrenderpipeline */
#include "render_internal.h"

static bool stencil(WGPUStencilOperation value, uint32_t *out)
{
    switch (value) {
    case WGPUStencilOperation_Undefined:
    case WGPUStencilOperation_Keep: *out = 0; return true;
    case WGPUStencilOperation_Zero: *out = 1; return true;
    case WGPUStencilOperation_Replace: *out = 2; return true;
    case WGPUStencilOperation_IncrementClamp: *out = 3; return true;
    case WGPUStencilOperation_DecrementClamp: *out = 4; return true;
    case WGPUStencilOperation_Invert: *out = 5; return true;
    case WGPUStencilOperation_IncrementWrap: *out = 6; return true;
    case WGPUStencilOperation_DecrementWrap: *out = 7; return true;
    default: return false;
    }
}
static bool face(const WGPUStencilFaceState *source, doe_vk_stencil_face *target)
{
    return native_compare_operation(source->compare, 7, &target->compare_operation) &&
        stencil(source->failOp, &target->fail_operation) &&
        stencil(source->depthFailOp, &target->depth_fail_operation) &&
        stencil(source->passOp, &target->pass_operation);
}
static bool writes(doe_vk_stencil_face face)
{
    return face.fail_operation || face.pass_operation || face.depth_fail_operation;
}

bool native_render_depth(uint32_t features, const WGPUDepthStencilState *source, doe_vk_render_pipeline_desc *target,
    doe_resource_render_pipeline_desc *resource)
{
    if (!source) return true;
    if (source->nextInChain || source->depthBias || source->depthBiasSlopeScale != 0 || source->depthBiasClamp != 0 ||
        !native_depth_format(features, source->format, &target->depth_stencil_format) ||
        !face(&source->stencilFront, &target->stencil_front) || !face(&source->stencilBack, &target->stencil_back) ||
        !native_compare_operation(source->depthCompare, 7, &target->depth_compare)) return false;
    WGPUTextureFormat aspect;
    bool depth = doe_resource_format_aspect_format(source->format, WGPUTextureAspect_DepthOnly, &aspect) == DOE_RESOURCE_OK;
    bool has_stencil = doe_resource_format_aspect_format(source->format, WGPUTextureAspect_StencilOnly, &aspect) == DOE_RESOURCE_OK;
    switch (source->depthWriteEnabled) {
    case WGPUOptionalBool_True: target->depth_write = true; break;
    case WGPUOptionalBool_False: target->depth_write = false; break;
    case WGPUOptionalBool_Undefined:
        if (depth) return false;
        break;
    default: return false;
    }
    if (!depth && (target->depth_write || target->depth_compare != 7)) return false;
    if (!has_stencil && (writes(target->stencil_front) || writes(target->stencil_back) ||
        target->stencil_front.compare_operation != 7 || target->stencil_back.compare_operation != 7)) return false;
    if (source->depthCompare == WGPUCompareFunction_Undefined && (target->depth_write ||
        target->stencil_front.depth_fail_operation || target->stencil_back.depth_fail_operation)) return false;
    target->depth_test = depth;
    target->stencil_test = has_stencil;
    target->stencil_read_mask = source->stencilReadMask;
    target->stencil_write_mask = source->stencilWriteMask;
    resource->depth_stencil_format = source->format;
    resource->depth_write = target->depth_write;
    resource->stencil_write = has_stencil && source->stencilWriteMask &&
        ((!(target->cull_mask & 1) && writes(target->stencil_front)) ||
         (!(target->cull_mask & 2) && writes(target->stencil_back)));
    return true;
}
