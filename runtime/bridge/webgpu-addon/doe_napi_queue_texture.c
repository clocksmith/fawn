/*
 * doe_napi_queue_texture.c — GPUQueue.writeTexture through the Doe entry or
 * the standard wgpuQueueWriteTexture.
 */
#include "doe_napi_internal.h"

bool doe_queue_write_texture_native(void* queue, void* texture, const void* data, size_t data_len,
    uint32_t bytes_per_row, uint32_t rows_per_image, uint32_t x, uint32_t y, uint32_t z,
    uint32_t mip, uint32_t width, uint32_t height, uint32_t depth_or_layers, uint32_t aspect) {
    /* The Doe entry writes every aspect; a single-aspect write needs the standard procedure. */
    if (pfn_doeNativeQueueWriteTexture && (aspect == 0u || aspect == 1u || !pfn_wgpuQueueWriteTexture)) {
        pfn_doeNativeQueueWriteTexture(queue, texture, data, data_len, bytes_per_row, rows_per_image,
            x, y, z, mip, 0, width, height, depth_or_layers);
        return true;
    }
    if (!pfn_wgpuQueueWriteTexture) return false;
    WGPUTexelCopyTextureInfo destination; memset(&destination, 0, sizeof(destination));
    destination.texture = texture;
    destination.mipLevel = mip;
    destination.origin = (WGPUOrigin3D){x, y, z};
    destination.aspect = aspect ? aspect : 1u; /* WGPUTextureAspect_All when omitted */
    const WGPUTexelCopyBufferLayout layout = {0u, bytes_per_row, rows_per_image};
    const WGPUExtent3D size = {width, height, depth_or_layers};
    pfn_wgpuQueueWriteTexture(queue, &destination, data, data_len, &layout, &size);
    return true;
}

/* queueWriteTexture(queueNative, textureNative, dataBuffer, dataOffset,
 *                  bytesPerRow, rowsPerImage, mipLevel,
 *                  originX, originY, originZ, width, height, depthOrArrayLayers[, aspect]) */
napi_value doe_queue_write_texture(napi_env env, napi_callback_info info) {
    NAPI_ASSERT_ARGC(env, info, 14);
    CHECK_LIB_LOADED(env);
    void* queue   = unwrap_ptr(env, _args[0]);
    void* texture = unwrap_ptr(env, _args[1]);
    if (!queue || !texture) NAPI_THROW(env, "queueWriteTexture: invalid queue or texture");
    void* data = NULL; size_t data_len = 0;
    bool is_typedarray = false;
    napi_is_typedarray(env, _args[2], &is_typedarray);
    if (is_typedarray) {
        napi_typedarray_type ta_type; size_t ta_length; napi_value ab; size_t byte_offset;
        napi_get_typedarray_info(env, _args[2], &ta_type, &ta_length, &data, &ab, &byte_offset);
        size_t elem_size = 1;
        switch (ta_type) {
            case napi_int16_array: case napi_uint16_array: elem_size = 2; break;
            case napi_int32_array: case napi_uint32_array: case napi_float32_array: elem_size = 4; break;
            case napi_float64_array: case napi_bigint64_array: case napi_biguint64_array: elem_size = 8; break;
            default: elem_size = 1; break;
        }
        data_len = ta_length * elem_size;
    } else {
        bool is_ab = false; napi_is_arraybuffer(env, _args[2], &is_ab);
        if (is_ab) napi_get_arraybuffer_info(env, _args[2], &data, &data_len);
        else NAPI_THROW(env, "queueWriteTexture: data must be TypedArray or ArrayBuffer");
    }
    uint32_t data_offset = 0; napi_get_value_uint32(env, _args[3], &data_offset);
    uint32_t bytes_per_row = 0; napi_get_value_uint32(env, _args[4], &bytes_per_row);
    uint32_t rows_per_image = 0; napi_get_value_uint32(env, _args[5], &rows_per_image);
    uint32_t mip_level = 0; napi_get_value_uint32(env, _args[6], &mip_level);
    uint32_t origin_x = 0; napi_get_value_uint32(env, _args[7], &origin_x);
    uint32_t origin_y = 0; napi_get_value_uint32(env, _args[8], &origin_y);
    uint32_t origin_z = 0; napi_get_value_uint32(env, _args[9], &origin_z);
    uint32_t width = 1; napi_get_value_uint32(env, _args[10], &width);
    uint32_t height = 1; napi_get_value_uint32(env, _args[11], &height);
    uint32_t depth_or_layers = 1; napi_get_value_uint32(env, _args[12], &depth_or_layers);
    uint32_t aspect = 0;
    if (_argc > 13) napi_get_value_uint32(env, _args[13], &aspect);
    if (data_offset > 0 && data_offset < (uint32_t)data_len) {
        data = ((uint8_t*)data) + data_offset;
        data_len -= data_offset;
    }
    if (!doe_queue_write_texture_native(queue, texture, data, data_len, bytes_per_row, rows_per_image,
            origin_x, origin_y, origin_z, mip_level, width, height, depth_or_layers, aspect))
        NAPI_THROW(env, "queueWriteTexture: no implementation available in loaded library");
    napi_value undefined_value;
    napi_get_undefined(env, &undefined_value);
    return undefined_value;
}
