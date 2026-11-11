#include "doe_napi_internal.h"

/* Forward declarations for methods defined in other nd files */
napi_value native_direct_queue_submit(napi_env env, napi_callback_info info);
napi_value native_direct_queue_write_buffer(napi_env env, napi_callback_info info);
napi_value native_direct_queue_write_buffer_batch(napi_env env, napi_callback_info info);
napi_value native_direct_queue_write_texture(napi_env env, napi_callback_info info);
napi_value native_direct_queue_on_submitted_work_done(napi_env env, napi_callback_info info);
napi_value native_direct_buffer_map_async(napi_env env, napi_callback_info info);
napi_value native_direct_buffer_get_mapped_range(napi_env env, napi_callback_info info);
napi_value native_direct_buffer_read_copy(napi_env env, napi_callback_info info);
napi_value native_direct_buffer_map_read_copy_unmap(napi_env env, napi_callback_info info);
napi_value native_direct_buffer_unmap(napi_env env, napi_callback_info info);
napi_value native_direct_buffer_destroy(napi_env env, napi_callback_info info);
napi_value native_direct_device_create_buffer(napi_env env, napi_callback_info info);
napi_value native_direct_device_create_shader_module(napi_env env, napi_callback_info info);
napi_value native_direct_device_create_compute_pipeline(napi_env env, napi_callback_info info);
napi_value native_direct_device_create_compute_pipeline_async(napi_env env, napi_callback_info info);
napi_value native_direct_device_create_bind_group_layout(napi_env env, napi_callback_info info);
napi_value native_direct_device_create_bind_group(napi_env env, napi_callback_info info);
napi_value native_direct_device_create_pipeline_layout(napi_env env, napi_callback_info info);
napi_value native_direct_device_create_command_encoder(napi_env env, napi_callback_info info);
napi_value native_direct_device_destroy(napi_env env, napi_callback_info info);
napi_value native_direct_device_add_event_listener(napi_env env, napi_callback_info info);
napi_value native_direct_device_remove_event_listener(napi_env env, napi_callback_info info);
napi_value native_direct_device_import_external_texture(napi_env env, napi_callback_info info);
napi_value native_direct_command_encoder_begin_compute_pass(napi_env env, napi_callback_info info);
napi_value native_direct_command_encoder_copy_buffer_to_buffer(napi_env env, napi_callback_info info);
napi_value native_direct_command_encoder_clear_buffer(napi_env env, napi_callback_info info);
napi_value native_direct_command_encoder_copy_texture_to_texture(napi_env env, napi_callback_info info);
napi_value native_direct_command_encoder_finish(napi_env env, napi_callback_info info);
napi_value native_direct_compute_pass_set_pipeline(napi_env env, napi_callback_info info);
napi_value native_direct_compute_pass_set_bind_group(napi_env env, napi_callback_info info);
napi_value native_direct_compute_pass_dispatch_workgroups(napi_env env, napi_callback_info info);
napi_value native_direct_compute_pass_dispatch_workgroups_indirect(napi_env env, napi_callback_info info);
napi_value native_direct_compute_pass_end(napi_env env, napi_callback_info info);
napi_value native_direct_compute_pass_set_immediates(napi_env env, napi_callback_info info);
napi_value native_direct_compute_pass_push_debug_group(napi_env env, napi_callback_info info);
napi_value native_direct_compute_pass_pop_debug_group(napi_env env, napi_callback_info info);
napi_value native_direct_compute_pass_insert_debug_marker(napi_env env, napi_callback_info info);
napi_value native_direct_render_pass_set_immediates(napi_env env, napi_callback_info info);
napi_value native_direct_render_pass_set_viewport(napi_env env, napi_callback_info info);
napi_value native_direct_render_pass_set_scissor_rect(napi_env env, napi_callback_info info);
napi_value native_direct_render_pass_set_blend_constant(napi_env env, napi_callback_info info);
napi_value native_direct_render_pass_set_stencil_reference(napi_env env, napi_callback_info info);
napi_value native_direct_render_pass_push_debug_group(napi_env env, napi_callback_info info);
napi_value native_direct_render_pass_pop_debug_group(napi_env env, napi_callback_info info);
napi_value native_direct_render_pass_insert_debug_marker(napi_env env, napi_callback_info info);
napi_value native_direct_render_pass_draw_indirect(napi_env env, napi_callback_info info);
napi_value native_direct_render_pass_draw_indexed_indirect(napi_env env, napi_callback_info info);
napi_value native_direct_render_bundle_encoder_set_immediates(napi_env env, napi_callback_info info);
napi_value doe_get_preferred_canvas_format(napi_env env, napi_callback_info info);
void doe_free_device_descriptor_owned(WGPUDeviceDescriptor* desc, WGPUFeatureName* required_features);
bool doe_feature_entry_at(size_t index, const char** out_name, uint32_t* out_value);

typedef struct {
    const char* name;
    napi_callback fn;
    size_t ref_offset;
} NdMethodSpec;

static void nd_bind_methods(napi_env env, napi_value obj, const NdMethodSpec* specs, size_t count) {
    char* state_base = (char*)doe_env_state(env);
    for (size_t i = 0; i < count; i++) {
        napi_ref* ref = (napi_ref*)(state_base + specs[i].ref_offset);
        native_direct_add_cached_method(env, obj, specs[i].name, specs[i].fn, ref);
    }
}

static napi_value create_nd_wrapped_handle(napi_env env, WGPUInstance instance, void* native) {
    napi_value obj;
    napi_create_object(env, &obj);
    if (instance) native_direct_set_external_prop(env, obj, DOE_DIRECT_INSTANCE, instance);
    if (native) native_direct_set_external_prop(env, obj, DOE_DIRECT_NATIVE, native);
    native_direct_wrap_handle_cache(env, obj, instance, native);
    return obj;
}

/* ================================================================
 * Adapter — getInfo
 * ================================================================ */

static napi_value native_direct_adapter_get_info(napi_env env, napi_callback_info info) {
    size_t argc = 0;
    napi_value this_arg;
    napi_get_cb_info(env, info, &argc, NULL, &this_arg, NULL);
    NativeDirectHandleCache* cache = native_direct_get_handle_cache(env, this_arg);
    void* adapter = cache ? cache->native
        : native_direct_unwrap_external_prop(env, this_arg, DOE_DIRECT_NATIVE);
    return create_adapter_info_object(env, adapter);
}

/* ================================================================
 * ShaderModule — getCompilationInfo
 * ================================================================ */

static napi_value native_direct_shader_module_get_compilation_info(napi_env env, napi_callback_info info) {
    size_t argc = 0;
    napi_value this_arg;
    napi_get_cb_info(env, info, &argc, NULL, &this_arg, NULL);

    NativeDirectHandleCache* cache = native_direct_get_handle_cache(env, this_arg);
    void* module_raw = cache ? cache->native
                             : native_direct_unwrap_external_prop(env, this_arg, DOE_DIRECT_NATIVE);

    napi_value messages = shader_compilation_messages(env, module_raw);
    if (!messages) return NULL;

    napi_value compilation_info;
    napi_create_object(env, &compilation_info);
    napi_set_named_property(env, compilation_info, "messages", messages);
    return native_direct_resolved_promise(env, compilation_info);
}

/* ================================================================
 * Adapter lifecycle — requestAdapter, requestDevice, destroy
 * ================================================================ */

static napi_value native_direct_gpu_request_adapter(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value argv[1];
    napi_value this_arg;
    napi_get_cb_info(env, info, &argc, argv, &this_arg, NULL);
    WGPUInstance inst = native_direct_unwrap_external_prop(env, this_arg, DOE_DIRECT_INSTANCE);
    napi_value options = (argc >= 1) ? argv[0] : NULL;
    WGPUAdapter adapter = native_direct_request_adapter_sync(env, inst, options);
    if (!adapter) return NULL;
    return native_direct_resolved_promise(env, create_native_direct_adapter_object(env, inst, adapter));
}

static napi_value native_direct_adapter_request_device(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value argv[1];
    napi_value this_arg;
    napi_get_cb_info(env, info, &argc, argv, &this_arg, NULL);
    WGPUInstance inst = native_direct_unwrap_external_prop(env, this_arg, DOE_DIRECT_INSTANCE);
    WGPUAdapter adapter = native_direct_unwrap_external_prop(env, this_arg, DOE_DIRECT_NATIVE);
    WGPUDeviceDescriptor descriptor = {0};
    WGPUFeatureName* required_features = NULL;
    WGPULimits required_limits = {0};
    const WGPUDeviceDescriptor* descriptor_ptr = NULL;
    if (argc >= 1 && argv[0] != NULL) {
        napi_valuetype vt = napi_undefined;
        if (napi_typeof(env, argv[0], &vt) == napi_ok && vt == napi_object) {
            if (!doe_parse_device_descriptor(env, argv[0], &descriptor, &required_features, &required_limits)) {
                doe_free_device_descriptor_owned(&descriptor, required_features);
                return NULL;
            }
            descriptor_ptr = &descriptor;
        }
    }

    DeviceRequestResult result = {0};
    const WGPURequestDeviceCallbackInfo callback_info = {
        .nextInChain = NULL,
        .mode = WGPU_CALLBACK_MODE_ALLOW_PROCESS_EVENTS,
        .callback = device_callback,
        .userdata1 = &result,
        .userdata2 = NULL,
    };
    /* Providers without the Doe setters take uncaptured-error and lost
     * callbacks only here; without them the device's errors are dropped. */
    void* events = device_events_describe(&descriptor);
    if (events) descriptor_ptr = &descriptor;
    WGPUFuture future = pfn_doeNativeAdapterRequestDevice
        ? pfn_doeNativeAdapterRequestDevice(adapter, descriptor_ptr, callback_info)
        : pfn_wgpuAdapterRequestDevice(adapter, descriptor_ptr, callback_info);
    if (future.id == 0) {
        doe_free_device_descriptor_owned(&descriptor, required_features);
        NAPI_THROW(env, "requestDevice future unavailable");
    }
    if (!process_events_until(inst, &result.done, current_timeout_ns(env))) {
        doe_free_device_descriptor_owned(&descriptor, required_features);
        throw_status_error(env, "DOE_REQUEST_DEVICE_TIMEOUT", "requestDevice timed out", result.status, result.message);
        return NULL;
    }
    if (result.status != WGPU_REQUEST_STATUS_SUCCESS || !result.device) {
        doe_free_device_descriptor_owned(&descriptor, required_features);
        throw_status_error(env, "DOE_REQUEST_DEVICE_ERROR", "requestDevice failed", result.status, result.message);
        return NULL;
    }

    device_events_attach(events, result.device);
    napi_value device_obj = create_native_direct_device_object(env, inst, result.device);
    if (device_obj && descriptor_ptr) {
        if (descriptor.label.data && descriptor.label.length > 0) {
            napi_value label_value;
            napi_create_string_utf8(env, descriptor.label.data, descriptor.label.length, &label_value);
            napi_set_named_property(env, device_obj, "label", label_value);
        }
        if (descriptor.defaultQueue.label.data && descriptor.defaultQueue.label.length > 0) {
            napi_value queue_obj;
            if (napi_get_named_property(env, device_obj, "queue", &queue_obj) == napi_ok) {
                napi_value label_value;
                napi_create_string_utf8(env, descriptor.defaultQueue.label.data, descriptor.defaultQueue.label.length, &label_value);
                napi_set_named_property(env, queue_obj, "label", label_value);
            }
        }
    }
    doe_free_device_descriptor_owned(&descriptor, required_features);
    return native_direct_resolved_promise(env, device_obj);
}

static napi_value native_direct_adapter_destroy(napi_env env, napi_callback_info info) {
    size_t argc = 0;
    napi_value this_arg;
    napi_get_cb_info(env, info, &argc, NULL, &this_arg, NULL);
    WGPUAdapter adapter = native_direct_unwrap_external_prop(env, this_arg, DOE_DIRECT_NATIVE);
    if (adapter) {
        pfn_wgpuAdapterRelease(adapter);
        native_direct_set_external_prop(env, this_arg, DOE_DIRECT_NATIVE, NULL);
    }
    napi_value undefined_value;
    napi_get_undefined(env, &undefined_value);
    return undefined_value;
}

/* ================================================================
 * Object constructors
 * ================================================================ */

static napi_value native_direct_create_feature_set(
    napi_env env,
    bool (*has_feature_fn)(void* handle, uint32_t feature),
    void* handle
) {
    napi_value set = native_direct_create_empty_set(env);
    if (!has_feature_fn || !handle) return set;

    napi_value add_fn;
    napi_get_named_property(env, set, "add", &add_fn);
    const char* name = NULL;
    uint32_t value = 0;
    for (size_t i = 0; doe_feature_entry_at(i, &name, &value); i += 1) {
        if (!has_feature_fn(handle, value)) continue;
        napi_value name_value;
        napi_create_string_utf8(env, name, NAPI_AUTO_LENGTH, &name_value);
        napi_value argv[] = { name_value };
        napi_call_function(env, set, add_fn, 1, argv, NULL);
    }
    return set;
}

static bool native_direct_adapter_has_feature_value(void* handle, uint32_t feature) {
    if (!handle) return false;
    if (pfn_doeNativeAdapterHasFeature) return pfn_doeNativeAdapterHasFeature(handle, feature) != 0;
    if (pfn_wgpuAdapterHasFeature) return pfn_wgpuAdapterHasFeature(handle, feature) != 0;
    return false;
}

static bool native_direct_device_has_feature_value(void* handle, uint32_t feature) {
    if (!handle) return false;
    if (pfn_doeNativeDeviceHasFeature) return pfn_doeNativeDeviceHasFeature(handle, feature) != 0;
    if (pfn_wgpuDeviceHasFeature) return pfn_wgpuDeviceHasFeature(handle, feature) != 0;
    return false;
}

#define ND_METHOD(js_name, fn_sym, ref_field) \
    { (js_name), (fn_sym), offsetof(DoeEnvState, ref_field) }

napi_value create_native_direct_gpu_object(napi_env env, WGPUInstance instance) {
    napi_value obj = create_nd_wrapped_handle(env, instance, NULL);
    native_direct_add_cached_method(env, obj, "requestAdapter", native_direct_gpu_request_adapter, &doe_env_state(env)->native_direct_method_gpu_request_adapter_ref);
    return obj;
}

napi_value create_native_direct_adapter_object(napi_env env, WGPUInstance instance, WGPUAdapter adapter) {
    static const NdMethodSpec kMethods[] = {
        ND_METHOD("requestDevice", native_direct_adapter_request_device, native_direct_method_adapter_request_device_ref),
        ND_METHOD("destroy", native_direct_adapter_destroy, native_direct_method_adapter_destroy_ref),
        ND_METHOD("getPreferredCanvasFormat", doe_get_preferred_canvas_format, native_direct_method_adapter_get_preferred_canvas_format_ref),
        ND_METHOD("getInfo", native_direct_adapter_get_info, native_direct_method_adapter_get_info_ref),
    };
    napi_value obj = create_nd_wrapped_handle(env, instance, adapter);
    bool limits_ok = false;
    WGPULimits limits = native_direct_query_adapter_limits(adapter, &limits_ok);
    native_direct_set_object_prop(env, obj, "limits", limits_ok ? create_limits_object(env, &limits) : native_direct_create_empty_object(env));
    native_direct_set_object_prop(env, obj, "features", native_direct_create_feature_set(env, native_direct_adapter_has_feature_value, adapter));
    nd_bind_methods(env, obj, kMethods, sizeof(kMethods) / sizeof(kMethods[0]));
    return obj;
}

napi_value create_native_direct_queue_object(napi_env env, WGPUInstance instance, WGPUQueue queue) {
    static const NdMethodSpec kMethods[] = {
        ND_METHOD("submit", native_direct_queue_submit, native_direct_method_queue_submit_ref),
        ND_METHOD("writeBuffer", native_direct_queue_write_buffer, native_direct_method_queue_write_buffer_ref),
        ND_METHOD("writeBufferBatch", native_direct_queue_write_buffer_batch, native_direct_method_queue_write_buffer_batch_ref),
        ND_METHOD("writeTexture", native_direct_queue_write_texture, native_direct_method_queue_write_texture_ref),
        ND_METHOD("onSubmittedWorkDone", native_direct_queue_on_submitted_work_done, native_direct_method_queue_on_submitted_work_done_ref),
    };
    napi_value obj;
    napi_create_object(env, &obj);
    napi_value empty_label;
    napi_create_string_utf8(env, "", NAPI_AUTO_LENGTH, &empty_label);
    napi_set_named_property(env, obj, "label", empty_label);
    native_direct_set_external_prop(env, obj, DOE_DIRECT_INSTANCE, instance);
    native_direct_set_external_prop(env, obj, DOE_DIRECT_NATIVE, queue);
    native_direct_set_external_prop(env, obj, DOE_DIRECT_QUEUE_NATIVE, queue);
    native_direct_set_uint32_prop(env, obj, DOE_DIRECT_SUBMITTED_SERIAL, 0);
    native_direct_set_uint32_prop(env, obj, DOE_DIRECT_COMPLETED_SERIAL, 0);
    native_direct_set_double_prop(env, obj, DOE_DIRECT_DIAG_SUBMIT_WAIT_MS, 0.0);
    native_direct_set_double_prop(env, obj, DOE_DIRECT_DIAG_QUEUE_FLUSH_MS, 0.0);
    native_direct_wrap_queue_cache(env, obj, instance, queue);
    nd_bind_methods(env, obj, kMethods, sizeof(kMethods) / sizeof(kMethods[0]));
    return obj;
}

napi_value create_native_direct_device_object(napi_env env, WGPUInstance instance, WGPUDevice device) {
    static const NdMethodSpec kMethods[] = {
        ND_METHOD("createBuffer", native_direct_device_create_buffer, native_direct_method_device_create_buffer_ref),
        ND_METHOD("createShaderModule", native_direct_device_create_shader_module, native_direct_method_device_create_shader_module_ref),
        ND_METHOD("createComputePipeline", native_direct_device_create_compute_pipeline, native_direct_method_device_create_compute_pipeline_ref),
        ND_METHOD("createComputePipelineAsync", native_direct_device_create_compute_pipeline_async, native_direct_method_device_create_compute_pipeline_async_ref),
        ND_METHOD("createBindGroupLayout", native_direct_device_create_bind_group_layout, native_direct_method_device_create_bind_group_layout_ref),
        ND_METHOD("createBindGroup", native_direct_device_create_bind_group, native_direct_method_device_create_bind_group_ref),
        ND_METHOD("createPipelineLayout", native_direct_device_create_pipeline_layout, native_direct_method_device_create_pipeline_layout_ref),
        ND_METHOD("createCommandEncoder", native_direct_device_create_command_encoder, native_direct_method_device_create_command_encoder_ref),
        ND_METHOD("destroy", native_direct_device_destroy, native_direct_method_device_destroy_ref),
        ND_METHOD("addEventListener", native_direct_device_add_event_listener, native_direct_method_device_add_event_listener_ref),
        ND_METHOD("removeEventListener", native_direct_device_remove_event_listener, native_direct_method_device_remove_event_listener_ref),
        ND_METHOD("importExternalTexture", native_direct_device_import_external_texture, native_direct_method_device_import_external_texture_ref),
    };
    WGPUQueue queue = pfn_wgpuDeviceGetQueue(device);
    napi_value obj = create_nd_wrapped_handle(env, instance, device);
    napi_value empty_label;
    napi_create_string_utf8(env, "", NAPI_AUTO_LENGTH, &empty_label);
    napi_set_named_property(env, obj, "label", empty_label);
    bool limits_ok = false;
    WGPULimits limits = native_direct_query_device_limits(device, &limits_ok);
    native_direct_set_object_prop(env, obj, "limits", limits_ok ? create_limits_object(env, &limits) : native_direct_create_empty_object(env));
    native_direct_set_object_prop(env, obj, "features", native_direct_create_feature_set(env, native_direct_device_has_feature_value, device));
    native_direct_set_object_prop(env, obj, "queue", create_native_direct_queue_object(env, instance, queue));
    nd_bind_methods(env, obj, kMethods, sizeof(kMethods) / sizeof(kMethods[0]));
    return obj;
}

napi_value create_native_direct_buffer_object(napi_env env, WGPUInstance instance, napi_value queue_obj, WGPUBuffer buffer, uint64_t size, uint64_t usage) {
    static const char* const kDiagProps[] = {
        DOE_DIRECT_DIAG_MAP_ASYNC_MS,
        DOE_DIRECT_DIAG_MAP_QUEUE_FLUSH_MS,
        DOE_DIRECT_DIAG_GET_MAPPED_RANGE_MS,
        DOE_DIRECT_DIAG_MAP_READ_COPY_UNMAP_QUEUE_WAIT_COMPLETED_MS,
        DOE_DIRECT_DIAG_MAP_READ_COPY_UNMAP_DEFERRED_COPY_MS,
        DOE_DIRECT_DIAG_MAP_READ_COPY_UNMAP_DEFERRED_RESOLVE_MS,
        DOE_DIRECT_DIAG_MAP_READ_COPY_UNMAP_MAP_MS,
        DOE_DIRECT_DIAG_MAP_READ_COPY_UNMAP_COPY_MS,
        DOE_DIRECT_DIAG_MAP_READ_COPY_UNMAP_UNMAP_MS,
    };
    static const NdMethodSpec kMethods[] = {
        ND_METHOD("mapAsync", native_direct_buffer_map_async, native_direct_method_buffer_map_async_ref),
        ND_METHOD("getMappedRange", native_direct_buffer_get_mapped_range, native_direct_method_buffer_get_mapped_range_ref),
        ND_METHOD("_readCopy", native_direct_buffer_read_copy, native_direct_method_buffer_read_copy_ref),
        ND_METHOD("_mapReadCopyUnmap", native_direct_buffer_map_read_copy_unmap, native_direct_method_buffer_map_read_copy_unmap_ref),
        ND_METHOD("unmap", native_direct_buffer_unmap, native_direct_method_buffer_unmap_ref),
        ND_METHOD("destroy", native_direct_buffer_destroy, native_direct_method_buffer_destroy_ref),
    };
    napi_value obj;
    napi_create_object(env, &obj);
    native_direct_set_external_prop(env, obj, DOE_DIRECT_INSTANCE, instance);
    native_direct_set_external_prop(env, obj, DOE_DIRECT_NATIVE, buffer);
    native_direct_set_object_prop(env, obj, DOE_DIRECT_QUEUE, queue_obj);
    native_direct_set_double_prop(env, obj, "size", (double)size);
    native_direct_set_double_prop(env, obj, "usage", (double)usage);
    for (size_t i = 0; i < sizeof(kDiagProps) / sizeof(kDiagProps[0]); i++) {
        native_direct_set_double_prop(env, obj, kDiagProps[i], 0.0);
    }
    native_direct_wrap_buffer_cache(env, obj, instance, buffer, size, usage, queue_obj);
    nd_bind_methods(env, obj, kMethods, sizeof(kMethods) / sizeof(kMethods[0]));
    return obj;
}

napi_value create_native_direct_bind_group_layout_object(napi_env env, WGPUBindGroupLayout layout) {
    return create_nd_wrapped_handle(env, NULL, layout);
}

napi_value create_native_direct_bind_group_object(napi_env env, WGPUBindGroup group) {
    return create_nd_wrapped_handle(env, NULL, group);
}

napi_value create_native_direct_pipeline_layout_object(napi_env env, WGPUPipelineLayout layout) {
    return create_nd_wrapped_handle(env, NULL, layout);
}

napi_value create_native_direct_shader_module_object(napi_env env, WGPUShaderModule shader_module) {
    napi_value obj = create_nd_wrapped_handle(env, NULL, shader_module);
    /* GPUShaderModule.getCompilationInfo() — returns a Promise<GPUCompilationInfo>. */
    native_direct_add_cached_method(env, obj, "getCompilationInfo", native_direct_shader_module_get_compilation_info, &doe_env_state(env)->native_direct_method_shader_module_get_compilation_info_ref);
    return obj;
}

napi_value create_native_direct_compute_pipeline_object(napi_env env, WGPUComputePipeline pipeline) {
    return create_nd_wrapped_handle(env, NULL, pipeline);
}

napi_value create_native_direct_command_buffer_object(napi_env env, WGPUCommandBuffer command_buffer) {
    return create_nd_wrapped_handle(env, NULL, command_buffer);
}

napi_value create_native_direct_command_encoder_object(napi_env env, WGPUCommandEncoder encoder) {
    static const NdMethodSpec kMethods[] = {
        ND_METHOD("beginComputePass", native_direct_command_encoder_begin_compute_pass, native_direct_method_command_encoder_begin_compute_pass_ref),
        ND_METHOD("copyBufferToBuffer", native_direct_command_encoder_copy_buffer_to_buffer, native_direct_method_command_encoder_copy_buffer_to_buffer_ref),
        ND_METHOD("clearBuffer", native_direct_command_encoder_clear_buffer, native_direct_method_command_encoder_clear_buffer_ref),
        ND_METHOD("copyTextureToTexture", native_direct_command_encoder_copy_texture_to_texture, native_direct_method_command_encoder_copy_texture_to_texture_ref),
        ND_METHOD("finish", native_direct_command_encoder_finish, native_direct_method_command_encoder_finish_ref),
    };
    napi_value obj = create_nd_wrapped_handle(env, NULL, encoder);
    nd_bind_methods(env, obj, kMethods, sizeof(kMethods) / sizeof(kMethods[0]));
    return obj;
}

napi_value create_native_direct_compute_pass_object(napi_env env, WGPUComputePassEncoder pass) {
    static const NdMethodSpec kMethods[] = {
        ND_METHOD("setPipeline", native_direct_compute_pass_set_pipeline, native_direct_method_compute_pass_set_pipeline_ref),
        ND_METHOD("setBindGroup", native_direct_compute_pass_set_bind_group, native_direct_method_compute_pass_set_bind_group_ref),
        ND_METHOD("dispatchWorkgroups", native_direct_compute_pass_dispatch_workgroups, native_direct_method_compute_pass_dispatch_workgroups_ref),
        ND_METHOD("dispatchWorkgroupsIndirect", native_direct_compute_pass_dispatch_workgroups_indirect, native_direct_method_compute_pass_dispatch_workgroups_indirect_ref),
        ND_METHOD("end", native_direct_compute_pass_end, native_direct_method_compute_pass_end_ref),
        ND_METHOD("setImmediates", native_direct_compute_pass_set_immediates, native_direct_method_compute_pass_set_immediates_ref),
        ND_METHOD("pushDebugGroup", native_direct_compute_pass_push_debug_group, native_direct_method_compute_pass_push_debug_group_ref),
        ND_METHOD("popDebugGroup", native_direct_compute_pass_pop_debug_group, native_direct_method_compute_pass_pop_debug_group_ref),
        ND_METHOD("insertDebugMarker", native_direct_compute_pass_insert_debug_marker, native_direct_method_compute_pass_insert_debug_marker_ref),
    };
    napi_value obj = create_nd_wrapped_handle(env, NULL, pass);
    nd_bind_methods(env, obj, kMethods, sizeof(kMethods) / sizeof(kMethods[0]));
    return obj;
}

/* create_native_direct_render_pass_object: wraps a WGPURenderPassEncoder as a JS
 * object with all GPURenderPassEncoder control methods registered. */
napi_value create_native_direct_render_pass_object(napi_env env, WGPURenderPassEncoder pass) {
    static const NdMethodSpec kMethods[] = {
        ND_METHOD("setImmediates", native_direct_render_pass_set_immediates, native_direct_method_render_pass_set_immediates_ref),
        ND_METHOD("setViewport", native_direct_render_pass_set_viewport, native_direct_method_render_pass_set_viewport_ref),
        ND_METHOD("setScissorRect", native_direct_render_pass_set_scissor_rect, native_direct_method_render_pass_set_scissor_rect_ref),
        ND_METHOD("setBlendConstant", native_direct_render_pass_set_blend_constant, native_direct_method_render_pass_set_blend_constant_ref),
        ND_METHOD("setStencilReference", native_direct_render_pass_set_stencil_reference, native_direct_method_render_pass_set_stencil_reference_ref),
        ND_METHOD("drawIndirect", native_direct_render_pass_draw_indirect, native_direct_method_render_pass_draw_indirect_ref),
        ND_METHOD("drawIndexedIndirect", native_direct_render_pass_draw_indexed_indirect, native_direct_method_render_pass_draw_indexed_indirect_ref),
        ND_METHOD("pushDebugGroup", native_direct_render_pass_push_debug_group, native_direct_method_render_pass_push_debug_group_ref),
        ND_METHOD("popDebugGroup", native_direct_render_pass_pop_debug_group, native_direct_method_render_pass_pop_debug_group_ref),
        ND_METHOD("insertDebugMarker", native_direct_render_pass_insert_debug_marker, native_direct_method_render_pass_insert_debug_marker_ref),
    };
    napi_value obj = create_nd_wrapped_handle(env, NULL, pass);
    nd_bind_methods(env, obj, kMethods, sizeof(kMethods) / sizeof(kMethods[0]));
    return obj;
}

/* native_direct_create_instance — exported as "nativeDirectCreate".
 * Creates a WGPUInstance and returns a JS GPU object bound to it. */
napi_value doe_native_direct_create(napi_env env, napi_callback_info info) {
    (void)info;
    CHECK_LIB_LOADED(env);
    WGPUInstance inst = pfn_wgpuCreateInstance(NULL);
    if (!inst) NAPI_THROW(env, "wgpuCreateInstance returned NULL");
    return create_native_direct_gpu_object(env, inst);
}
