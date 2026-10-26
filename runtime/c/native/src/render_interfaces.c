/* SPDX-License-Identifier: MIT */
#include "render_internal.h"

static const DoeCompilerStageInterfaceV7 *location(const native_stage *stage, uint32_t direction, uint32_t slot)
{
    size_t entry_index = (size_t)(stage->entry - stage->view.entries);
    for (size_t i = 0; i < stage->view.interface_count; ++i) {
        const DoeCompilerStageInterfaceV7 *item = &stage->view.interfaces[i];
        if (item->entry_index == entry_index && item->direction == direction &&
            !item->builtin && item->location == slot && !(item->flags & DOE_COMPILER_INTERFACE_BLEND_SRC_ONE)) return item;
    }
    return NULL;
}

/* Native stage carriers are f32 for both WGSL floating scalar widths. Keep
 * canonical reflection unchanged so interstage f16/f32 mismatches still fail. */
static bool scalar_compatible(uint32_t format_scalar, uint32_t shader_scalar)
{
    return format_scalar == shader_scalar ||
        (format_scalar == DOE_COMPILER_LEAF_F32 && shader_scalar == DOE_COMPILER_LEAF_F16);
}

static bool source_alpha(const WGPUBlendState *blend)
{
    if (!blend) return false;
    WGPUBlendFactor from = blend->color.srcFactor;
    WGPUBlendFactor to = blend->color.dstFactor;
    return from == WGPUBlendFactor_SrcAlpha || from == WGPUBlendFactor_OneMinusSrcAlpha ||
        from == WGPUBlendFactor_SrcAlphaSaturated || to == WGPUBlendFactor_SrcAlpha ||
        to == WGPUBlendFactor_OneMinusSrcAlpha || to == WGPUBlendFactor_SrcAlphaSaturated;
}

/* Each used target with a nonzero write mask needs the fragment output at its
 * slot index with the format's scalar kind and enough components. */
static bool target_output(const native_stage *fragment, const WGPUColorTargetState *color, uint32_t slot)
{
    if (color->format == WGPUTextureFormat_Undefined) return true;
    /* Raw native source carries no WGSL interface proof. Format/state admission
     * remains in the shared render owner; native PSO linking owns shader IO. */
    if (fragment && fragment->imported) return true;
    uint32_t scalar = 0, components = 0;
    if (!native_color_scalar(color->format, &scalar) ||
        doe_resource_format_color_components(color->format, &components) != DOE_RESOURCE_OK) return false;
    const DoeCompilerStageInterfaceV7 *written = fragment ? location(fragment, DOE_COMPILER_INTERFACE_OUTPUT, slot) : NULL;
    if (!written) return !color->writeMask;
    return scalar_compatible(scalar, written->scalar_kind) && written->lane_count >= components &&
        (!source_alpha(color->blend) || written->lane_count == 4);
}

/* Shared interface matching traverses caller-owned vertex spans before some
 * backends lower layouts. Bound every span first, independent of shader IO. */
static bool vertex_spans(const WGPUVertexState *vertex, const WGPULimits *limits)
{
    if (!vertex || !limits || vertex->bufferCount > DOE_RESOURCE_MAX_VERTEX_BUFFERS ||
        vertex->bufferCount > limits->maxVertexBuffers || (vertex->bufferCount && !vertex->buffers)) return false;
    size_t attributes = 0;
    for (size_t slot = 0; slot < vertex->bufferCount; ++slot) {
        const WGPUVertexBufferLayout *buffer = &vertex->buffers[slot];
        if (buffer->attributeCount > limits->maxVertexAttributes - attributes ||
            (buffer->attributeCount && !buffer->attributes)) return false;
        attributes += buffer->attributeCount;
    }
    return true;
}

bool native_render_interfaces(const native_stage stages[2], size_t stage_count, const WGPUVertexState *vertex,
    const WGPUFragmentState *fragment, bool alpha_to_coverage, bool point_list,
    const native_graphics_limits *caps, const WGPULimits *limits)
{
    if (!vertex_spans(vertex, limits)) return false;
    const native_stage *fragment_stage = stage_count == 2 ? &stages[1] : NULL;
    size_t targets = fragment ? fragment->targetCount : 0;
    for (size_t i = 0; i < targets; ++i)
        if (!target_output(fragment_stage, &fragment->targets[i], (uint32_t)i)) return false;
    const DoeCompilerStageInterfaceV7 *written = fragment_stage ?
        location(fragment_stage, DOE_COMPILER_INTERFACE_OUTPUT, 0) : NULL;
    const DoeCompilerStageInterfaceV7 *secondary=NULL;
    if (fragment_stage && !fragment_stage->imported) for (size_t i=0;i<fragment_stage->view.interface_count;++i) {
        const DoeCompilerStageInterfaceV7 *item=&fragment_stage->view.interfaces[i];
        if (item->entry_index==(uint32_t)(fragment_stage->entry-fragment_stage->view.entries) &&
            item->direction==DOE_COMPILER_INTERFACE_OUTPUT && (item->flags & DOE_COMPILER_INTERFACE_BLEND_SRC_ONE)) secondary=item;
    }
    if (secondary && (secondary->location!=0 || !written ||
        secondary->scalar_kind!=written->scalar_kind || secondary->lane_count!=written->lane_count)) return false;
    for (size_t i=0; (!fragment_stage || !fragment_stage->imported) && i<targets; ++i) {
        const WGPUBlendState *blend=fragment->targets[i].blend;
        if (!blend) continue;
        WGPUBlendFactor factors[4]={blend->color.srcFactor,blend->color.dstFactor,blend->alpha.srcFactor,blend->alpha.dstFactor};
        for (size_t j=0;j<4;++j) {
            bool source1=factors[j]==WGPUBlendFactor_Src1 || factors[j]==WGPUBlendFactor_OneMinusSrc1 ||
                factors[j]==WGPUBlendFactor_Src1Alpha || factors[j]==WGPUBlendFactor_OneMinusSrc1Alpha;
            if (source1 && (!secondary || i!=0)) return false;
            if (j<2 && (factors[j]==WGPUBlendFactor_Src1Alpha || factors[j]==WGPUBlendFactor_OneMinusSrc1Alpha) && secondary->lane_count!=4) return false;
        }
    }
    if (alpha_to_coverage && (!fragment_stage || !fragment_stage->imported) && (!written || !scalar_compatible(DOE_COMPILER_LEAF_F32, written->scalar_kind) ||
        written->lane_count != 4)) return false;
    /* https://gpuweb.github.io/gpuweb/#abstract-opdef-validating-gpufragmentstate */
    for (size_t s = 0; s < stage_count; ++s) {
        const native_stage *stage = &stages[s];
        if (stage->imported) continue; /* No fabricated native-source interface rows. */
        size_t entry_index = (size_t)(stage->entry - stage->view.entries);
        uint32_t components = 0;
        /* https://gpuweb.github.io/gpuweb/#abstract-opdef-validating-inter-stage-interfaces:
         * user-defined inter-stage variables, less one slot for point-list
         * vertex output and one per fragment front_facing input. */
        uint32_t variables = 0, variable_budget = limits->maxInterStageShaderVariables;
        if (s == 0 && point_list && variable_budget) --variable_budget;
        for (size_t i = 0; i < stage->view.interface_count; ++i) {
            const DoeCompilerStageInterfaceV7 *item = &stage->view.interfaces[i];
            if (item->entry_index != entry_index) continue;
            if (item->builtin) {
                if (item->builtin==DOE_COMPILER_BUILTIN_CLIP_DISTANCES) {
                    uint32_t slots=(item->lane_count+3u)/4u;
                    if (slots>variable_budget || item->lane_count>8 || !item->lane_count) return false;
                    variable_budget-=slots;
                    components+=item->lane_count;
                }
                const bool fragment_input = s == 1 && item->direction == DOE_COMPILER_INTERFACE_INPUT;
                const bool sample_builtin = item->builtin == DOE_COMPILER_BUILTIN_SAMPLE_INDEX ||
                    item->builtin == DOE_COMPILER_BUILTIN_SAMPLE_MASK;
                /* Each front_facing, sample_index and sample_mask input takes one slot. */
                if (fragment_input && (sample_builtin || item->builtin == DOE_COMPILER_BUILTIN_FRONT_FACING || item->builtin == DOE_COMPILER_BUILTIN_PRIMITIVE_INDEX)) {
                    if (!variable_budget) return false;
                    --variable_budget;
                }
                if (s == 1 && (sample_builtin || item->builtin == DOE_COMPILER_BUILTIN_FRAG_DEPTH) &&
                    !caps->fragment_builtins) return false;
                /* https://gpuweb.github.io/gpuweb/#abstract-opdef-validating-gpumultisamplestate */
                if (s == 1 && item->direction == DOE_COMPILER_INTERFACE_OUTPUT &&
                    item->builtin == DOE_COMPILER_BUILTIN_SAMPLE_MASK && alpha_to_coverage) return false;
                continue;
            }
            if (s == 0 && item->direction == DOE_COMPILER_INTERFACE_INPUT) {
                bool found = false;
                for (size_t slot = 0; slot < vertex->bufferCount; ++slot)
                    for (size_t at = 0; at < vertex->buffers[slot].attributeCount; ++at) {
                        const WGPUVertexAttribute *attribute = &vertex->buffers[slot].attributes[at];
                        if (attribute->shaderLocation != item->location) continue;
                        doe_vk_vertex_format native;
                        uint32_t scalar, bytes;
                        if (found || !native_vertex_format(attribute->format, &native, &scalar, &bytes) ||
                            !scalar_compatible(scalar, item->scalar_kind)) return false;
                        found = true; /* Component-count mismatch is legal vertex fetching. */
                    }
                if (!found) return false;
            } else if (s == 1 && item->direction == DOE_COMPILER_INTERFACE_OUTPUT) {
                continue; /* Targeted outputs are checked above; untargeted ones are ignored. */
            } else {
                if (item->location >= limits->maxInterStageShaderVariables) return false;
                ++variables;
                if (components > UINT32_MAX - item->lane_count) return false;
                components += item->lane_count;
                if (s == 1 && !stages[0].imported) {
                    const DoeCompilerStageInterfaceV7 *output = location(&stages[0], DOE_COMPILER_INTERFACE_OUTPUT, item->location);
                    if (!output || output->scalar_kind != item->scalar_kind || output->lane_count != item->lane_count ||
                        output->interpolation != item->interpolation || output->sampling != item->sampling) return false;
                }
            }
        }
        if (variables > variable_budget) return false;
        /* Native component capacity stays a backstop; the reported limit is
         * derived from it (limits.c), so admitted shaders fit. */
        if (components > (s ? caps->max_fragment_input_components : caps->max_vertex_output_components)) return false;
    }
    return true;
}

bool native_render_fragment_depth(const native_stage stages[2], size_t stage_count,
    const WGPUDepthStencilState *depth_stencil)
{
    if (stage_count != 2) return true;
    const native_stage *fragment = &stages[1];
    size_t entry_index = (size_t)(fragment->entry - fragment->view.entries);
    for (size_t i = 0; i < fragment->view.interface_count; ++i) {
        const DoeCompilerStageInterfaceV7 *item = &fragment->view.interfaces[i];
        if (item->entry_index != entry_index || item->direction != DOE_COMPILER_INTERFACE_OUTPUT ||
            item->builtin != DOE_COMPILER_BUILTIN_FRAG_DEPTH) continue;
        WGPUTextureFormat aspect;
        /* https://gpuweb.github.io/gpuweb/#abstract-opdef-validating-gpudepthstencilstate */
        return depth_stencil && doe_resource_format_aspect_format(depth_stencil->format,
            WGPUTextureAspect_DepthOnly, &aspect) == DOE_RESOURCE_OK;
    }
    return true;
}
