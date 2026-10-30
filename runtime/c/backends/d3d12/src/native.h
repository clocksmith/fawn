/* SPDX-License-Identifier: MIT */
#ifndef DOE_DX_NATIVE_H
#define DOE_DX_NATIVE_H
#include "../include/doe_d3d12_mesh.h"
#include "../include/doe_d3d12_texture_regions.h"
#include "../include/doe_d3d12_render.h"
#if defined(DOE_DX_REQUIRE_WINDOWS) && !defined(_WIN32)
#error "The native D3D12 build requires a Windows target and actual Windows SDK."
#endif

/* Private typed platform calls. Windows implements these using SDK C COM
 * declarations, not reconstructed vtables. Host tests replace these symbols at
 * link time and only prove ownership/ordering, never SDK or GPU behavior. */
typedef struct doe_dx_native_context doe_dx_native_context;
typedef struct doe_dx_native_buffer doe_dx_native_buffer;
typedef struct doe_dx_native_pipeline doe_dx_native_pipeline;
typedef struct doe_dx_native_texture doe_dx_native_texture;
typedef struct doe_dx_native_commands doe_dx_native_commands;
typedef struct doe_dx_native_timestamps doe_dx_native_timestamps;
typedef struct doe_dx_native_binding {
    doe_dx_native_buffer *buffer;
    uint64_t offset, size;
} doe_dx_native_binding;
typedef struct doe_dx_native_resources {
    doe_dx_native_binding buffers[DOE_DX_MAX_BINDINGS];
    doe_dx_native_texture *textures[DOE_DX_MAX_BINDINGS];
    doe_dx_texture_view_desc texture_views[DOE_DX_MAX_BINDINGS];
    size_t buffer_count, texture_count;
    doe_dx_sampler_desc samplers[DOE_DX_MAX_DYNAMIC_SAMPLERS];
    size_t sampler_count;
    uint32_t immediate_words[16];
    uint32_t logical_size_count,logical_size_indices[DOE_DX_MAX_BINDINGS];
} doe_dx_native_resources;

/* Plain copied layout, not a pipeline/refcount/resource owner. */
typedef struct doe_dx_binding_plan {
    uint32_t immediate_bytes;
    uint64_t immediate_required_bytes;
    uint32_t logical_size_count,logical_size_indices[DOE_DX_MAX_BINDINGS];
    size_t binding_count,texture_count,sampler_count;
    doe_dx_binding_layout bindings[DOE_DX_MAX_BINDINGS];
    doe_dx_texture_binding_layout textures[DOE_DX_MAX_BINDINGS];
    doe_dx_texture_contract texture_contracts[DOE_DX_MAX_BINDINGS];
    doe_dx_sampler_layout samplers[DOE_DX_MAX_DYNAMIC_SAMPLERS];
} doe_dx_binding_plan;
void doe_dx_binding_plan_copy(doe_dx_binding_plan *,const doe_dx_dynamic_pipeline_desc *);
typedef struct doe_dx_format_info { uint32_t bytes,block_width,block_height,kind; } doe_dx_format_info;
bool doe_dx_format_properties(doe_dx_format,doe_dx_format_info *);
bool doe_dx_format_compatible(doe_dx_format,doe_dx_format);
bool doe_dx_format_renderable(doe_dx_format);
bool doe_dx_format_exact_clear(doe_dx_format);
bool doe_dx_format_depth(doe_dx_format);
bool doe_dx_format_stencil(doe_dx_format);
/* Normalizes only the legacy additive shape; semantic validation stays in owner. */
uint32_t doe_dx_render_target_count(const doe_dx_render_pipeline_desc *);
doe_dx_render_target doe_dx_render_target_at(const doe_dx_render_pipeline_desc *,uint32_t);
typedef struct doe_dx_native_render_color {
    doe_dx_native_texture *texture,*resolve;
    bool clear,discard;
    float value[4];
    doe_dx_texture_view_desc view,resolve_view;
    uint32_t depth_slice;
} doe_dx_native_render_color;
typedef struct doe_dx_native_render_depth {
    doe_dx_native_texture *texture;
    bool clear, discard;
    float value;
    bool clear_stencil, discard_stencil;
    uint32_t stencil;
    doe_dx_texture_view_desc view;
    bool depth_read_only,stencil_read_only;
} doe_dx_native_render_depth;
typedef struct doe_dx_native_render_draw {
    doe_dx_native_texture *attachment_depth; /* Plane states owned by enclosing pass. */
    doe_dx_native_timestamps *query;
    uint32_t query_index;
    bool query_begin;
    doe_dx_native_pipeline *pipeline;
    doe_dx_native_resources stages[DOE_DX_RENDER_STAGES];
    doe_dx_native_binding vertices[DOE_DX_MAX_VERTEX_BUFFERS];
    doe_dx_native_binding indirect, indices;
    doe_dx_index_format index_format;
    size_t vertex_buffer_count;
    uint32_t vertex_count,instance_count,first_vertex,first_instance;
    uint32_t stencil_reference;
    float blend_constant[4];
    float viewport[6];
    uint32_t scissor[4];
} doe_dx_native_render_draw;
typedef struct doe_dx_native_geometry {
    doe_dx_native_binding vertices[DOE_DX_MAX_VERTEX_BUFFERS];
    doe_dx_native_binding indices;
    doe_dx_native_texture *depth;
} doe_dx_native_geometry;

typedef enum doe_dx_descriptor_kind {
    DOE_DX_DESCRIPTOR_INVALID, DOE_DX_DESCRIPTOR_SRV, DOE_DX_DESCRIPTOR_UAV, DOE_DX_DESCRIPTOR_CBV
} doe_dx_descriptor_kind;
/* One portable classification used by admission and SDK descriptor lowering. */
doe_dx_descriptor_kind doe_dx_buffer_descriptor_kind(const doe_dx_binding_layout *layout);

doe_dx_status doe_dx_fail(doe_dx_status status, int32_t native_code,
    const char *operation, doe_dx_error *error);
doe_dx_status doe_dx_native_open(const doe_dx_options *options,
    doe_dx_native_context **out, doe_dx_caps *caps, doe_dx_error *error);
doe_dx_status doe_dx_native_query_luid(uint32_t low, int32_t high,
    doe_dx_adapter_identity *out, doe_dx_error *error);
doe_dx_status doe_dx_native_close(doe_dx_native_context *context);
/* Creates the context's long-lived shader-visible descriptor heap if absent. */
doe_dx_status doe_dx_native_descriptors_prepare(doe_dx_native_context *context, doe_dx_error *error);
/* Creates the context's first UPLOAD buffer heap if none exists. */
doe_dx_status doe_dx_native_upload_heap_prepare(doe_dx_native_context *context, doe_dx_error *error);
/* Shared actual-identity check, also exercised through the host link seam. */
doe_dx_status doe_dx_validate_adapter(const doe_dx_options *options,
    const doe_dx_caps *caps, doe_dx_error *error);
doe_dx_status doe_dx_native_buffer_create(doe_dx_native_context *context,
    uint64_t size, doe_dx_memory memory, doe_dx_native_buffer **out, doe_dx_error *error);
void doe_dx_native_buffer_destroy(doe_dx_native_buffer *buffer);
doe_dx_status doe_dx_native_zero_readback(doe_dx_native_buffer *buffer, doe_dx_error *error);
doe_dx_status doe_dx_native_write(doe_dx_native_buffer *buffer, uint64_t offset,
    const void *bytes, size_t size, doe_dx_error *error);
doe_dx_status doe_dx_native_map_read(doe_dx_native_buffer *buffer, uint64_t offset, size_t size,
    const void **out, doe_dx_error *error);
void doe_dx_native_unmap_read(doe_dx_native_buffer *buffer);
doe_dx_status doe_dx_native_read(doe_dx_native_buffer *buffer, uint64_t offset,
    void *bytes, size_t size, doe_dx_error *error);
doe_dx_status doe_dx_native_pipeline_create(doe_dx_native_context *context,
    const doe_dx_resource_pipeline_desc *desc, doe_dx_native_pipeline **out, doe_dx_error *error);
doe_dx_status doe_dx_native_pipeline_create_dynamic(doe_dx_native_context *context,
    const doe_dx_dynamic_pipeline_desc *desc,doe_dx_native_pipeline **out,doe_dx_error *error);
/* Shared normalized footprint; native SDK query must agree before allocation publishes. */
doe_dx_status doe_dx_texture_layout_2d(const doe_dx_texture_desc *desc,
    doe_dx_texture_layout layout, doe_dx_texture_footprint *out);
doe_dx_status doe_dx_native_format_caps(doe_dx_native_context *,doe_dx_format,doe_dx_format_caps *,doe_dx_error *);
doe_dx_status doe_dx_native_texture_create(doe_dx_native_context *context,
    const doe_dx_texture_shape *shape, doe_dx_native_texture **out, doe_dx_error *error);
uint64_t doe_dx_native_texture_upload_bytes(const doe_dx_native_texture *);
doe_dx_status doe_dx_native_texture_initialize(doe_dx_native_commands *,doe_dx_native_texture *,doe_dx_native_buffer *,doe_dx_error *);
void doe_dx_native_texture_destroy(doe_dx_native_texture *texture);
void doe_dx_native_texture_transfer(doe_dx_native_commands *commands,
    doe_dx_native_buffer *buffer, doe_dx_native_texture *texture,
    const doe_dx_texture_footprint *footprint, bool upload);
void doe_dx_native_texture_region_transfer_ex(doe_dx_native_commands *,doe_dx_native_buffer *,doe_dx_native_texture *,
    const doe_dx_texture_footprint *,doe_dx_texture_region_ex,bool);
void doe_dx_native_texture_blit_ex(doe_dx_native_commands *,doe_dx_native_texture *,doe_dx_texture_region_ex,
    doe_dx_native_texture *,doe_dx_texture_region_ex);
void doe_dx_native_texture_region_transfer(doe_dx_native_commands *commands,
    doe_dx_native_buffer *buffer, doe_dx_native_texture *texture,
    const doe_dx_texture_footprint *footprint, doe_dx_texture_region region, bool upload);
void doe_dx_native_texture_blit(doe_dx_native_commands *commands,
    doe_dx_native_texture *source, doe_dx_texture_region from, doe_dx_native_texture *destination,
    uint32_t destination_x, uint32_t destination_y);
void doe_dx_native_texture_copy(doe_dx_native_commands *commands,
    doe_dx_native_texture *source, doe_dx_native_texture *destination);
doe_dx_status doe_dx_native_render_create(doe_dx_native_context *,const doe_dx_render_pipeline_desc *,
    doe_dx_native_pipeline **,doe_dx_error *);
doe_dx_status doe_dx_native_render_targets(doe_dx_native_commands *,const doe_dx_native_render_color *,uint32_t,
    const doe_dx_native_render_depth *,const doe_dx_native_render_draw *,size_t,doe_dx_error *);
doe_dx_status doe_dx_native_render_resolve(doe_dx_native_commands *,doe_dx_native_texture *,doe_dx_native_texture *,bool,
    const float [4],bool,const doe_dx_native_render_depth *,const doe_dx_native_render_draw *,size_t,doe_dx_error *);
doe_dx_status doe_dx_native_render(doe_dx_native_commands *,doe_dx_native_texture *,bool,
    const float [4],bool,const doe_dx_native_render_depth *,const doe_dx_native_render_draw *,size_t,doe_dx_error *);
doe_dx_status doe_dx_native_graphics_create(doe_dx_native_context *context,
    const doe_dx_mesh_pipeline_desc *desc, doe_dx_native_pipeline **out, doe_dx_error *error);
doe_dx_status doe_dx_native_draw(doe_dx_native_commands *commands,
    doe_dx_native_pipeline *pipeline, const doe_dx_native_resources *resources,
    doe_dx_native_texture *color, const doe_dx_native_geometry *geometry,
    const doe_dx_mesh_draw_desc *draw, doe_dx_error *error);
doe_dx_status doe_dx_native_timestamps_create(doe_dx_native_context *context, doe_dx_query_kind kind,uint32_t count,
    doe_dx_native_timestamps **out, doe_dx_error *error);
void doe_dx_native_timestamps_destroy(doe_dx_native_timestamps *timestamps);
doe_dx_status doe_dx_native_timestamp_frequency(doe_dx_native_context *context,
    uint64_t *out, doe_dx_error *error);
void doe_dx_native_timestamp(doe_dx_native_commands *commands,
    doe_dx_native_timestamps *timestamps, uint32_t index);
void doe_dx_native_resolve_timestamps(doe_dx_native_commands *commands,
    doe_dx_native_timestamps *timestamps, uint32_t first, uint32_t count,
    doe_dx_native_buffer *destination, uint64_t offset);
void doe_dx_native_pipeline_destroy(doe_dx_native_pipeline *pipeline);
doe_dx_status doe_dx_native_commands_create(doe_dx_native_context *context,
    doe_dx_native_commands **out, doe_dx_error *error);
void doe_dx_native_commands_destroy(doe_dx_native_commands *commands);
/* Only the serialized common retirement owner may recycle proven-complete work. */
void doe_dx_native_commands_recycle(doe_dx_native_commands *commands);
void doe_dx_native_copy(doe_dx_native_commands *commands, doe_dx_native_buffer *source,
    uint64_t source_offset, doe_dx_native_buffer *destination,
    uint64_t destination_offset, uint64_t size);
/* Allocation failure is before recording; only successful calls append work. */
doe_dx_status doe_dx_native_dispatch(doe_dx_native_commands *commands,
    doe_dx_native_pipeline *pipeline, const doe_dx_native_resources *resources,
    uint32_t x, uint32_t y, uint32_t z, doe_dx_error *error);
doe_dx_status doe_dx_native_dispatch_indirect(doe_dx_native_commands *commands,
    doe_dx_native_pipeline *pipeline, const doe_dx_native_resources *resources,
    doe_dx_native_buffer *arguments, uint64_t offset, doe_dx_error *error);
doe_dx_status doe_dx_native_finish(doe_dx_native_commands *commands, doe_dx_error *error);
void doe_dx_native_execute(doe_dx_native_context *context, doe_dx_native_commands *commands);
doe_dx_status doe_dx_native_signal(doe_dx_native_context *context, uint64_t value,
    doe_dx_error *error);
/* DEVICE_LOST must mean terminal device removal, not an arbitrary HRESULT. */
doe_dx_status doe_dx_native_completed(doe_dx_native_context *context, uint64_t *out,
    doe_dx_error *error);
doe_dx_status doe_dx_native_progress_close(doe_dx_native_context *context);
doe_dx_status doe_dx_native_progress_hold(doe_dx_native_context *, uint64_t, doe_dx_wait_record **, doe_dx_error *);
doe_dx_status doe_dx_native_progress_block(const doe_dx_wait_record *, uint32_t, doe_dx_error *);
doe_dx_status doe_dx_native_progress_unhold(doe_dx_native_context *, doe_dx_wait_record *, doe_dx_error *);
doe_dx_status doe_dx_native_wait(doe_dx_native_context *context, uint64_t value,
    uint32_t wait_ms, doe_dx_error *error);
#endif
