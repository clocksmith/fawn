#include "internal.h"

doe_vk_status doe_vk_fail(doe_vk_status status, const char *operation, doe_vk_error *error) {
    if (error != NULL) *error = (doe_vk_error){ status, 0, operation };
    return status;
}

doe_vk_status doe_vk_result(doe_vk_context *context, VkResult result,
                           const char *operation, doe_vk_error *error) {
    doe_vk_status status;
    switch (result) {
        case 0: status = DOE_VK_OK; break;
        case 1: status = DOE_VK_PENDING; break;
        case 2: status = DOE_VK_TIMEOUT; break;
        case -1: case -2: status = DOE_VK_OUT_OF_MEMORY; break;
        case VK_ERROR_OUT_OF_DATE_KHR: status = DOE_VK_SURFACE_OUTDATED; break;
        case VK_ERROR_SURFACE_LOST_KHR: status = DOE_VK_SURFACE_LOST; break;
        case -4:
            status = DOE_VK_DEVICE_LOST;
            if (context != NULL) context->lost = true;
            break;
        /* Preserve retained vulkan_errors.zig UnsupportedFeature mapping,
         * including VK_ERROR_UNKNOWN; native_result remains available. */
        case -7: case -8: case -9: case -10: case -11: case -12: case -13:
            status = DOE_VK_UNSUPPORTED;
            break;
        default: status = DOE_VK_NATIVE_FAILURE; break;
    }
    if (error != NULL) *error = (doe_vk_error){ status, result, operation };
    return status;
}

const char *doe_vk_status_name(doe_vk_status status) {
    switch (status) {
        case DOE_VK_OK: return "ok";
        case DOE_VK_PENDING: return "pending";
        case DOE_VK_TIMEOUT: return "timeout";
        case DOE_VK_INVALID_ARGUMENT: return "invalid argument";
        case DOE_VK_OUT_OF_MEMORY: return "out of memory";
        case DOE_VK_LOADER_UNAVAILABLE: return "loader unavailable";
        case DOE_VK_UNSUPPORTED: return "unsupported";
        case DOE_VK_NATIVE_FAILURE: return "native failure";
        case DOE_VK_DEVICE_LOST: return "device lost";
        case DOE_VK_BUSY: return "busy";
        case DOE_VK_INVALID_STATE: return "invalid state";
        case DOE_VK_SURFACE_OUTDATED: return "surface outdated";
        case DOE_VK_SURFACE_LOST: return "surface lost";
    }
    return "unknown status";
}

bool doe_vk_range(uint64_t allocation, uint64_t offset, uint64_t size) {
    return offset <= allocation && size <= allocation - offset;
}

doe_vk_status doe_vk_memory_type(const VkPhysicalDeviceMemoryProperties *properties,
    uint32_t bits, uint32_t required, uint32_t preferred, uint32_t *out) {
    if (properties == NULL || out == NULL || properties->memoryTypeCount > 32 ||
        properties->memoryHeapCount > 16) return DOE_VK_INVALID_ARGUMENT;
    uint32_t fallback = UINT32_MAX;
    for (uint32_t i = 0; i < properties->memoryTypeCount; ++i) {
        const VkMemoryType *type = &properties->memoryTypes[i];
        if (!(bits & (UINT32_C(1) << i)) || type->heapIndex >= properties->memoryHeapCount ||
            (type->propertyFlags & required) != required) continue;
        if ((type->propertyFlags & preferred) == preferred) {
            *out = i;
            return DOE_VK_OK;
        }
        if (fallback == UINT32_MAX) fallback = i;
    }
    if (fallback == UINT32_MAX) return DOE_VK_UNSUPPORTED;
    *out = fallback;
    return DOE_VK_OK;
}

doe_vk_status doe_vk_buffer_create(doe_vk_context *context, uint64_t size,
    uint32_t usage, doe_vk_memory_kind kind, doe_vk_buffer **out, doe_vk_error *error) {
    const uint32_t allowed = DOE_VK_BUFFER_COPY_SRC | DOE_VK_BUFFER_COPY_DST |
        DOE_VK_BUFFER_UNIFORM | DOE_VK_BUFFER_STORAGE | DOE_VK_BUFFER_INDIRECT | DOE_VK_BUFFER_VERTEX;
    if (context == NULL || out == NULL || size == 0 || size > SIZE_MAX ||
        usage == 0 || (usage & ~allowed) != 0 ||
        (kind != DOE_VK_MEMORY_HOST && kind != DOE_VK_MEMORY_DEVICE))
        return doe_vk_fail(DOE_VK_INVALID_ARGUMENT, "buffer descriptor", error);
    if (context->lost) return doe_vk_fail(DOE_VK_DEVICE_LOST, "buffer context", error);
    doe_vk_buffer *buffer = calloc(1, sizeof(*buffer));
    if (buffer == NULL) return doe_vk_fail(DOE_VK_OUT_OF_MEMORY, "buffer owner", error);
    doe_vk_status status = doe_vk_context_retain(context);
    if (status != DOE_VK_OK) {
        free(buffer);
        return doe_vk_fail(status, "buffer context retain", error);
    }
    buffer->context = context;
    buffer->references = 1;
    buffer->size = size;
    buffer->usage = usage;
    const VkBufferCreateInfo create = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = usage
    };
    status = doe_vk_result(context, context->vk.vkCreateBuffer(context->device, &create, NULL, &buffer->buffer), "vkCreateBuffer", error);
    if (status != DOE_VK_OK) goto failed;
    VkMemoryRequirements requirements = {0};
    context->vk.vkGetBufferMemoryRequirements(context->device, buffer->buffer, &requirements);
    if (requirements.size < size || requirements.size == 0) {
        status = doe_vk_fail(DOE_VK_NATIVE_FAILURE, "buffer memory requirements", error);
        goto failed;
    }
    uint32_t required = kind == DOE_VK_MEMORY_HOST ?
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    uint32_t preferred = kind == DOE_VK_MEMORY_HOST ? VK_MEMORY_PROPERTY_HOST_CACHED_BIT : 0;
    uint32_t index = 0;
    status = doe_vk_memory_type(&context->memory, requirements.memoryTypeBits, required, preferred, &index);
    if (status != DOE_VK_OK) {
        doe_vk_fail(status, "compatible buffer memory", error);
        goto failed;
    }
    const VkMemoryAllocateInfo allocate = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = requirements.size,
        .memoryTypeIndex = index
    };
    status = doe_vk_result(context, context->vk.vkAllocateMemory(context->device, &allocate, NULL, &buffer->memory), "vkAllocateMemory", error);
    if (status != DOE_VK_OK) goto failed;
    status = doe_vk_result(context, context->vk.vkBindBufferMemory(context->device, buffer->buffer, buffer->memory, 0), "vkBindBufferMemory", error);
    if (status != DOE_VK_OK) goto failed;
    if (kind == DOE_VK_MEMORY_HOST) {
        status = doe_vk_result(context, context->vk.vkMapMemory(context->device, buffer->memory, 0, size, 0, &buffer->mapped), "vkMapMemory", error);
        if (status != DOE_VK_OK) goto failed;
        if (buffer->mapped == NULL) {
            status = doe_vk_fail(DOE_VK_NATIVE_FAILURE, "null mapped buffer", error);
            goto failed;
        }
        memset(buffer->mapped, 0, (size_t)size);
    }
    *out = buffer;
    return doe_vk_fail(DOE_VK_OK, "buffer ready", error);
failed:
    doe_vk_buffer_release(buffer);
    return status;
}

doe_vk_status doe_vk_buffer_retain(doe_vk_buffer *buffer) {
    if (buffer == NULL) return DOE_VK_INVALID_ARGUMENT;
    if (buffer->references == UINT32_MAX) return DOE_VK_OUT_OF_MEMORY;
    ++buffer->references;
    return DOE_VK_OK;
}

void doe_vk_buffer_release(doe_vk_buffer *buffer) {
    if (buffer == NULL || --buffer->references != 0) return;
    doe_vk_context *context = buffer->context;
    if (buffer->mapped != NULL) context->vk.vkUnmapMemory(context->device, buffer->memory);
    if (buffer->buffer != 0) context->vk.vkDestroyBuffer(context->device, buffer->buffer, NULL);
    if (buffer->memory != 0) context->vk.vkFreeMemory(context->device, buffer->memory, NULL);
    free(buffer);
    doe_vk_context_release(context);
}

uint64_t doe_vk_buffer_size(const doe_vk_buffer *buffer) {
    return buffer != NULL ? buffer->size : 0;
}

static doe_vk_status host_access(doe_vk_buffer *buffer, uint64_t offset, size_t size) {
    if (buffer == NULL || !doe_vk_range(buffer->size, offset, size)) return DOE_VK_INVALID_ARGUMENT;
    if (buffer->context->lost) return DOE_VK_DEVICE_LOST;
    if (buffer->recorded_uses != 0) return DOE_VK_BUSY;
    if (buffer->mapped == NULL) return DOE_VK_UNSUPPORTED;
    return DOE_VK_OK;
}

doe_vk_status doe_vk_buffer_write(doe_vk_buffer *buffer, uint64_t offset,
    const void *bytes, size_t size) {
    if (size != 0 && bytes == NULL) return DOE_VK_INVALID_ARGUMENT;
    doe_vk_status status = host_access(buffer, offset, size);
    if (status != DOE_VK_OK) return status;
    if (buffer->content_status != DOE_VK_OK && (offset != 0 || size != buffer->size))
        return buffer->content_status;
    if (size != 0) memcpy((uint8_t *)buffer->mapped + (size_t)offset, bytes, size);
    if (offset == 0 && size == buffer->size) buffer->content_status = DOE_VK_OK;
    return DOE_VK_OK;
}

doe_vk_status doe_vk_buffer_read(doe_vk_buffer *buffer, uint64_t offset,
    void *bytes, size_t size) {
    if (size != 0 && bytes == NULL) return DOE_VK_INVALID_ARGUMENT;
    doe_vk_status status = host_access(buffer, offset, size);
    if (status != DOE_VK_OK) return status;
    if (buffer->content_status != DOE_VK_OK) return buffer->content_status;
    if (size != 0) memcpy(bytes, (const uint8_t *)buffer->mapped + (size_t)offset, size);
    return DOE_VK_OK;
}
