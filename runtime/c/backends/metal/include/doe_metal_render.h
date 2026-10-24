/* SPDX-License-Identifier: MIT */
#ifndef DOE_METAL_RENDER_H
#define DOE_METAL_RENDER_H
#include "doe_metal_texture.h"
#include "doe_metal_draw_guard.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct doe_mtl_render_pipeline doe_mtl_render_pipeline;
enum { DOE_MTL_COLOR_ATTACHMENTS = 8, DOE_MTL_VERTEX_ATTRIBUTES = 31 };
/* Named native formats consumed by the public frontend. SDK controls bind these
 * identities; shader scalar/default-component policy remains upstream. */
typedef enum doe_mtl_vertex_format {
    DOE_MTL_VERTEX_UCHAR = 45, DOE_MTL_VERTEX_UCHAR2 = 1, DOE_MTL_VERTEX_UCHAR4 = 3,
    DOE_MTL_VERTEX_CHAR = 46, DOE_MTL_VERTEX_CHAR2 = 4, DOE_MTL_VERTEX_CHAR4 = 6,
    DOE_MTL_VERTEX_UCHAR_NORMALIZED = 47, DOE_MTL_VERTEX_UCHAR2_NORMALIZED = 7,
    DOE_MTL_VERTEX_UCHAR4_NORMALIZED = 9,
    DOE_MTL_VERTEX_CHAR_NORMALIZED = 48, DOE_MTL_VERTEX_CHAR2_NORMALIZED = 10,
    DOE_MTL_VERTEX_CHAR4_NORMALIZED = 12,
    DOE_MTL_VERTEX_USHORT = 49, DOE_MTL_VERTEX_USHORT2 = 13, DOE_MTL_VERTEX_USHORT4 = 15,
    DOE_MTL_VERTEX_SHORT = 50, DOE_MTL_VERTEX_SHORT2 = 16, DOE_MTL_VERTEX_SHORT4 = 18,
    DOE_MTL_VERTEX_USHORT_NORMALIZED = 51, DOE_MTL_VERTEX_USHORT2_NORMALIZED = 19,
    DOE_MTL_VERTEX_USHORT4_NORMALIZED = 21,
    DOE_MTL_VERTEX_SHORT_NORMALIZED = 52, DOE_MTL_VERTEX_SHORT2_NORMALIZED = 22,
    DOE_MTL_VERTEX_SHORT4_NORMALIZED = 24,
    DOE_MTL_VERTEX_HALF = 53, DOE_MTL_VERTEX_HALF2 = 25, DOE_MTL_VERTEX_HALF4 = 27,
    DOE_MTL_VERTEX_FLOAT = 28, DOE_MTL_VERTEX_FLOAT2 = 29,
    DOE_MTL_VERTEX_FLOAT3 = 30, DOE_MTL_VERTEX_FLOAT4 = 31,
    DOE_MTL_VERTEX_UINT = 36, DOE_MTL_VERTEX_UINT2 = 37,
    DOE_MTL_VERTEX_UINT3 = 38, DOE_MTL_VERTEX_UINT4 = 39,
    DOE_MTL_VERTEX_INT = 32, DOE_MTL_VERTEX_INT2 = 33,
    DOE_MTL_VERTEX_INT3 = 34, DOE_MTL_VERTEX_INT4 = 35,
    DOE_MTL_VERTEX_UINT1010102_NORMALIZED = 41,
    DOE_MTL_VERTEX_UCHAR4_NORMALIZED_BGRA = 42
} doe_mtl_vertex_format;
typedef struct doe_mtl_shader_stage {
    const char *msl;
    size_t msl_length;
    const char *entrypoint; /* Explicit compiler-resolved symbol, no fallback. */
    uint32_t sizes_slot; /* UINT32_MAX disables the compiler's buffer-size table. */
} doe_mtl_shader_stage;
typedef struct doe_mtl_blend_component {
    uint32_t operation; /* Native add/subtract/reverse/min/max = 0..4. */
    uint32_t source, destination; /* Native factors 0..14; dual-source unsupported. */
} doe_mtl_blend_component;
typedef struct doe_mtl_color_target {
    doe_mtl_format format; /* Zero is an unoccupied color slot. */
    uint32_t write_mask; /* Native R=8,G=4,B=2,A=1, not WebGPU's bit order. */
    bool blend;
    doe_mtl_blend_component color, alpha;
} doe_mtl_color_target;
typedef struct doe_mtl_vertex_layout {
    uint32_t slot, stride;
    uint32_t step; /* Native constant=0, per-vertex=1, per-instance=2; rate=1. */
} doe_mtl_vertex_layout;
typedef struct doe_mtl_vertex_attribute {
    uint32_t location, slot, offset;
    uint32_t format; /* Explicit native MTLVertexFormat, layout-checked by owner. */
} doe_mtl_vertex_attribute;
typedef struct doe_mtl_stencil_face {
    uint32_t compare, fail, depth_fail, pass; /* Native compare/operation enums 0..7. */
} doe_mtl_stencil_face;
typedef struct doe_mtl_depth_stencil {
    doe_mtl_format depth_format; /* Zero, pure depth or combined depth/stencil. */
    doe_mtl_format stencil_format; /* Zero, Stencil8 or combined depth/stencil. */
    uint32_t depth_compare;
    bool depth_write;
    doe_mtl_stencil_face front, back;
    uint32_t stencil_read_mask, stencil_write_mask;
} doe_mtl_depth_stencil;
typedef struct doe_mtl_render_pipeline_desc {
    doe_mtl_shader_stage vertex, fragment; /* Fragment may be absent for depth-only. */
    doe_mtl_color_target colors[DOE_MTL_COLOR_ATTACHMENTS];
    uint32_t color_count;
    const doe_mtl_vertex_layout *layouts;
    size_t layout_count;
    const doe_mtl_vertex_attribute *attributes;
    size_t attribute_count;
    doe_mtl_depth_stencil depth_stencil;
    uint32_t sample_count; /* Explicit device-admitted sample count. */
    bool alpha_to_coverage; /* Native pipeline state; requires MSAA + color/fragment. */
    bool override_sample_mask;
    uint32_t sample_mask; /* Only all-active-samples masks are admitted here. */
} doe_mtl_render_pipeline_desc;
doe_mtl_status doe_mtl_render_pipeline_create(doe_mtl_context *context,
    const doe_mtl_render_pipeline_desc *desc, doe_mtl_render_pipeline **out, doe_mtl_error *error);
doe_mtl_status doe_mtl_render_pipeline_retain(doe_mtl_render_pipeline *pipeline);
void doe_mtl_render_pipeline_release(doe_mtl_render_pipeline *pipeline);
void *doe_mtl_render_pipeline_native(const doe_mtl_render_pipeline *pipeline);

typedef enum doe_mtl_load_action { DOE_MTL_LOAD_DISCARD, DOE_MTL_LOAD, DOE_MTL_CLEAR } doe_mtl_load_action;
typedef enum doe_mtl_store_action {
    DOE_MTL_STORE_DISCARD, DOE_MTL_STORE, DOE_MTL_RESOLVE, DOE_MTL_STORE_RESOLVE
} doe_mtl_store_action;
typedef struct doe_mtl_clear_color { double red, green, blue, alpha; } doe_mtl_clear_color;
typedef struct doe_mtl_render_attachment {
    doe_mtl_texture_view *view; /* Exactly one mip and one 2D/array/cube layer. */
    doe_mtl_load_action load;
    doe_mtl_store_action store;
    doe_mtl_clear_color clear_color;
    double clear_depth;
    uint32_t clear_stencil;
    doe_mtl_texture_view *resolve_view; /* Required only for resolve store actions. */
    uint32_t resolve_filter; /* Depth sample0/min/max=0/1/2; stencil sample0/depth=0/1. */
} doe_mtl_render_attachment;
typedef struct doe_mtl_render_pass_desc {
    doe_mtl_render_attachment colors[DOE_MTL_COLOR_ATTACHMENTS];
    uint32_t color_count;
    doe_mtl_render_attachment depth, stencil;
    doe_mtl_buffer *visibility_buffer; /* Optional initialized result storage; uint64 results. */
} doe_mtl_render_pass_desc;
typedef struct doe_mtl_render_bindings {
    const doe_mtl_buffer_binding *buffers;
    size_t buffer_count;
    const doe_mtl_texture_binding *textures;
    size_t texture_count;
    const doe_mtl_sampler_binding *samplers;
    size_t sampler_count;
} doe_mtl_render_bindings;
typedef struct doe_mtl_viewport { double x, y, width, height, min_depth, max_depth; } doe_mtl_viewport;
typedef struct doe_mtl_scissor { uint32_t x, y, width, height; } doe_mtl_scissor;
typedef struct doe_mtl_render_draw {
    doe_mtl_render_pipeline *pipeline;
    doe_mtl_render_bindings vertex, fragment; /* Vertex input buffers use this vertex slot space. */
    uint32_t topology; /* Native point/line/line-strip/triangle/triangle-strip = 0..4. */
    uint32_t count, instance_count, first_vertex, first_instance;
    doe_mtl_buffer *index_buffer; /* NULL selects a nonindexed draw. */
    uint64_t index_offset;
    uint32_t index_type; /* Native uint16=0, uint32=1. */
    int32_t base_vertex;
    bool viewport_enabled, scissor_enabled;
    doe_mtl_viewport viewport;
    doe_mtl_scissor scissor;
    uint32_t winding, cull; /* Native clockwise=0/counterclockwise=1; none/front/back=0/1/2. */
    float blend_constant[4];
    float depth_bias, depth_slope, depth_clamp;
    uint32_t stencil_reference;
    uint32_t visibility_mode; /* Native disabled=0, boolean=1, counting=2. */
    uint64_t visibility_offset; /* 8-byte aligned; each offset occupies one contiguous run. */
    const doe_mtl_guarded_draw *guarded; /* Copies/normalizes/scans on this command before the pass. */
} doe_mtl_render_draw;
/* Encodes one whole pass, including clear-only passes. All inputs are borrowed
 * only for this call; resources/pipelines are leased before native encoding and
 * remain under the ordinary command completion owner. No open encoder escapes.
 * Loads require initialized contents; discard never establishes initialized
 * output. Clears initialize whole attachment subresources only when stored.
 * Visibility result reset/aggregation and query-set policy remain upstream;
 * the result buffer shares ordinary pending/error access restrictions. No CPU
 * visibility value is synthesized, including for empty draw ranges.
 * Legacy direct indexed vertex-fetch proofs remain upstream. A typed guarded
 * draw instead runs the shared GPU normalizer/index scan on command-owned scratch
 * before native rendering. Shader interface/access admission remains upstream;
 * no caller arguments/indices are read by the host. */
doe_mtl_status doe_mtl_commands_render(doe_mtl_commands *commands,
    const doe_mtl_render_pass_desc *pass, const doe_mtl_render_draw *draws,
    size_t draw_count, doe_mtl_error *error);
/* Clear only uninitialized aspects in the selected 2D/array/cube/MS mip/layer
 * range, using the same speculative command state as rendering and transfers.
 * Initialized aspects are loaded/stored unchanged. All views/preflight complete
 * before the first clear encoder; failure poisons this recording. Unknown work
 * retains its leases and only successful completion publishes initialization. */
doe_mtl_status doe_mtl_commands_initialize_attachment(doe_mtl_commands *commands,
    doe_mtl_texture_view *view, doe_mtl_error *error);
#ifdef __cplusplus
}
#endif
#endif
