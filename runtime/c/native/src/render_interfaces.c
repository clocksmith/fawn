/* SPDX-License-Identifier: MIT */
#include "render_internal.h"

static const DoeCompilerStageInterfaceV7 *location(const native_stage *stage, uint32_t direction, uint32_t slot)
{
    size_t entry_index = (size_t)(stage->entry - stage->view.entries);
    for (size_t i = 0; i < stage->view.interface_count; ++i) {
        const DoeCompilerStageInterfaceV7 *item = &stage->view.interfaces[i];
        if (item->entry_index == entry_index && item->direction == direction &&
            !item->builtin && item->location == slot) return item;
    }
    return NULL;
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
    uint32_t scalar = 0, components = 0;
    if (!native_color_scalar(color->format, &scalar) ||
        doe_resource_format_color_components(color->format, &components) != DOE_RESOURCE_OK) return false;
    const DoeCompilerStageInterfaceV7 *written = fragment ? location(fragment, DOE_COMPILER_INTERFACE_OUTPUT, slot) : NULL;
    if (!written) return !color->writeMask;
    return written->scalar_kind == scalar && written->lane_count >= components &&
        (!source_alpha(color->blend) || written->lane_count == 4);
}

bool native_render_interfaces(const native_stage stages[2], size_t stage_count, const WGPUVertexState *vertex,
    const WGPUFragmentState *fragment, bool alpha_to_coverage, bool point_list,
    const native_graphics_limits *caps, const WGPULimits *limits)
{
    const native_stage *fragment_stage = stage_count == 2 ? &stages[1] : NULL;
    size_t targets = fragment ? fragment->targetCount : 0;
    for (size_t i = 0; i < targets; ++i)
        if (!target_output(fragment_stage, &fragment->targets[i], (uint32_t)i)) return false;
    const DoeCompilerStageInterfaceV7 *written = fragment_stage ?
        location(fragment_stage, DOE_COMPILER_INTERFACE_OUTPUT, 0) : NULL;
    if (alpha_to_coverage && (!written || written->scalar_kind != DOE_COMPILER_LEAF_F32 ||
        written->lane_count != 4)) return false;
    /* https://gpuweb.github.io/gpuweb/#abstract-opdef-validating-gpufragmentstate */
    for (size_t s = 0; s < stage_count; ++s) {
        const native_stage *stage = &stages[s];
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
                if (s == 1 && item->direction == DOE_COMPILER_INTERFACE_INPUT &&
                    item->builtin == DOE_COMPILER_BUILTIN_FRONT_FACING && variable_budget) --variable_budget;
                if (s == 1 && (item->builtin == DOE_COMPILER_BUILTIN_FRAG_DEPTH ||
                    item->builtin == DOE_COMPILER_BUILTIN_SAMPLE_INDEX || item->builtin == DOE_COMPILER_BUILTIN_SAMPLE_MASK))
                    return false; /* Fragment depth outputs and sample-rate builtins remain unsupported. */
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
                            scalar != item->scalar_kind) return false;
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
                if (s == 1) {
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
