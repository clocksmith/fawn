/* SPDX-License-Identifier: MIT */
#include "render_internal.h"

static bool factor(WGPUBlendFactor value, uint32_t fallback, uint32_t *out)
{
    switch (value) {
    case WGPUBlendFactor_Undefined: *out = fallback; return true;
    case WGPUBlendFactor_Zero: *out = 0; return true;
    case WGPUBlendFactor_One: *out = 1; return true;
    case WGPUBlendFactor_Src: *out = 2; return true;
    case WGPUBlendFactor_OneMinusSrc: *out = 3; return true;
    case WGPUBlendFactor_Dst: *out = 4; return true;
    case WGPUBlendFactor_OneMinusDst: *out = 5; return true;
    case WGPUBlendFactor_SrcAlpha: *out = 6; return true;
    case WGPUBlendFactor_OneMinusSrcAlpha: *out = 7; return true;
    case WGPUBlendFactor_DstAlpha: *out = 8; return true;
    case WGPUBlendFactor_OneMinusDstAlpha: *out = 9; return true;
    case WGPUBlendFactor_Constant: *out = 10; return true;
    case WGPUBlendFactor_OneMinusConstant: *out = 11; return true;
    case WGPUBlendFactor_SrcAlphaSaturated: *out = 14; return true;
    default: return false; /* Dual-source is not enabled. */
    }
}
static bool blend(const WGPUBlendComponent *source, uint32_t *operation, uint32_t *from, uint32_t *to)
{
    switch (source->operation) {
    case WGPUBlendOperation_Undefined:
    case WGPUBlendOperation_Add: *operation = 0; break;
    case WGPUBlendOperation_Subtract: *operation = 1; break;
    case WGPUBlendOperation_ReverseSubtract: *operation = 2; break;
    case WGPUBlendOperation_Min: *operation = 3; break;
    case WGPUBlendOperation_Max: *operation = 4; break;
    default: return false;
    }
    return factor(source->srcFactor, 1, from) && factor(source->dstFactor, 0, to) &&
        (*operation < 3 || (*from == 1 && *to == 1));
}

bool native_render_fixed(uint32_t features, const WGPURenderPipelineDescriptor *source, doe_vk_render_pipeline_desc *target)
{
    /* stripIndexFormat admission (strip-only, Uint16/Uint32) belongs to the
     * resource pipeline owner; native strips always enable primitive restart. */
    /* unclippedDepth needs depth-clip-control: native depth clamping replaces clipping. */
    if (source->primitive.nextInChain ||
        (source->primitive.unclippedDepth && !(features & NATIVE_FEATURE(WGPUFeatureName_DepthClipControl))) ||
        source->multisample.nextInChain || (source->multisample.count != 1 && source->multisample.count != 4) ||
        (source->multisample.alphaToCoverageEnabled && source->multisample.count == 1)) return false;
    target->unclipped_depth = source->primitive.unclippedDepth != 0;
    target->sample_count = source->multisample.count;
    target->override_sample_mask = true;
    target->sample_mask = source->multisample.mask;
    target->alpha_to_coverage = source->multisample.alphaToCoverageEnabled;
    switch (source->primitive.topology) {
    case WGPUPrimitiveTopology_Undefined:
    case WGPUPrimitiveTopology_TriangleList: target->topology = DOE_VK_TRIANGLES; break;
    case WGPUPrimitiveTopology_TriangleStrip: target->topology = DOE_VK_TRIANGLE_STRIP; break;
    case WGPUPrimitiveTopology_LineList: target->topology = DOE_VK_LINES; break;
    case WGPUPrimitiveTopology_LineStrip: target->topology = DOE_VK_LINE_STRIP; break;
    /* The Vulkan owner admits points only with enabled maintenance5 (default
     * PointSize 1.0); shaders are never rewritten to supply a size. */
    case WGPUPrimitiveTopology_PointList: target->topology = DOE_VK_POINTS; break;
    default: return false;
    }
    switch (source->primitive.cullMode) {
    case WGPUCullMode_Undefined:
    case WGPUCullMode_None: target->cull_mask = 0; break;
    case WGPUCullMode_Front: target->cull_mask = 1; break;
    case WGPUCullMode_Back: target->cull_mask = 2; break;
    default: return false;
    }
    switch (source->primitive.frontFace) {
    case WGPUFrontFace_Undefined:
    case WGPUFrontFace_CCW: target->clockwise_front = false; break;
    case WGPUFrontFace_CW: target->clockwise_front = true; break;
    default: return false;
    }
    const WGPUFragmentState *fragment = source->fragment;
    size_t count = fragment ? fragment->targetCount : 0;
    if (count > DOE_VK_MAX_COLOR_ATTACHMENTS) return false;
    target->color_target_count = (uint32_t)count;
    /* Alpha-to-coverage reads target 0's alpha; a missing slot 0 cannot supply it. */
    if (source->multisample.alphaToCoverageEnabled &&
        (!count || fragment->targets[0].format == WGPUTextureFormat_Undefined)) return false;
    for (size_t i = 0; i < count; ++i) {
        const WGPUColorTargetState *color = &fragment->targets[i];
        doe_vk_color_target *slot = &target->color_targets[i];
        *slot = (doe_vk_color_target){0};
        if (color->nextInChain) return false;
        if (color->format == WGPUTextureFormat_Undefined) {
            /* An unused slot carries no blend; its write mask is not consumed. */
            if (color->blend) return false;
            continue;
        }
        if (color->writeMask & ~WGPUColorWriteMask_All || !native_texture_format(features, color->format, &slot->format) ||
            !native_color_renderable(features, color->format) ||
            native_depth_format(features, color->format, &slot->format)) return false;
        /* 32-bit float targets are blendable only with float32-blendable; the
         * backend separately requires the native blend format feature. */
        if (color->blend && !(features & NATIVE_FEATURE(WGPUFeatureName_Float32Blendable)) &&
            (color->format == WGPUTextureFormat_R32Float || color->format == WGPUTextureFormat_RG32Float ||
            color->format == WGPUTextureFormat_RGBA32Float)) return false;
        slot->write_mask = (uint32_t)color->writeMask;
        slot->blend_enabled = color->blend != NULL;
        if (color->blend && (!blend(&color->blend->color, &slot->color_operation,
                &slot->color_source_factor, &slot->color_destination_factor) ||
            !blend(&color->blend->alpha, &slot->alpha_operation,
                &slot->alpha_source_factor, &slot->alpha_destination_factor))) return false;
    }
    return true;
}
