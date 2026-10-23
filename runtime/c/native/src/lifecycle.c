/* SPDX-License-Identifier: MIT */
#include "surface.h"
#include <stdlib.h>
#include <string.h>

WGPUInstance wgpuCreateInstance(const WGPUInstanceDescriptor *descriptor)
{
    if (descriptor && (descriptor->nextInChain || descriptor->requiredFeatureCount ||
            descriptor->requiredLimits)) return NULL;
    doe_instance_config config = {
        .abi_version = DOE_INSTANCE_ABI_VERSION,
        .object_allocator = {NULL, NULL, native_allocate, native_deallocate},
        .wait_allocator = {NULL, NULL, native_allocate, native_deallocate}
    };
    doe_instance *instance = NULL;
    if (doe_instance_create(&config, &instance) != DOE_INSTANCE_OK) return NULL;
    return (WGPUInstance)instance;
}

void wgpuInstanceAddRef(WGPUInstance instance) { doe_instance_retain((doe_instance *)instance); }
void wgpuInstanceRelease(WGPUInstance instance) { doe_instance_release((doe_instance *)instance); }
void wgpuInstanceProcessEvents(WGPUInstance instance) { doe_instance_process_events((doe_instance *)instance); }
WGPUWaitStatus wgpuInstanceWaitAny(WGPUInstance instance, size_t count,
    WGPUFutureWaitInfo *infos, uint64_t timeout)
{
    return doe_instance_wait_any((doe_instance *)instance, count, infos, timeout);
}

void wgpuAdapterAddRef(WGPUAdapter handle)
{
    native_adapter *adapter = (native_adapter *)handle;
    if (adapter) native_retain_count(&adapter->references);
}

void wgpuAdapterRelease(WGPUAdapter handle)
{
    native_adapter *adapter = (native_adapter *)handle;
    if (!adapter || !native_release_count(&adapter->references)) return;
    doe_instance *instance = adapter->instance;
    native_backend_close(&adapter->backend);
    if (pthread_mutex_destroy(&adapter->mutex)) abort();
    free(adapter);
    doe_instance_release(instance);
}

typedef struct adapter_delivery {
    WGPURequestAdapterCallbackInfo callback;
    WGPURequestAdapterStatus status;
    native_adapter *adapter;
    const char *message;
} adapter_delivery;

static void copy_adapter(void *destination, const void *source)
{
    *(adapter_delivery *)destination = *(const adapter_delivery *)source;
}

static void deliver_adapter(void *context)
{
    adapter_delivery value = *(adapter_delivery *)context;
    if (value.callback.callback)
        value.callback.callback(value.status, (WGPUAdapter)value.adapter,
            native_string(value.message), value.callback.userdata1, value.callback.userdata2);
    else wgpuAdapterRelease((WGPUAdapter)value.adapter);
}

WGPUFuture wgpuInstanceRequestAdapter(WGPUInstance handle,
    const WGPURequestAdapterOptions *options, WGPURequestAdapterCallbackInfo callback)
{
    doe_instance *instance = (doe_instance *)handle;
    if (!instance || callback.nextInChain) return (WGPUFuture){0};
    doe_instance_retain(instance);
    adapter_delivery delivery = {
        .callback = callback, .status = WGPURequestAdapterStatus_Unavailable,
        .message = "requested C backend unavailable"
    };
    bool valid = !options || !options->nextInChain;
    /* A compatibleSurface enables exactly its platform's WSI chain on this
     * adapter and its devices; absence of that chain is unavailable, not fallback. */
    WGPUSurface compatible = options ? options->compatibleSurface : NULL;
    WGPUBackendType kind = options ? options->backendType : WGPUBackendType_Undefined;
    if (kind == WGPUBackendType_Undefined) {
#if defined(__APPLE__)
        kind = WGPUBackendType_Metal;
#else
        kind = WGPUBackendType_Vulkan;
#endif
    }
    if (valid) {
        native_backend backend = {0};
        native_result status = native_backend_open(kind, options && options->forceFallbackAdapter,
            native_surface_platform(compatible), &backend);
        if (status == NATIVE_OK && compatible) {
            status = native_surface_compatible(compatible, &backend);
            if (status != NATIVE_OK) native_backend_close(&backend);
        }
        if (status == NATIVE_OK) {
            native_adapter *adapter = calloc(1, sizeof(*adapter));
            if (adapter && pthread_mutex_init(&adapter->mutex, NULL)) {
                free(adapter);
                adapter = NULL;
            }
            if (adapter) {
                atomic_init(&adapter->references, 1);
                adapter->instance = instance;
                doe_instance_retain(instance);
                adapter->backend = backend;
                delivery.adapter = adapter;
                delivery.status = WGPURequestAdapterStatus_Success;
                delivery.message = "";
            } else {
                native_backend_close(&backend);
                delivery.status = WGPURequestAdapterStatus_Error;
                delivery.message = "adapter allocation failed";
            }
        } else if (status != NATIVE_UNAVAILABLE && status != NATIVE_UNSUPPORTED) {
            delivery.status = WGPURequestAdapterStatus_Error;
            delivery.message = "requested backend initialization failed";
        }
    } else delivery.message = "requested adapter extension is unsupported";
    WGPUFuture future = native_callback(instance, callback.mode, &delivery,
        sizeof(delivery), _Alignof(adapter_delivery), copy_adapter, deliver_adapter);
    if (!future.id) wgpuAdapterRelease((WGPUAdapter)delivery.adapter);
    doe_instance_release(instance);
    return future;
}

static void uncaptured_error(uint32_t type, WGPUStringView message,
    void *userdata1, void *userdata2)
{
    (void)userdata2;
    native_device *device = userdata1;
    WGPUUncapturedErrorCallbackInfo callback = device->uncaptured;
    if (callback.callback) {
        WGPUDevice handle = (WGPUDevice)device;
        callback.callback(&handle, (WGPUErrorType)type, message, callback.userdata1, callback.userdata2);
    }
}

void native_device_retain(void *identity)
{
    native_device *device = identity;
    if (device) native_retain_count(&device->references);
}

bool native_device_alive(void *identity)
{
    return native_device_result(identity) == NATIVE_OK;
}

native_result native_device_result(native_device *device)
{
    if (!device || device->destroyed) return NATIVE_INVALID_STATE;
    return native_backend_status(&device->backend);
}

doe_vk_status native_device_status(native_device *device)
{
    if (!device || device->backend.kind != WGPUBackendType_Vulkan) return DOE_VK_UNSUPPORTED;
    return native_to_vulkan(native_device_result(device));
}

void native_device_release(void *identity)
{
    native_device *device = identity;
    if (!device) return;
    /* Serialize the last caller's drop with the progress owner's sleep/exit.
     * Its ordinary device lease prevents finalization while work is unresolved. */
    native_lock(device);
    if (device->pending_progress_ready && !device->pending &&
            atomic_load_explicit(&device->references, memory_order_acquire) == 2 &&
            !pthread_equal(pthread_self(), device->pending_thread)) {
        /* No pending native use remains. Keep this caller's lease while joining
         * so ordinary final release completes before a loader may unload us. */
        device->pending_progress_join = true;
        pthread_t thread = device->pending_thread;
        native_pending_progress_wake(device);
        native_unlock(device);
        if (pthread_join(thread, NULL)) abort();
        native_device_release(device);
        return;
    }
    bool last = native_release_count(&device->references);
    if (!last) native_pending_progress_wake(device);
    native_unlock(device);
    if (!last) return;
    /* Pending resource submissions hold device leases, so finalization cannot
     * race or discard an unresolved backend obligation. */
    if (device->pending) abort();
    native_pending_progress_destroy(device);
    if (doe_error_scope_deinit(device->errors).status != DOE_ERROR_SCOPE_OK) abort();
    free(device->error_storage);
    if (doe_label_store_destroy(device->labels) != DOE_LABEL_OK) abort();
    native_loss_bind(device->loss, NULL);
    native_loss_release(device->loss);
    doe_vk_timestamp_normalizer_release(device->timestamp_normalizer);
    native_indirect_guards_release(device);
    native_backend_close(&device->backend);
    doe_instance *instance = device->instance;
    if (pthread_mutex_destroy(&device->mutex)) abort();
    free(device);
    doe_instance_release(instance);
}

static native_device *create_device(native_adapter *adapter, const WGPUDeviceDescriptor *descriptor, native_loss *loss)
{
    uint32_t features = 0;
    if (!native_requested_features(&adapter->backend, descriptor, &features)) return NULL;
    if (descriptor && (descriptor->nextInChain || descriptor->defaultQueue.nextInChain ||
        descriptor->deviceLostCallbackInfo.nextInChain ||
        descriptor->uncapturedErrorCallbackInfo.nextInChain)) return NULL;
    WGPULimits limits;
    if (!native_resolve_limits(&adapter->backend.limits, descriptor ? descriptor->requiredLimits : NULL, &limits))
        return NULL;
    native_device *device = calloc(1, sizeof(*device));
    if (!device) return NULL;
    pthread_mutexattr_t attributes;
    if (pthread_mutexattr_init(&attributes)) { free(device); return NULL; }
    int mutex_status = pthread_mutexattr_settype(&attributes, PTHREAD_MUTEX_RECURSIVE);
    if (!mutex_status) mutex_status = pthread_mutex_init(&device->mutex, &attributes);
    if (pthread_mutexattr_destroy(&attributes)) abort();
    if (mutex_status) { free(device); return NULL; }
    doe_error_scope_requirements requirements;
    doe_error_scope_result result = doe_error_scope_storage_requirements(DOE_ERROR_SCOPES_ABI_VERSION,
        &requirements);
    if (result.status != DOE_ERROR_SCOPE_OK) goto failed_mutex;
    device->error_storage = native_allocate(NULL, NULL, requirements.size, requirements.alignment);
    if (!device->error_storage) goto failed_mutex;
    result = doe_error_scope_init(DOE_ERROR_SCOPES_ABI_VERSION, device->error_storage,
        requirements.size, &device->errors);
    if (result.status != DOE_ERROR_SCOPE_OK) {
        if (result.status == DOE_ERROR_SCOPE_CLEANUP_REQUIRED) abort();
        goto failed_storage;
    }
    doe_label_config label_config = {.abi_version=DOE_LABELS_ABI_VERSION,
        .allocator={NULL,NULL,native_allocate,native_deallocate}};
    if (doe_label_store_create(&label_config, &device->labels) != DOE_LABEL_OK) goto failed_errors;
    if (descriptor) {
        size_t label_size, queue_size;
        if (!native_string_size(descriptor->label, &label_size) ||
            !native_string_size(descriptor->defaultQueue.label, &queue_size) ||
            doe_label_store_set(device->labels, device, (const unsigned char *)descriptor->label.data,
                label_size) != DOE_LABEL_OK ||
            doe_label_store_set(device->labels, &device->queue_label_key,
                (const unsigned char *)descriptor->defaultQueue.label.data, queue_size) != DOE_LABEL_OK)
            goto failed_errors;
    }
    if (native_backend_device(&adapter->backend, &device->backend) != NATIVE_OK) goto failed_errors;
    /* The device context reopens the adapter's own hardware, so its facts match
     * the adapter's; every frontend check reads the resolved device limits. */
    device->backend.limits = limits;
    device->features = features;
    if ((features & NATIVE_FEATURE(WGPUFeatureName_TimestampQuery)) && native_timestamp_initialize(device) != DOE_VK_OK) {
        native_backend_close(&device->backend);
        goto failed_errors;
    }
    atomic_init(&device->references, 1);
    atomic_init(&device->external_references, 1);
    device->loss = loss;
    device->instance = adapter->instance;
    doe_instance_retain(device->instance);
    device->resources = (doe_resource_config){
        .allocator = {NULL, NULL, native_allocate, native_deallocate},
        .device = {device, native_device_alive, native_device_retain, native_device_release},
        .labels = device->labels,
        .max_texture_dimension_2d = device->backend.limits.maxTextureDimension2D,
        .max_color_attachments = device->backend.limits.maxColorAttachments,
        .max_color_attachment_bytes_per_sample = device->backend.limits.maxColorAttachmentBytesPerSample,
        .slot_limits = {
            .max_bind_groups = limits.maxBindGroups,
            .max_dynamic_uniform_buffers = limits.maxDynamicUniformBuffersPerPipelineLayout,
            .max_dynamic_storage_buffers = limits.maxDynamicStorageBuffersPerPipelineLayout,
            .max_sampled_textures = limits.maxSampledTexturesPerShaderStage,
            .max_samplers = limits.maxSamplersPerShaderStage,
            .max_storage_buffers = limits.maxStorageBuffersPerShaderStage,
            .max_storage_textures = limits.maxStorageTexturesPerShaderStage,
            .max_uniform_buffers = limits.maxUniformBuffersPerShaderStage
        }
    };
    if (descriptor) device->uncaptured = descriptor->uncapturedErrorCallbackInfo;
    result = doe_error_scope_set_handler(device->errors, uncaptured_error, device, NULL);
    if (result.status != DOE_ERROR_SCOPE_OK) abort();
    if (!native_pending_progress_start(device)) {
        doe_vk_timestamp_normalizer_release(device->timestamp_normalizer);
        native_backend_close(&device->backend);
        doe_instance_release(device->instance);
        goto failed_errors;
    }
    native_loss_bind(loss, device);
    return device;
failed_errors:
    if (doe_label_store_destroy(device->labels) != DOE_LABEL_OK) abort();
    if (doe_error_scope_deinit(device->errors).status != DOE_ERROR_SCOPE_OK) abort();
failed_storage:
    free(device->error_storage);
failed_mutex:
    if (pthread_mutex_destroy(&device->mutex)) abort();
    free(device);
    return NULL;
}

typedef struct device_delivery {
    WGPURequestDeviceCallbackInfo callback;
    native_device *device;
} device_delivery;

static void copy_device(void *destination, const void *source)
{
    *(device_delivery *)destination = *(const device_delivery *)source;
}

static void deliver_device(void *context)
{
    device_delivery value = *(device_delivery *)context;
    if (value.callback.callback)
        value.callback.callback(value.device ? WGPURequestDeviceStatus_Success : WGPURequestDeviceStatus_Error,
            (WGPUDevice)value.device, native_string(value.device ? "" : "C device creation failed or descriptor unsupported"),
            value.callback.userdata1, value.callback.userdata2);
    else wgpuDeviceRelease((WGPUDevice)value.device);
}

WGPUFuture wgpuAdapterRequestDevice(WGPUAdapter handle,
    const WGPUDeviceDescriptor *descriptor, WGPURequestDeviceCallbackInfo callback)
{
    native_adapter *adapter = (native_adapter *)handle;
    if (!adapter || callback.nextInChain) return (WGPUFuture){0};
    wgpuAdapterAddRef(handle);
    WGPUDeviceLostCallbackInfo lost = descriptor ? descriptor->deviceLostCallbackInfo :
        (WGPUDeviceLostCallbackInfo){0};
    native_loss *loss = native_loss_create(adapter->instance, lost);
    device_delivery delivery = {callback, loss ? create_device(adapter, descriptor, loss) : NULL};
    if (loss && !delivery.device) {
        native_loss_complete(loss, WGPUDeviceLostReason_FailedCreation, "device initialization failed");
        native_loss_release(loss);
    }
    WGPUFuture future = native_callback(adapter->instance, callback.mode, &delivery,
        sizeof(delivery), _Alignof(device_delivery), copy_device, deliver_device);
    if (!future.id) wgpuDeviceRelease((WGPUDevice)delivery.device);
    wgpuAdapterRelease(handle);
    return future;
}

void wgpuDeviceDestroy(WGPUDevice handle)
{
    native_device *device = (native_device *)handle;
    if (!device) return;
    native_device_retain(device);
    native_lock(device);
    bool first = !device->destroyed;
    if (first) {
        /* Destruction does not observe a queue error. Poll without consuming
         * its receipt; a later work-done callback must retain the first detail. */
        native_progress_pending(device);
        device->destroyed = true;
        if (doe_error_scope_set_handler(device->errors, NULL, NULL, NULL).status != DOE_ERROR_SCOPE_OK) abort();
    }
    native_unlock(device);
    if (first) native_loss_complete(device->loss, WGPUDeviceLostReason_Destroyed, "device destroyed");
    native_device_release(device);
}

WGPUQueue wgpuDeviceGetQueue(WGPUDevice handle)
{
    native_device_retain((native_device *)handle);
    return (WGPUQueue)handle;
}
void wgpuQueueAddRef(WGPUQueue queue) { native_device_retain((native_device *)queue); }
void wgpuQueueRelease(WGPUQueue queue) { native_device_release((native_device *)queue); }
WGPUStatus wgpuAdapterGetInfo(WGPUAdapter adapter, WGPUAdapterInfo *out)
{
    return adapter ? native_backend_info(&((native_adapter *)adapter)->backend, out) : WGPUStatus_Error;
}
WGPUStatus wgpuDeviceGetAdapterInfo(WGPUDevice device, WGPUAdapterInfo *out)
{
    return device ? native_backend_info(&((native_device *)device)->backend, out) : WGPUStatus_Error;
}
WGPUStatus wgpuAdapterGetLimits(WGPUAdapter adapter, WGPULimits *out)
{
    return adapter ? native_limits(&((native_adapter *)adapter)->backend, out) : WGPUStatus_Error;
}
WGPUStatus wgpuDeviceGetLimits(WGPUDevice device, WGPULimits *out)
{
    return device ? native_limits(&((native_device *)device)->backend, out) : WGPUStatus_Error;
}
