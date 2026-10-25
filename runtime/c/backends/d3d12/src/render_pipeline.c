/* SPDX-License-Identifier: MIT */
#include "internal.h"
#include <math.h>

void doe_dx_binding_plan_copy(doe_dx_binding_plan *out,const doe_dx_dynamic_pipeline_desc *desc)
{
    *out=(doe_dx_binding_plan){.binding_count=desc->resources.buffers.binding_count,
        .texture_count=desc->resources.texture_count,.sampler_count=desc->sampler_count};
    if (out->binding_count) memcpy(out->bindings,desc->resources.buffers.bindings,
        out->binding_count*sizeof(*out->bindings));
    if (out->texture_count) {
        memcpy(out->textures,desc->resources.textures,out->texture_count*sizeof(*out->textures));
        if (desc->texture_contracts) memcpy(out->texture_contracts,desc->texture_contracts,
            out->texture_count*sizeof(*out->texture_contracts));
    }
    if (out->sampler_count) memcpy(out->samplers,desc->samplers,out->sampler_count*sizeof(*out->samplers));
}

static bool blend_component_valid(doe_dx_blend_component component)
{
    bool source=component.source<=11 || component.source==14;
    bool destination=component.destination<=11 || component.destination==14;
    return source && destination && component.operation<=4 &&
        (component.operation<3 || (component.source==1 && component.destination==1));
}

doe_dx_status doe_dx_pipeline_create_render(doe_dx_context *context,
    const doe_dx_render_pipeline_desc *desc,doe_dx_pipeline **out,doe_dx_error *error)
{
    if (!context || !desc || !out) return doe_dx_fail(DOE_DX_INVALID_ARGUMENT,0,"render.pipeline",error);
    if (context->lost) return doe_dx_fail(DOE_DX_DEVICE_LOST,0,"render.pipeline",error);
    if (desc->color_format!=DOE_DX_RGBA8_UNORM && desc->color_format!=DOE_DX_BGRA8_UNORM)
        return doe_dx_fail(DOE_DX_UNSUPPORTED,0,"render.color_format",error);
    if (desc->depth_enabled && (desc->depth_compare<DOE_DX_NEVER || desc->depth_compare>DOE_DX_ALWAYS ||
        !isfinite(desc->depth_bias_slope) || !isfinite(desc->depth_bias_clamp)))
        return doe_dx_fail(DOE_DX_INVALID_ARGUMENT,0,"render.depth",error);
    if (desc->blend_enabled && (!blend_component_valid(desc->color_blend) ||
        !blend_component_valid(desc->alpha_blend)))
        return doe_dx_fail(DOE_DX_INVALID_ARGUMENT,0,"render.blend",error);
    switch (desc->topology) {
    case DOE_DX_TRIANGLES: case DOE_DX_TRIANGLE_STRIP: case DOE_DX_LINES:
    case DOE_DX_LINE_STRIP: case DOE_DX_POINTS: break;
    default: return doe_dx_fail(DOE_DX_INVALID_ARGUMENT,0,"render.topology",error);
    }
    if (desc->cull!=DOE_DX_CULL_NONE && desc->cull!=DOE_DX_CULL_FRONT && desc->cull!=DOE_DX_CULL_BACK)
        return doe_dx_fail(DOE_DX_INVALID_ARGUMENT,0,"render.cull",error);
    if (desc->masks_explicit && (desc->color_write_mask & ~UINT32_C(15)))
        return doe_dx_fail(DOE_DX_INVALID_ARGUMENT,0,"render.color_write_mask",error);
    for (size_t i=0;i<DOE_DX_RENDER_STAGES;++i) {
        if (desc->stages[i].resources.sampler_count)
            return doe_dx_fail(DOE_DX_UNSUPPORTED,0,"render.static_samplers",error);
        doe_dx_status status=doe_dx_validate_layout(context,&desc->stages[i].resources,&desc->stages[i]);
        if (status!=DOE_DX_OK) return doe_dx_fail(status,0,"render.stage_layout",error);
    }
    uint32_t ends[DOE_DX_MAX_VERTEX_BUFFERS]={0};
    doe_dx_status vertex_status=doe_dx_validate_render_vertices(desc,ends);
    if (vertex_status!=DOE_DX_OK) return doe_dx_fail(vertex_status,0,"render.vertex_layout",error);
    if (context->objects==SIZE_MAX) return doe_dx_fail(DOE_DX_RANGE,0,"render.objects",error);
    doe_dx_pipeline *pipeline=calloc(1,sizeof(*pipeline));
    if (!pipeline) return doe_dx_fail(DOE_DX_OUT_OF_MEMORY,0,"render.pipeline.allocate",error);
    pipeline->render=calloc(1,sizeof(*pipeline->render));
    if (!pipeline->render) {
        free(pipeline);
        return doe_dx_fail(DOE_DX_OUT_OF_MEMORY,0,"render.layout.allocate",error);
    }
    doe_dx_status status=doe_dx_native_render_create(context->native,desc,&pipeline->native,error);
    if (status!=DOE_DX_OK) {
        free(pipeline->render);
        free(pipeline);
        return doe_dx_context_result(context,status,error);
    }
    for (size_t i=0;i<DOE_DX_RENDER_STAGES;++i)
        doe_dx_binding_plan_copy(&pipeline->render->stages[i],&desc->stages[i]);
    pipeline->context=context;
    pipeline->references=1;
    pipeline->graphics=true;
    pipeline->vertex_buffer_count=desc->vertex_buffer_count;
    if (desc->vertex_buffer_count) memcpy(pipeline->vertex_buffers,desc->vertex_buffers,
        desc->vertex_buffer_count*sizeof(*desc->vertex_buffers));
    memcpy(pipeline->vertex_ends,ends,sizeof(ends));
    pipeline->color_format=desc->color_format;
    pipeline->depth_enabled=desc->depth_enabled;
    context->objects++;
    *out=pipeline;
    return doe_dx_fail(DOE_DX_OK,0,"render.pipeline.create",error);
}
