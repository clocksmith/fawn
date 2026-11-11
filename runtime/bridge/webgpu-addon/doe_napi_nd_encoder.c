#include "doe_napi_internal.h"

static napi_value nd_enc_undefined(napi_env env) {
    napi_value v = NULL;
    napi_get_undefined(env, &v);
    return v;
}

static void* nd_unwrap_handle(napi_env env, napi_value obj) {
    NativeDirectHandleCache* cache = native_direct_get_handle_cache(env, obj);
    return cache ? cache->native : native_direct_unwrap_external_prop(env, obj, DOE_DIRECT_NATIVE);
}

static WGPUBuffer nd_unwrap_buffer(napi_env env, napi_value obj) {
    NativeDirectBufferCache* cache = native_direct_get_buffer_cache(env, obj);
    return cache ? cache->buffer : native_direct_unwrap_external_prop(env, obj, DOE_DIRECT_NATIVE);
}

static void parse_nd_texel_copy_side(
    napi_env env,
    napi_value side,
    WGPUTexelCopyTextureInfo* out
) {
    memset(out, 0, sizeof(*out));
    out->texture = native_direct_unwrap_external_prop(env, get_prop(env, side, "texture"), DOE_DIRECT_NATIVE);
    napi_value tmp;
    if (napi_get_named_property(env, side, "mipLevel", &tmp) == napi_ok) {
        napi_get_value_uint32(env, tmp, &out->mipLevel);
    }
    napi_value origin_val;
    napi_valuetype origin_type = napi_undefined;
    if (napi_get_named_property(env, side, "origin", &origin_val) == napi_ok) {
        napi_typeof(env, origin_val, &origin_type);
    }
    if (origin_type == napi_object) {
        if (napi_get_named_property(env, origin_val, "x", &tmp) == napi_ok) napi_get_value_uint32(env, tmp, &out->origin.x);
        if (napi_get_named_property(env, origin_val, "y", &tmp) == napi_ok) napi_get_value_uint32(env, tmp, &out->origin.y);
        if (napi_get_named_property(env, origin_val, "z", &tmp) == napi_ok) napi_get_value_uint32(env, tmp, &out->origin.z);
        if (napi_get_named_property(env, origin_val, "aspect", &tmp) == napi_ok) napi_get_value_uint32(env, tmp, &out->aspect);
    }
}

napi_value native_direct_command_encoder_begin_compute_pass(napi_env env, napi_callback_info info) {
    size_t argc = 2;
    napi_value argv[2];
    napi_value this_arg;
    napi_get_cb_info(env, info, &argc, argv, &this_arg, NULL);
    WGPUCommandEncoder encoder = nd_unwrap_handle(env, this_arg);
    if (!encoder) NAPI_THROW(env, "Invalid encoder");
    char* label = NULL;
    size_t label_len = 0;
    WGPUComputePassTimestampWrites ts_writes = {0};
    WGPUComputePassTimestampWrites* ts_writes_ptr = NULL;
    if (argc >= 1) {
        napi_valuetype desc_type = napi_undefined;
        if (napi_typeof(env, argv[0], &desc_type) == napi_ok && desc_type == napi_object) {
            if (has_prop(env, argv[0], "label") && prop_type(env, argv[0], "label") == napi_string) {
                label = dup_string_value(env, get_prop(env, argv[0], "label"), &label_len);
                if (!label) NAPI_THROW(env, "beginComputePass: out of memory while reading label");
            }
            if (has_prop(env, argv[0], "timestampWrites") && prop_type(env, argv[0], "timestampWrites") == napi_object) {
                napi_value tw = get_prop(env, argv[0], "timestampWrites");
                ts_writes.querySet = native_direct_unwrap_external_prop(env, get_prop(env, tw, "querySet"), DOE_DIRECT_NATIVE);
                ts_writes.beginningOfPassWriteIndex = get_uint32_prop(env, tw, "beginningOfPassWriteIndex");
                ts_writes.endOfPassWriteIndex = get_uint32_prop(env, tw, "endOfPassWriteIndex");
                ts_writes_ptr = &ts_writes;
            }
        }
    }
    WGPUComputePassDescriptor desc = {
        .nextInChain = NULL,
        .label = { .data = label, .length = label ? label_len : 0 },
        .timestampWrites = ts_writes_ptr,
    };
    WGPUComputePassEncoder pass = pfn_wgpuCommandEncoderBeginComputePass(encoder, &desc);
    free(label);
    if (!pass) NAPI_THROW(env, "beginComputePass failed");
    return create_native_direct_compute_pass_object(env, pass);
}

napi_value native_direct_command_encoder_copy_buffer_to_buffer(napi_env env, napi_callback_info info) {
    size_t argc = 5;
    napi_value argv[5];
    napi_value this_arg;
    napi_get_cb_info(env, info, &argc, argv, &this_arg, NULL);
    if (argc < 5) NAPI_THROW(env, "copyBufferToBuffer requires source, sourceOffset, target, targetOffset, and size");
    WGPUCommandEncoder encoder = nd_unwrap_handle(env, this_arg);
    WGPUBuffer src = nd_unwrap_buffer(env, argv[0]);
    WGPUBuffer dst = nd_unwrap_buffer(env, argv[2]);
    int64_t src_offset = 0;
    int64_t dst_offset = 0;
    int64_t size = 0;
    if (!encoder || !src || !dst) NAPI_THROW(env, "copyBufferToBuffer requires encoder and buffers");
    napi_get_value_int64(env, argv[1], &src_offset);
    napi_get_value_int64(env, argv[3], &dst_offset);
    napi_get_value_int64(env, argv[4], &size);
    pfn_wgpuCommandEncoderCopyBufferToBuffer(encoder, src, (uint64_t)src_offset, dst, (uint64_t)dst_offset, (uint64_t)size);
    return NULL;
}

napi_value native_direct_command_encoder_finish(napi_env env, napi_callback_info info) {
    size_t argc = 0;
    napi_value this_arg;
    napi_get_cb_info(env, info, &argc, NULL, &this_arg, NULL);
    NativeDirectHandleCache* encoder_cache = native_direct_get_handle_cache(env, this_arg);
    WGPUCommandEncoder encoder = encoder_cache ? (WGPUCommandEncoder)encoder_cache->native : native_direct_unwrap_external_prop(env, this_arg, DOE_DIRECT_NATIVE);
    if (!encoder) NAPI_THROW(env, "Invalid encoder");
    WGPUCommandBufferDescriptor desc = {
        .nextInChain = NULL,
        .label = { .data = NULL, .length = 0 },
    };
    WGPUCommandBuffer command_buffer = pfn_wgpuCommandEncoderFinish(encoder, &desc);
    if (!command_buffer) NAPI_THROW(env, "commandEncoderFinish failed");
    pfn_wgpuCommandEncoderRelease(encoder);
    native_direct_set_external_prop(env, this_arg, DOE_DIRECT_NATIVE, NULL);
    if (encoder_cache) encoder_cache->native = NULL;
    return create_native_direct_command_buffer_object(env, command_buffer);
}

/* GPUCommandEncoder.clearBuffer(buffer, offset?, size?)
 * argv[0]: GPUBuffer, argv[1]: offset (optional, default 0), argv[2]: size (optional, default WGPU_WHOLE_SIZE) */
napi_value native_direct_command_encoder_clear_buffer(napi_env env, napi_callback_info info) {
    size_t argc = 3;
    napi_value argv[3];
    napi_value this_arg;
    napi_get_cb_info(env, info, &argc, argv, &this_arg, NULL);
    if (argc < 1) NAPI_THROW(env, "clearBuffer requires a buffer");
    void* encoder = nd_unwrap_handle(env, this_arg);
    void* buffer = nd_unwrap_buffer(env, argv[0]);
    if (!encoder || !buffer) NAPI_THROW(env, "clearBuffer: invalid encoder or buffer");
    uint64_t offset = 0;
    uint64_t size = UINT64_MAX; /* WGPU_WHOLE_SIZE */
    if (argc >= 2 && argv[1]) {
        int64_t v = 0;
        napi_get_value_int64(env, argv[1], &v);
        if (v > 0) offset = (uint64_t)v;
    }
    if (argc >= 3 && argv[2]) {
        int64_t v = 0;
        napi_get_value_int64(env, argv[2], &v);
        if (v > 0) size = (uint64_t)v;
    }
    if (pfn_doeNativeCommandEncoderClearBuffer) pfn_doeNativeCommandEncoderClearBuffer(encoder, buffer, offset, size);
    else if (pfn_wgpuCommandEncoderClearBuffer) pfn_wgpuCommandEncoderClearBuffer(encoder, buffer, offset, size);
    else NAPI_THROW(env, "clearBuffer: no implementation available in loaded library");
    return nd_enc_undefined(env);
}

/* GPUCommandEncoder.copyTextureToTexture(source, destination, copySize) */
napi_value native_direct_command_encoder_copy_texture_to_texture(napi_env env, napi_callback_info info) {
    size_t argc = 3;
    napi_value argv[3];
    napi_value this_arg;
    napi_get_cb_info(env, info, &argc, argv, &this_arg, NULL);
    if (argc < 3) NAPI_THROW(env, "copyTextureToTexture requires source, destination, and copySize");
    void* encoder = nd_unwrap_handle(env, this_arg);
    if (!encoder) NAPI_THROW(env, "copyTextureToTexture: invalid encoder");
    WGPUTexelCopyTextureInfo src;
    WGPUTexelCopyTextureInfo dst;
    parse_nd_texel_copy_side(env, argv[0], &src);
    parse_nd_texel_copy_side(env, argv[1], &dst);
    if (!src.texture || !dst.texture) NAPI_THROW(env, "copyTextureToTexture: invalid source or destination texture");
    WGPUExtent3D copy_size = { .width = 1, .height = 1, .depthOrArrayLayers = 1 };
    napi_valuetype size_type = napi_undefined;
    napi_typeof(env, argv[2], &size_type);
    if (size_type == napi_object) {
        napi_value tmp;
        if (napi_get_named_property(env, argv[2], "width", &tmp) == napi_ok) napi_get_value_uint32(env, tmp, &copy_size.width);
        if (napi_get_named_property(env, argv[2], "height", &tmp) == napi_ok) napi_get_value_uint32(env, tmp, &copy_size.height);
        if (napi_get_named_property(env, argv[2], "depthOrArrayLayers", &tmp) == napi_ok) napi_get_value_uint32(env, tmp, &copy_size.depthOrArrayLayers);
        if (copy_size.depthOrArrayLayers == 0) copy_size.depthOrArrayLayers = 1;
    }
    if (pfn_wgpuCommandEncoderCopyTextureToTexture) {
        pfn_wgpuCommandEncoderCopyTextureToTexture(encoder, &src, &dst, &copy_size);
    } else if (pfn_doeNativeCommandEncoderCopyTextureToTexture) {
        pfn_doeNativeCommandEncoderCopyTextureToTexture(
            encoder,
            src.texture, src.mipLevel, 0, src.origin.x, src.origin.y, src.origin.z,
            dst.texture, dst.mipLevel, 0, dst.origin.x, dst.origin.y, dst.origin.z,
            copy_size.width, copy_size.height, copy_size.depthOrArrayLayers);
    } else {
        NAPI_THROW(env, "copyTextureToTexture is not available in the loaded library");
    }
    return nd_enc_undefined(env);
}

napi_value native_direct_compute_pass_set_pipeline(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value argv[1];
    napi_value this_arg;
    napi_get_cb_info(env, info, &argc, argv, &this_arg, NULL);
    if (argc < 1) NAPI_THROW(env, "setPipeline requires a pipeline");
    WGPUComputePassEncoder pass = nd_unwrap_handle(env, this_arg);
    WGPUComputePipeline pipeline = nd_unwrap_handle(env, argv[0]);
    pfn_wgpuComputePassEncoderSetPipeline(pass, pipeline);
    return NULL;
}

napi_value native_direct_compute_pass_dispatch_workgroups(napi_env env, napi_callback_info info) {
    size_t argc = 3;
    napi_value argv[3];
    napi_value this_arg;
    napi_get_cb_info(env, info, &argc, argv, &this_arg, NULL);
    if (argc < 1) NAPI_THROW(env, "dispatchWorkgroups requires x");
    WGPUComputePassEncoder pass = nd_unwrap_handle(env, this_arg);
    uint32_t x = 1, y = 1, z = 1;
    napi_get_value_uint32(env, argv[0], &x);
    if (argc >= 2 && argv[1]) napi_get_value_uint32(env, argv[1], &y);
    if (argc >= 3 && argv[2]) napi_get_value_uint32(env, argv[2], &z);
    pfn_wgpuComputePassEncoderDispatchWorkgroups(pass, x, y, z);
    return NULL;
}

napi_value native_direct_compute_pass_dispatch_workgroups_indirect(napi_env env, napi_callback_info info) {
    size_t argc = 2;
    napi_value argv[2];
    napi_value this_arg;
    napi_get_cb_info(env, info, &argc, argv, &this_arg, NULL);
    if (argc < 1) NAPI_THROW(env, "dispatchWorkgroupsIndirect requires a buffer");
    WGPUComputePassEncoder pass = nd_unwrap_handle(env, this_arg);
    WGPUBuffer buffer = nd_unwrap_buffer(env, argv[0]);
    int64_t offset = 0;
    if (argc >= 2 && argv[1]) napi_get_value_int64(env, argv[1], &offset);
    if (pfn_doeNativeComputePassDispatchIndirect) {
        pfn_doeNativeComputePassDispatchIndirect(pass, buffer, (uint64_t)offset);
    } else {
        pfn_wgpuComputePassEncoderDispatchWorkgroupsIndirect(pass, buffer, (uint64_t)offset);
    }
    return nd_enc_undefined(env);
}

static napi_value nd_call_set_immediates(
    napi_env env,
    napi_callback_info info,
    const char* invalid_msg,
    const char* unavail_msg,
    void (*doe_fn)(void*, uint32_t, const uint8_t*, size_t),
    void (*wgpu_fn)(void*, uint32_t, const void*, size_t)
) {
    size_t argc = 2;
    napi_value argv[2];
    napi_value this_arg;
    napi_get_cb_info(env, info, &argc, argv, &this_arg, NULL);
    if (argc < 2) NAPI_THROW(env, "setImmediates requires index and data");
    uint32_t index = 0;
    napi_get_value_uint32(env, argv[0], &index);
    void* data_ptr = NULL;
    size_t data_len = 0;
    extract_buffer_data(env, argv[1], &data_ptr, &data_len);
    void* pass = nd_unwrap_handle(env, this_arg);
    if (!pass) NAPI_THROW(env, invalid_msg);
    if (doe_fn) doe_fn(pass, index, (const uint8_t*)data_ptr, data_len);
    else if (wgpu_fn) wgpu_fn(pass, index, data_ptr, data_len);
    else NAPI_THROW(env, unavail_msg);
    return nd_enc_undefined(env);
}

static napi_value nd_call_debug_label(
    napi_env env,
    napi_callback_info info,
    const char* missing_arg_msg,
    const char* invalid_msg,
    const char* unavail_msg,
    void (*doe_fn)(void*, const char*, size_t),
    void (*wgpu_fn)(void*, WGPUStringView)
) {
    size_t argc = 1;
    napi_value argv[1];
    napi_value this_arg;
    napi_get_cb_info(env, info, &argc, argv, &this_arg, NULL);
    if (argc < 1) NAPI_THROW(env, missing_arg_msg);
    if (!doe_fn && !wgpu_fn) NAPI_THROW(env, unavail_msg);
    void* pass = nd_unwrap_handle(env, this_arg);
    if (!pass) NAPI_THROW(env, invalid_msg);
    size_t label_len = 0;
    char* label = dup_string_value(env, argv[0], &label_len);
    if (!label) NAPI_THROW(env, "out of memory copying a debug label");
    if (doe_fn) doe_fn(pass, label, label_len);
    else wgpu_fn(pass, (WGPUStringView){ .data = label, .length = label_len });
    free(label);
    return nd_enc_undefined(env);
}

static napi_value nd_call_debug_pop(
    napi_env env,
    napi_callback_info info,
    const char* invalid_msg,
    const char* unavail_msg,
    void (*doe_fn)(void*),
    void (*wgpu_fn)(void*)
) {
    size_t argc = 0;
    napi_value this_arg;
    napi_get_cb_info(env, info, &argc, NULL, &this_arg, NULL);
    if (!doe_fn && !wgpu_fn) NAPI_THROW(env, unavail_msg);
    void* pass = nd_unwrap_handle(env, this_arg);
    if (!pass) NAPI_THROW(env, invalid_msg);
    if (doe_fn) doe_fn(pass);
    else wgpu_fn(pass);
    return nd_enc_undefined(env);
}

/* GPUBindingCommandsMixin#setImmediates — registered on compute pass encoders */
napi_value native_direct_compute_pass_set_immediates(napi_env env, napi_callback_info info) {
    return nd_call_set_immediates(
        env, info, "Invalid compute pass", "computePassSetImmediates is not available in the loaded library",
        pfn_doeNativeComputePassSetImmediates, pfn_wgpuComputePassEncoderSetImmediates);
}

napi_value native_direct_compute_pass_push_debug_group(napi_env env, napi_callback_info info) {
    return nd_call_debug_label(
        env, info, "pushDebugGroup requires groupLabel", "Invalid compute pass", "computePassPushDebugGroup not available",
        NULL, pfn_wgpuComputePassEncoderPushDebugGroup);
}

napi_value native_direct_compute_pass_pop_debug_group(napi_env env, napi_callback_info info) {
    return nd_call_debug_pop(
        env, info, "Invalid compute pass", "computePassPopDebugGroup not available",
        NULL, pfn_wgpuComputePassEncoderPopDebugGroup);
}

napi_value native_direct_compute_pass_insert_debug_marker(napi_env env, napi_callback_info info) {
    return nd_call_debug_label(
        env, info, "insertDebugMarker requires markerLabel", "Invalid compute pass", "computePassInsertDebugMarker not available",
        NULL, pfn_wgpuComputePassEncoderInsertDebugMarker);
}

napi_value native_direct_compute_pass_end(napi_env env, napi_callback_info info) {
    size_t argc = 0;
    napi_value this_arg;
    napi_get_cb_info(env, info, &argc, NULL, &this_arg, NULL);
    NativeDirectHandleCache* pass_cache = native_direct_get_handle_cache(env, this_arg);
    WGPUComputePassEncoder pass = pass_cache ? (WGPUComputePassEncoder)pass_cache->native : native_direct_unwrap_external_prop(env, this_arg, DOE_DIRECT_NATIVE);
    pfn_wgpuComputePassEncoderEnd(pass);
    pfn_wgpuComputePassEncoderRelease(pass);
    native_direct_set_external_prop(env, this_arg, DOE_DIRECT_NATIVE, NULL);
    if (pass_cache) pass_cache->native = NULL;
    return nd_enc_undefined(env);
}

/* GPURenderPassEncoder#setImmediates */
napi_value native_direct_render_pass_set_immediates(napi_env env, napi_callback_info info) {
    return nd_call_set_immediates(
        env, info, "Invalid render pass", "renderPassSetImmediates is not available in the loaded library",
        pfn_doeNativeRenderPassSetImmediates, pfn_wgpuRenderPassEncoderSetImmediates);
}

/* GPURenderBundleEncoder#setImmediates */
napi_value native_direct_render_bundle_encoder_set_immediates(napi_env env, napi_callback_info info) {
    return nd_call_set_immediates(
        env, info, "Invalid render bundle encoder", "renderBundleEncoderSetImmediates is not available in the loaded library",
        pfn_doeNativeRenderBundleEncoderSetImmediates, pfn_wgpuRenderBundleEncoderSetImmediates);
}

/* GPURenderPassEncoder#setViewport(x, y, width, height, minDepth, maxDepth) */
napi_value native_direct_render_pass_set_viewport(napi_env env, napi_callback_info info) {
    size_t argc = 6;
    napi_value argv[6];
    napi_value this_arg;
    napi_get_cb_info(env, info, &argc, argv, &this_arg, NULL);
    if (argc < 6) NAPI_THROW(env, "setViewport requires x, y, width, height, minDepth, maxDepth");
    double x = 0, y = 0, width = 0, height = 0, min_depth = 0, max_depth = 1;
    napi_get_value_double(env, argv[0], &x);
    napi_get_value_double(env, argv[1], &y);
    napi_get_value_double(env, argv[2], &width);
    napi_get_value_double(env, argv[3], &height);
    napi_get_value_double(env, argv[4], &min_depth);
    napi_get_value_double(env, argv[5], &max_depth);
    void* pass = nd_unwrap_handle(env, this_arg);
    if (!pass) NAPI_THROW(env, "Invalid render pass");
    if (pfn_doeNativeRenderPassSetViewport) pfn_doeNativeRenderPassSetViewport(pass, x, y, width, height, min_depth, max_depth);
    else if (pfn_wgpuRenderPassEncoderSetViewport) pfn_wgpuRenderPassEncoderSetViewport(pass, (float)x, (float)y, (float)width, (float)height,
        (float)min_depth, (float)max_depth); /* GPUViewport is f32 in the standard ABI. */
    else NAPI_THROW(env, "renderPassSetViewport is not available in the loaded library");
    return nd_enc_undefined(env);
}

/* GPURenderPassEncoder#setScissorRect(x, y, width, height) */
napi_value native_direct_render_pass_set_scissor_rect(napi_env env, napi_callback_info info) {
    size_t argc = 4;
    napi_value argv[4];
    napi_value this_arg;
    napi_get_cb_info(env, info, &argc, argv, &this_arg, NULL);
    if (argc < 4) NAPI_THROW(env, "setScissorRect requires x, y, width, height");
    uint32_t x = 0, y = 0, width = 0, height = 0;
    napi_get_value_uint32(env, argv[0], &x);
    napi_get_value_uint32(env, argv[1], &y);
    napi_get_value_uint32(env, argv[2], &width);
    napi_get_value_uint32(env, argv[3], &height);
    void* pass = nd_unwrap_handle(env, this_arg);
    if (!pass) NAPI_THROW(env, "Invalid render pass");
    if (pfn_doeNativeRenderPassSetScissorRect) pfn_doeNativeRenderPassSetScissorRect(pass, x, y, width, height);
    else if (pfn_wgpuRenderPassEncoderSetScissorRect) pfn_wgpuRenderPassEncoderSetScissorRect(pass, x, y, width, height);
    else NAPI_THROW(env, "renderPassSetScissorRect is not available in the loaded library");
    return nd_enc_undefined(env);
}

/* GPURenderPassEncoder#setBlendConstant(color) — color: {r,g,b,a} or [r,g,b,a] */
napi_value native_direct_render_pass_set_blend_constant(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value argv[1];
    napi_value this_arg;
    napi_get_cb_info(env, info, &argc, argv, &this_arg, NULL);
    if (argc < 1) NAPI_THROW(env, "setBlendConstant requires a color argument");
    double r = 0, g = 0, b = 0, a = 1;
    napi_valuetype vt;
    napi_typeof(env, argv[0], &vt);
    if (vt == napi_object) {
        napi_value tmp;
        bool is_array = false;
        napi_is_array(env, argv[0], &is_array);
        if (is_array) {
            napi_get_element(env, argv[0], 0, &tmp); napi_get_value_double(env, tmp, &r);
            napi_get_element(env, argv[0], 1, &tmp); napi_get_value_double(env, tmp, &g);
            napi_get_element(env, argv[0], 2, &tmp); napi_get_value_double(env, tmp, &b);
            napi_get_element(env, argv[0], 3, &tmp); napi_get_value_double(env, tmp, &a);
        } else {
            if (napi_get_named_property(env, argv[0], "r", &tmp) == napi_ok) napi_get_value_double(env, tmp, &r);
            if (napi_get_named_property(env, argv[0], "g", &tmp) == napi_ok) napi_get_value_double(env, tmp, &g);
            if (napi_get_named_property(env, argv[0], "b", &tmp) == napi_ok) napi_get_value_double(env, tmp, &b);
            if (napi_get_named_property(env, argv[0], "a", &tmp) == napi_ok) napi_get_value_double(env, tmp, &a);
        }
    }
    void* pass = nd_unwrap_handle(env, this_arg);
    if (!pass) NAPI_THROW(env, "Invalid render pass");
    const WGPUColor color = {r, g, b, a};
    if (pfn_doeNativeRenderPassSetBlendConstant) pfn_doeNativeRenderPassSetBlendConstant(pass, r, g, b, a);
    else if (pfn_wgpuRenderPassEncoderSetBlendConstant) pfn_wgpuRenderPassEncoderSetBlendConstant(pass, &color);
    else NAPI_THROW(env, "renderPassSetBlendConstant is not available in the loaded library");
    return nd_enc_undefined(env);
}

/* GPURenderPassEncoder#setStencilReference(reference) */
napi_value native_direct_render_pass_set_stencil_reference(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value argv[1];
    napi_value this_arg;
    napi_get_cb_info(env, info, &argc, argv, &this_arg, NULL);
    if (argc < 1) NAPI_THROW(env, "setStencilReference requires reference");
    uint32_t reference = 0;
    napi_get_value_uint32(env, argv[0], &reference);
    void* pass = nd_unwrap_handle(env, this_arg);
    if (!pass) NAPI_THROW(env, "Invalid render pass");
    if (pfn_doeNativeRenderPassSetStencilReference) pfn_doeNativeRenderPassSetStencilReference(pass, reference);
    else if (pfn_wgpuRenderPassEncoderSetStencilReference) pfn_wgpuRenderPassEncoderSetStencilReference(pass, reference);
    else NAPI_THROW(env, "renderPassSetStencilReference is not available in the loaded library");
    return nd_enc_undefined(env);
}

/* GPURenderPassEncoder#pushDebugGroup(groupLabel) */
napi_value native_direct_render_pass_push_debug_group(napi_env env, napi_callback_info info) {
    return nd_call_debug_label(
        env, info, "pushDebugGroup requires groupLabel", "Invalid render pass",
        "renderPassPushDebugGroup is not available in the loaded library",
        pfn_doeNativeRenderPassPushDebugGroup, pfn_wgpuRenderPassEncoderPushDebugGroup);
}

/* GPURenderPassEncoder#popDebugGroup() */
napi_value native_direct_render_pass_pop_debug_group(napi_env env, napi_callback_info info) {
    return nd_call_debug_pop(
        env, info, "Invalid render pass",
        "renderPassPopDebugGroup is not available in the loaded library",
        pfn_doeNativeRenderPassPopDebugGroup, pfn_wgpuRenderPassEncoderPopDebugGroup);
}

/* GPURenderPassEncoder#insertDebugMarker(markerLabel) */
napi_value native_direct_render_pass_insert_debug_marker(napi_env env, napi_callback_info info) {
    return nd_call_debug_label(
        env, info, "insertDebugMarker requires markerLabel", "Invalid render pass",
        "renderPassInsertDebugMarker is not available in the loaded library",
        pfn_doeNativeRenderPassInsertDebugMarker, pfn_wgpuRenderPassEncoderInsertDebugMarker);
}

static napi_value nd_call_indirect_draw(
    napi_env env,
    napi_callback_info info,
    const char* missing_msg,
    const char* invalid_msg,
    const char* unavail_msg,
    void (*fn)(WGPURenderPassEncoder, WGPUBuffer, uint64_t)
) {
    size_t argc = 2;
    napi_value argv[2];
    napi_value this_arg;
    napi_get_cb_info(env, info, &argc, argv, &this_arg, NULL);
    if (argc < 1) NAPI_THROW(env, missing_msg);
    if (!fn) NAPI_THROW(env, unavail_msg);
    WGPURenderPassEncoder pass = nd_unwrap_handle(env, this_arg);
    WGPUBuffer buffer = nd_unwrap_buffer(env, argv[0]);
    if (!pass || !buffer) NAPI_THROW(env, invalid_msg);
    int64_t offset = 0;
    if (argc >= 2 && argv[1]) napi_get_value_int64(env, argv[1], &offset);
    fn(pass, buffer, (uint64_t)offset);
    return nd_enc_undefined(env);
}

napi_value native_direct_render_pass_draw_indirect(napi_env env, napi_callback_info info) {
    return nd_call_indirect_draw(
        env, info, "drawIndirect requires an indirect buffer",
        "drawIndirect requires a render pass and indirect buffer",
        "renderPassDrawIndirect not available",
        pfn_wgpuRenderPassEncoderDrawIndirect);
}

napi_value native_direct_render_pass_draw_indexed_indirect(napi_env env, napi_callback_info info) {
    return nd_call_indirect_draw(
        env, info, "drawIndexedIndirect requires an indirect buffer",
        "drawIndexedIndirect requires a render pass and indirect buffer",
        "renderPassDrawIndexedIndirect not available",
        pfn_wgpuRenderPassEncoderDrawIndexedIndirect);
}
