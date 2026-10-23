/* SPDX-License-Identifier: MIT */
#ifndef DOE_NATIVE_INTERNAL_H
#define DOE_NATIVE_INTERNAL_H
#include "webgpu.h"
#include "doe_instance.h"
#include "doe_callbacks.h"
#include "doe_resources.h"
#include "doe_shader.h"
#include "doe_vk.h"
#include "doe_vk_bindings.h"
#include "doe_error_scopes.h"
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>

typedef struct native_adapter {
    atomic_uint references;
    doe_instance *instance;
    doe_vk_options options;
    doe_vk_caps caps;
} native_adapter;

typedef struct native_pending native_pending;
typedef struct native_loss native_loss;
typedef struct native_device {
    atomic_uint references;
    atomic_uint external_references;
    native_loss *loss;
    pthread_mutex_t mutex;
    doe_instance *instance;
    doe_vk_context *context;
    doe_vk_caps caps;
    doe_resource_config resources;
    doe_label_store *labels;
    unsigned char queue_label_key;
    doe_error_scope_stack *errors;
    void *error_storage;
    WGPUUncapturedErrorCallbackInfo uncaptured;
    bool destroyed;
    bool queue_failed;
    native_pending *pending;
} native_device;

/* Native payloads have no independent reference count. Their resource owner
 * alone releases them after all public/recorded/submission references settle. */
typedef struct native_buffer {
    native_device *device; /* Borrowed through the resource's device lease. */
    doe_vk_buffer *buffer;
    unsigned char *mapping;
} native_buffer;

typedef struct native_shader {
    doe_shader *compiled;
    doe_shader_error error;
} native_shader;

typedef struct native_texture {
    native_device *device;
    doe_vk_texture *texture;
} native_texture;
typedef struct native_view {
    native_device *device;
    doe_vk_texture_view *view;
} native_view;
typedef struct native_sampler {
    native_device *device;
    doe_vk_sampler *sampler;
} native_sampler;

typedef struct native_pipeline_error {
    WGPUErrorType type;
    const char *message;
} native_pipeline_error;

typedef struct native_pipeline {
    native_device *device;
    doe_vk_pipeline *pipeline;
    size_t binding_count;
    doe_vk_binding_layout bindings[DOE_RESOURCE_MAX_GROUPS * DOE_RESOURCE_MAX_BINDINGS];
    uint64_t minimum_sizes[DOE_RESOURCE_MAX_GROUPS * DOE_RESOURCE_MAX_BINDINGS];
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
void native_lock(native_device *device);
void native_unlock(native_device *device);
void native_error(native_device *device, WGPUErrorType type, const char *message);
WGPUErrorType native_resource_error(doe_resource_status status);
WGPUErrorType native_backend_error(doe_vk_status status);
WGPUErrorType native_label_error(doe_label_status status);
void native_label(native_device *device, const void *identity, WGPUStringView label);
WGPUStringView native_string(const char *text);
bool native_string_size(WGPUStringView view, size_t *size);
WGPUFuture native_callback(doe_instance *instance, WGPUCallbackMode mode,
    void *context, size_t size, size_t alignment,
    doe_callback_copy_context copy, doe_callback_deliver_context deliver);
WGPUStatus native_adapter_info(const doe_vk_caps *caps, WGPUAdapterInfo *out);
WGPUStatus native_limits(const doe_vk_caps *caps, WGPULimits *out);
void native_buffer_release(void *userdata, void *handle);
void native_pipeline_release(void *userdata, void *handle);
WGPUComputePipeline native_create_compute_pipeline(native_device *device,
    const WGPUComputePipelineDescriptor *descriptor, native_pipeline_error *error);
/* Caller holds device mutex. Attempts terminal recovery; unknown work stays
 * linked and retains its backend and resource submissions, never freed. */
bool native_settle_pending(native_device *device);
/* Caller holds device mutex. Always adopts commands, including failed/unknown
 * completion. A private initialization submission has its own device lease. */
doe_vk_status native_submit_commands(native_device *device, doe_vk_commands *commands);
bool native_texture_format(WGPUTextureFormat format, doe_vk_texture_format *out);
bool native_storage_format(uint32_t format, WGPUTextureFormat *out);
doe_resource_status native_automatic_layout(native_device *device,
    const doe_shader_view *view, const DoeCompilerEntryV5 *entry, doe_resource_pipeline_layout **out);
bool native_pipeline_bindings(native_pipeline *payload, const doe_shader_view *view,
    const DoeCompilerEntryV5 *entry, doe_resource_pipeline_layout *layout);
#endif
