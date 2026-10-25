/* SPDX-License-Identifier: MIT */
#ifndef DOE_RESOURCES_INTERNAL_H
#define DOE_RESOURCES_INTERNAL_H
#include "doe_resources_extended.h"
#include <stdatomic.h>

typedef struct resource_base {
    atomic_uint references;
    doe_resource_config config;
    doe_resource_native native;
} resource_base;

struct doe_resource_shader { resource_base base; };

struct doe_resource_buffer {
    resource_base base;
    doe_resource_buffer_desc desc;
    bool destroyed, mapped, map_pending;
    uint64_t generation, map_offset, map_size;
    WGPUMapMode map_mode;
    unsigned char *mapped_bytes;
};
struct doe_resource_texture {
    resource_base base;
    doe_resource_texture_desc desc;
    bool destroyed;
    WGPUTextureFormat view_formats[2];
};
struct doe_resource_view {
    resource_base base;
    doe_resource_texture *texture;
    doe_resource_view_desc desc;
};
struct doe_resource_sampler {
    resource_base base;
    WGPUSamplerDescriptor desc;
};
struct doe_resource_layout {
    resource_base base;
    size_t count;
    bool automatic, automatic_claimed;
    /* Claimed automatic layouts name their pipeline's first claimed group;
     * every other group of that pipeline retains it (WebGPU exclusivePipeline). */
    doe_resource_layout *exclusive;
    size_t dynamic_count;
    doe_resource_layout_entry entries[]; /* count entries, ascending binding. */
};
struct doe_resource_pipeline_layout {
    resource_base base;
    uint32_t immediate_size;
    size_t count;
    doe_resource_layout *groups[DOE_RESOURCE_MAX_GROUPS];
    doe_resource_layout *empty; /* Lazily materialized owner for absent/trailing getters. */
};
struct doe_resource_bind_group {
    resource_base base;
    doe_resource_layout *layout;
    doe_resource_binding_limits limits;
    doe_resource_binding_features features;
    size_t count;
    doe_resource_binding bindings[]; /* count bindings in layout entry order. */
};
struct doe_resource_pipeline {
    resource_base base;
    doe_resource_pipeline_layout *layout;
    uint32_t workgroup[3];
    uint64_t immediate_required_bytes;
    doe_resource_binding_minimum *minimums;
    size_t minimum_count;
};
doe_resource_status resource_pipeline_minimums_check(const doe_resource_pipeline *pipeline,
    const doe_resource_dispatch_group *groups, size_t group_count);
/* Bind groups a compute pass set, whether or not a dispatch used them: every
 * setBindGroup is part of the command buffer's usage, so submission checks
 * them (a destroyed buffer or texture fails it). Each is retained once. */
typedef struct {
    doe_resource_bind_group **items;
    size_t count, capacity;
} resource_group_references;
doe_resource_status resource_encoder_reference_group(doe_resource_encoder *encoder, doe_resource_bind_group *group);

struct doe_resource_encoder {
    resource_base base;
    doe_resource_status status;
    bool finished;
    size_t debug_depth;
    void *active_pass; /* Exact active pass identity, not backend storage. */
    size_t count, capacity;
    doe_resource_command *commands;
    resource_group_references groups;
};
struct doe_resource_commands {
    resource_base base;
    atomic_uint submission_state;
    size_t count, capacity;
    doe_resource_command *commands;
    resource_group_references groups;
};

struct doe_resource_compute_pass {
    resource_base base;
    doe_resource_immediate_state immediates;
    doe_resource_encoder *encoder;
    doe_resource_pipeline *pipeline;
    doe_resource_bind_group *groups[DOE_RESOURCE_MAX_GROUPS];
    doe_resource_dispatch_offsets offsets;
    doe_resource_pass_timestamps timestamps;
    size_t debug_depth;
    bool ended;
};

struct doe_resource_query_set {
    resource_base base;
    WGPUQueryType type;
    uint32_t count;
    bool destroyed;
};
struct doe_resource_external_texture {
    resource_base base;
    doe_resource_view *planes[2];
    size_t plane_count;
    bool expired;
};

/* Write-only and read-write storage textures are distinct usages: one
 * subresource may repeat either within a scope but not mix them. */
typedef enum resource_usage {
    RESOURCE_READ, RESOURCE_STORAGE, RESOURCE_STORAGE_READ_WRITE, RESOURCE_ATTACHMENT
} resource_usage;
typedef struct resource_use {
    doe_resource_buffer *buffer;
    doe_resource_view *view;
    doe_resource_external_texture *external;
    uint64_t offset, size;
    unsigned aspects;
    WGPUShaderStage visibility;
    resource_usage usage;
} resource_use;
typedef struct resource_usage_scope {
    resource_use *uses;
    size_t count, capacity;
} resource_usage_scope;
/* A binding contributes at most two uses (two external-texture planes). */
enum { RESOURCE_USES_PER_BINDING = 2 };


struct doe_resource_render_pipeline {
    resource_base base;
    doe_resource_pipeline_layout *layout;
    doe_resource_render_pipeline_desc desc;
};
struct doe_resource_render_record {
    resource_base base;
    doe_resource_render_packet packet;
    resource_usage_scope scope;
};
struct doe_resource_render_pass {
    resource_base base;
    doe_resource_encoder *encoder;
    doe_resource_render_packet state;
    doe_resource_render_record *last_record;
    resource_usage_scope scope;
    uint64_t draw_count;
    uint64_t occlusion_written[DOE_RESOURCE_MAX_QUERIES / 64];
    doe_resource_pass_timestamps timestamps;
    size_t debug_depth;
    bool ended;
};

doe_resource_status resource_immediates_update(const doe_resource_config *config,
    doe_resource_immediate_state *state, uint32_t offset, const void *data, size_t size);
void resource_pipeline_layout_claim(doe_resource_pipeline_layout *layout);
doe_resource_status resource_shader_bindings_validate(const doe_resource_pipeline_layout *layout,
    const doe_resource_dispatch_group *groups, size_t group_count,
    doe_resource_buffer *indirect, bool *has_dynamic_offsets);
unsigned resource_view_aspects(const doe_resource_view *view);
doe_resource_status resource_use_conflict(const resource_use *a, const resource_use *b, bool binding_aliases);
/* Scratch capacity callers keep on the stack; larger groups allocate. */
enum { RESOURCE_STACK_USES = 128 };
/* Collects a group's resolved usages into stack when they fit, otherwise
 * into config-allocated storage; release with resource_group_uses_free. */
doe_resource_status resource_group_uses(const doe_resource_config *config, const doe_resource_dispatch_group *group,
    resource_use stack[RESOURCE_STACK_USES], resource_use **uses, size_t *count, size_t *capacity);
void resource_group_uses_free(const doe_resource_config *config, resource_use *uses, size_t capacity,
    resource_use *stack);
/* Full group validation and dynamic-offset resolution of one entry index;
 * *next_dynamic counts dynamic entries already consumed before index. */
doe_resource_status resource_group_resolve_at(const doe_resource_bind_group *group, const uint32_t *offsets,
    size_t offset_count, size_t index, size_t *next_dynamic, doe_resource_binding *out);
/* Whole-group validation plus dynamic offset count/alignment/range, no output. */
doe_resource_status resource_group_check(const doe_resource_bind_group *group,
    const uint32_t *offsets, size_t offset_count);
doe_resource_status resource_scope_add(const doe_resource_config *config, resource_usage_scope *scope,
    const resource_use *uses, size_t count);
doe_resource_status resource_scope_group(const doe_resource_config *config, resource_usage_scope *scope,
    const doe_resource_dispatch_group *group);
doe_resource_status resource_scope_input(const doe_resource_config *config, resource_usage_scope *scope,
    doe_resource_buffer *buffer);
doe_resource_status resource_scope_targets(const doe_resource_config *config, resource_usage_scope *scope,
    const doe_resource_render_targets *targets);
doe_resource_status resource_scope_validate(const resource_usage_scope *scope, bool submit);
void resource_scope_release(const doe_resource_config *config, resource_usage_scope *scope);
doe_resource_status resource_record_dispatch(doe_resource_encoder *encoder,
    doe_resource_pipeline *pipeline, const doe_resource_dispatch_group *groups,
    size_t group_count, uint32_t x, uint32_t y, uint32_t z);
doe_resource_status resource_indirect_validate(const void *device,
    const doe_resource_buffer *arguments, uint64_t offset, uint64_t bytes);
doe_resource_status resource_record_dispatch_indirect(doe_resource_encoder *encoder,
    doe_resource_pipeline *pipeline, const doe_resource_dispatch_group *groups,
    size_t group_count, doe_resource_buffer *arguments, uint64_t offset);
doe_resource_status resource_encoder_fail(doe_resource_encoder *encoder, doe_resource_status status);
bool resource_status_is_rejection(doe_resource_status status);
doe_resource_status resource_debug_apply(size_t *depth, doe_resource_debug_operation operation);
doe_resource_status resource_encoder_open(doe_resource_encoder *encoder);
doe_resource_status resource_encoder_reserve(doe_resource_encoder *encoder);
doe_resource_status resource_pass_timestamps_validate(const void *device,
    const doe_resource_pass_timestamps *timestamps);
doe_resource_status resource_record_timestamp(doe_resource_encoder *encoder,
    doe_resource_query_set *set, uint32_t index, doe_resource_timestamp_position position);
bool resource_query_available(const doe_resource_query_set *set);
bool resource_external_available(const doe_resource_external_texture *texture);
doe_resource_status resource_render_pass_active(doe_resource_render_pass *pass);
doe_resource_status resource_render_replay_packet(doe_resource_render_pass *pass,
    const doe_resource_render_packet *packet);
void resource_render_clear_bindings(doe_resource_render_packet *state);
void resource_render_packet_retain(const doe_resource_render_packet *packet);
void resource_render_packet_release(const doe_resource_render_packet *packet);
/* Color-target layout rules: src/render_targets.c is their only owner. */
uint32_t resource_max_color_attachments(const doe_resource_config *config);
doe_resource_status resource_color_layout_validate(const doe_resource_config *config,
    const WGPUTextureFormat *formats, uint32_t count, WGPUTextureFormat depth_stencil, uint32_t samples);
doe_resource_status resource_color_formats_normalize(WGPUTextureFormat legacy, uint32_t count,
    const WGPUTextureFormat *formats, uint32_t *out_count, WGPUTextureFormat out[DOE_RESOURCE_MAX_COLOR_ATTACHMENTS]);
doe_resource_status resource_render_targets_normalize(const doe_resource_render_targets *in,
    doe_resource_render_targets *out);
bool resource_color_layouts_equal(uint32_t a_count, const WGPUTextureFormat *a,
    uint32_t b_count, const WGPUTextureFormat *b);
doe_resource_render_bundle_desc resource_render_targets_layout(const doe_resource_render_targets *targets);
doe_resource_status resource_render_set_pipeline(void *device, doe_resource_render_packet *state,
    const doe_resource_render_bundle_desc *signature, doe_resource_render_pipeline *pipeline);
doe_resource_status resource_render_set_group(const doe_resource_config *config,
    resource_usage_scope *scope, doe_resource_render_packet *state,
    uint32_t index, const doe_resource_dispatch_group *group);
doe_resource_status resource_render_set_vertex(const doe_resource_config *config,
    resource_usage_scope *scope, doe_resource_render_packet *state,
    uint32_t slot, doe_resource_buffer *buffer, uint64_t offset, uint64_t size);
doe_resource_status resource_render_set_index(const doe_resource_config *config,
    resource_usage_scope *scope, doe_resource_render_packet *state,
    doe_resource_buffer *buffer, WGPUIndexFormat format, uint64_t offset, uint64_t size);
doe_resource_status resource_render_draw_validate(const doe_resource_render_packet *packet,
    void *device, const doe_resource_render_bundle_desc *signature);
void resource_render_record_release(doe_resource_render_record *record);
doe_resource_status resource_render_record_validate(const doe_resource_render_record *record);
bool resource_config_valid(const doe_resource_config *config);
bool resource_alive(const resource_base *base);
void *resource_allocate(const doe_resource_config *config, size_t size, size_t alignment);
void resource_free(const doe_resource_config *config, void *memory, size_t size, size_t alignment);
void resource_init(resource_base *base, const doe_resource_config *config, doe_resource_native native);
void resource_retain(resource_base *base);
bool resource_last_reference(resource_base *base);
void resource_finalize(resource_base *base, size_t size, size_t alignment);
bool resource_buffer_available(const doe_resource_buffer *buffer);
bool resource_texture_available(const doe_resource_texture *texture);
/* A texture still referenced, destroyed or not: WebGPU admits a destroyed
 * texture when encoding and rejects it at submit (resource_texture_available). */
bool resource_texture_live(const doe_resource_texture *texture);
doe_resource_status resource_range(uint64_t total, uint64_t offset, uint64_t requested, uint64_t *size);
bool resource_format_compatible(WGPUTextureFormat base, WGPUTextureFormat view);
bool resource_format_depth(WGPUTextureFormat format);
/* WebGPU color-target capabilities (render_targets.c): renderable, multisample
 * (depth formats included) and resolve. */
bool resource_format_color_renderable(WGPUTextureFormat format);
bool resource_format_multisample(WGPUTextureFormat format);
bool resource_format_resolvable(WGPUTextureFormat format);
/* Core storage-texture format; read_write reports the read-write access subset. */
bool resource_storage_format(WGPUTextureFormat format, bool *read_write);
bool resource_format_aspect(WGPUTextureFormat format, WGPUTextureAspect aspect);
unsigned resource_format_aspects(WGPUTextureFormat format);
doe_resource_status resource_format_block(WGPUTextureFormat format, uint32_t *bytes,
    uint32_t *width, uint32_t *height);
doe_resource_status resource_group_validate(const doe_resource_bind_group *group, bool submit);
void resource_command_release(doe_resource_command *command);
#endif
