/* SPDX-License-Identifier: MIT
 * Copies, texture writes, queries and destruction over the public WebGPU C
 * ABI, with the resource rules of doe-zig-runtime's WebGPU command path
 * (runtime/zig/src/core/resource): a copy source that does not exist yet is
 * created zero-filled, a resource too small or of another shape is replaced
 * (losing its contents), and each copy command is one submission governed by the
 * selected queue synchronization mode. Buffer-side layouts fall back to the other side's bytes per row
 * and rows per image as that path does. An active-mode quirk's temporary
 * buffer routes buffer-to-texture and texture-to-texture copies through a
 * staging buffer of the command's bytes rounded up to its alignment, as that
 * path's uses_temporary_buffer branches do. */
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
    uint32_t mips = desc->mip_levels ? desc->mip_levels : 1;
    if (existing && existing->declared.width == desc->width && existing->declared.height == desc->height &&
        existing->declared.depth == desc->depth && existing->declared.format == desc->format &&
        (existing->usage & usage) == usage && (existing->declared.mip_levels ? existing->declared.mip_levels : 1) >= mips) {
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
    descriptor.mipLevelCount = mips;
    WGPUTexture texture = wgpuDeviceCreateTexture(runtime->device, &descriptor);
    if (!texture) {
        runtime_fail(execution, "texture_create_failed", "texture %llu was not created", (unsigned long long)desc->handle);
        return NULL;
    }
    doe_texture_desc declared = *desc;
    declared.mip_levels = mips;
    runtime->textures[runtime->texture_count++] = (texture_entry){.texture = texture, .declared = declared, .usage = usage};
    return texture;
}

/* wgpu_resources.zig getOrCreateTextureFromBinding: the handle's texture when
 * its usage covers the binding's and the format matches; otherwise one of the
 * same shape (a missing texture: 1x1) with the union of usages replaces it. */
WGPUTexture runtime_binding_texture(doe_runtime *runtime, uint64_t handle, WGPUTextureUsage required, bool has_format,
    doe_texture_format format, doe_texture_format *view_format, doe_trace_execution *execution)
{
    texture_entry *existing = runtime_find_texture(runtime, handle);
    if (existing && (existing->usage & required) == required && (!has_format || existing->declared.format == format)) {
        *view_format = existing->declared.format;
        return existing->texture;
    }
    doe_texture_desc desc = existing ? existing->declared :
        (doe_texture_desc){.handle = handle, .width = 1, .height = 1, .depth = 1, .format = DOE_FORMAT_R8_UNORM};
    if (has_format) desc.format = format;
    desc.usage = (existing ? existing->usage : 0) | required;
    *view_format = desc.format;
    return ensure_texture(runtime, &desc, execution);
}

static uint32_t layout_value(uint32_t primary, uint32_t fallback)
{
    uint32_t value = primary ? primary : fallback;
    return value ? value : WGPU_COPY_STRIDE_UNDEFINED;
}

static WGPUTexelCopyTextureInfo texture_info(WGPUTexture texture, uint32_t mip_level)
{
    WGPUTexelCopyTextureInfo info = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
    info.texture = texture;
    info.mipLevel = mip_level;
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

/* The extent of one mip level (doe-zig-runtime's WebGPU path copies the full
 * extent and creates single-level textures, so it cannot copy a level > 0). */
static WGPUExtent3D extent_of(const doe_texture_desc *desc, uint32_t mip_level)
{
    uint32_t width = desc->width >> mip_level, height = desc->height >> mip_level;
    return (WGPUExtent3D){.width = width ? width : 1, .height = height ? height : 1, .depthOrArrayLayers = desc->depth};
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
    WGPUBuffer staging = NULL;
    bool staged = command->as.copy.temporary_buffer && (command->as.copy.direction == DOE_COPY_BUFFER_TO_TEXTURE ||
        command->as.copy.direction == DOE_COPY_TEXTURE_TO_TEXTURE);
    if (ok && staged) {
        uint64_t alignment = command->as.copy.temporary_buffer_alignment ? command->as.copy.temporary_buffer_alignment : 1;
        if (bytes > UINT64_MAX - (alignment - 1)) ok = runtime_fail(execution, "copy_range_invalid", "staging size overflows");
        WGPUBufferDescriptor descriptor = WGPU_BUFFER_DESCRIPTOR_INIT;
        descriptor.size = ok ? (bytes + alignment - 1) / alignment * alignment : 0;
        descriptor.usage = WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst;
        if (ok && !(staging = wgpuDeviceCreateBuffer(runtime->device, &descriptor)))
            ok = runtime_fail(execution, "copy_staging_failed", "the staging buffer was not created");
    }
    uint64_t setup_end = doe_runtime_monotonic_ns();
    uint64_t encode_end = setup_end;
    if (ok && staged) {
        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(runtime->device, NULL);
        WGPUTexelCopyBufferInfo through = WGPU_TEXEL_COPY_BUFFER_INFO_INIT;
        through.buffer = staging;
        WGPUTexelCopyTextureInfo destination = texture_info(dst_texture, dst->mip_level);
        if (command->as.copy.direction == DOE_COPY_BUFFER_TO_TEXTURE) {
            wgpuCommandEncoderCopyBufferToBuffer(encoder, src_buffer, src->offset, staging, 0, bytes);
            through.layout.bytesPerRow = layout_value(src->bytes_per_row, dst->bytes_per_row);
            through.layout.rowsPerImage = layout_value(src->rows_per_image, dst->rows_per_image);
            WGPUExtent3D extent = extent_of(&dst->texture, dst->mip_level);
            wgpuCommandEncoderCopyBufferToTexture(encoder, &through, &destination, &extent);
        } else {
            WGPUTexelCopyTextureInfo source = texture_info(src_texture, src->mip_level);
            WGPUExtent3D src_extent = extent_of(&src->texture, src->mip_level), dst_extent = extent_of(&dst->texture, dst->mip_level);
            through.layout.bytesPerRow = layout_value(src->bytes_per_row, dst->bytes_per_row);
            through.layout.rowsPerImage = layout_value(src->rows_per_image, dst->rows_per_image);
            wgpuCommandEncoderCopyTextureToBuffer(encoder, &source, &through, &src_extent);
            through.layout.bytesPerRow = layout_value(dst->bytes_per_row, src->bytes_per_row);
            through.layout.rowsPerImage = layout_value(dst->rows_per_image, src->rows_per_image);
            wgpuCommandEncoderCopyBufferToTexture(encoder, &through, &destination, &dst_extent);
        }
        WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, NULL);
        wgpuCommandEncoderRelease(encoder);
        encode_end = doe_runtime_monotonic_ns();
        ok = runtime_submit(runtime, commands, execution);
        execution->submit_count = 1;
    } else if (ok) {
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
            destination = texture_info(dst_texture, dst->mip_level);
            extent = extent_of(&dst->texture, dst->mip_level);
            wgpuCommandEncoderCopyBufferToTexture(encoder, &buffer, &destination, &extent);
            break;
        case DOE_COPY_TEXTURE_TO_BUFFER:
            source = texture_info(src_texture, src->mip_level);
            buffer = buffer_info(dst_buffer, dst, src);
            extent = extent_of(&src->texture, src->mip_level);
            wgpuCommandEncoderCopyTextureToBuffer(encoder, &source, &buffer, &extent);
            break;
        case DOE_COPY_TEXTURE_TO_TEXTURE:
            source = texture_info(src_texture, src->mip_level);
            destination = texture_info(dst_texture, dst->mip_level);
            extent = extent_of(&src->texture, src->mip_level);
            wgpuCommandEncoderCopyTextureToTexture(encoder, &source, &destination, &extent);
            break;
        }
        WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, NULL);
        wgpuCommandEncoderRelease(encoder);
        encode_end = doe_runtime_monotonic_ns();
        ok = runtime_submit(runtime, commands, execution);
        execution->submit_count = 1;
    }
    uint64_t end = doe_runtime_monotonic_ns();
    if (staging) wgpuBufferRelease(staging);
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
        WGPUTexelCopyTextureInfo destination = texture_info(texture, 0);
        WGPUTexelCopyBufferLayout layout = WGPU_TEXEL_COPY_BUFFER_LAYOUT_INIT;
        layout.offset = command->as.texture_write.offset;
        layout.bytesPerRow = layout_value(command->as.texture_write.bytes_per_row, desc->width * doe_texture_format_bytes(desc->format));
        layout.rowsPerImage = layout_value(command->as.texture_write.rows_per_image, desc->height);
        /* The first layer, as doe-zig-runtime's whole-mip write. */
        WGPUExtent3D extent = {.width = desc->width, .height = desc->height, .depthOrArrayLayers = 1};
        wgpuQueueWriteTexture(runtime->queue, &destination, command->as.texture_write.data,
            command->as.texture_write.data_size, &layout, &extent);
        ok = runtime_after_submit(runtime, execution);
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
    bool ok = runtime_submit(runtime, copy, execution);
    if (ok && runtime->options.defer_queue_sync) ok = runtime_wait_queue(runtime, execution);
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
