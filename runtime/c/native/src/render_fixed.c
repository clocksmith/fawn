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

bool native_render_fixed(const WGPURenderPipelineDescriptor *source, doe_vk_render_pipeline_desc *target)
{
    /* stripIndexFormat admission (strip-only, Uint16/Uint32) belongs to the
     * resource pipeline owner; native strips always enable primitive restart. */
    if (source->primitive.nextInChain || source->primitive.unclippedDepth ||
        source->multisample.nextInChain || (source->multisample.count != 1 && source->multisample.count != 4) ||
        (source->multisample.alphaToCoverageEnabled && source->multisample.count == 1)) return false;
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
    default: return false; /* Point-size lowering is not yet admitted. */
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
    const WGPUColorTargetState *color = source->fragment && source->fragment->targetCount ? source->fragment->targets : NULL;
    if (!color || color->format == WGPUTextureFormat_Undefined)
        return !source->multisample.alphaToCoverageEnabled;
    if (color->nextInChain || color->writeMask & ~WGPUColorWriteMask_All ||
        !native_texture_format(color->format, &target->color_format)) return false;
    target->color_write_mask = (uint32_t)color->writeMask;
    target->blend_enabled = color->blend != NULL;
    if (!color->blend) return true;
    return blend(&color->blend->color, &target->color_operation,
        &target->color_source_factor, &target->color_destination_factor) &&
        blend(&color->blend->alpha, &target->alpha_operation,
        &target->alpha_source_factor, &target->alpha_destination_factor);
}
