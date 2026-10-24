/* SPDX-License-Identifier: MIT */
#ifndef DOE_MTL_RENDER_INTERNAL_H
#define DOE_MTL_RENDER_INTERNAL_H
#include "texture_internal.h"
struct doe_mtl_render_pipeline {
    doe_mtl_context *context;
    uint32_t references;
    void *native, *depth_stencil;
    doe_mtl_render_pipeline_desc desc;
    doe_mtl_vertex_layout layouts[DOE_MTL_BUFFER_SLOTS];
    doe_mtl_vertex_attribute attributes[DOE_MTL_VERTEX_ATTRIBUTES];
};
typedef struct doe_mtl_render_pipeline_use {
    doe_mtl_render_pipeline *pipeline;
    struct doe_mtl_render_pipeline_use *next;
} doe_mtl_render_pipeline_use;
uint32_t doe_mtl_vertex_format_size(uint32_t format);
doe_mtl_status doe_mtl_render_pass_preflight(doe_mtl_commands *commands,
    const doe_mtl_render_pass_desc *pass, doe_mtl_native_render_pass *native);
doe_mtl_status doe_mtl_render_pass_retain(doe_mtl_commands *commands,
    const doe_mtl_render_pass_desc *pass);
void doe_mtl_render_pass_publish(doe_mtl_commands *commands, const doe_mtl_render_pass_desc *pass);
doe_mtl_status doe_mtl_render_draw_preflight(doe_mtl_commands *commands,
    const doe_mtl_render_pass_desc *pass, const doe_mtl_native_render_pass *native_pass,
    const doe_mtl_render_draw *draw, doe_mtl_native_render_draw *native);
doe_mtl_status doe_mtl_render_draw_retain(doe_mtl_commands *commands, const doe_mtl_render_draw *draw);
doe_mtl_status doe_mtl_render_visibility_preflight(doe_mtl_commands *commands,
    const doe_mtl_render_pass_desc *pass, const doe_mtl_render_draw *draws, size_t count);
void doe_mtl_render_retire(doe_mtl_commands *commands);
doe_mtl_status doe_mtl_draw_guard_preflight(doe_mtl_commands *, const doe_mtl_render_draw *);
doe_mtl_status doe_mtl_draw_guard_prepare(doe_mtl_commands *, const doe_mtl_render_draw *,
    doe_mtl_native_render_draw *, doe_mtl_error *);
#endif
