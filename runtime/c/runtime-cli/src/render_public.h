/* SPDX-License-Identifier: MIT */
#ifndef DOE_C_RENDER_PUBLIC_H
#define DOE_C_RENDER_PUBLIC_H
#include "runtime_internal.h"

enum { PUBLIC_RENDER_UNIFORM_BYTES = 16384, PUBLIC_RENDER_UNIFORM_STRIDE = 256, PUBLIC_RENDER_BINDING_BYTES = 12 };
typedef struct public_render_state {
    WGPUBuffer vertex;
    WGPUBindGroupLayout layout;
    WGPUBindGroup group;
    WGPUSampler sampler;
    WGPUTextureView sampled_view;
    WGPUTexture depth;
    uint32_t depth_width, depth_height;
    WGPURenderPipeline pipelines[DOE_FORMAT_RGBA32_FLOAT + 1];
    bool shader_compiled;
} public_render_state;

/* State is runtime-owned; failed partial initialization is released at close. */
public_render_state *runtime_public_render_state(doe_runtime *runtime, doe_trace_execution *execution);
WGPURenderPipeline runtime_public_render_pipeline(doe_runtime *runtime, doe_texture_format format,
    doe_trace_execution *execution);
const char *runtime_public_render_shader(void);
#endif
