/* SPDX-License-Identifier: MIT */
#ifndef DOE_MTL_NATIVE_H
#define DOE_MTL_NATIVE_H
#include "../include/doe_metal.h"
#include "../include/doe_metal_cache.h"
#include "../include/doe_metal_texture.h"
#include "../include/doe_metal_external.h"
#include "../include/doe_metal_render.h"
#include "../include/doe_metal_query.h"
#include "../include/doe_metal_surface.h"

/* Private actual-platform call boundary. Tests substitute these calls only;
 * the production constructor has no injectable/public fake-runtime option.
 * Native objects are opaque Objective-C id handles. Constructors return +1.
 * Native errors must fill the supplied diagnostic; successful calls clear it. */
typedef struct doe_mtl_native_binding {
    void *buffer;
    size_t offset;
    uint32_t size;
} doe_mtl_native_binding;

typedef struct doe_mtl_native_resources {
    doe_mtl_native_binding buffers[DOE_MTL_BUFFER_SLOTS];
    void *textures[DOE_MTL_TEXTURE_SLOTS];
    void *samplers[DOE_MTL_SAMPLER_SLOTS];
    uint32_t immediate_words[16], immediate_size, immediate_slot;
    uint32_t texture_count, sampler_count; /* Highest occupied slot plus one. */
} doe_mtl_native_resources;
typedef struct doe_mtl_native_texture_copy {
    size_t buffer_offset, bytes_per_row, bytes_per_image;
    doe_mtl_texture_region region;
    bool volume;
    size_t options; /* Native depth/stencil component selection for buffer copies. */
} doe_mtl_native_texture_copy;

typedef struct doe_mtl_native_render_attachment {
    void *texture;
    doe_mtl_load_action load;
    doe_mtl_store_action store;
    doe_mtl_clear_color color;
    double depth;
    uint32_t stencil;
    void *resolve;
    uint32_t resolve_filter;
} doe_mtl_native_render_attachment;
typedef struct doe_mtl_native_render_pass {
    doe_mtl_native_render_attachment colors[DOE_MTL_COLOR_ATTACHMENTS];
    uint32_t color_count, width, height, samples;
    doe_mtl_native_render_attachment depth, stencil;
    void *visibility_buffer;
} doe_mtl_native_render_pass;
typedef struct doe_mtl_native_render_draw {
    void *pipeline, *depth_stencil;
    doe_mtl_native_resources vertex, fragment;
    uint32_t vertex_sizes_slot, fragment_sizes_slot;
    void *index_buffer;
    size_t index_offset;
    void *indirect_arguments;
    doe_mtl_render_draw values; /* Only scalar/dynamic fields used by adapter. */
} doe_mtl_native_render_draw;

typedef struct doe_mtl_native_timestamps {
    void *buffer;
    uint32_t begin, end;
} doe_mtl_native_timestamps;

typedef struct doe_mtl_native_external {
    void *planes[2];
    doe_mtl_texture_desc desc[2];
    uint32_t count;
} doe_mtl_native_external;

typedef struct doe_mtl_native {
    void *userdata;
    void (*close)(void *userdata);
    void (*release)(void *userdata, void *object);
    /* Transactional: failure releases partial imports and leaves out untouched. */
    doe_mtl_status (*external_texture)(void *, void *, doe_mtl_external_source, void *,
        doe_mtl_native_external *, doe_mtl_error *);
    doe_mtl_status (*shared_surface)(void *, void *, void **, uint32_t *, uint32_t *, doe_mtl_format *, doe_mtl_error *);
    void (*shared_surface_release)(void *, void *);
    doe_mtl_status (*shared_texture)(void *, void *, void *, const doe_mtl_texture_desc *, void **, doe_mtl_error *);
    doe_mtl_status (*shared_event_retain)(void *, void *, void **, doe_mtl_error *);
    doe_mtl_status (*shared_event_wait)(void *, void *, uint64_t, doe_mtl_error *);
    bool (*surface_supported)(void *userdata, void *layer);
    doe_mtl_status (*surface_configure)(void *userdata, void *device, void *layer,
        const doe_mtl_surface_desc *desc, void **out, doe_mtl_error *error);
    doe_mtl_status (*surface_acquire)(void *userdata, void *layer, const doe_mtl_surface_desc *desc,
        void **drawable, void **texture, doe_mtl_error *error);
    doe_mtl_status (*surface_present)(void *userdata, void *command, void *drawable, doe_mtl_error *error);
    size_t (*surface_observe)(void *userdata, void *command, void *drawable, bool wait, doe_mtl_error *error);
    doe_mtl_status (*device)(void *userdata, void **out, doe_mtl_caps *caps,
        doe_mtl_error *error);
    doe_mtl_status (*queue)(void *userdata, void *device, void **out,
        doe_mtl_error *error);
    doe_mtl_status (*buffer)(void *userdata, void *device, size_t length,
        doe_mtl_memory memory, void **out, doe_mtl_error *error);
    void *(*contents)(void *userdata, void *buffer);
    doe_mtl_status (*shader_validate)(void *userdata, void *device,
        const char *source, size_t length, doe_mtl_error *error);
    doe_mtl_status (*pipeline)(void *userdata, void *device,
        const doe_mtl_pipeline_desc *desc, void **out, size_t *max_threads,
        doe_mtl_error *error);
    doe_mtl_status (*command)(void *userdata, void *queue, void **out,
        doe_mtl_error *error);
    doe_mtl_status (*copy)(void *userdata, void *command, void *source,
        size_t source_offset, void *destination, size_t destination_offset,
        size_t length, doe_mtl_error *error);
    doe_mtl_status (*fill)(void *userdata, void *command, void *buffer,
        size_t offset, size_t length, uint8_t value, doe_mtl_error *error);
    doe_mtl_status (*dispatch)(void *userdata, void *command, void *pipeline,
        const doe_mtl_native_resources *resources, uint32_t sizes_slot,
        doe_mtl_dimensions groups, doe_mtl_dimensions workgroup,
        uint32_t repeat, const doe_mtl_native_timestamps *timestamps, doe_mtl_error *error);
    doe_mtl_status (*dispatch_indirect)(void *userdata, void *command, void *pipeline,
        const doe_mtl_native_resources *resources, uint32_t sizes_slot,
        void *arguments, size_t offset, doe_mtl_dimensions workgroup, doe_mtl_error *error);
    doe_mtl_status (*texture)(void *userdata, void *device,
        const doe_mtl_texture_desc *desc, void **out, doe_mtl_error *error);
    doe_mtl_status (*view)(void *userdata, void *texture,
        const doe_mtl_texture_view_desc *desc, void **out, doe_mtl_error *error);
    doe_mtl_status (*sampler)(void *userdata, void *device,
        const doe_mtl_sampler_desc *desc, void **out, doe_mtl_error *error);
    doe_mtl_status (*upload_texture)(void *userdata, void *command, void *buffer,
        void *texture, const doe_mtl_native_texture_copy *copy, doe_mtl_error *error);
    doe_mtl_status (*read_texture)(void *userdata, void *command, void *texture,
        void *buffer, const doe_mtl_native_texture_copy *copy, doe_mtl_error *error);
    doe_mtl_status (*copy_texture)(void *userdata, void *command, void *source,
        const doe_mtl_native_texture_copy *source_copy, void *destination,
        const doe_mtl_native_texture_copy *destination_copy, doe_mtl_error *error);
    doe_mtl_status (*render_pipeline)(void *userdata, void *device,
        const doe_mtl_render_pipeline_desc *desc, void **pipeline, void **depth_stencil, doe_mtl_error *error);
    doe_mtl_status (*render)(void *userdata, void *command,
        const doe_mtl_native_render_pass *pass, const doe_mtl_native_render_draw *draws,
        size_t draw_count, doe_mtl_error *error);
    doe_mtl_status (*timestamps)(void *userdata, void *device, uint32_t count,
        void **out, doe_mtl_error *error);
    doe_mtl_status (*timestamp)(void *userdata, void *command, void *timestamps,
        uint32_t index, doe_mtl_error *error);
    doe_mtl_status (*timestamp_boundary)(void *userdata, void *command, void *timestamps,
        uint32_t index, bool stage, void *scratch, void **event, uint64_t value, doe_mtl_error *error);
    doe_mtl_status (*resolve_timestamps)(void *userdata, void *command, void *timestamps,
        uint32_t first, uint32_t count, void *destination, size_t offset,
        void **event, uint64_t value, doe_mtl_error *error);
    doe_mtl_status (*pipeline_cache)(void *, void *, const char *, size_t, doe_mtl_error *);
    doe_mtl_status (*pipeline_cache_flush)(void *, doe_mtl_error *);
    void (*pipeline_cache_stats)(void *, doe_mtl_pipeline_cache_stats *);
    doe_mtl_status (*timeline_create)(void *, void *, void **, doe_mtl_error *);
    doe_mtl_status (*timeline_signal)(void *, void *, void *, uint64_t, doe_mtl_error *);
    uint64_t (*timeline_value)(void *, void *);
    void (*commit)(void *userdata, void *command);
    /* Returns native MTLCommandBufferStatus. Terminal NSError operation/code/text
     * are copied before its autorelease pool drains; no borrowed string escapes. */
    size_t (*observe)(void *userdata, void *command, bool wait, doe_mtl_error *error);
} doe_mtl_native;

doe_mtl_status doe_mtl_native_open(doe_mtl_native *out, doe_mtl_error *error);
/* Takes the native loader obligation on all paths, only used by owner/tests. */
doe_mtl_status doe_mtl_context_open(doe_mtl_native native,
    doe_mtl_context **out, doe_mtl_error *error);
doe_mtl_status doe_mtl_fail(doe_mtl_status status, const char *operation,
    doe_mtl_error *error);
#endif
