/* SPDX-License-Identifier: MIT */
#ifndef DOE_MTL_OBJC_RENDER_TYPES_H
#define DOE_MTL_OBJC_RENDER_TYPES_H
/* Included after the native BOOL ABI in objc_abi.h. */
typedef struct doe_mtl_scissor_abi { size_t x, y, width, height; } doe_mtl_scissor_abi;
typedef void *(*doe_objc_index_object_call)(void *, void *, size_t);
typedef void (*doe_objc_set_bool_call)(void *, void *, doe_objc_bool);
typedef void (*doe_objc_set_u32_call)(void *, void *, uint32_t);
typedef void (*doe_objc_set_double_call)(void *, void *, double);
typedef void (*doe_objc_clear_color_call)(void *, void *, doe_mtl_clear_color);
typedef void (*doe_objc_viewport_call)(void *, void *, doe_mtl_viewport);
typedef void (*doe_objc_scissor_call)(void *, void *, doe_mtl_scissor_abi);
typedef void (*doe_objc_blend_color_call)(void *, void *, float, float, float, float);
typedef void (*doe_objc_depth_bias_call)(void *, void *, float, float, float);
typedef void (*doe_objc_visibility_call)(void *, void *, size_t, size_t);
typedef void (*doe_objc_draw_call)(void *, void *, size_t, size_t, size_t, size_t, size_t);
typedef void (*doe_objc_draw_indexed_call)(void *, void *, size_t, size_t, size_t,
    void *, size_t, size_t, intptr_t, size_t);
typedef struct doe_mtl_objc_graphics {
    doe_objc_index_object_call index_object;
    doe_objc_set_bool_call set_bool;
    doe_objc_set_u32_call set_u32;
    doe_objc_set_double_call set_double;
    doe_objc_clear_color_call clear_color;
    doe_objc_viewport_call viewport;
    doe_objc_scissor_call scissor;
    doe_objc_blend_color_call blend_color;
    doe_objc_depth_bias_call depth_bias;
    doe_objc_draw_call draw;
    doe_objc_visibility_call visibility;
    doe_objc_draw_indexed_call draw_indexed;
    void *pipeline_class, *vertex_class, *pass_class, *depth_class, *stencil_class;
    void *new_pipeline, *new_depth, *new_encoder;
    void *set_vertex_function, *set_fragment_function, *colors, *index;
    void *set_raster_samples, *set_vertex_descriptor, *set_depth_format, *set_stencil_format;
    void *set_blend, *set_write_mask, *set_color_op, *set_color_source, *set_color_destination;
    void *set_alpha_op, *set_alpha_source, *set_alpha_destination;
    void *layouts, *attributes, *set_stride, *set_step, *set_rate, *set_attribute_format, *set_offset, *set_buffer_index;
    void *set_depth_compare, *set_depth_write, *set_front_stencil, *set_back_stencil;
    void *set_stencil_compare, *set_stencil_fail, *set_depth_fail, *set_stencil_pass, *set_read_mask, *set_stencil_write_mask;
    void *depth_attachment, *stencil_attachment, *set_texture, *set_load, *set_store, *set_level, *set_slice;
    void *set_clear_color, *set_clear_depth, *set_clear_stencil;
    void *set_resolve, *set_resolve_level, *set_resolve_slice, *set_depth_resolve, *set_stencil_resolve;
    void *set_pipeline, *set_depth_state;
    void *set_vertex_buffer, *set_fragment_buffer, *set_vertex_bytes, *set_fragment_bytes;
    void *set_vertex_texture, *set_fragment_texture, *set_vertex_sampler, *set_fragment_sampler;
    void *set_viewport, *set_scissor, *set_winding, *set_cull, *set_blend_color, *set_depth_bias, *set_stencil_reference;
    void *draw_primitives, *draw_indexed_primitives;
    void *set_visibility_buffer, *set_visibility;
} doe_mtl_objc_graphics;
#endif
