/* SPDX-License-Identifier: MIT */
#ifndef DOE_NATIVE_INTERNAL_H
#define DOE_NATIVE_INTERNAL_H
#include "doe_platform.h"
#include "webgpu.h"
#include "backend.h"
#include "doe_metal_render.h"
#include "doe_metal_query.h"
#include "doe_metal_indirect.h"
#include "doe_instance.h"
#include "doe_callbacks.h"
#include "doe_resources.h"
#include "doe_resources_extended.h"
#include "doe_shader.h"
#include "doe_vk.h"
#include "doe_vk_bindings.h"
#include "doe_vk_timestamp.h"
#include "doe_vk_render.h"
#include "doe_vk_indirect.h"
#include "doe_error_scopes.h"
#include <stdatomic.h>
#include <stdbool.h>

typedef struct native_adapter {
    atomic_uint references;
    doe_mutex mutex; /* Serializes surface queries on the adapter's own context. */
    doe_instance *instance;
    native_backend backend;
} native_adapter;

typedef struct native_pending native_pending;
typedef struct native_loss native_loss;
typedef struct native_device {
    atomic_uint references;
    atomic_uint external_references;
    native_loss *loss;
    doe_mutex mutex;
    doe_condition pending_changed;
    doe_thread pending_thread;
    bool pending_progress_ready;
    bool pending_progress_join;
    doe_instance *instance;
    native_backend backend;
    doe_resource_config resources;
    doe_label_store *labels;
    unsigned char queue_label_key;
    doe_error_scope_stack *errors;
    void *error_storage;
    WGPUUncapturedErrorCallbackInfo uncaptured;
    bool destroyed;
    bool queue_failed;
    uint32_t features; /* Enabled WebGPU features, NATIVE_FEATURE bits. */
    doe_vk_timestamp_normalizer *timestamp_normalizer;
    doe_mtl_pipeline *metal_timestamp_pipeline;
    uint32_t metal_timestamp_slot;
    doe_vk_dispatch_validator *dispatch_validator;
    doe_mtl_dispatch_guard *metal_dispatch_guard;
    doe_mtl_draw_guard *metal_draw_guard;
    doe_vk_draw_validator *draw_validator;
    native_pending *pending;
    native_failure retired_failure;
    /* Vulkan: unsubmitted zero initialization of newly created textures,
     * flushed ahead of the next recording that may touch them. */
    doe_vk_commands *init_batch;
    uint32_t init_batch_textures; /* Textures recorded into init_batch. */
} native_device;

/* Native payloads have no independent reference count. Their resource owner
 * alone releases them after all public/recorded/submission references settle. */
typedef struct native_buffer {
    native_device *device; /* Borrowed through the resource's device lease. */
    native_buffer_handle handle;
    unsigned char *mapping;
} native_buffer;

typedef struct native_shader {
    doe_shader *module;
    doe_shader_error error;
} native_shader;

typedef struct native_texture {
    native_device *device;
    union { doe_vk_texture *texture; doe_mtl_texture *metal; struct doe_dx_texture *d3d12; };
    struct native_surface *surface; /* Borrowed frame payload owns one surface lease. */
} native_texture;
typedef struct native_view {
    native_device *device;
    union { doe_vk_texture_view *view; doe_mtl_texture_view *metal; };
} native_view;
typedef struct native_sampler {
    native_device *device;
    union { doe_vk_sampler *sampler; doe_mtl_sampler *metal; };
} native_sampler;

typedef struct native_queries {
    native_device *device;
    union { doe_vk_query_pool *pool; doe_mtl_buffer *metal_results; };
    doe_mtl_timestamps *metal_timestamps;
} native_queries;

typedef struct native_pipeline_error {
    WGPUErrorType type;
    char message[NATIVE_FAILURE_MESSAGE_CAPACITY];
} native_pipeline_error;

typedef struct native_stage {
    doe_shader *compiled; /* Temporary selected artifact; release outside owner locks. */
    doe_shader_view view;
    const DoeCompilerEntryV5 *entry;
    WGPUShaderStage visibility;
    char name[64];
} native_stage;

/* Distinct resources one pipeline references: at most two stages, each within
 * the per-stage binding limits native_supported_limits reports (limits.c). */
enum { NATIVE_MAX_PIPELINE_BINDINGS = 128 };

typedef struct native_binding {
    uint32_t group, binding;
    doe_resource_binding_kind kind;
    WGPUBufferBindingType buffer_type;
} native_binding;

typedef struct native_pipeline {
    native_device *device;
    union {
        doe_vk_pipeline *vulkan;
        doe_mtl_pipeline *metal;
        doe_mtl_render_pipeline *metal_render;
        doe_dx_pipeline *d3d12;
    } handle;
    struct native_metal_graphics *metal_graphics;
    size_t binding_count;
    size_t vertex_count;
    struct native_metal_stage_map *metal_compute;
    struct native_d3d12_compute *d3d12_compute;
    struct native_d3d12_graphics *d3d12_graphics;
    native_binding bindings[NATIVE_MAX_PIPELINE_BINDINGS];
    uint64_t minimum_sizes[NATIVE_MAX_PIPELINE_BINDINGS];
} native_pipeline;

void *native_allocate(void *state, const void *vtable, size_t size, size_t alignment);
void native_deallocate(void *state, const void *vtable, void *memory, size_t size, size_t alignment);
void native_retain_count(atomic_uint *count);
bool native_release_count(atomic_uint *count);
native_loss *native_loss_create(doe_instance *instance, WGPUDeviceLostCallbackInfo callback);
void native_loss_release(native_loss *loss);
void native_loss_bind(native_loss *loss, native_device *device);
void native_loss_complete(native_loss *loss, WGPUDeviceLostReason reason, const char *message);
void native_device_retain(void *device);
void native_device_release(void *device);
bool native_device_alive(void *device);
doe_vk_status native_device_status(native_device *device);
void native_lock(native_device *device);
void native_unlock(native_device *device);
void native_error(native_device *device, WGPUErrorType type, const char *message);
/* The WebGPU encoding rule: a rejected encoder or pass command only invalidates
 * the encoder, and Finish reports that once; a command on an encoder or pass
 * that was already ended (sampled under the device lock before the call) is
 * reported at the call. */
void native_encoding_error(native_device *device, bool ended, doe_resource_status status, const char *message);
/* setImmediates range: 4-byte aligned offset and size within maxImmediateSize.
 * No shader declares immediates yet (pipeline layouts accept an immediateSize
 * but no pipeline reads it), so a valid call records nothing a draw or
 * dispatch could read. */
bool native_immediates_valid(const native_device *device, uint32_t offset, const void *data, size_t size);
WGPUErrorType native_resource_error(doe_resource_status status);
WGPUErrorType native_backend_error(doe_vk_status status);
WGPUErrorType native_label_error(doe_label_status status);
void native_label(native_device *device, const void *identity, WGPUStringView label);
WGPUStringView native_string(const char *text);
bool native_string_size(WGPUStringView view, size_t *size);
WGPUFuture native_callback(doe_instance *instance, WGPUCallbackMode mode,
    void *context, size_t size, size_t alignment,
    doe_callback_copy_context copy, doe_callback_deliver_context deliver);
/* native_callback that also returns the admission status. */
WGPUFuture native_callback_admit(doe_instance *instance, WGPUCallbackMode mode,
    void *context, size_t size, size_t alignment,
    doe_callback_copy_context copy, doe_callback_deliver_context deliver, doe_callback_status *status);
/* A device-scoped operation whose callback was refused for lack of memory and
 * that reports no other error reports OutOfMemory, so its callback is never
 * lost silently. Other refusals (invalid mode, released instance) remain a zero
 * future only. Call after rollback, without the device mutex. */
void native_callback_refused(native_device *device, doe_callback_status status);

/* lost_objects.c: WebGPU invalid objects of a Vulkan device that is no longer
 * alive (destroy() or loss); creation reports nothing and returns these. */
bool native_device_dead(native_device *device);
WGPUBuffer native_lost_buffer(native_device *device, const WGPUBufferDescriptor *descriptor);
WGPUTexture native_lost_texture(native_device *device, const WGPUTextureDescriptor *descriptor);
WGPUTextureView native_lost_view(native_device *device, doe_resource_texture *texture, const WGPUTextureViewDescriptor *descriptor);
WGPUSampler native_lost_sampler(native_device *device, const WGPUSamplerDescriptor *descriptor);
WGPUBindGroupLayout native_lost_bind_group_layout(native_device *device, WGPUStringView label);
WGPUPipelineLayout native_lost_pipeline_layout(native_device *device, WGPUStringView label);
WGPUBindGroup native_lost_bind_group(native_device *device, WGPUStringView label);
WGPUShaderModule native_lost_shader(native_device *device, WGPUStringView label);
WGPUComputePipeline native_lost_compute_pipeline(native_device *device, WGPUStringView label);
WGPURenderPipeline native_lost_render_pipeline(native_device *device, WGPUStringView label);
WGPUQuerySet native_lost_query_set(native_device *device, const WGPUQuerySetDescriptor *descriptor);
WGPUCommandEncoder native_lost_encoder(native_device *device, const WGPUCommandEncoderDescriptor *descriptor);

WGPUStatus native_limits(const native_backend *backend, WGPULimits *out);
/* wgpuDeviceCreateBuffer with an explicit size bound: the public entry passes
 * the device's maxBufferSize; internal staging (not bound by WebGPU limits)
 * passes native_allocation_bound, the backend's real single-allocation size. */
WGPUBuffer native_device_create_buffer(native_device *device, const WGPUBufferDescriptor *descriptor,
    uint64_t max_size);
uint64_t native_allocation_bound(const native_device *device);
/* requestDevice limit resolution (limits.c); false rejects the request. */
bool native_resolve_limits(const WGPULimits *supported, const WGPULimits *required, WGPULimits *out);
void native_buffer_release(void *userdata, void *handle);
native_failure native_buffer_read(native_buffer *buffer, uint64_t offset, void *bytes, size_t size);
native_failure native_buffer_write(native_buffer *buffer, uint64_t offset, const void *bytes, size_t size);
native_failure native_buffer_create(native_buffer *buffer, uint64_t size, uint32_t usage);
void native_buffer_close(native_buffer *buffer);
native_result native_device_result(native_device *device);
void native_pipeline_release(void *userdata, void *handle);
native_failure native_metal_pipeline_create(native_pipeline *pipeline, const native_stage *stage);
native_failure native_metal_dispatch(doe_mtl_commands *commands, const doe_resource_command *record);
bool native_vulkan_binding_layout(const native_pipeline *pipeline, doe_vk_binding_layout *out);
doe_resource_status native_resolve_resources(const native_pipeline *pipeline,
    doe_resource_pipeline_layout *layout, doe_resource_bind_group *const *groups,
    const doe_resource_dispatch_offsets *offsets, doe_resource_binding *out);
bool native_stage_load(native_device *device, WGPUShaderModule module, WGPUStringView name,
    uint32_t stage, const WGPUConstantEntry *constants, size_t count,
    native_stage *out, native_pipeline_error *error);
void native_stage_release(native_stage *stage);
/* Non-NULL commands also zero-initialize each bound texture's undefined
 * subresources (after an attachment Discard) before the binding is recorded. */
doe_vk_status native_resolve_bindings(doe_vk_commands *commands, const native_pipeline *pipeline,
    doe_resource_pipeline_layout *layout, doe_resource_bind_group *const *groups,
    const doe_resource_dispatch_offsets *offsets, doe_vk_binding_resource *bindings);
doe_vk_status native_record_render(doe_vk_commands *commands,
    const doe_resource_command *records, size_t count, size_t *consumed);
WGPURenderPipeline native_create_render_pipeline(native_device *device,
    const WGPURenderPipelineDescriptor *descriptor, native_pipeline_error *error);
/* The native shader owner selects the shared semantic opt-ins for all targets. */
uint32_t native_shader_semantic_flags(void);
doe_shader_status native_compile_shader(const char *source, size_t size, doe_shader **out, doe_shader_error *error);
doe_shader_status native_check_shader_module(doe_shader_target target, const char *source, size_t size,
    doe_shader_error *error);
/* Sets the calling thread's diagnostic that doeNativeCopyLastError* report
 * (device-less check and binding reflection); a NULL message clears it. */
void native_check_report(const char *stage, const char *kind, const char *message, uint32_t line, uint32_t column);
doe_shader_status native_compile_shader_target(doe_shader_target target, const char *source,
    size_t size, doe_shader **out, doe_shader_error *error);
bool native_shader_capabilities(const doe_shader_view *view, const doe_vk_caps *caps);
bool native_timestamp_supported(const doe_vk_caps *caps);
uint32_t native_timestamp_workgroup(const doe_vk_caps *caps);
doe_vk_status native_timestamp_initialize(native_device *device);
native_failure native_metal_timestamp_initialize(native_device *device);
bool native_metal_timestamp_supported(const doe_mtl_caps *caps);
doe_vk_status native_dispatch_guard_initialize(native_device *device);
doe_vk_status native_draw_guard_initialize(native_device *device);
void native_indirect_guards_release(native_device *device);
/* Optional WebGPU features as bits of their pinned WGPUFeatureName values. */
#define NATIVE_FEATURE(name) (UINT32_C(1) << (uint32_t)(name))
uint32_t native_adapter_features(const native_backend *backend);
bool native_requested_features(const native_backend *backend, const WGPUDeviceDescriptor *descriptor, uint32_t *enabled);
bool native_feature_enabled(const native_device *device, WGPUFeatureName feature);
doe_vk_status native_record_query(doe_vk_commands *commands, const doe_resource_command *record);
WGPUComputePipeline native_create_compute_pipeline(native_device *device,
    const WGPUComputePipelineDescriptor *descriptor, native_pipeline_error *error);
/* Caller holds device mutex. Attempts terminal recovery; unknown work stays
 * linked and retains its backend and resource submissions, never freed. */
typedef struct native_queue_settlement {
    bool complete;
    native_failure failure;
} native_queue_settlement;
native_queue_settlement native_settle_pending(native_device *device);
/* Observes completion without blocking: running work stays linked and is not
 * a failure. routine polls on Vulkan, whose submissions run asynchronously,
 * and recovers on the backends that still wait at submit. */
native_queue_settlement native_poll_pending(native_device *device);
native_queue_settlement native_settle_routine(native_device *device);
/* Caller holds the device mutex. Settles before host access to buffer. */
native_failure native_settle_for_buffer(native_device *device, const native_buffer *buffer);
/* Lowers a resource command list into native commands (the one recorder for
 * queue submission and compute-program recording/replay). */
native_failure native_record_resource_commands(native_command_handle *native, doe_resource_commands *commands);
/* Caller holds the device mutex and has begun a replay of commands. Adopts the
 * native holder and the replay, including on failure. */
native_failure native_submit_program(native_device *device, native_command_handle native,
    doe_resource_commands *commands);
void native_progress_pending(native_device *device);
/* Caller holds the device mutex. Blocking settlement that, like progress,
 * leaves the first terminal failure for the next public observer. */
void native_drain_pending(native_device *device);
/* Prepared before device publication: progress shares the existing pending list
 * and owns one ordinary device lease until all callers and work retire. */
bool native_pending_progress_start(native_device *device);
void native_pending_progress_wake(native_device *device);
void native_pending_progress_destroy(native_device *device);
/* Caller holds device mutex. Always adopts commands, including failed/unknown
 * completion. A private initialization submission has its own device lease. */
native_failure native_submit_commands(native_device *device, doe_vk_commands *commands);
/* Caller holds device mutex. Batched public texture initialization (Vulkan):
 * queue records into device->init_batch; flush submits it, without waiting,
 * before any recording that may touch those textures; release discards an
 * unsubmitted batch at final device release. */
native_failure native_queue_texture_initialization(native_device *device, doe_vk_texture *texture,
    const doe_vk_texture_desc *desc, const doe_vk_format_caps *caps);
native_failure native_flush_initialization(native_device *device);
/* Textures one initialization batch holds before queueing submits it. */
#define NATIVE_INIT_BATCH_TEXTURES 64u
void native_release_initialization(native_device *device);
/* Caller holds the device mutex and one ordinary device lease. Always adopts
 * the tagged command object, including allocation failure/unknown completion. */
native_failure native_submit_backend_commands(native_device *device, native_command_handle commands);
native_failure native_initialize_texture(native_device *device, doe_vk_texture *texture,
    const doe_vk_texture_desc *desc, const doe_vk_format_caps *caps);
/* Resource finalizer for a native_texture payload: releases its backend texture
 * under the device mutex, then frees the payload. */
void native_texture_release(void *userdata, void *handle);
void native_view_release(void *userdata, void *handle);
void native_sampler_release(void *userdata, void *handle);
bool native_compare_operation(WGPUCompareFunction value, uint32_t fallback, uint32_t *out);
/* features: the device's enabled NATIVE_FEATURE bits; feature formats map only when enabled. */
bool native_depth_format(uint32_t features, WGPUTextureFormat format, doe_vk_texture_format *out);
bool native_texture_format(uint32_t features, WGPUTextureFormat format, doe_vk_texture_format *out);
bool native_color_renderable(uint32_t features, WGPUTextureFormat format);
/* Resource-normalized copy aspect to native aspect bits; Undefined is rejected
 * because the resource owner always resolves a concrete aspect first. */
bool native_texture_aspect(WGPUTextureAspect aspect, doe_vk_texture_aspect *out);
bool native_storage_format(uint32_t format, WGPUTextureFormat *out);
doe_resource_status native_automatic_layout(native_device *device,
    const native_stage *stages, size_t stage_count, doe_resource_pipeline_layout **out);
bool native_pipeline_bindings(native_pipeline *payload, const native_stage *stages,
    size_t stage_count, doe_resource_pipeline_layout *layout);
#endif
