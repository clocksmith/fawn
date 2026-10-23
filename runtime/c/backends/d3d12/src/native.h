/* SPDX-License-Identifier: MIT */
#ifndef DOE_DX_NATIVE_H
#define DOE_DX_NATIVE_H
#include "../include/doe_d3d12_mesh.h"
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
    size_t buffer_count, texture_count;
} doe_dx_native_resources;

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
void doe_dx_native_close(doe_dx_native_context *context);
doe_dx_status doe_dx_native_buffer_create(doe_dx_native_context *context,
    uint64_t size, doe_dx_memory memory, doe_dx_native_buffer **out, doe_dx_error *error);
void doe_dx_native_buffer_destroy(doe_dx_native_buffer *buffer);
doe_dx_status doe_dx_native_write(doe_dx_native_buffer *buffer, uint64_t offset,
    const void *bytes, size_t size, doe_dx_error *error);
doe_dx_status doe_dx_native_read(doe_dx_native_buffer *buffer, uint64_t offset,
    void *bytes, size_t size, doe_dx_error *error);
doe_dx_status doe_dx_native_pipeline_create(doe_dx_native_context *context,
    const doe_dx_resource_pipeline_desc *desc, doe_dx_native_pipeline **out, doe_dx_error *error);
/* Shared normalized footprint; native SDK query must agree before allocation publishes. */
doe_dx_status doe_dx_texture_layout_2d(const doe_dx_texture_desc *desc,
    doe_dx_texture_layout layout, doe_dx_texture_footprint *out);
doe_dx_status doe_dx_native_texture_create(doe_dx_native_context *context,
    const doe_dx_texture_desc *desc, doe_dx_native_texture **out, doe_dx_error *error);
void doe_dx_native_texture_destroy(doe_dx_native_texture *texture);
void doe_dx_native_texture_transfer(doe_dx_native_commands *commands,
    doe_dx_native_buffer *buffer, doe_dx_native_texture *texture,
    const doe_dx_texture_footprint *footprint, bool upload);
void doe_dx_native_texture_copy(doe_dx_native_commands *commands,
    doe_dx_native_texture *source, doe_dx_native_texture *destination);
doe_dx_status doe_dx_native_graphics_create(doe_dx_native_context *context,
    const doe_dx_mesh_pipeline_desc *desc, doe_dx_native_pipeline **out, doe_dx_error *error);
doe_dx_status doe_dx_native_draw(doe_dx_native_commands *commands,
    doe_dx_native_pipeline *pipeline, const doe_dx_native_resources *resources,
    doe_dx_native_texture *color, const doe_dx_native_geometry *geometry,
    const doe_dx_mesh_draw_desc *draw, doe_dx_error *error);
doe_dx_status doe_dx_native_timestamps_create(doe_dx_native_context *context, uint32_t count,
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
doe_dx_status doe_dx_native_wait(doe_dx_native_context *context, uint64_t value,
    uint32_t wait_ms, doe_dx_error *error);
#endif
