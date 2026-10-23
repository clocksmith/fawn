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

bool native_render_interfaces(const native_stage stages[2], const WGPUVertexState *vertex,
    const WGPUColorTargetState *color, const doe_vk_caps *caps)
{
    uint32_t color_scalar, color_components;
    if (!native_color_scalar(color->format, &color_scalar) ||
        doe_resource_format_color_components(color->format, &color_components) != DOE_RESOURCE_OK) return false;
    const DoeCompilerStageInterfaceV7 *written = location(&stages[1], DOE_COMPILER_INTERFACE_OUTPUT, 0);
    if (color->writeMask && !written) return false;
    bool source_alpha = false;
    if (color->blend) {
        WGPUBlendFactor from = color->blend->color.srcFactor;
        WGPUBlendFactor to = color->blend->color.dstFactor;
        source_alpha = from == WGPUBlendFactor_SrcAlpha || from == WGPUBlendFactor_OneMinusSrcAlpha ||
            from == WGPUBlendFactor_SrcAlphaSaturated || to == WGPUBlendFactor_SrcAlpha ||
            to == WGPUBlendFactor_OneMinusSrcAlpha || to == WGPUBlendFactor_SrcAlphaSaturated;
    }
    /* https://gpuweb.github.io/gpuweb/#abstract-opdef-validating-gpufragmentstate */
    for (size_t s = 0; s < 2; ++s) {
        const native_stage *stage = &stages[s];
        size_t entry_index = (size_t)(stage->entry - stage->view.entries);
        uint32_t components = 0;
        for (size_t i = 0; i < stage->view.interface_count; ++i) {
            const DoeCompilerStageInterfaceV7 *item = &stage->view.interfaces[i];
            if (item->entry_index != entry_index) continue;
            if (item->builtin) {
                if (s == 1 && (item->builtin == DOE_COMPILER_BUILTIN_FRAG_DEPTH ||
                    item->builtin == DOE_COMPILER_BUILTIN_SAMPLE_INDEX || item->builtin == DOE_COMPILER_BUILTIN_SAMPLE_MASK))
                    return false; /* Depth/MSAA/sample-rate semantics are not enabled. */
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
                if (item->location != 0) continue; /* Untargeted outputs are ignored. */
                if (item->scalar_kind != color_scalar || item->lane_count < color_components ||
                    (source_alpha && item->lane_count != 4)) return false;
            } else {
                uint32_t max_components = caps->max_vertex_output_components < caps->max_fragment_input_components ?
                    caps->max_vertex_output_components : caps->max_fragment_input_components;
                if (item->location >= max_components / 4) return false;
                if (components > UINT32_MAX - item->lane_count) return false;
                components += item->lane_count;
                if (s == 1) {
                    const DoeCompilerStageInterfaceV7 *output = location(&stages[0], DOE_COMPILER_INTERFACE_OUTPUT, item->location);
                    if (!output || output->scalar_kind != item->scalar_kind || output->lane_count != item->lane_count ||
                        output->interpolation != item->interpolation || output->sampling != item->sampling) return false;
                }
            }
        }
        if (components > (s ? caps->max_fragment_input_components : caps->max_vertex_output_components)) return false;
    }
    return true;
}
