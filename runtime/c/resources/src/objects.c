/* SPDX-License-Identifier: MIT */
#include "internal.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

bool resource_config_valid(const doe_resource_config *config)
{
    return config && config->allocator.allocate && config->allocator.deallocate &&
        config->device.identity && config->device.alive &&
        config->device.retain && config->device.release;
}

bool resource_alive(const resource_base *base)
{
    return base->config.device.alive(base->config.device.identity);
}

void *resource_allocate(const doe_resource_config *config, size_t size, size_t alignment)
{
    return config->allocator.allocate(config->allocator.state,
        config->allocator.vtable, size, alignment);
}

void resource_free(const doe_resource_config *config, void *memory, size_t size,
    size_t alignment)
{
    if (memory) config->allocator.deallocate(config->allocator.state,
        config->allocator.vtable, memory, size, alignment);
}

void resource_init(resource_base *base, const doe_resource_config *config,
    doe_resource_native native)
{
    atomic_init(&base->references, 1);
    base->config = *config;
    base->native = native;
    config->device.retain(config->device.identity);
}

void resource_retain(resource_base *base)
{
    unsigned current = atomic_load_explicit(&base->references, memory_order_relaxed);
    for (;;) {
        if (!current || current == UINT_MAX) abort();
        if (atomic_compare_exchange_weak_explicit(&base->references, &current,
                current + 1, memory_order_relaxed, memory_order_relaxed)) return;
    }
}

bool resource_last_reference(resource_base *base)
{
    unsigned previous = atomic_fetch_sub_explicit(&base->references, 1,
        memory_order_acq_rel);
    if (!previous) abort();
    return previous == 1;
}

void resource_finalize(resource_base *base, size_t size, size_t alignment)
{
    doe_resource_config config = base->config;
    doe_resource_native native = base->native;
    if (config.labels && doe_label_store_remove(config.labels, base) != DOE_LABEL_OK)
        abort();
    if (native.release) native.release(native.userdata, native.handle);
    resource_free(&config, base, size, alignment);
    config.device.release(config.device.identity);
}

doe_resource_status doe_resource_shader_create(const doe_resource_config *config,
    doe_resource_native native, doe_resource_shader **out)
{
    if (!out || !resource_config_valid(config) || !native.handle || !native.release)
        return DOE_RESOURCE_INVALID_ARGUMENT;
    if (!config->device.alive(config->device.identity)) return DOE_RESOURCE_UNAVAILABLE;
    doe_resource_shader *shader = resource_allocate(config, sizeof(*shader), _Alignof(doe_resource_shader));
    if (!shader) return DOE_RESOURCE_OUT_OF_MEMORY;
    resource_init(&shader->base, config, native);
    *out = shader;
    return DOE_RESOURCE_OK;
}

void doe_resource_shader_retain(doe_resource_shader *shader)
{
    if (shader) resource_retain(&shader->base);
}

void doe_resource_shader_release(doe_resource_shader *shader)
{
    if (shader && resource_last_reference(&shader->base))
        resource_finalize(&shader->base, sizeof(*shader), _Alignof(doe_resource_shader));
}

void *doe_resource_shader_native(const doe_resource_shader *shader)
{
    return shader ? shader->base.native.handle : NULL;
}

void *doe_resource_shader_device(const doe_resource_shader *shader)
{
    return shader ? shader->base.config.device.identity : NULL;
}

bool resource_buffer_available(const doe_resource_buffer *buffer)
{
    return buffer && !buffer->destroyed && resource_alive(&buffer->base);
}

bool resource_texture_available(const doe_resource_texture *texture)
{
    return texture && !texture->destroyed && resource_alive(&texture->base);
}

bool resource_texture_live(const doe_resource_texture *texture)
{
    return texture && !texture->error_object && resource_alive(&texture->base);
}

doe_resource_status resource_range(uint64_t total, uint64_t offset,
    uint64_t requested, uint64_t *size)
{
    if (offset > total) return DOE_RESOURCE_RANGE;
    uint64_t resolved = requested == UINT64_MAX ? total - offset : requested;
    if (resolved > total - offset) return DOE_RESOURCE_RANGE;
    *size = resolved;
    return DOE_RESOURCE_OK;
}

doe_resource_status doe_resource_buffer_validate(const doe_resource_buffer_desc *desc)
{
    if (!desc) return DOE_RESOURCE_INVALID_ARGUMENT;
    const uint64_t valid_usage = WGPUBufferUsage_MapRead | WGPUBufferUsage_MapWrite |
        WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst | WGPUBufferUsage_Index |
        WGPUBufferUsage_Vertex | WGPUBufferUsage_Uniform | WGPUBufferUsage_Storage |
        WGPUBufferUsage_Indirect | WGPUBufferUsage_QueryResolve;
    if (!desc->usage || (desc->usage & ~valid_usage)) return DOE_RESOURCE_USAGE;
    if ((desc->usage & WGPUBufferUsage_MapRead) &&
        (desc->usage & ~(WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst)))
        return DOE_RESOURCE_USAGE;
    if ((desc->usage & WGPUBufferUsage_MapWrite) &&
        (desc->usage & ~(WGPUBufferUsage_MapWrite | WGPUBufferUsage_CopySrc)))
        return DOE_RESOURCE_USAGE;
    if (desc->mapped_at_creation && desc->size % 4) return DOE_RESOURCE_ALIGNMENT;
    if (desc->mapped_at_creation && desc->size > SIZE_MAX) return DOE_RESOURCE_OVERFLOW;
    return DOE_RESOURCE_OK;
}

doe_resource_status doe_resource_buffer_create(const doe_resource_config *config,
    const doe_resource_buffer_desc *desc, doe_resource_native native,
    doe_resource_buffer **out)
{
    if (!out || !desc || !resource_config_valid(config) || !native.handle || !native.release)
        return DOE_RESOURCE_INVALID_ARGUMENT;
    if (!config->device.alive(config->device.identity)) return DOE_RESOURCE_UNAVAILABLE;
    doe_resource_status status = doe_resource_buffer_validate(desc);
    if (status != DOE_RESOURCE_OK) return status;
    doe_resource_buffer *buffer = resource_allocate(config, sizeof(*buffer), _Alignof(doe_resource_buffer));
    if (!buffer) return DOE_RESOURCE_OUT_OF_MEMORY;
    memset(buffer, 0, sizeof(*buffer));
    buffer->desc = *desc;
    buffer->mapped = desc->mapped_at_creation;
    buffer->map_mode = desc->mapped_at_creation ? WGPUMapMode_Write : WGPUMapMode_None;
    buffer->map_size = desc->mapped_at_creation ? desc->size : 0;
    resource_init(&buffer->base, config, native);
    *out = buffer;
    return DOE_RESOURCE_OK;
}

void doe_resource_buffer_retain(doe_resource_buffer *buffer)
{
    if (buffer) resource_retain(&buffer->base);
}

void doe_resource_buffer_release(doe_resource_buffer *buffer)
{
    if (buffer && resource_last_reference(&buffer->base))
        resource_finalize(&buffer->base, sizeof(*buffer), _Alignof(doe_resource_buffer));
}

void doe_resource_buffer_destroy(doe_resource_buffer *buffer)
{
    if (!buffer || buffer->destroyed) return;
    buffer->destroyed = true;
    buffer->mapped = false;
    buffer->map_pending = false;
    buffer->mapped_bytes = NULL;
    buffer->map_mode = WGPUMapMode_None;
}

const doe_resource_buffer_desc *doe_resource_buffer_description(const doe_resource_buffer *buffer)
{
    return buffer ? &buffer->desc : NULL;
}

void *doe_resource_buffer_native(const doe_resource_buffer *buffer)
{
    return buffer ? buffer->base.native.handle : NULL;
}

doe_resource_status doe_resource_buffer_map_begin(doe_resource_buffer *buffer,
    WGPUMapMode mode, uint64_t offset, uint64_t size, uint64_t *generation)
{
    if (!buffer || !generation) return DOE_RESOURCE_INVALID_ARGUMENT;
    if (!resource_buffer_available(buffer)) return DOE_RESOURCE_UNAVAILABLE;
    if (buffer->mapped || buffer->map_pending) return DOE_RESOURCE_STATE;
    WGPUBufferUsage usage;
    if (mode == WGPUMapMode_Read) usage = WGPUBufferUsage_MapRead;
    else if (mode == WGPUMapMode_Write) usage = WGPUBufferUsage_MapWrite;
    else return DOE_RESOURCE_INVALID_ARGUMENT;
    if (!(buffer->desc.usage & usage)) return DOE_RESOURCE_USAGE;
    uint64_t resolved;
    doe_resource_status status = resource_range(buffer->desc.size, offset, size, &resolved);
    if (status != DOE_RESOURCE_OK) return status;
    if (offset % 8 || resolved % 4) return DOE_RESOURCE_ALIGNMENT;
    if (offset > SIZE_MAX || resolved > SIZE_MAX) return DOE_RESOURCE_OVERFLOW;
    if (buffer->generation == UINT64_MAX) return DOE_RESOURCE_OVERFLOW;
    buffer->generation++;
    buffer->map_pending = true;
    buffer->map_mode = mode;
    buffer->map_offset = offset;
    buffer->map_size = resolved;
    *generation = buffer->generation;
    return DOE_RESOURCE_OK;
}

doe_resource_status doe_resource_buffer_map_complete(doe_resource_buffer *buffer,
    uint64_t generation, bool success, unsigned char *mapped_bytes)
{
    if (!buffer) return DOE_RESOURCE_INVALID_ARGUMENT;
    if (!resource_buffer_available(buffer)) return DOE_RESOURCE_UNAVAILABLE;
    bool creation = buffer->mapped && !buffer->map_pending &&
        buffer->desc.mapped_at_creation && buffer->generation == 0 && generation == 0;
    if ((!buffer->map_pending && !creation) || generation != buffer->generation)
        return DOE_RESOURCE_STATE;
    if (success && !mapped_bytes && buffer->map_size) return DOE_RESOURCE_INVALID_ARGUMENT;
    /* Consume the initial token even for a valid empty mapping with no pointer. */
    if (creation) buffer->generation++;
    buffer->map_pending = false;
    buffer->mapped = success;
    if (!success) buffer->map_mode = WGPUMapMode_None;
    buffer->mapped_bytes = success ? mapped_bytes : NULL;
    return success ? DOE_RESOURCE_OK : DOE_RESOURCE_EXECUTION_FAILED;
}

doe_resource_status doe_resource_buffer_mapped_range_const(doe_resource_buffer *buffer,
    uint64_t offset, uint64_t size, const unsigned char **out)
{
    if (!buffer || !out) return DOE_RESOURCE_INVALID_ARGUMENT;
    if (!resource_buffer_available(buffer)) return DOE_RESOURCE_UNAVAILABLE;
    if (!buffer->mapped) return DOE_RESOURCE_STATE;
    uint64_t resolved;
    doe_resource_status status = resource_range(buffer->desc.size, offset, size, &resolved);
    if (status != DOE_RESOURCE_OK) return status;
    if (offset % 8 || resolved % 4) return DOE_RESOURCE_ALIGNMENT;
    if (offset < buffer->map_offset || offset - buffer->map_offset > buffer->map_size ||
        resolved > buffer->map_size - (offset - buffer->map_offset)) return DOE_RESOURCE_RANGE;
    if (!buffer->mapped_bytes && (resolved || offset != buffer->map_offset)) return DOE_RESOURCE_STATE;
    *out = buffer->mapped_bytes ? buffer->mapped_bytes + (size_t)(offset - buffer->map_offset) : NULL;
    return DOE_RESOURCE_OK;
}

doe_resource_status doe_resource_buffer_mapped_range(doe_resource_buffer *buffer,
    uint64_t offset, uint64_t size, unsigned char **out)
{
    if (!buffer || !out) return DOE_RESOURCE_INVALID_ARGUMENT;
    if (buffer->map_mode != WGPUMapMode_Write) return DOE_RESOURCE_USAGE;
    const unsigned char *range;
    doe_resource_status status = doe_resource_buffer_mapped_range_const(buffer, offset, size, &range);
    if (status != DOE_RESOURCE_OK) return status;
    *out = (unsigned char *)range;
    return DOE_RESOURCE_OK;
}

doe_resource_status doe_resource_buffer_unmap(doe_resource_buffer *buffer)
{
    if (!buffer) return DOE_RESOURCE_INVALID_ARGUMENT;
    if (!resource_buffer_available(buffer)) return DOE_RESOURCE_UNAVAILABLE;
    buffer->map_pending = false;
    buffer->mapped = false;
    buffer->mapped_bytes = NULL;
    buffer->map_mode = WGPUMapMode_None;
    buffer->map_offset = 0;
    buffer->map_size = 0;
    return DOE_RESOURCE_OK;
}

doe_resource_status doe_resource_buffer_copy_validate(const void *device,
    const doe_resource_buffer_copy *copy, doe_resource_alias_policy alias,
    uint64_t *resolved_size)
{
    if (!device || !copy || !resolved_size || !copy->source || !copy->destination ||
        (alias != DOE_RESOURCE_DISTINCT_BUFFERS && alias != DOE_RESOURCE_DISJOINT_RANGES))
        return DOE_RESOURCE_INVALID_ARGUMENT;
    const doe_resource_buffer *source = copy->source;
    const doe_resource_buffer *destination = copy->destination;
    if (!resource_buffer_available(source) || !resource_buffer_available(destination))
        return DOE_RESOURCE_UNAVAILABLE;
    if (source->base.config.device.identity != device || destination->base.config.device.identity != device)
        return DOE_RESOURCE_DEVICE_MISMATCH;
    if (!(source->desc.usage & WGPUBufferUsage_CopySrc) ||
        !(destination->desc.usage & WGPUBufferUsage_CopyDst)) return DOE_RESOURCE_USAGE;
    if (copy->source_offset > source->desc.size || copy->destination_offset > destination->desc.size)
        return DOE_RESOURCE_RANGE;
    uint64_t size = copy->size == UINT64_MAX ? source->desc.size - copy->source_offset : copy->size;
    if (copy->source_offset % 4 || copy->destination_offset % 4 || size % 4)
        return DOE_RESOURCE_ALIGNMENT;
    if (size > source->desc.size - copy->source_offset || size > destination->desc.size - copy->destination_offset)
        return DOE_RESOURCE_RANGE;
    if (source == destination && (alias == DOE_RESOURCE_DISTINCT_BUFFERS ||
        (size && copy->source_offset < copy->destination_offset + size &&
        copy->destination_offset < copy->source_offset + size))) return DOE_RESOURCE_ALIAS;
    *resolved_size = size;
    return DOE_RESOURCE_OK;
}

doe_resource_status resource_indirect_validate(const void *device,
    const doe_resource_buffer *arguments, uint64_t offset, uint64_t bytes)
{
    if (!resource_buffer_available(arguments)) return DOE_RESOURCE_UNAVAILABLE;
    if (arguments->base.config.device.identity != device) return DOE_RESOURCE_DEVICE_MISMATCH;
    if (!(arguments->desc.usage & WGPUBufferUsage_Indirect)) return DOE_RESOURCE_USAGE;
    if (offset % 4) return DOE_RESOURCE_ALIGNMENT;
    if (offset > arguments->desc.size || bytes > arguments->desc.size - offset) return DOE_RESOURCE_RANGE;
    return DOE_RESOURCE_OK;
}
