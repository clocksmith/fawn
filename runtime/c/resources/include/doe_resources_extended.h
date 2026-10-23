/* SPDX-License-Identifier: MIT */
#ifndef DOE_RESOURCES_EXTENDED_H
#define DOE_RESOURCES_EXTENDED_H
#include "doe_resources.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct doe_resource_render_pipeline doe_resource_render_pipeline;
typedef struct doe_resource_render_pass doe_resource_render_pass;

/* Canonical allocation-free WebGPU descriptor admission. Feature enablement
 * remains with the device owner. Zero queries need no native pool; nonzero
 * queries require an adopted native payload. */
enum { DOE_RESOURCE_MAX_QUERIES = 4096 };
doe_resource_status doe_resource_query_set_validate(WGPUQueryType type, uint32_t count);
doe_resource_status doe_resource_query_set_create(const doe_resource_config *config,
    WGPUQueryType type, uint32_t count, doe_resource_native native,
    doe_resource_query_set **out);
void doe_resource_query_set_retain(doe_resource_query_set *set);
void doe_resource_query_set_release(doe_resource_query_set *set);
void doe_resource_query_set_destroy(doe_resource_query_set *set);
void *doe_resource_query_set_native(const doe_resource_query_set *set);
uint32_t doe_resource_query_set_count(const doe_resource_query_set *set);
/* NULL type is zero (not a valid pinned query type); device is borrowed. */
WGPUQueryType doe_resource_query_set_type(const doe_resource_query_set *set);
void *doe_resource_query_set_device(const doe_resource_query_set *set);
typedef enum doe_resource_timestamp_position {
    DOE_TIMESTAMP_COMMAND, DOE_TIMESTAMP_PASS_BEGIN, DOE_TIMESTAMP_PASS_END
} doe_resource_timestamp_position;
/* UINT32_MAX omits that write. Feature admission remains with the backend.
 * The pass retains its end-write pool even if caller references are released. */
typedef struct doe_resource_pass_timestamps {
    doe_resource_query_set *set;
    uint32_t begin_index, end_index;
} doe_resource_pass_timestamps;
doe_resource_status doe_resource_encoder_begin_compute_with_timestamps(doe_resource_encoder *encoder,
    const doe_resource_pass_timestamps *timestamps, doe_resource_compute_pass **out);

doe_resource_status doe_resource_encoder_write_timestamp(doe_resource_encoder *encoder,
    doe_resource_query_set *set, uint32_t index, doe_resource_timestamp_position position);
doe_resource_status doe_resource_encoder_resolve_queries(doe_resource_encoder *encoder,
    doe_resource_query_set *set, uint32_t first, uint32_t count,
    doe_resource_buffer *destination, uint64_t offset);

/* Backend import normalizes native planes into existing texture/view owners,
 * not raw pointer offsets or a second native-release path. Device lease retains
 * the originating device/instance chain. Plane contents are never CPU emulated. */
doe_resource_status doe_resource_external_texture_create(const doe_resource_config *config,
    doe_resource_view *plane0, doe_resource_view *plane1, doe_resource_external_texture **out);
void doe_resource_external_texture_retain(doe_resource_external_texture *texture);
void doe_resource_external_texture_release(doe_resource_external_texture *texture);
void doe_resource_external_texture_expire(doe_resource_external_texture *texture);
doe_resource_status doe_resource_external_texture_refresh(doe_resource_external_texture *texture);
/* Borrows retained planes, even when expired, for diagnostics only. */
doe_resource_view *doe_resource_external_texture_plane(const doe_resource_external_texture *texture,
    size_t plane);

/* One optional 2D color attachment and optional depth/stencil, at least one
 * required. Eight vertex buffers and four groups. Native support is separate. */
enum { DOE_RESOURCE_MAX_VERTEX_BUFFERS = 8 };
/* Undefined step mode plus last_attribute_end0 denotes a NULL slot, distinct
 * from an explicit Vertex/Instance slot with no attributes. Nonempty Undefined
 * defaults to Vertex. Pipeline construction normalizes actual holes to zero. */
typedef struct doe_resource_vertex_layout {
    uint64_t stride, last_attribute_end;
    WGPUVertexStepMode step_mode;
} doe_resource_vertex_layout;
typedef struct doe_resource_render_pipeline_desc {
    WGPUTextureFormat color_format, depth_stencil_format;
    uint32_t samples;
    bool depth_write, stencil_write;
    size_t vertex_count;
    doe_resource_vertex_layout vertices[DOE_RESOURCE_MAX_VERTEX_BUFFERS];
    WGPUPrimitiveTopology topology; /* Undefined defaults to TriangleList. */
    WGPUIndexFormat strip_index_format; /* Only meaningful for strip topologies. */
} doe_resource_render_pipeline_desc;
typedef struct doe_resource_render_bundle_desc {
    WGPUTextureFormat color_format, depth_stencil_format;
    uint32_t samples;
    bool depth_read_only, stencil_read_only;
} doe_resource_render_bundle_desc;

typedef struct doe_resource_render_targets {
    doe_resource_view *color, *depth_stencil;
    WGPULoadOp color_load, depth_load, stencil_load;
    WGPUStoreOp color_store, depth_store, stencil_store;
    double clear_color[4];
    float clear_depth;
    uint32_t clear_stencil;
    bool depth_read_only, stencil_read_only;
    doe_resource_query_set *occlusion_queries;
    uint64_t max_draw_count;
    /* Optional single-sample destination. Packets and whole-pass usage retain
     * it; the backend resolves once at the pass end, including empty passes. */
    doe_resource_view *resolve_target;
    /* False omits max_draw_count and ignores its value. True uses the exact
     * uint64 budget, including zero. Begin stores a normalized present value. */
    bool has_max_draw_count;
} doe_resource_render_targets;
typedef struct doe_resource_render_state {
    float viewport[6]; /* x, y, width, height, minimum depth, maximum depth */
    uint32_t scissor[4]; /* x, y, width, height */
    double blend[4];
    uint32_t stencil_reference;
} doe_resource_render_state;
typedef struct doe_resource_vertex_binding {
    doe_resource_buffer *buffer;
    uint64_t offset, size;
} doe_resource_vertex_binding;
typedef struct doe_resource_render_packet {
    doe_resource_render_targets targets;
    doe_resource_render_pipeline *pipeline;
    doe_resource_bind_group *groups[DOE_RESOURCE_MAX_GROUPS];
    doe_resource_dispatch_offsets offsets;
    doe_resource_vertex_binding vertices[DOE_RESOURCE_MAX_VERTEX_BUFFERS];
    doe_resource_vertex_binding index;
    WGPUIndexFormat index_format;
    doe_resource_buffer *indirect;
    uint64_t indirect_offset;
    doe_resource_render_state state;
    /* Each pass has explicit begin/end boundary-only packets bracketing query
     * events and draw packets. Load/clear only at first, store only at last. */
    bool boundary_only, first_in_pass, last_in_pass, indexed, occlusion_active;
    uint32_t count, instances, first, first_instance, occlusion_index;
    int32_t base_vertex;
} doe_resource_render_packet;

doe_resource_status doe_resource_render_pipeline_create(const doe_resource_config *config,
    doe_resource_pipeline_layout *layout, const doe_resource_render_pipeline_desc *desc,
    doe_resource_native native, doe_resource_render_pipeline **out);
void doe_resource_render_pipeline_retain(doe_resource_render_pipeline *pipeline);
void doe_resource_render_pipeline_release(doe_resource_render_pipeline *pipeline);
void *doe_resource_render_pipeline_native(const doe_resource_render_pipeline *pipeline);
/* Borrowed identity/layout/description remain valid while the object is held. */
void *doe_resource_render_pipeline_device(const doe_resource_render_pipeline *pipeline);
doe_resource_pipeline_layout *doe_resource_render_pipeline_get_layout(const doe_resource_render_pipeline *pipeline);
const doe_resource_render_pipeline_desc *doe_resource_render_pipeline_description(const doe_resource_render_pipeline *pipeline);
/* Canonical NULL-hole predicate; explicit empty layouts still require a buffer. */
bool doe_resource_render_pipeline_vertex_required(const doe_resource_render_pipeline *pipeline, size_t slot);
void *doe_resource_render_pass_device(const doe_resource_render_pass *pass);
doe_resource_status doe_resource_encoder_begin_render(doe_resource_encoder *encoder,
    const doe_resource_render_targets *targets, doe_resource_render_pass **out);
doe_resource_status doe_resource_encoder_begin_render_with_timestamps(doe_resource_encoder *encoder,
    const doe_resource_render_targets *targets, const doe_resource_pass_timestamps *timestamps,
    doe_resource_render_pass **out);
/* Frontend preflight rejection poisons the canonical active recording owner;
 * non-rejection statuses are invalid arguments and do not mutate state. */
doe_resource_status doe_resource_render_pass_invalidate(doe_resource_render_pass *pass,
    doe_resource_status rejection);
/* Pass owns an encoder lease. Releasing an unended pass poisons that encoder. */
void doe_resource_render_pass_retain(doe_resource_render_pass *pass);
void doe_resource_render_pass_release(doe_resource_render_pass *pass);
doe_resource_status doe_resource_render_pass_set_pipeline(doe_resource_render_pass *pass,
    doe_resource_render_pipeline *pipeline);
doe_resource_status doe_resource_render_pass_set_group(doe_resource_render_pass *pass,
    uint32_t index, const doe_resource_dispatch_group *group);
/* NULL unbinds only with offset0 and size0/UINT64_MAX; historical usage remains. */
doe_resource_status doe_resource_render_pass_set_vertex(doe_resource_render_pass *pass,
    uint32_t slot, doe_resource_buffer *buffer, uint64_t offset, uint64_t size);
doe_resource_status doe_resource_render_pass_set_index(doe_resource_render_pass *pass,
    doe_resource_buffer *buffer, WGPUIndexFormat format, uint64_t offset, uint64_t size);
const doe_resource_render_state *doe_resource_render_pass_state(const doe_resource_render_pass *pass);
doe_resource_status doe_resource_render_pass_set_state(doe_resource_render_pass *pass,
    const doe_resource_render_state *state);
doe_resource_status doe_resource_render_pass_draw(doe_resource_render_pass *pass,
    uint32_t vertex_count, uint32_t instances, uint32_t first_vertex, uint32_t first_instance);
doe_resource_status doe_resource_render_pass_draw_indexed(doe_resource_render_pass *pass,
    uint32_t index_count, uint32_t instances, uint32_t first_index,
    int32_t base_vertex, uint32_t first_instance);
/* Indirect records retain arguments; native execution owns data-dependent
 * bounds, first-instance feature admission and robust vertex/index accesses. */
doe_resource_status doe_resource_render_pass_draw_indirect(doe_resource_render_pass *pass,
    doe_resource_buffer *arguments, uint64_t offset, bool indexed);
doe_resource_status doe_resource_render_pass_begin_occlusion(doe_resource_render_pass *pass, uint32_t index);
doe_resource_status doe_resource_render_pass_end_occlusion(doe_resource_render_pass *pass);
doe_resource_status doe_resource_render_pass_debug(doe_resource_render_pass *pass,
    doe_resource_debug_operation operation);
doe_resource_status doe_resource_render_pass_end(doe_resource_render_pass *pass);
const doe_resource_render_packet *doe_resource_render_record_borrow(const doe_resource_render_record *record);

/* Bundles contain self-contained draw snapshots, not synthetic attachments.
 * At most 4096 successful setter/draw operations, matching retained command budget.
 * Finish moves storage. Replay expands through the same pass recording owner;
 * caller pipeline/group/vertex/index bindings are cleared even for zero bundles;
 * viewport/scissor/blend/stencil and attachment/query state remain unchanged. */
typedef struct doe_resource_bundle_encoder doe_resource_bundle_encoder;
typedef struct doe_resource_render_bundle doe_resource_render_bundle;
doe_resource_status doe_resource_bundle_encoder_invalidate(doe_resource_bundle_encoder *encoder,
    doe_resource_status rejection);
doe_resource_status doe_resource_bundle_encoder_create(const doe_resource_config *config,
    const doe_resource_render_bundle_desc *desc, doe_resource_bundle_encoder **out);
/* Borrowed getters, NULL for a NULL handle. */
void *doe_resource_bundle_encoder_device(const doe_resource_bundle_encoder *encoder);
const doe_resource_render_bundle_desc *doe_resource_bundle_encoder_description(const doe_resource_bundle_encoder *encoder);
void doe_resource_bundle_encoder_retain(doe_resource_bundle_encoder *encoder);
void doe_resource_bundle_encoder_release(doe_resource_bundle_encoder *encoder);
doe_resource_status doe_resource_bundle_encoder_set_pipeline(doe_resource_bundle_encoder *encoder,
    doe_resource_render_pipeline *pipeline);
doe_resource_status doe_resource_bundle_encoder_set_group(doe_resource_bundle_encoder *encoder,
    uint32_t index, const doe_resource_dispatch_group *group);
doe_resource_status doe_resource_bundle_encoder_set_vertex(doe_resource_bundle_encoder *encoder,
    uint32_t slot, doe_resource_buffer *buffer, uint64_t offset, uint64_t size);
doe_resource_status doe_resource_bundle_encoder_set_index(doe_resource_bundle_encoder *encoder,
    doe_resource_buffer *buffer, WGPUIndexFormat format, uint64_t offset, uint64_t size);
doe_resource_status doe_resource_bundle_encoder_draw(doe_resource_bundle_encoder *encoder,
    bool indexed, uint32_t count, uint32_t instances, uint32_t first,
    int32_t base_vertex, uint32_t first_instance);
doe_resource_status doe_resource_bundle_encoder_draw_indirect(doe_resource_bundle_encoder *encoder,
    doe_resource_buffer *arguments, uint64_t offset, bool indexed);
doe_resource_status doe_resource_bundle_encoder_debug(doe_resource_bundle_encoder *encoder,
    doe_resource_debug_operation operation);
doe_resource_status doe_resource_bundle_encoder_finish(doe_resource_bundle_encoder *encoder,
    doe_resource_render_bundle **out);
void *doe_resource_render_bundle_device(const doe_resource_render_bundle *bundle);
const doe_resource_render_bundle_desc *doe_resource_render_bundle_description(const doe_resource_render_bundle *bundle);
void doe_resource_render_bundle_retain(doe_resource_render_bundle *bundle);
void doe_resource_render_bundle_release(doe_resource_render_bundle *bundle);
doe_resource_status doe_resource_render_pass_execute_bundles(doe_resource_render_pass *pass,
    doe_resource_render_bundle *const *bundles, size_t count);

#ifdef __cplusplus
}
#endif
#endif
