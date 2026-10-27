/* SPDX-License-Identifier: MIT */
#include "surface.h"
#include "d3d12_queries.h"
#include "adapter_request.h"
#include <stdlib.h>
#include <string.h>

WGPUInstance wgpuCreateInstance(const WGPUInstanceDescriptor *descriptor)
{
    if (descriptor) {
        if (descriptor->nextInChain ||
                (descriptor->requiredFeatureCount && !descriptor->requiredFeatures)) return NULL;
        for (size_t i = 0; i < descriptor->requiredFeatureCount; ++i)
            if (!doe_instance_has_feature(descriptor->requiredFeatures[i])) return NULL;
        if (descriptor->requiredLimits && (descriptor->requiredLimits->nextInChain ||
                descriptor->requiredLimits->timedWaitAnyMaxCount >
                    doe_instance_limits().timedWaitAnyMaxCount)) return NULL;
    }
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
    if (doe_mutex_destroy(&adapter->mutex).status != DOE_HOST_OK) abort();
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
    native_adapter_selection selection = {0};
    /* A compatibleSurface enables exactly its platform's WSI chain on this
     * adapter and its devices; absence of that chain is unavailable, not fallback. */
    WGPUSurface compatible = options ? options->compatibleSurface : NULL;
    WGPUBackendType kind = options ? options->backendType : WGPUBackendType_Undefined;
    if (kind == WGPUBackendType_Undefined) {
#if defined(__APPLE__)
        kind = WGPUBackendType_Metal;
#elif defined(_WIN32)
        kind = WGPUBackendType_D3D12;
#else
        kind = WGPUBackendType_Vulkan;
#endif
    }
    const bool valid = native_adapter_request_decode(kind, options, &selection);
    if (valid) {
        native_backend backend = {0};
        const bool force_fallback = options && options->forceFallbackAdapter;
        /* webgpu.h lets any adapter that can present configure a surface;
         * compatibleSurface only narrows selection to adapters that can. So a
         * default Vulkan request enables every composed WSI platform when the
         * loader and device provide it, and opens without WSI when they do not
         * (UNSUPPORTED; UNAVAILABLE means no such device at all). The
         * device order is kept: a non-CPU device without WSI is preferred to
         * the CPU fallback with it. */
        const uint32_t required = native_surface_platform(compatible);
        const bool discover = !compatible && kind == WGPUBackendType_Vulkan && native_surface_default_platforms();
        native_result status = selection.has_luid ?
            native_backend_open_luid(selection.luid_low, selection.luid_high, required, &backend) :
            native_backend_open(kind, force_fallback,
                discover ? native_surface_default_platforms() : required, &backend);
        if (discover && status == NATIVE_UNSUPPORTED)
            status = native_backend_open(kind, force_fallback, 0, &backend);
        /* WebGPU may answer a default request with the fallback adapter; on
         * Vulkan a host whose only device is a CPU implementation (lavapipe)
         * gets it from the default open, reported as the fallback adapter,
         * and a non-CPU device that fails admission retries with the CPU one. */
        if ((status == NATIVE_UNAVAILABLE || status == NATIVE_UNSUPPORTED) && !force_fallback &&
            kind == WGPUBackendType_Vulkan) {
            status = native_backend_open(kind, true, discover ? native_surface_default_platforms() : required, &backend);
            if (discover && status == NATIVE_UNSUPPORTED)
                status = native_backend_open(kind, true, 0, &backend);
        }
        if (status == NATIVE_OK && compatible) {
            status = native_surface_compatible(compatible, &backend);
            if (status != NATIVE_OK) native_backend_close(&backend);
        }
        if (status == NATIVE_OK) {
            native_adapter *adapter = calloc(1, sizeof(*adapter));
            if (adapter && doe_mutex_init(&adapter->mutex, false).status != DOE_HOST_OK) {
                if (doe_mutex_destroy(&adapter->mutex).status != DOE_HOST_OK) abort();
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
    doe_callback_status admitted;
    WGPUFuture future = native_callback_admit(instance, callback.mode, &delivery,
        sizeof(delivery), _Alignof(adapter_delivery), copy_adapter, deliver_adapter, &admitted);
    if (!future.id) {
        wgpuAdapterRelease((WGPUAdapter)delivery.adapter);
        /* No device channel exists here. A spontaneous callback may run on the
         * calling thread, so a refusal for lack of memory is delivered now as
         * an error; other modes only ever run inside WaitAny/ProcessEvents and
         * get the null future, webgpu.h's synchronous-error sentinel. */
        if (admitted == DOE_CALLBACK_OUT_OF_MEMORY && callback.mode == WGPUCallbackMode_AllowSpontaneous &&
                callback.callback)
            callback.callback(WGPURequestAdapterStatus_Error, NULL,
                native_string("adapter callback registration failed for lack of memory"),
                callback.userdata1, callback.userdata2);
    }
    doe_instance_release(instance);
    return future;
}

static void uncaptured_error(uint32_t type, WGPUStringView message,
    void *userdata1, void *userdata2)
{
    (void)userdata2;
    native_device *device = userdata1;
    native_lock(device);
    WGPUUncapturedErrorCallbackInfo callback = device->uncaptured;
    DoeLegacyUncapturedErrorCallback legacy = device->legacy_uncaptured;
    native_unlock(device);
    if (legacy) {
        legacy(type, message, callback.userdata1, callback.userdata2);
        return;
    }
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
            !doe_thread_equal(doe_thread_current(), doe_thread_identity(&device->pending_thread))) {
        /* No pending native use remains. Keep this caller's lease while joining
         * so ordinary final release completes before a loader may unload us. */
        device->pending_progress_join = true;
        native_pending_progress_wake(device);
        native_unlock(device);
        /* This caller's ordinary lease keeps the stored handle alive while the
         * worker drops its lease; join consumes the one real host handle. */
        if (doe_thread_join(&device->pending_thread).status != DOE_HOST_OK) abort();
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
    native_release_initialization(device);
    native_pending_progress_destroy(device);
    if (doe_error_scope_deinit(device->errors).status != DOE_ERROR_SCOPE_OK) abort();
    free(device->error_storage);
    if (doe_label_store_destroy(device->labels) != DOE_LABEL_OK) abort();
    native_loss_bind(device->loss, NULL);
    native_loss_release(device->loss);
    doe_vk_timestamp_normalizer_release(device->timestamp_normalizer);
    doe_mtl_pipeline_release(device->metal_timestamp_pipeline);
    native_d3d12_timestamp_close(device);
    native_indirect_guards_release(device);
    doe_mtl_dispatch_guard_release(device->metal_dispatch_guard);
    doe_mtl_draw_guard_release(device->metal_draw_guard);
    native_shader_cache_clear(&device->shaders);
    native_backend_close(&device->backend);
    wgpuAdapterRelease((WGPUAdapter)device->adapter);
    doe_instance *instance = device->instance;
    if (doe_mutex_destroy(&device->mutex).status != DOE_HOST_OK) abort();
    free(device);
    doe_instance_release(instance);
}

static native_device *create_device(native_adapter *adapter, const WGPUDeviceDescriptor *descriptor, native_loss *loss, const DoeNativeDevicePolicy *policy)
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
    if (doe_mutex_init(&device->mutex, true).status != DOE_HOST_OK) {
        if (doe_mutex_destroy(&device->mutex).status != DOE_HOST_OK) abort();
        free(device);
        return NULL;
    }
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
    if (native_backend_device_policy(&adapter->backend, policy, &device->backend) != NATIVE_OK) goto failed_errors;
    /* The device context reopens the adapter's own hardware, so its facts match
     * the adapter's; every frontend check reads the resolved device limits. */
    device->backend.limits = limits;
    device->features = features;
    if ((features & NATIVE_FEATURE(WGPUFeatureName_TimestampQuery)) &&
        (device->backend.kind == WGPUBackendType_D3D12 ?
            native_d3d12_timestamp_initialize(device).status != NATIVE_OK :
        device->backend.kind == WGPUBackendType_Metal ?
            native_metal_timestamp_initialize(device).status != NATIVE_OK :
            native_timestamp_initialize(device) != DOE_VK_OK)) {
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
        .format_features = (native_feature_enabled(device,WGPUFeatureName_BGRA8UnormStorage) ? DOE_RESOURCE_FORMAT_BGRA8_STORAGE : 0) |
            (native_feature_enabled(device,WGPUFeatureName_TextureFormatsTier1) ? DOE_RESOURCE_FORMAT_TIER1 : 0) |
            (native_feature_enabled(device,WGPUFeatureName_TextureFormatsTier2) ? DOE_RESOURCE_FORMAT_TIER2 : 0),
        .max_texture_dimension_2d = device->backend.limits.maxTextureDimension2D,
        .max_color_attachments = device->backend.limits.maxColorAttachments,
        .max_color_attachment_bytes_per_sample = device->backend.limits.maxColorAttachmentBytesPerSample,
        .max_immediate_size = device->backend.limits.maxImmediateSize,
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
        doe_mtl_pipeline_release(device->metal_timestamp_pipeline);
        native_d3d12_timestamp_close(device);
        native_backend_close(&device->backend);
        doe_instance_release(device->instance);
        goto failed_errors;
    }
    device->adapter = adapter;
    wgpuAdapterAddRef((WGPUAdapter)adapter);
    native_loss_bind(loss, device);
    return device;
failed_errors:
    if (doe_label_store_destroy(device->labels) != DOE_LABEL_OK) abort();
    if (doe_error_scope_deinit(device->errors).status != DOE_ERROR_SCOPE_OK) abort();
failed_storage:
    free(device->error_storage);
failed_mutex:
    if (doe_mutex_destroy(&device->mutex).status != DOE_HOST_OK) abort();
    free(device);
    return NULL;
}

WGPUDevice wgpuAdapterCreateDevice(WGPUAdapter handle, const WGPUDeviceDescriptor *descriptor)
{
    native_adapter *adapter = (native_adapter *)handle;
    if (!adapter) return NULL;
    wgpuAdapterAddRef(handle);
    WGPUDeviceLostCallbackInfo callback = descriptor ? descriptor->deviceLostCallbackInfo :
        (WGPUDeviceLostCallbackInfo){0};
    native_loss *loss = native_loss_create(adapter->instance, callback);
    native_device *device = loss ? create_device(adapter, descriptor, loss, NULL) : NULL;
    if (loss && !device) {
        native_loss_complete(loss, WGPUDeviceLostReason_FailedCreation, "device initialization failed");
        native_loss_release(loss);
    }
    wgpuAdapterRelease(handle);
    return (WGPUDevice)device;
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

static WGPUFuture request_device(WGPUAdapter handle,
    const WGPUDeviceDescriptor *descriptor, WGPURequestDeviceCallbackInfo callback,
    const DoeNativeDevicePolicy *policy, bool valid_policy)
{
    native_adapter *adapter = (native_adapter *)handle;
    if (!adapter || callback.nextInChain) return (WGPUFuture){0};
    wgpuAdapterAddRef(handle);
    WGPUDeviceLostCallbackInfo lost = descriptor ? descriptor->deviceLostCallbackInfo :
        (WGPUDeviceLostCallbackInfo){0};
    native_loss *loss = native_loss_create(adapter->instance, lost);
    device_delivery delivery = {callback, loss && valid_policy ? create_device(adapter, descriptor, loss, policy) : NULL};
    if (loss && !delivery.device) {
        native_loss_complete(loss, WGPUDeviceLostReason_FailedCreation, "device initialization failed");
        native_loss_release(loss);
    }
    doe_callback_status admitted;
    WGPUFuture future = native_callback_admit(adapter->instance, callback.mode, &delivery,
        sizeof(delivery), _Alignof(device_delivery), copy_device, deliver_device, &admitted);
    if (!future.id) {
        wgpuDeviceRelease((WGPUDevice)delivery.device);
        /* As for adapters: spontaneous mode reports the refusal now; others
         * get the null future. */
        if (admitted == DOE_CALLBACK_OUT_OF_MEMORY && callback.mode == WGPUCallbackMode_AllowSpontaneous &&
                callback.callback)
            callback.callback(WGPURequestDeviceStatus_Error, NULL,
                native_string("device callback registration failed for lack of memory"),
                callback.userdata1, callback.userdata2);
    }
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

WGPUFuture wgpuAdapterRequestDevice(WGPUAdapter handle,
    const WGPUDeviceDescriptor *descriptor, WGPURequestDeviceCallbackInfo callback)
{
    return request_device(handle, descriptor, callback, NULL, true);
}

WGPUFuture doeNativeAdapterRequestDeviceWithPolicy(WGPUAdapter adapter,
    const WGPUDeviceDescriptor *descriptor, WGPURequestDeviceCallbackInfo callback,
    uint32_t version, const DoeNativeDevicePolicy *policy, size_t policy_size)
{
    bool valid = version == DOE_NATIVE_DEVICE_POLICY_ABI_VERSION && policy && policy_size == sizeof(*policy);
    DoeNativeDevicePolicy snapshot = {0};
    if (valid) snapshot = *policy;
    return request_device(adapter, descriptor, callback, &snapshot, valid);
}
