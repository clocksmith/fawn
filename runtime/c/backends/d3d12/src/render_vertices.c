/* SPDX-License-Identifier: MIT */
#include "internal.h"

/* The resource owner distinguishes public holes from required empty layouts.
 * This private owner only needs actual fetch extents; it never invents a slot. */
doe_dx_status doe_dx_validate_render_vertices(const doe_dx_render_pipeline_desc *desc,uint32_t *ends)
{
    if (desc->vertex_buffer_count>DOE_DX_MAX_VERTEX_BUFFERS || desc->attribute_count>DOE_DX_MAX_VERTEX_ATTRIBUTES)
        return DOE_DX_RANGE;
    if ((desc->vertex_buffer_count && !desc->vertex_buffers) || (desc->attribute_count && !desc->attributes))
        return DOE_DX_INVALID_ARGUMENT;
    for (size_t i=0;i<desc->vertex_buffer_count;++i) {
        const doe_dx_vertex_layout *layout=&desc->vertex_buffers[i];
        if (layout->stride>2048 || layout->stride%4 ||
            (layout->step!=DOE_DX_PER_VERTEX && layout->step!=DOE_DX_PER_INSTANCE)) return DOE_DX_INVALID_ARGUMENT;
    }
    for (size_t i=0;i<desc->attribute_count;++i) {
        const doe_dx_vertex_attribute *attribute=&desc->attributes[i];
        uint32_t bytes=doe_dx_vertex_format_size(attribute->format);
        if (!bytes || attribute->buffer_slot>=desc->vertex_buffer_count || attribute->shader_location>=32)
            return DOE_DX_INVALID_ARGUMENT;
        const doe_dx_vertex_layout *layout=&desc->vertex_buffers[attribute->buffer_slot];
        uint32_t alignment=bytes<4 ? bytes : 4;
        uint32_t stride=layout->stride ? layout->stride : 2048;
        if (attribute->offset%alignment || attribute->offset>stride || bytes>stride-attribute->offset)
            return DOE_DX_RANGE;
        for (size_t j=0;j<i;++j) if (desc->attributes[j].shader_location==attribute->shader_location)
            return DOE_DX_INVALID_ARGUMENT;
        uint32_t end=attribute->offset+bytes;
        if (ends[attribute->buffer_slot]<end) ends[attribute->buffer_slot]=end;
    }
    return DOE_DX_OK;
}

static bool writable_alias(const doe_dx_pipeline *pipeline,const doe_dx_render_draw *draw,doe_dx_buffer *buffer)
{
    for (size_t stage=0;stage<DOE_DX_RENDER_STAGES;++stage) {
        const doe_dx_binding_plan *plan=&pipeline->render->stages[stage];
        for (size_t i=0;i<plan->binding_count;++i)
            if (draw->stages[stage].resources.buffers[i].buffer==buffer &&
                doe_dx_buffer_descriptor_kind(&plan->bindings[i])==DOE_DX_DESCRIPTOR_UAV) return true;
    }
    return false;
}

doe_dx_status doe_dx_prepare_render_vertices(doe_dx_commands *commands,doe_dx_pipeline *pipeline,
    const doe_dx_render_draw *draw,doe_dx_native_render_draw *native,doe_dx_reservation *needed,doe_dx_error *error)
{
    if (draw->vertex_buffer_count!=pipeline->vertex_buffer_count || (draw->vertex_buffer_count && !draw->vertices))
        return doe_dx_fail(DOE_DX_INVALID_ARGUMENT,0,"render.vertex_count",error);
    native->vertex_buffer_count=draw->vertex_buffer_count;
    bool active=draw->indirect || (draw->vertex_count && draw->instance_count);
    for (size_t i=0;i<draw->vertex_buffer_count;++i) {
        const doe_dx_buffer_binding *binding=&draw->vertices[i];
        uint32_t end=pipeline->vertex_ends[i];
        if (!binding->buffer && !binding->offset && !binding->size && !end) continue;
        doe_dx_status status=doe_dx_buffer_preflight(commands,binding->buffer,active && end!=0);
        if (status!=DOE_DX_OK) return doe_dx_fail(status,0,"render.vertex_buffer",error);
        if (binding->buffer->memory!=DOE_DX_DEFAULT)
            return doe_dx_fail(DOE_DX_UNSUPPORTED,0,"render.vertex_heap",error);
        if (binding->offset%4) return doe_dx_fail(DOE_DX_ALIGNMENT,0,"render.vertex_offset",error);
        if (binding->size>UINT32_MAX || !doe_dx_range(binding->buffer->size,binding->offset,binding->size))
            return doe_dx_fail(DOE_DX_RANGE,0,"render.vertex_range",error);
        if (active && end && !draw->indirect) {
            const doe_dx_vertex_layout *layout=&pipeline->vertex_buffers[i];
            uint64_t first=layout->step==DOE_DX_PER_INSTANCE ? draw->first_instance : draw->first_vertex;
            uint64_t count=layout->step==DOE_DX_PER_INSTANCE ? draw->instance_count : draw->vertex_count;
            uint64_t capacity=end>binding->size ? 0 : layout->stride ? (binding->size-end)/layout->stride+1 : UINT64_MAX;
            if (first>capacity || count>capacity-first)
                return doe_dx_fail(DOE_DX_RANGE,0,"render.vertex_span",error);
        }
        if (writable_alias(pipeline,draw,binding->buffer))
            return doe_dx_fail(DOE_DX_UNSUPPORTED,0,"render.vertex_write_alias",error);
        bool repeated=false;
        for (size_t j=0;j<i;++j) if (draw->vertices[j].buffer==binding->buffer) repeated=true;
        if (!repeated && !doe_dx_find_buffer(commands,binding->buffer)) {
            if (needed->buffers==SIZE_MAX) return doe_dx_fail(DOE_DX_RANGE,0,"render.vertex_reservation",error);
            ++needed->buffers;
        }
        native->vertices[i]=(doe_dx_native_binding){binding->buffer->native,binding->offset,binding->size};
    }
    return DOE_DX_OK;
}


doe_dx_status doe_dx_render_draw_capacities(const doe_dx_render_draw *draw,uint64_t out[2])
{
    if (!draw || !out || !draw->pipeline || !draw->pipeline->render ||
        draw->vertex_buffer_count!=draw->pipeline->vertex_buffer_count ||
        (draw->vertex_buffer_count && !draw->vertices)) return DOE_DX_INVALID_ARGUMENT;
    uint64_t capacities[2]={UINT64_MAX,UINT64_MAX};
    for (size_t i=0;i<draw->vertex_buffer_count;++i) {
        uint32_t end=draw->pipeline->vertex_ends[i];
        if (!end) continue;
        const doe_dx_buffer_binding *binding=&draw->vertices[i];
        const doe_dx_vertex_layout *layout=&draw->pipeline->vertex_buffers[i];
        if (!binding->buffer || !doe_dx_range(binding->buffer->size,binding->offset,binding->size)) return DOE_DX_RANGE;
        uint64_t capacity=end>binding->size ? 0 : layout->stride ? (binding->size-end)/layout->stride+1 : UINT64_MAX;
        size_t axis=layout->step==DOE_DX_PER_INSTANCE ? 1u : 0u;
        if (capacity<capacities[axis]) capacities[axis]=capacity;
    }
    memcpy(out,capacities,sizeof(capacities));
    return DOE_DX_OK;
}
