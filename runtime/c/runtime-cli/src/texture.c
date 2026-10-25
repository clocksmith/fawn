/* SPDX-License-Identifier: MIT
 * Copies, texture writes, queries and destruction over the public WebGPU C
 * ABI, with the resource rules of doe-zig-runtime's WebGPU command path
 * (runtime/zig/src/core/resource): a copy source that does not exist yet is
 * created zero-filled, a resource too small or of another shape is replaced
 * (losing its contents), and each command is one submission waited to
 * completion. Buffer-side layouts fall back to the other side's bytes per row
 * and rows per image as that path does. */
#include "runtime_internal.h"
#include <stdlib.h>
#include <string.h>

WGPUTextureFormat runtime_webgpu_format(doe_texture_format format)
{
    switch (format) {
    case DOE_FORMAT_R8_UNORM: return WGPUTextureFormat_R8Unorm;
    case DOE_FORMAT_RG8_UNORM: return WGPUTextureFormat_RG8Unorm;
    case DOE_FORMAT_RGBA8_UNORM: return WGPUTextureFormat_RGBA8Unorm;
    case DOE_FORMAT_RGBA8_UNORM_SRGB: return WGPUTextureFormat_RGBA8UnormSrgb;
    case DOE_FORMAT_BGRA8_UNORM: return WGPUTextureFormat_BGRA8Unorm;
    case DOE_FORMAT_R32_UINT: return WGPUTextureFormat_R32Uint;
    case DOE_FORMAT_R32_FLOAT: return WGPUTextureFormat_R32Float;
    case DOE_FORMAT_RGBA16_FLOAT: return WGPUTextureFormat_RGBA16Float;
    case DOE_FORMAT_RGBA32_FLOAT: return WGPUTextureFormat_RGBA32Float;
    }
    return WGPUTextureFormat_Undefined;
}

texture_entry *runtime_find_texture(doe_runtime *runtime, uint64_t handle)
{
    for (size_t i = 0; i < runtime->texture_count; i++) if (runtime->textures[i].declared.handle == handle) return &runtime->textures[i];
    return NULL;
}

static void remove_texture(doe_runtime *runtime, texture_entry *entry)
{
    wgpuTextureDestroy(entry->texture);
    wgpuTextureRelease(entry->texture);
    *entry = runtime->textures[--runtime->texture_count];
}

void runtime_release_textures(doe_runtime *runtime)
{
    for (size_t i = 0; i < runtime->texture_count; i++) wgpuTextureRelease(runtime->textures[i].texture);
    free(runtime->textures);
    runtime->textures = NULL;
    runtime->texture_count = runtime->texture_capacity = 0;
}

/* The handle's texture when its shape matches and its usage covers the
 * declared one; otherwise a new zero-filled texture replaces it. */
static WGPUTexture ensure_texture(doe_runtime *runtime, const doe_texture_desc *desc, doe_trace_execution *execution)
{
    WGPUTextureUsage usage = (WGPUTextureUsage)desc->usage | WGPUTextureUsage_CopySrc | WGPUTextureUsage_CopyDst;
    texture_entry *existing = runtime_find_texture(runtime, desc->handle);
    if (existing && existing->declared.width == desc->width && existing->declared.height == desc->height &&
        existing->declared.depth == desc->depth && existing->declared.format == desc->format &&
        (existing->usage & usage) == usage) {
        return existing->texture;
    }
    if (existing) {
        usage |= existing->usage;
        remove_texture(runtime, existing);
    }
    if (runtime->texture_count == runtime->texture_capacity) {
        size_t capacity = runtime->texture_capacity ? runtime->texture_capacity * 2 : 8;
        texture_entry *grown = realloc(runtime->textures, capacity * sizeof(*grown));
        if (!grown) { runtime_fail(execution, "out_of_memory", "out of memory"); return NULL; }
        runtime->textures = grown;
        runtime->texture_capacity = capacity;
    }
    WGPUTextureDescriptor descriptor = WGPU_TEXTURE_DESCRIPTOR_INIT;
    descriptor.usage = usage;
    descriptor.dimension = WGPUTextureDimension_2D;
    descriptor.size = (WGPUExtent3D){.width = desc->width, .height = desc->height, .depthOrArrayLayers = desc->depth};
    descriptor.format = runtime_webgpu_format(desc->format);
    WGPUTexture texture = wgpuDeviceCreateTexture(runtime->device, &descriptor);
    if (!texture) {
        runtime_fail(execution, "texture_create_failed", "texture %llu was not created", (unsigned long long)desc->handle);
        return NULL;
    }
    runtime->textures[runtime->texture_count++] = (texture_entry){.texture = texture, .declared = *desc, .usage = usage};
    return texture;
}

static uint32_t layout_value(uint32_t primary, uint32_t fallback)
{
    uint32_t value = primary ? primary : fallback;
    return value ? value : WGPU_COPY_STRIDE_UNDEFINED;
}

static WGPUTexelCopyTextureInfo texture_info(WGPUTexture texture)
{
    WGPUTexelCopyTextureInfo info = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
    info.texture = texture;
    return info;
}

static WGPUTexelCopyBufferInfo buffer_info(WGPUBuffer buffer, const doe_copy_side *buffer_side, const doe_copy_side *other)
{
    WGPUTexelCopyBufferInfo info = WGPU_TEXEL_COPY_BUFFER_INFO_INIT;
    info.buffer = buffer;
    info.layout.offset = buffer_side->offset;
    info.layout.bytesPerRow = layout_value(buffer_side->bytes_per_row, other->bytes_per_row);
    info.layout.rowsPerImage = layout_value(buffer_side->rows_per_image, other->rows_per_image);
    return info;
}

static WGPUExtent3D extent_of(const doe_texture_desc *desc)
{
    return (WGPUExtent3D){.width = desc->width, .height = desc->height, .depthOrArrayLayers = desc->depth};
}

/* bytes + offset rounded up to 4, the buffer size doe-zig-runtime requires. */
static bool required_bytes(uint64_t bytes, uint64_t offset, uint64_t *out, doe_trace_execution *execution)
{
    if (offset > UINT64_MAX - bytes || bytes + offset > UINT64_MAX - 3)
        return runtime_fail(execution, "copy_range_invalid", "copy range overflows");
    *out = (bytes + offset + 3) & ~UINT64_C(3);
    return true;
}

static WGPUBuffer side_buffer(doe_runtime *runtime, const doe_copy_side *side, uint64_t bytes, doe_trace_execution *execution)
{
    uint64_t size = 0;
    if (!required_bytes(bytes, side->offset, &size, execution)) return NULL;
    buffer_entry *entry = runtime_ensure_buffer(runtime, side->handle, size, execution);
    return entry ? entry->buffer : NULL;
}

bool runtime_execute_copy(doe_runtime *runtime, const doe_command *command, doe_trace_execution *execution)
{
    const doe_copy_side *src = &command->as.copy.src, *dst = &command->as.copy.dst;
    uint64_t bytes = command->as.copy.bytes;
    uint64_t start = doe_runtime_monotonic_ns();
    runtime_push_scopes(runtime);
    WGPUBuffer src_buffer = NULL, dst_buffer = NULL;
    WGPUTexture src_texture = NULL, dst_texture = NULL;
    bool ok = true;
    switch (command->as.copy.direction) {
    case DOE_COPY_BUFFER_TO_BUFFER:
        ok = (src_buffer = side_buffer(runtime, src, bytes, execution)) && (dst_buffer = side_buffer(runtime, dst, bytes, execution));
        break;
    case DOE_COPY_BUFFER_TO_TEXTURE:
        ok = (src_buffer = side_buffer(runtime, src, bytes, execution)) && (dst_texture = ensure_texture(runtime, &dst->texture, execution));
        break;
    case DOE_COPY_TEXTURE_TO_BUFFER:
        ok = (src_texture = ensure_texture(runtime, &src->texture, execution)) && (dst_buffer = side_buffer(runtime, dst, bytes, execution));
        break;
    case DOE_COPY_TEXTURE_TO_TEXTURE:
        ok = (src_texture = ensure_texture(runtime, &src->texture, execution)) && (dst_texture = ensure_texture(runtime, &dst->texture, execution));
        break;
    }
    uint64_t setup_end = doe_runtime_monotonic_ns();
    uint64_t encode_end = setup_end;
    if (ok) {
        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(runtime->device, NULL);
        WGPUTexelCopyBufferInfo buffer;
        WGPUTexelCopyTextureInfo source, destination;
        WGPUExtent3D extent;
        switch (command->as.copy.direction) {
        case DOE_COPY_BUFFER_TO_BUFFER:
            wgpuCommandEncoderCopyBufferToBuffer(encoder, src_buffer, src->offset, dst_buffer, dst->offset, bytes);
            break;
        case DOE_COPY_BUFFER_TO_TEXTURE:
            buffer = buffer_info(src_buffer, src, dst);
            destination = texture_info(dst_texture);
            extent = extent_of(&dst->texture);
            wgpuCommandEncoderCopyBufferToTexture(encoder, &buffer, &destination, &extent);
            break;
        case DOE_COPY_TEXTURE_TO_BUFFER:
            source = texture_info(src_texture);
            buffer = buffer_info(dst_buffer, dst, src);
            extent = extent_of(&src->texture);
            wgpuCommandEncoderCopyTextureToBuffer(encoder, &source, &buffer, &extent);
            break;
        case DOE_COPY_TEXTURE_TO_TEXTURE:
            source = texture_info(src_texture);
            destination = texture_info(dst_texture);
            extent = extent_of(&src->texture);
            wgpuCommandEncoderCopyTextureToTexture(encoder, &source, &destination, &extent);
            break;
        }
        WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, NULL);
        wgpuCommandEncoderRelease(encoder);
        encode_end = doe_runtime_monotonic_ns();
        ok = runtime_submit_and_wait(runtime, commands, execution);
        execution->submit_count = 1;
    }
    uint64_t end = doe_runtime_monotonic_ns();
    if (!runtime_pop_scopes(runtime, execution)) ok = false;
    execution->setup_ns = setup_end - start;
    execution->encode_ns = encode_end - setup_end;
    execution->submit_wait_ns = end - encode_end;
    return ok;
}

static bool texture_write(doe_runtime *runtime, const doe_command *command, doe_trace_execution *execution)
{
    const doe_texture_desc *desc = &command->as.texture_write.texture;
    uint64_t start = doe_runtime_monotonic_ns();
    runtime_push_scopes(runtime);
    WGPUTexture texture = ensure_texture(runtime, desc, execution);
    bool ok = texture != NULL;
    uint64_t setup_end = doe_runtime_monotonic_ns();
    if (ok && command->as.texture_write.data_size) {
        WGPUTexelCopyTextureInfo destination = texture_info(texture);
        WGPUTexelCopyBufferLayout layout = WGPU_TEXEL_COPY_BUFFER_LAYOUT_INIT;
        layout.offset = command->as.texture_write.offset;
        layout.bytesPerRow = layout_value(command->as.texture_write.bytes_per_row, desc->width * doe_texture_format_bytes(desc->format));
        layout.rowsPerImage = layout_value(command->as.texture_write.rows_per_image, desc->height);
        /* The first layer, as doe-zig-runtime's whole-mip write. */
        WGPUExtent3D extent = {.width = desc->width, .height = desc->height, .depthOrArrayLayers = 1};
        wgpuQueueWriteTexture(runtime->queue, &destination, command->as.texture_write.data,
            command->as.texture_write.data_size, &layout, &extent);
        ok = runtime_wait_queue(runtime, execution);
        execution->submit_count = 1;
    }
    uint64_t end = doe_runtime_monotonic_ns();
    if (!runtime_pop_scopes(runtime, execution)) ok = false;
    execution->setup_ns = setup_end - start;
    execution->submit_wait_ns = end - setup_end;
    return ok;
}

static bool texture_query(doe_runtime *runtime, const doe_command *command, doe_trace_execution *execution)
{
    const doe_texture_query *q = &command->as.texture_query;
    texture_entry *entry = runtime_find_texture(runtime, q->handle);
    if (!entry) return runtime_fail(execution, "texture_query_mismatch", "texture %llu does not exist", (unsigned long long)q->handle);
    const doe_texture_desc *t = &entry->declared;
    const char *field = q->has_width && q->width != t->width ? "width" :
        q->has_height && q->height != t->height ? "height" :
        q->has_depth && q->depth != t->depth ? "depthOrArrayLayers" :
        q->has_format && q->format != t->format ? "format" :
        q->has_dimension && !q->dimension_2d ? "dimension" :
        q->has_view_dimension && q->view_dimension != (t->depth > 1 ? 1 : 0) ? "viewDimension" :
        q->has_sample_count && q->sample_count != 1 ? "sampleCount" :
        q->has_usage && (entry->usage & q->usage) != q->usage ? "usage" : NULL;
    if (field) return runtime_fail(execution, "texture_query_mismatch", "texture %llu %s differs from the query",
        (unsigned long long)q->handle, field);
    return true;
}

bool runtime_execute_texture(doe_runtime *runtime, const doe_command *command, doe_trace_execution *execution)
{
    if (command->kind == DOE_COMMAND_TEXTURE_WRITE) return texture_write(runtime, command, execution);
    if (command->kind == DOE_COMMAND_TEXTURE_QUERY) return texture_query(runtime, command, execution);
    /* A texture that was never created is not an error, as in doe-zig-runtime. */
    texture_entry *entry = runtime_find_texture(runtime, command->as.texture_destroy.handle);
    if (entry) remove_texture(runtime, entry);
    return true;
}

typedef struct map_reply {
    bool done;
    WGPUMapAsyncStatus status;
} map_reply;

static void mapped(WGPUMapAsyncStatus status, WGPUStringView message, void *userdata1, void *userdata2)
{
    (void)message;
    (void)userdata2;
    map_reply *reply = userdata1;
    reply->done = true;
    reply->status = status;
}

bool runtime_read_back(doe_runtime *runtime, WGPUBuffer source, uint64_t offset, uint64_t size, uint8_t *out,
    doe_trace_execution *execution)
{
    WGPUBufferDescriptor descriptor = WGPU_BUFFER_DESCRIPTOR_INIT;
    descriptor.size = size;
    descriptor.usage = WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst;
    WGPUBuffer readback = wgpuDeviceCreateBuffer(runtime->device, &descriptor);
    if (!readback) return runtime_fail(execution, "readback_failed", "readback buffer was not created");
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(runtime->device, NULL);
    wgpuCommandEncoderCopyBufferToBuffer(encoder, source, offset, readback, 0, size);
    WGPUCommandBuffer copy = wgpuCommandEncoderFinish(encoder, NULL);
    wgpuCommandEncoderRelease(encoder);
    bool ok = runtime_submit_and_wait(runtime, copy, execution);
    if (ok) {
        map_reply reply = {0};
        WGPUBufferMapCallbackInfo callback = {.mode = WGPUCallbackMode_WaitAnyOnly, .callback = mapped, .userdata1 = &reply};
        if (!runtime_wait_future(runtime, wgpuBufferMapAsync(readback, WGPUMapMode_Read, 0, size, callback)) ||
            reply.status != WGPUMapAsyncStatus_Success) {
            ok = runtime_fail(execution, "readback_failed", "readback did not map");
        } else {
            const void *bytes = wgpuBufferGetConstMappedRange(readback, 0, size);
            if (bytes) memcpy(out, bytes, (size_t)size);
            else ok = runtime_fail(execution, "readback_failed", "readback has no mapped range");
            wgpuBufferUnmap(readback);
        }
    }
    wgpuBufferRelease(readback);
    return ok;
}

bool doe_runtime_read_buffer(doe_runtime *runtime, uint64_t handle, uint8_t **data, size_t *size,
    doe_trace_execution *execution)
{
    *execution = (doe_trace_execution){.ok = true};
    *data = NULL;
    *size = 0;
    buffer_entry *entry = runtime_find_buffer(runtime, handle);
    if (!entry) return runtime_fail(execution, "readback_missing", "buffer %llu does not exist", (unsigned long long)handle);
    if (entry->size > SIZE_MAX) return runtime_fail(execution, "readback_failed", "buffer exceeds host memory addressing");
    uint8_t *bytes = malloc((size_t)entry->size);
    if (!bytes) return runtime_fail(execution, "out_of_memory", "readback allocation failed");
    runtime_push_scopes(runtime);
    bool ok = runtime_read_back(runtime, entry->buffer, 0, entry->size, bytes, execution);
    if (!runtime_pop_scopes(runtime, execution)) ok = false;
    if (!ok) { free(bytes); return false; }
    *data = bytes;
    *size = (size_t)entry->size;
    return true;
}
