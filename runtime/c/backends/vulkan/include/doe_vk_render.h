/* SPDX-License-Identifier: MIT */
#ifndef DOE_VK_RENDER_H
#define DOE_VK_RENDER_H
#include "doe_vk_bindings.h"
#include "doe_vk_queries.h"
#ifdef __cplusplus
extern "C" {
#endif

enum { DOE_VK_MAX_VERTEX_BUFFERS = 8, DOE_VK_MAX_VERTEX_ATTRIBUTES = 16 };
typedef struct doe_vk_shader {
    const uint32_t *words;
    size_t word_count;
    const char *entrypoint;
} doe_vk_shader;
/* Native named formats from pinned vk_formats.zig; never WebGPU enum casts. */
typedef enum doe_vk_vertex_format {
    DOE_VK_VERTEX_UINT32 = 98, DOE_VK_VERTEX_SINT32 = 99, DOE_VK_VERTEX_FLOAT32 = 100,
    DOE_VK_VERTEX_UINT32X2 = 101, DOE_VK_VERTEX_SINT32X2 = 102, DOE_VK_VERTEX_FLOAT32X2 = 103,
    DOE_VK_VERTEX_UINT32X3 = 104, DOE_VK_VERTEX_SINT32X3 = 105, DOE_VK_VERTEX_FLOAT32X3 = 106,
    DOE_VK_VERTEX_UINT32X4 = 107, DOE_VK_VERTEX_SINT32X4 = 108, DOE_VK_VERTEX_FLOAT32X4 = 109
} doe_vk_vertex_format;
typedef struct doe_vk_vertex_layout {
    uint32_t stride;
    bool per_instance;
} doe_vk_vertex_layout;
typedef struct doe_vk_vertex_attribute {
    uint32_t location, slot, offset;
    doe_vk_vertex_format format;
} doe_vk_vertex_attribute;
typedef enum doe_vk_topology {
    DOE_VK_POINTS, DOE_VK_LINES, DOE_VK_LINE_STRIP, DOE_VK_TRIANGLES, DOE_VK_TRIANGLE_STRIP
} doe_vk_topology;
/* Native compare operations NEVER0..ALWAYS7 and stencil KEEP0..DECREMENT_WRAP7. */
typedef struct doe_vk_stencil_face {
    uint32_t fail_operation, pass_operation, depth_fail_operation, compare_operation;
} doe_vk_stencil_face;
typedef struct doe_vk_render_pipeline_desc {
    doe_vk_shader vertex, fragment;
    doe_vk_texture_format color_format;
    doe_vk_topology topology;
    uint32_t cull_mask; /* Native front=1, back=2. */
    bool clockwise_front;
    uint32_t color_write_mask; /* RGBA low four bits. */
    bool blend_enabled;
    float blend_constant[4];
    uint32_t color_source_factor, color_destination_factor, color_operation;
    uint32_t alpha_source_factor, alpha_destination_factor, alpha_operation;
    size_t vertex_buffer_count, attribute_count;
    const doe_vk_vertex_layout *vertex_buffers;
    const doe_vk_vertex_attribute *attributes;
    size_t binding_count;
    const doe_vk_binding_layout *bindings;
    doe_vk_texture_format depth_stencil_format; /* Zero means absent. */
    bool depth_test, depth_write, stencil_test;
    uint32_t depth_compare;
    doe_vk_stencil_face stencil_front, stencil_back;
    uint32_t stencil_read_mask, stencil_write_mask;
    uint32_t sample_count; /* Zero preserves sample1. */
    bool override_sample_mask;
    uint64_t sample_mask;
    bool alpha_to_coverage;
} doe_vk_render_pipeline_desc;
/* Zero/one-color graphics pipeline with format-admitted native sample counts. Compiler/native admission validates
 * shader interfaces/capabilities including disabled vertex/fragment stores.
 * Native blend factors0..14 (no dual source), operations0..4; constant blend
 * factors use the pipeline constant unless an explicit per-draw override is set.
 * Optional depth/stencil uses the same sample count. Sample shading stays disabled.
 * color_format=0 requires depth/stencil, disabled blend/alpha-to-coverage and zero
 * color_write_mask. Only then may fragment be entirely zero (no native stage). */
doe_vk_status doe_vk_render_pipeline_create(doe_vk_context *context,
    const doe_vk_render_pipeline_desc *desc, doe_vk_pipeline **out, doe_vk_error *error);

typedef union doe_vk_clear_color {
    float float32[4];
    uint32_t uint32[4];
    int32_t int32[4];
} doe_vk_clear_color;
typedef enum doe_vk_viewport_convention {
    DOE_VK_VIEWPORT_RAW_VULKAN = 0,
    DOE_VK_VIEWPORT_WEBGPU = 1
} doe_vk_viewport_convention;
typedef struct doe_vk_render_draw {
    uint32_t vertex_count, instance_count, first_vertex, first_instance;
    /* Logical positive-or-zero extent; negative origins are legal within actual
     * device bounds. Zero extent preserves the draw with an empty native scissor. */
    float viewport[6]; /* x,y,width,height,min/max depth; all explicit. */
    uint32_t scissor[4]; /* x,y,width,height within the attachment. */
    /* Raw preserves retained positive-height Vulkan mapping. WebGPU keeps
     * source Position unchanged and uses y+height/-height once at the viewport.
     * Scissor and fragment framebuffer coordinates stay top-left. Pipeline
     * clockwise_front remains the explicit native framebuffer winding choice. */
    doe_vk_viewport_convention viewport_convention;
    /* False preserves the pipeline constant for existing callers. True uses
     * this finite RGBA value as real dynamic state before this draw. */
    bool override_blend_constant;
    float blend_constant[4];
    uint32_t stencil_reference;
} doe_vk_render_draw;
typedef struct doe_vk_draw_validator doe_vk_draw_validator;
typedef struct doe_vk_render_indirect {
    doe_vk_draw_validator *validator;
    doe_vk_buffer *arguments;
    uint64_t offset;
} doe_vk_render_indirect;
/* Native VkIndexType values from pinned vk_constants.zig; never WebGPU enum casts. */
typedef enum doe_vk_index_format {
    DOE_VK_INDEX_UINT16 = 0,
    DOE_VK_INDEX_UINT32 = 1
} doe_vk_index_format;
/* Borrowed for the call. The buffer needs native INDEX usage; offset is aligned
 * to the format size and [offset, offset+size) is the exact bound index range.
 * Native binding has no size, so this range is enforced by direct admission and
 * by the GPU guard for indirect draws. Direct counts are ignored with indirect.
 * UINT32 requires caps.full_draw_index_uint32. Vertex-step fetches by GPU index
 * values rely on enabled robustBufferAccess, not on host index inspection. */
typedef struct doe_vk_render_indices {
    doe_vk_buffer *buffer;
    uint64_t offset, size;
    doe_vk_index_format format;
    uint32_t index_count, first_index;
    int32_t base_vertex;
} doe_vk_render_indices;
typedef struct doe_vk_render_pass_draw {
    doe_vk_pipeline *pipeline;
    const doe_vk_binding_resource *bindings;
    size_t binding_count;
    /* Zero NULL entries are allowed only for slots without attributes. */
    const doe_vk_buffer_binding *vertices;
    size_t vertex_buffer_count;
    doe_vk_render_draw draw;
    /* NULL uses direct counts. Otherwise count/first fields are ignored and
     * copied GPU arguments are guarded against these exact vertex bindings.
     * Arguments require native INDIRECT|COPY_SRC, aligned offset and16 bytes
     * (20 bytes for indexed draws).
     * Nonzero firstInstance becomes no-op: that optional feature is disabled.
     * Each draw owns distinct scratch through retirement; no in-pass compute. */
    const doe_vk_render_indirect *indirect;
    /* NULL is nonindexed. Otherwise draw.vertex_count/first_vertex must be zero;
     * instance_count/first_instance keep their meaning. Indirect indexed draws
     * copy 20 argument bytes and guard firstIndex+indexCount against this range. */
    const doe_vk_render_indices *indices;
} doe_vk_render_pass_draw;
typedef enum doe_vk_render_operation {
    DOE_VK_RENDER_DRAW,
    DOE_VK_RENDER_BEGIN_OCCLUSION,
    DOE_VK_RENDER_END_OCCLUSION
} doe_vk_render_operation;
typedef struct doe_vk_render_pass_op {
    doe_vk_render_operation kind;
    union {
        doe_vk_render_pass_draw draw;
        uint32_t query_index; /* BEGIN only; END closes the active index. */
    } value;
} doe_vk_render_pass_op;
typedef struct doe_vk_render_pass_desc {
    doe_vk_texture_view *color; /* May be NULL for a depth/stencil-only pass. */
    const doe_vk_clear_color *clear_color; /* NULL loads initialized color. */
    bool discard_color;
    doe_vk_texture_view *depth_stencil;
    bool clear_depth, clear_stencil; /* False loads that existing aspect. */
    bool discard_depth, discard_stencil; /* False stores. */
    bool depth_read_only, stencil_read_only;
    float depth_clear_value;
    uint32_t stencil_clear_value;
    doe_vk_texture_view *resolve_target; /* Optional single-sample color destination. */
} doe_vk_render_pass_desc;
/* General attachment owner, one native pass for all ops. Only existing aspects
 * participate. Read-only forbids clear/discard and any draw that may write that
 * aspect. LOAD requires initialized aspect; STORE preserves it; discard removes
 * it. All borrowed inputs die at return. Failure poisons commands; no partial
 * pass may be submitted. Samples must match the pipeline and depth/stencil.
 * A depth/stencil-only draw requires a matching color_format=0 pipeline. */
doe_vk_status doe_vk_commands_render_pass_full(doe_vk_commands *commands,
    const doe_vk_render_pass_desc *desc, doe_vk_query_pool *occlusion,
    const doe_vk_render_pass_op *ops, size_t count);
/* Explicit public-initialization primitive, not CPU shadow availability. Clears
 * only undefined aspects of every selected mip/layer to zero using this same
 * native pass owner. Initialized aspects are preserved. Supports 2D image views
 * (including array/cube selections); no-op when all selected aspects are valid.
 * Use before exposing new attachments and before public LOAD after discard.
 * Raw full-pass LOAD stays strict. Failure poisons commands; leases retain work. */
doe_vk_status doe_vk_commands_initialize_attachment(doe_vk_commands *commands,
    doe_vk_texture_view *view);
/* General typed pass span. Occlusion events require the one supplied occlusion
 * pool. Nesting, unclosed events and duplicate indices within a pass reject
 * before native begin. Written slots are reset before begin, then native query
 * begin/end brackets the actual draws. Results are real zero/nonzero values,
 * not precise sample counts. A later pass may reuse an index. NULL pool is valid
 * only for draw-only/empty spans. Borrowing and failure rules match below. */
doe_vk_status doe_vk_commands_render_pass_ops(doe_vk_commands *commands,
    doe_vk_texture_view *target, const doe_vk_clear_color *clear,
    doe_vk_query_pool *occlusion, const doe_vk_render_pass_op *ops, size_t count);
/* One real single-color native pass for the entire borrowed draw span. NULL
 * clear means LOAD of initialized contents; non-NULL clears once at begin.
 * All structural draw checks precede resource preparation; all descriptor/image
 * admission, barriers and image transitions finish before native begin. No caller data survives
 * the call; existing command leases retain each used native resource. On any
 * error discard the poisoned command owner, never submit a partial pass.
 * This consumes immutable resource packets, not a second recording engine.
 * This convenience has no depth/stencil or resolve. Indexed and indirect draws
 * use the same draw records through every pass entry point. */
doe_vk_status doe_vk_commands_render_pass(doe_vk_commands *commands,
    doe_vk_texture_view *target, const doe_vk_clear_color *clear,
    const doe_vk_render_pass_draw *draws, size_t count);
/* Convenience operations delegate to the same pass owner. A clear-only begin
 * initializes one full 2D view subresource; a later single draw LOADs it.
 * These conveniences always store; the full descriptor owns explicit discard. */
doe_vk_status doe_vk_commands_clear_color(doe_vk_commands *commands,
    doe_vk_texture_view *target, doe_vk_clear_color clear);
doe_vk_status doe_vk_commands_draw(doe_vk_commands *commands,
    doe_vk_pipeline *pipeline, doe_vk_texture_view *target,
    const doe_vk_binding_resource *bindings, size_t binding_count,
    const doe_vk_buffer_binding *vertices, size_t vertex_count,
    const doe_vk_render_draw *draw);
#ifdef __cplusplus
}
#endif
#endif
