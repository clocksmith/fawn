/* SPDX-License-Identifier: MIT */
#include "render_internal.h"

uint32_t doe_mtl_vertex_format_size(uint32_t format) {
    if (format >= 1 && format <= 12) return (format - 1) % 3 + 2;
    if (format >= 13 && format <= 27) return ((format - 13) % 3 + 2) * 2;
    if (format >= 28 && format <= 39) return ((format - 28) % 4 + 1) * 4;
    if ((format >= 40 && format <= 42) || format == 54 || format == 55) return 4;
    if (format >= 45 && format <= 48) return 1;
    if (format >= 49 && format <= 53) return 2;
    return 0;
}
static uint32_t vertex_format_alignment(uint32_t format) {
    if (format <= 12 || format == 42 || (format >= 45 && format <= 48)) return 1;
    if (format <= 27 || (format >= 49 && format <= 53)) return 2;
    return 4;
}
static bool stage_valid(doe_mtl_shader_stage stage) {
    return stage.msl != NULL && stage.msl_length > 0 && stage.entrypoint != NULL &&
        stage.entrypoint[0] != '\0' && (stage.sizes_slot == UINT32_MAX || stage.sizes_slot < DOE_MTL_BUFFER_SLOTS);
}
static bool face_valid(doe_mtl_stencil_face face) {
    return face.compare <= 7 && face.fail <= 7 && face.depth_fail <= 7 && face.pass <= 7;
}
static bool blend_valid(doe_mtl_blend_component blend) {
    return blend.operation <= 4 && blend.source <= 14 && blend.destination <= 14;
}
static doe_mtl_status pipeline_validate(const doe_mtl_render_pipeline_desc *desc) {
    size_t i, j;
    bool occupied = false;
    const doe_mtl_depth_stencil *depth;
    if (desc == NULL || !stage_valid(desc->vertex) || desc->color_count > DOE_MTL_COLOR_ATTACHMENTS ||
        desc->layout_count > DOE_MTL_BUFFER_SLOTS || desc->attribute_count > DOE_MTL_VERTEX_ATTRIBUTES ||
        (desc->layout_count != 0 && desc->layouts == NULL) ||
        (desc->attribute_count != 0 && desc->attributes == NULL)) return DOE_MTL_INVALID_ARGUMENT;
    if (desc->sample_count != 1) return DOE_MTL_UNSUPPORTED;
    if (desc->fragment.msl != NULL && !stage_valid(desc->fragment)) return DOE_MTL_INVALID_ARGUMENT;
    if (desc->fragment.msl == NULL && (desc->fragment.msl_length != 0 || desc->fragment.entrypoint != NULL))
        return DOE_MTL_INVALID_ARGUMENT;
    for (i = 0; i < desc->color_count; ++i) {
        const doe_mtl_color_target *color = &desc->colors[i];
        doe_mtl_format_info info;
        if (color->format == 0) continue;
        occupied = true;
        if (doe_mtl_texture_format_info(color->format, &info) != DOE_MTL_OK ||
            info.compressed || info.depth || info.stencil) return DOE_MTL_UNSUPPORTED;
        if (color->write_mask > 15 || !blend_valid(color->color) || !blend_valid(color->alpha))
            return DOE_MTL_INVALID_ARGUMENT;
    }
    if (occupied && !stage_valid(desc->fragment)) return DOE_MTL_INVALID_ARGUMENT;
    depth = &desc->depth_stencil;
    if ((depth->depth_format != 0 && depth->depth_format != DOE_MTL_DEPTH16_UNORM &&
         depth->depth_format != DOE_MTL_DEPTH32_FLOAT) ||
        (depth->stencil_format != 0 && depth->stencil_format != DOE_MTL_STENCIL8)) return DOE_MTL_UNSUPPORTED;
    if (!occupied && depth->depth_format == 0 && depth->stencil_format == 0) return DOE_MTL_INVALID_ARGUMENT;
    if (depth->depth_compare > 7 || !face_valid(depth->front) || !face_valid(depth->back))
        return DOE_MTL_INVALID_ARGUMENT;
    for (i = 0; i < desc->layout_count; ++i) {
        const doe_mtl_vertex_layout *layout = &desc->layouts[i];
        if (layout->slot >= DOE_MTL_BUFFER_SLOTS || layout->slot == desc->vertex.sizes_slot ||
            layout->stride > 2048 || layout->step > 2 || (layout->step != 0 && layout->stride == 0))
            return DOE_MTL_RANGE;
        for (j = 0; j < i; ++j) if (desc->layouts[j].slot == layout->slot) return DOE_MTL_INVALID_ARGUMENT;
    }
    for (i = 0; i < desc->attribute_count; ++i) {
        const doe_mtl_vertex_attribute *attribute = &desc->attributes[i];
        const uint32_t size = doe_mtl_vertex_format_size(attribute->format);
        const doe_mtl_vertex_layout *layout = NULL;
        if (attribute->location >= DOE_MTL_VERTEX_ATTRIBUTES || size == 0) return DOE_MTL_UNSUPPORTED;
        for (j = 0; j < i; ++j)
            if (desc->attributes[j].location == attribute->location) return DOE_MTL_INVALID_ARGUMENT;
        for (j = 0; j < desc->layout_count; ++j)
            if (desc->layouts[j].slot == attribute->slot) layout = &desc->layouts[j];
        if (size != 0 && attribute->offset % vertex_format_alignment(attribute->format) != 0)
            return DOE_MTL_ALIGNMENT;
        if (layout == NULL || attribute->offset > 2048 || size > 2048 - attribute->offset ||
            (layout->stride != 0 && attribute->offset + size > layout->stride)) return DOE_MTL_RANGE;
    }
    return DOE_MTL_OK;
}

doe_mtl_status doe_mtl_render_pipeline_create(doe_mtl_context *context,
    const doe_mtl_render_pipeline_desc *desc, doe_mtl_render_pipeline **out, doe_mtl_error *error) {
    doe_mtl_render_pipeline *pipeline;
    doe_mtl_status status;
    if (context == NULL || out == NULL) return doe_mtl_fail(DOE_MTL_INVALID_ARGUMENT, "render pipeline create", error);
    status = pipeline_validate(desc);
    if (status != DOE_MTL_OK) return doe_mtl_fail(status, "render pipeline descriptor", error);
    if (context->native.render_pipeline == NULL) return doe_mtl_fail(DOE_MTL_UNSUPPORTED, "render pipeline native boundary", error);
    pipeline = calloc(1, sizeof(*pipeline));
    if (pipeline == NULL) return doe_mtl_fail(DOE_MTL_OUT_OF_MEMORY, "render pipeline allocation", error);
    status = doe_mtl_context_retain(context);
    if (status != DOE_MTL_OK) {
        free(pipeline);
        return doe_mtl_fail(status, "render pipeline context lease", error);
    }
    pipeline->context = context;
    pipeline->references = 1;
    pipeline->desc = *desc;
    if (desc->layout_count != 0) memcpy(pipeline->layouts, desc->layouts, desc->layout_count * sizeof(*desc->layouts));
    if (desc->attribute_count != 0) memcpy(pipeline->attributes, desc->attributes, desc->attribute_count * sizeof(*desc->attributes));
    pipeline->desc.layouts = pipeline->layouts;
    pipeline->desc.attributes = pipeline->attributes;
    status = context->native.render_pipeline(context->native.userdata, context->device, desc,
        &pipeline->native, &pipeline->depth_stencil, error);
    if (status != DOE_MTL_OK) {
        doe_mtl_render_pipeline_release(pipeline);
        return status;
    }
    /* Stage source/name spans are call-borrowed. Keep only numeric layout facts. */
    pipeline->desc.vertex.msl = NULL;
    pipeline->desc.vertex.entrypoint = NULL;
    if (desc->fragment.msl == NULL) pipeline->desc.fragment.sizes_slot = UINT32_MAX;
    pipeline->desc.fragment.msl = NULL;
    pipeline->desc.fragment.entrypoint = NULL;
    *out = pipeline;
    return doe_mtl_fail(DOE_MTL_OK, "render pipeline create", error);
}
doe_mtl_status doe_mtl_render_pipeline_retain(doe_mtl_render_pipeline *pipeline) {
    if (pipeline == NULL) return DOE_MTL_INVALID_ARGUMENT;
    if (pipeline->references == UINT32_MAX) return DOE_MTL_RANGE;
    pipeline->references++;
    return DOE_MTL_OK;
}
void doe_mtl_render_pipeline_release(doe_mtl_render_pipeline *pipeline) {
    doe_mtl_context *context;
    if (pipeline == NULL || --pipeline->references != 0) return;
    context = pipeline->context;
    if (pipeline->depth_stencil != NULL) context->native.release(context->native.userdata, pipeline->depth_stencil);
    if (pipeline->native != NULL) context->native.release(context->native.userdata, pipeline->native);
    free(pipeline);
    doe_mtl_context_release(context);
}
void *doe_mtl_render_pipeline_native(const doe_mtl_render_pipeline *pipeline) {
    return pipeline != NULL ? pipeline->native : NULL;
}
