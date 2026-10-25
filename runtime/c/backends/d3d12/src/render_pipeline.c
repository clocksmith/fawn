/* SPDX-License-Identifier: MIT */
#include "internal.h"
#include <math.h>

void doe_dx_binding_plan_copy(doe_dx_binding_plan *out,const doe_dx_dynamic_pipeline_desc *desc)
{
    *out=(doe_dx_binding_plan){.immediate_bytes=desc->immediate_bytes,
        .immediate_required_bytes=desc->immediate_required_bytes,.binding_count=desc->resources.buffers.binding_count,
        .texture_count=desc->resources.texture_count,.sampler_count=desc->sampler_count};
    out->logical_size_count=desc->logical_size_count;
    if (desc->logical_size_count) memcpy(out->logical_size_indices,desc->logical_size_indices,
        out->binding_count*sizeof(*out->logical_size_indices));
    if (out->binding_count) memcpy(out->bindings,desc->resources.buffers.bindings,
        out->binding_count*sizeof(*out->bindings));
    if (out->texture_count) {
        memcpy(out->textures,desc->resources.textures,out->texture_count*sizeof(*out->textures));
        if (desc->texture_contracts) memcpy(out->texture_contracts,desc->texture_contracts,
            out->texture_count*sizeof(*out->texture_contracts));
    }
    if (out->sampler_count) memcpy(out->samplers,desc->samplers,out->sampler_count*sizeof(*out->samplers));
}

uint32_t doe_dx_render_target_count(const doe_dx_render_pipeline_desc *desc)
{
    return desc->targets_explicit ? desc->target_count : desc->color_absent ? 0u : 1u;
}
doe_dx_render_target doe_dx_render_target_at(const doe_dx_render_pipeline_desc *desc,uint32_t index)
{
    if (desc->targets_explicit) return desc->targets[index];
    return (doe_dx_render_target){.format=desc->color_format,.absent=desc->color_absent,
        .blend_enabled=desc->blend_enabled,.write_mask=desc->masks_explicit ? desc->color_write_mask : 15u,
        .color_blend=desc->color_blend,.alpha_blend=desc->alpha_blend};
}
static bool stencil_face_valid(doe_dx_stencil_face face) {
    return face.fail<=7 && face.depth_fail<=7 && face.pass<=7 && face.compare>=DOE_DX_NEVER && face.compare<=DOE_DX_ALWAYS;
}
static bool stencil_writes(doe_dx_stencil_face face) { return face.fail || face.depth_fail || face.pass; }
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
    uint32_t target_count=doe_dx_render_target_count(desc);
    if (target_count>DOE_DX_MAX_COLOR_TARGETS) return doe_dx_fail(DOE_DX_RANGE,0,"render.target_count",error);
    bool color_present=false;
    for (uint32_t i=0;i<target_count;++i) {
        doe_dx_render_target target=doe_dx_render_target_at(desc,i);
        if (target.absent) continue;
        color_present=true;
        if (!doe_dx_format_renderable(target.format))
            return doe_dx_fail(DOE_DX_UNSUPPORTED,0,"render.color_format",error);
        if ((target.write_mask & ~UINT32_C(15)) || (target.blend_enabled &&
            (!blend_component_valid(target.color_blend) || !blend_component_valid(target.alpha_blend))))
            return doe_dx_fail(DOE_DX_INVALID_ARGUMENT,0,"render.target_state",error);
    }
    if ((!color_present && !desc->depth_enabled) || (desc->fragment_absent && color_present))
        return doe_dx_fail(DOE_DX_UNSUPPORTED,0,"render.color_absence",error);
    uint32_t samples=desc->samples ? desc->samples : 1;
    if ((samples!=1 && samples!=4) || (desc->alpha_to_coverage && (samples!=4 || !target_count || doe_dx_render_target_at(desc,0).absent)))
        return doe_dx_fail(DOE_DX_INVALID_ARGUMENT,0,"render.samples",error);
    if (desc->strip_indexed && ((desc->topology!=DOE_DX_TRIANGLE_STRIP && desc->topology!=DOE_DX_LINE_STRIP) ||
        (desc->strip_index_format!=DOE_DX_INDEX_UINT16 && desc->strip_index_format!=DOE_DX_INDEX_UINT32)))
        return doe_dx_fail(DOE_DX_INVALID_ARGUMENT,0,"render.strip_index",error);
    doe_dx_format depth_format=desc->depth_state_explicit ? desc->depth_format : DOE_DX_D32_FLOAT;
    if (desc->depth_enabled && (!doe_dx_format_depth(depth_format) || (desc->stencil_test &&
        (!doe_dx_format_stencil(depth_format) || !stencil_face_valid(desc->stencil_front) || !stencil_face_valid(desc->stencil_back)))))
        return doe_dx_fail(DOE_DX_INVALID_ARGUMENT,0,"render.depth_stencil",error);
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
    if (desc->fragment_absent) {
        const doe_dx_dynamic_pipeline_desc *stage=&desc->stages[1];
        if (stage->resources.buffers.bytecode || stage->resources.buffers.bytecode_size ||
            stage->resources.buffers.bindings || stage->resources.buffers.binding_count ||
            stage->resources.textures || stage->resources.texture_count || stage->resources.samplers ||
            stage->resources.sampler_count || stage->texture_contracts || stage->samplers || stage->sampler_count ||
            stage->immediate_bytes || stage->immediate_required_bytes || stage->logical_size_count || stage->logical_size_indices)
            return doe_dx_fail(DOE_DX_INVALID_ARGUMENT,0,"render.fragment_absence",error);
    }
    for (size_t i=0;i<(desc->fragment_absent ? 1u : DOE_DX_RENDER_STAGES);++i) {
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
    pipeline->render->depth_format=depth_format;
    pipeline->render->depth_write=desc->depth_write;
    pipeline->render->stencil_write=desc->stencil_test && desc->stencil_write_mask &&
        ((desc->cull!=DOE_DX_CULL_FRONT && stencil_writes(desc->stencil_front)) ||
         (desc->cull!=DOE_DX_CULL_BACK && stencil_writes(desc->stencil_back)));
    pipeline->render->samples=samples;
    pipeline->render->color_absent=!color_present;
    pipeline->render->target_count=target_count;
    for (uint32_t i=0;i<target_count;++i) pipeline->render->targets[i]=doe_dx_render_target_at(desc,i);
    pipeline->render->fragment_absent=desc->fragment_absent;
    pipeline->render->topology=desc->topology;
    pipeline->render->strip_indexed=desc->strip_indexed;
    pipeline->render->strip_index_format=desc->strip_index_format;
    context->objects++;
    *out=pipeline;
    return doe_dx_fail(DOE_DX_OK,0,"render.pipeline.create",error);
}
