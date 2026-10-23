/* SPDX-License-Identifier: MIT */
#include "objc_abi.h"

static bool vertex_layout(doe_mtl_objc *r, void *descriptor, const doe_mtl_render_pipeline_desc *desc) {
    doe_mtl_objc_graphics *g = &r->graphics;
    void *vertex = doe_mtl_objc_object(r, g->vertex_class, r->new_object);
    void *layouts, *attributes;
    size_t i;
    bool success = false;
    if (vertex == NULL) return false;
    layouts = doe_mtl_objc_object(r, vertex, g->layouts);
    attributes = doe_mtl_objc_object(r, vertex, g->attributes);
    if (layouts == NULL || attributes == NULL) goto cleanup;
    for (i = 0; i < desc->layout_count; ++i) {
        const doe_mtl_vertex_layout *layout = &desc->layouts[i];
        void *item = g->index_object(layouts, g->index, layout->slot);
        if (item == NULL) goto cleanup;
        r->set_size_call(item, g->set_stride, layout->stride);
        r->set_size_call(item, g->set_step, layout->step);
        r->set_size_call(item, g->set_rate, 1);
    }
    for (i = 0; i < desc->attribute_count; ++i) {
        const doe_mtl_vertex_attribute *attribute = &desc->attributes[i];
        void *item = g->index_object(attributes, g->index, attribute->location);
        if (item == NULL) goto cleanup;
        r->set_size_call(item, g->set_attribute_format, attribute->format);
        r->set_size_call(item, g->set_offset, attribute->offset);
        r->set_size_call(item, g->set_buffer_index, attribute->slot);
    }
    r->set_object_call(descriptor, g->set_vertex_descriptor, vertex);
    success = true;
cleanup:
    doe_mtl_objc_void(r, vertex, r->release);
    return success;
}
static bool colors(doe_mtl_objc *r, void *descriptor, const doe_mtl_render_pipeline_desc *desc) {
    doe_mtl_objc_graphics *g = &r->graphics;
    void *array = doe_mtl_objc_object(r, descriptor, g->colors);
    uint32_t i;
    if (array == NULL) return false;
    for (i = 0; i < desc->color_count; ++i) {
        const doe_mtl_color_target *color = &desc->colors[i];
        void *item = g->index_object(array, g->index, i);
        if (item == NULL) return false;
        r->set_size_call(item, r->set_format, (size_t)color->format);
        r->set_size_call(item, g->set_write_mask, color->write_mask);
        g->set_bool(item, g->set_blend, (doe_objc_bool)color->blend);
        r->set_size_call(item, g->set_color_op, color->color.operation);
        r->set_size_call(item, g->set_color_source, color->color.source);
        r->set_size_call(item, g->set_color_destination, color->color.destination);
        r->set_size_call(item, g->set_alpha_op, color->alpha.operation);
        r->set_size_call(item, g->set_alpha_source, color->alpha.source);
        r->set_size_call(item, g->set_alpha_destination, color->alpha.destination);
    }
    return true;
}
doe_mtl_status doe_mtl_objc_render_pipeline(void *userdata, void *device,
    const doe_mtl_render_pipeline_desc *desc, void **out, void **out_depth, doe_mtl_error *error) {
    doe_mtl_objc *r = userdata;
    doe_mtl_objc_graphics *g = &r->graphics;
    void *pool = doe_mtl_objc_pool(r), *vertex = NULL, *fragment = NULL, *descriptor = NULL;
    void *pipeline = NULL, *depth = NULL, *native_error = NULL;
    doe_mtl_status status;
    if (pool == NULL) return doe_mtl_fail(DOE_MTL_NATIVE_FAILURE, "autorelease pool", error);
    status = doe_mtl_objc_function(r, device, desc->vertex.msl, desc->vertex.msl_length,
        desc->vertex.entrypoint, &vertex, error);
    if (status != DOE_MTL_OK) goto cleanup;
    if (desc->fragment.msl != NULL) {
        status = doe_mtl_objc_function(r, device, desc->fragment.msl, desc->fragment.msl_length,
            desc->fragment.entrypoint, &fragment, error);
        if (status != DOE_MTL_OK) goto cleanup;
    }
    status = DOE_MTL_NATIVE_FAILURE;
    descriptor = doe_mtl_objc_object(r, g->pipeline_class, r->new_object);
    if (descriptor == NULL) goto descriptor_failure;
    r->set_object_call(descriptor, g->set_vertex_function, vertex);
    r->set_object_call(descriptor, g->set_fragment_function, fragment);
    r->set_size_call(descriptor, g->set_raster_samples, desc->sample_count);
    r->set_size_call(descriptor, g->set_depth_format, (size_t)desc->depth_stencil.depth_format);
    r->set_size_call(descriptor, g->set_stencil_format, (size_t)desc->depth_stencil.stencil_format);
    if (!colors(r, descriptor, desc)) goto descriptor_failure;
    if ((desc->layout_count != 0 || desc->attribute_count != 0) && !vertex_layout(r, descriptor, desc))
        goto descriptor_failure;
    pipeline = r->pipeline_call(device, g->new_pipeline, descriptor, &native_error);
    if (pipeline == NULL) {
        status = doe_mtl_objc_error(r, native_error, "newRenderPipelineStateWithDescriptor:error:", error);
        goto cleanup;
    }
    if (desc->depth_stencil.depth_format != 0 || desc->depth_stencil.stencil_format != 0) {
        depth = doe_mtl_objc_depth_state(r, device, &desc->depth_stencil);
        if (depth == NULL) {
            status = doe_mtl_fail(DOE_MTL_NATIVE_FAILURE, "newDepthStencilStateWithDescriptor:", error);
            goto cleanup;
        }
    }
    *out = pipeline;
    *out_depth = depth;
    pipeline = NULL;
    depth = NULL;
    status = doe_mtl_fail(DOE_MTL_OK, "render pipeline create", error);
    goto cleanup;
descriptor_failure:
    status = doe_mtl_fail(DOE_MTL_NATIVE_FAILURE, "render pipeline descriptor", error);
cleanup:
    if (depth != NULL) doe_mtl_objc_void(r, depth, r->release);
    if (pipeline != NULL) doe_mtl_objc_void(r, pipeline, r->release);
    if (descriptor != NULL) doe_mtl_objc_void(r, descriptor, r->release);
    if (fragment != NULL) doe_mtl_objc_void(r, fragment, r->release);
    if (vertex != NULL) doe_mtl_objc_void(r, vertex, r->release);
    doe_mtl_objc_drain(r, pool);
    return status;
}
