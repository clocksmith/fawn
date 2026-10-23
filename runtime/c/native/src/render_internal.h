/* SPDX-License-Identifier: MIT */
#ifndef DOE_NATIVE_RENDER_INTERNAL_H
#define DOE_NATIVE_RENDER_INTERNAL_H
#include "internal.h"
bool native_vertex_format(WGPUVertexFormat format, doe_vk_vertex_format *native,
    uint32_t *scalar, uint32_t *bytes);
bool native_color_scalar(WGPUTextureFormat format, uint32_t *scalar);
bool native_render_interfaces(const native_stage stages[2], const WGPUVertexState *vertex,
    const WGPUColorTargetState *color, const doe_vk_caps *caps);
bool native_render_depth(const WGPUDepthStencilState *source, doe_vk_render_pipeline_desc *target,
    doe_resource_render_pipeline_desc *resource);
bool native_render_fixed(const WGPURenderPipelineDescriptor *source, doe_vk_render_pipeline_desc *target);
bool native_render_clear(WGPUTextureFormat format, const double values[4], doe_vk_clear_color *out);
#endif
