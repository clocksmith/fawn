/* SPDX-License-Identifier: MIT */
#ifndef DOE_MTL_OBJC_ABI_H
#define DOE_MTL_OBJC_ABI_H
#include "native.h"
#include <stdlib.h>
#include <string.h>

/* Narrow 64-bit macOS C ABI. MTLSize and NSRange use NSUInteger fields.
 * objc_msgSend is always cast to the exact selected method signature.
 * x86_64 uses objc_msgSend_stret for a returned three-word MTLSize; arm64
 * uses the ordinary dispatcher. No opaque guessed-size SDK structures. */
typedef struct doe_mtl_size { size_t width, height, depth; } doe_mtl_size;
typedef struct doe_mtl_origin { size_t x, y, z; } doe_mtl_origin;
typedef struct doe_mtl_swizzle_abi { uint8_t red, green, blue, alpha; } doe_mtl_swizzle_abi;
typedef struct doe_mtl_range_abi { size_t location, length; } doe_mtl_range_abi;
typedef void (*doe_objc_message)(void);
#if defined(__aarch64__) || defined(__arm64__)
typedef bool doe_objc_bool;
#else
typedef signed char doe_objc_bool;
#endif
#include "objc_render_types.h"
#include "objc_query_types.h"
typedef doe_objc_bool (*doe_objc_responds_call)(void *, void *, void *);
typedef void *(*doe_objc_object_call)(void *, void *);
typedef void (*doe_objc_void_call)(void *, void *);
typedef size_t (*doe_objc_size_call)(void *, void *);
typedef intptr_t (*doe_objc_integer_call)(void *, void *);
typedef const char *(*doe_objc_string_call)(void *, void *);
typedef void *(*doe_objc_buffer_call)(void *, void *, size_t, size_t);
typedef void *(*doe_objc_string_init_call)(void *, void *, const void *, size_t, size_t);
typedef void *(*doe_objc_library_call)(void *, void *, void *, void *, void **);
typedef void *(*doe_objc_named_call)(void *, void *, void *);
typedef void *(*doe_objc_pipeline_call)(void *, void *, void *, void **);
typedef void (*doe_objc_set_object_call)(void *, void *, void *);
typedef void (*doe_objc_bind_buffer_call)(void *, void *, void *, size_t, size_t);
typedef void (*doe_objc_bind_bytes_call)(void *, void *, const void *, size_t, size_t);
typedef void (*doe_objc_dispatch_call)(void *, void *, doe_mtl_size, doe_mtl_size);
typedef void (*doe_objc_copy_call)(void *, void *, void *, size_t, void *, size_t, size_t);
typedef void (*doe_objc_fill_call)(void *, void *, void *, doe_mtl_range_abi, uint8_t);
typedef doe_mtl_size (*doe_objc_dimensions_call)(void *, void *);
typedef void (*doe_objc_dimensions_stret_call)(doe_mtl_size *, void *, void *);

typedef void (*doe_objc_set_size_call)(void *, void *, size_t);
typedef void (*doe_objc_set_float_call)(void *, void *, float);
typedef doe_objc_bool (*doe_objc_bool_call)(void *, void *);
typedef doe_objc_bool (*doe_objc_samples_call)(void *, void *, size_t);
typedef doe_objc_bool (*doe_objc_family_call)(void *, void *, intptr_t);
typedef void *(*doe_objc_view_call)(void *, void *, size_t, size_t,
    doe_mtl_range_abi, doe_mtl_range_abi, doe_mtl_swizzle_abi);
typedef void (*doe_objc_bind_image_call)(void *, void *, void *, size_t);
typedef void (*doe_objc_upload_image_call)(void *, void *, void *, size_t, size_t,
    size_t, doe_mtl_size, void *, size_t, size_t, doe_mtl_origin, size_t);
typedef void (*doe_objc_read_image_call)(void *, void *, void *, size_t, size_t,
    doe_mtl_origin, doe_mtl_size, void *, size_t, size_t, size_t, size_t);
typedef void (*doe_objc_copy_image_call)(void *, void *, void *, size_t, size_t,
    doe_mtl_origin, doe_mtl_size, void *, size_t, size_t, doe_mtl_origin);

typedef struct doe_mtl_objc {
    doe_mtl_objc_graphics graphics;
    doe_mtl_objc_queries queries;
    void *objc_library;
    void *foundation_library;
    void *metal_library;
    void *(*get_class)(const char *);
    void *(*selector)(const char *);
    void *(*create_device)(void);
    /* Typed views of the same native dispatcher, separately callable at the
     * private test boundary without an incompatible variadic C mock. */
    doe_objc_responds_call responds_call;
    doe_objc_object_call object_call;
    doe_objc_void_call void_call;
    doe_objc_size_call size_call;
    doe_objc_integer_call integer_call;
    doe_objc_string_call string_call;
    doe_objc_buffer_call buffer_call;
    doe_objc_string_init_call string_init_call;
    doe_objc_library_call library_call;
    doe_objc_named_call named_call;
    doe_objc_pipeline_call pipeline_call;
    doe_objc_set_object_call set_object_call;
    doe_objc_bind_buffer_call bind_buffer_call;
    doe_objc_bind_bytes_call bind_bytes_call;
    doe_objc_dispatch_call dispatch_call;
    doe_objc_copy_call copy_call;
    doe_objc_fill_call fill_call;
    doe_objc_dimensions_call dimensions_call;
    doe_objc_dimensions_stret_call dimensions_stret_call;
    doe_objc_set_size_call set_size_call;
    doe_objc_set_float_call set_float_call;
    doe_objc_bool_call bool_call;
    doe_objc_family_call family_call;
    doe_objc_samples_call samples_call;
    doe_objc_view_call view_call;
    doe_objc_bind_image_call bind_image_call;
    doe_objc_upload_image_call upload_image_call;
    doe_objc_read_image_call read_image_call;
    doe_objc_copy_image_call copy_image_call;
    void *texture_class, *sampler_class;
    void *new_texture, *new_view, *new_sampler;
    void *set_type, *set_format, *set_width, *set_height, *set_depth;
    void *set_mips, *set_layers, *set_samples, *set_storage, *set_usage;
    void *set_min_filter, *set_mag_filter, *set_mip_filter;
    void *set_address_u, *set_address_v, *set_address_w;
    void *set_min_lod, *set_max_lod, *set_compare, *set_anisotropy;
    void *upload_image, *read_image, *copy_image, *set_texture, *set_sampler;
    void *supports_family, *supports_bc, *supports_samples, *supports_depth24;
    void *pool_class;
    void *string_class;
    void *responds;
    void *new_object;
    void *alloc;
    void *retain;
    void *release;
    void *drain;
    void *new_queue;
    void *new_buffer;
    void *contents;
    void *max_buffer_length;
    void *max_threadgroup_memory;
    void *max_threads;
    void *name;
    void *utf8;
    void *string_init;
    void *new_library;
    void *new_function;
    void *new_pipeline;
    void *pipeline_max_threads;
    void *localized_description;
    void *code;
    void *new_command;
    void *blit_encoder;
    void *compute_encoder;
    void *copy_buffer;
    void *fill_buffer;
    void *end_encoding;
    void *set_pipeline;
    void *set_buffer;
    void *set_bytes;
    void *dispatch;
    void *commit;
    void *wait;
    void *status;
    void *error;
} doe_mtl_objc;

void *doe_mtl_objc_object(doe_mtl_objc *runtime, void *object, void *selector);
void doe_mtl_objc_void(doe_mtl_objc *runtime, void *object, void *selector);
size_t doe_mtl_objc_size(doe_mtl_objc *runtime, void *object, void *selector);
bool doe_mtl_objc_limits(doe_mtl_objc *runtime, void *device, doe_mtl_caps *caps);
void *doe_mtl_objc_pool(doe_mtl_objc *runtime);
void doe_mtl_objc_drain(doe_mtl_objc *runtime, void *pool);
void doe_mtl_objc_release(void *userdata, void *object);
void *doe_mtl_objc_string(doe_mtl_objc *runtime, const char *bytes, size_t length);
doe_mtl_status doe_mtl_objc_error(doe_mtl_objc *runtime, void *native_error,
    const char *operation, doe_mtl_error *error);
void doe_mtl_objc_text(doe_mtl_objc *runtime, void *string, char *out, size_t capacity);
doe_mtl_status doe_mtl_objc_function(doe_mtl_objc *runtime, void *device,
    const char *msl, size_t length, const char *entrypoint, void **out, doe_mtl_error *error);
doe_mtl_status doe_mtl_objc_pipeline(void *userdata, void *device,
    const doe_mtl_pipeline_desc *desc, void **out, size_t *max_threads, doe_mtl_error *error);
doe_mtl_status doe_mtl_objc_command(void *userdata, void *queue, void **out,
    doe_mtl_error *error);
doe_mtl_status doe_mtl_objc_copy(void *userdata, void *command, void *source,
    size_t source_offset, void *destination, size_t destination_offset, size_t length,
    doe_mtl_error *error);
doe_mtl_status doe_mtl_objc_fill(void *userdata, void *command, void *buffer,
    size_t offset, size_t length, uint8_t value, doe_mtl_error *error);
doe_mtl_status doe_mtl_objc_dispatch(void *userdata, void *command, void *pipeline,
    const doe_mtl_native_resources *resources, uint32_t sizes_slot,
    doe_mtl_dimensions groups, doe_mtl_dimensions workgroup, uint32_t repeat,
    const doe_mtl_native_timestamps *timestamps, doe_mtl_error *error);
bool doe_mtl_objc_load_image_selectors(doe_mtl_objc *runtime);
void doe_mtl_objc_image_caps(doe_mtl_objc *runtime, void *device, doe_mtl_caps *caps);
doe_mtl_status doe_mtl_objc_texture(void *userdata, void *device,
    const doe_mtl_texture_desc *desc, void **out, doe_mtl_error *error);
doe_mtl_status doe_mtl_objc_view(void *userdata, void *texture,
    const doe_mtl_texture_view_desc *desc, void **out, doe_mtl_error *error);
doe_mtl_status doe_mtl_objc_sampler(void *userdata, void *device,
    const doe_mtl_sampler_desc *desc, void **out, doe_mtl_error *error);
doe_mtl_status doe_mtl_objc_upload_image(void *userdata, void *command, void *buffer,
    void *texture, const doe_mtl_native_texture_copy *copy, doe_mtl_error *error);
doe_mtl_status doe_mtl_objc_read_image(void *userdata, void *command, void *texture,
    void *buffer, const doe_mtl_native_texture_copy *copy, doe_mtl_error *error);
doe_mtl_status doe_mtl_objc_copy_image(void *userdata, void *command, void *source,
    const doe_mtl_native_texture_copy *from, void *destination,
    const doe_mtl_native_texture_copy *to, doe_mtl_error *error);
bool doe_mtl_objc_load_render(doe_mtl_objc *runtime, doe_objc_message message);
doe_mtl_status doe_mtl_objc_render_pipeline(void *userdata, void *device,
    const doe_mtl_render_pipeline_desc *desc, void **pipeline, void **depth_stencil, doe_mtl_error *error);
doe_mtl_status doe_mtl_objc_render(void *userdata, void *command,
    const doe_mtl_native_render_pass *pass, const doe_mtl_native_render_draw *draws,
    size_t draw_count, doe_mtl_error *error);
void doe_mtl_objc_render_encode_draw(doe_mtl_objc *runtime, void *encoder,
    const doe_mtl_native_render_pass *pass, const doe_mtl_native_render_draw *draw);
void *doe_mtl_objc_depth_state(doe_mtl_objc *runtime, void *device,
    const doe_mtl_depth_stencil *desc);
void doe_mtl_objc_load_queries(doe_mtl_objc *runtime, doe_objc_message message);
void doe_mtl_objc_query_caps(doe_mtl_objc *runtime, void *device, doe_mtl_caps *caps);
doe_mtl_status doe_mtl_objc_timestamps(void *userdata, void *device,
    uint32_t count, void **out, doe_mtl_error *error);
doe_mtl_status doe_mtl_objc_timestamp(void *userdata, void *command,
    void *timestamps, uint32_t index, doe_mtl_error *error);
doe_mtl_status doe_mtl_objc_resolve_timestamps(void *userdata, void *command,
    void *timestamps, uint32_t first, uint32_t count, void *destination,
    size_t offset, doe_mtl_error *error);
void *doe_mtl_objc_timed_compute(doe_mtl_objc *runtime, void *command,
    const doe_mtl_native_timestamps *timestamps);
void doe_mtl_objc_commit(void *userdata, void *command);
size_t doe_mtl_objc_observe(void *userdata, void *command, bool wait, doe_mtl_error *error);
#endif
