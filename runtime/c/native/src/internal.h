/* SPDX-License-Identifier: MIT */
#ifndef DOE_NATIVE_INTERNAL_H
#define DOE_NATIVE_INTERNAL_H
#include "webgpu.h"
#include "doe_instance.h"
#include "doe_callbacks.h"
#include "doe_resources.h"
#include "doe_shader.h"
#include "doe_vk.h"
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
typedef struct native_device {
    atomic_uint references;
    pthread_mutex_t mutex;
    doe_instance *instance;
    doe_vk_context *context;
    doe_vk_caps caps;
    doe_resource_config resources;
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
void native_device_retain(void *device);
void native_device_release(void *device);
bool native_device_alive(void *device);
void native_lock(native_device *device);
void native_unlock(native_device *device);
void native_error(native_device *device, WGPUErrorType type, const char *message);
WGPUStringView native_string(const char *text);
bool native_string_size(WGPUStringView view, size_t *size);
WGPUFuture native_callback(doe_instance *instance, WGPUCallbackMode mode,
    void *context, size_t size, size_t alignment,
    doe_callback_copy_context copy, doe_callback_deliver_context deliver);
WGPUStatus native_adapter_info(const doe_vk_caps *caps, WGPUAdapterInfo *out);
WGPUStatus native_limits(const doe_vk_caps *caps, WGPULimits *out);
void native_buffer_release(void *userdata, void *handle);
void native_pipeline_release(void *userdata, void *handle);
/* Caller holds device mutex. Attempts terminal recovery; unknown work stays
 * linked and retains its backend and resource submissions, never freed. */
bool native_settle_pending(native_device *device);
#endif
