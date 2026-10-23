#include "internal.h"
#include <dlfcn.h>

static doe_vk_status load_functions(doe_vk_context *context) {
    void *symbol;
    symbol = dlsym(context->library, "vkCreateInstance");
    if (symbol == NULL) return DOE_VK_LOADER_UNAVAILABLE;
    _Static_assert(sizeof(context->vk.vkCreateInstance) == sizeof(symbol), "POSIX function pointer size");
    memcpy(&context->vk.vkCreateInstance, &symbol, sizeof(symbol));
    symbol = dlsym(context->library, "vkDestroyInstance");
    if (symbol == NULL) return DOE_VK_LOADER_UNAVAILABLE;
    _Static_assert(sizeof(context->vk.vkDestroyInstance) == sizeof(symbol), "POSIX function pointer size");
    memcpy(&context->vk.vkDestroyInstance, &symbol, sizeof(symbol));
    symbol = dlsym(context->library, "vkEnumeratePhysicalDevices");
    if (symbol == NULL) return DOE_VK_LOADER_UNAVAILABLE;
    _Static_assert(sizeof(context->vk.vkEnumeratePhysicalDevices) == sizeof(symbol), "POSIX function pointer size");
    memcpy(&context->vk.vkEnumeratePhysicalDevices, &symbol, sizeof(symbol));
    symbol = dlsym(context->library, "vkGetPhysicalDeviceQueueFamilyProperties");
    if (symbol == NULL) return DOE_VK_LOADER_UNAVAILABLE;
    _Static_assert(sizeof(context->vk.vkGetPhysicalDeviceQueueFamilyProperties) == sizeof(symbol), "POSIX function pointer size");
    memcpy(&context->vk.vkGetPhysicalDeviceQueueFamilyProperties, &symbol, sizeof(symbol));
    symbol = dlsym(context->library, "vkGetPhysicalDeviceMemoryProperties");
    if (symbol == NULL) return DOE_VK_LOADER_UNAVAILABLE;
    _Static_assert(sizeof(context->vk.vkGetPhysicalDeviceMemoryProperties) == sizeof(symbol), "POSIX function pointer size");
    memcpy(&context->vk.vkGetPhysicalDeviceMemoryProperties, &symbol, sizeof(symbol));
    symbol = dlsym(context->library, "vkGetPhysicalDeviceProperties2");
    if (symbol == NULL) return DOE_VK_LOADER_UNAVAILABLE;
    _Static_assert(sizeof(context->vk.vkGetPhysicalDeviceProperties2) == sizeof(symbol), "POSIX function pointer size");
    memcpy(&context->vk.vkGetPhysicalDeviceProperties2, &symbol, sizeof(symbol));
    symbol = dlsym(context->library, "vkGetPhysicalDeviceFeatures2");
    if (symbol == NULL) return DOE_VK_LOADER_UNAVAILABLE;
    _Static_assert(sizeof(context->vk.vkGetPhysicalDeviceFeatures2) == sizeof(symbol), "POSIX function pointer size");
    memcpy(&context->vk.vkGetPhysicalDeviceFeatures2, &symbol, sizeof(symbol));
    symbol = dlsym(context->library, "vkCreateDevice");
    if (symbol == NULL) return DOE_VK_LOADER_UNAVAILABLE;
    _Static_assert(sizeof(context->vk.vkCreateDevice) == sizeof(symbol), "POSIX function pointer size");
    memcpy(&context->vk.vkCreateDevice, &symbol, sizeof(symbol));
    symbol = dlsym(context->library, "vkDestroyDevice");
    if (symbol == NULL) return DOE_VK_LOADER_UNAVAILABLE;
    _Static_assert(sizeof(context->vk.vkDestroyDevice) == sizeof(symbol), "POSIX function pointer size");
    memcpy(&context->vk.vkDestroyDevice, &symbol, sizeof(symbol));
    symbol = dlsym(context->library, "vkGetDeviceQueue");
    if (symbol == NULL) return DOE_VK_LOADER_UNAVAILABLE;
    _Static_assert(sizeof(context->vk.vkGetDeviceQueue) == sizeof(symbol), "POSIX function pointer size");
    memcpy(&context->vk.vkGetDeviceQueue, &symbol, sizeof(symbol));
    symbol = dlsym(context->library, "vkCreateCommandPool");
    if (symbol == NULL) return DOE_VK_LOADER_UNAVAILABLE;
    _Static_assert(sizeof(context->vk.vkCreateCommandPool) == sizeof(symbol), "POSIX function pointer size");
    memcpy(&context->vk.vkCreateCommandPool, &symbol, sizeof(symbol));
    symbol = dlsym(context->library, "vkDestroyCommandPool");
    if (symbol == NULL) return DOE_VK_LOADER_UNAVAILABLE;
    _Static_assert(sizeof(context->vk.vkDestroyCommandPool) == sizeof(symbol), "POSIX function pointer size");
    memcpy(&context->vk.vkDestroyCommandPool, &symbol, sizeof(symbol));
    symbol = dlsym(context->library, "vkAllocateCommandBuffers");
    if (symbol == NULL) return DOE_VK_LOADER_UNAVAILABLE;
    _Static_assert(sizeof(context->vk.vkAllocateCommandBuffers) == sizeof(symbol), "POSIX function pointer size");
    memcpy(&context->vk.vkAllocateCommandBuffers, &symbol, sizeof(symbol));
    symbol = dlsym(context->library, "vkFreeCommandBuffers");
    if (symbol == NULL) return DOE_VK_LOADER_UNAVAILABLE;
    _Static_assert(sizeof(context->vk.vkFreeCommandBuffers) == sizeof(symbol), "POSIX function pointer size");
    memcpy(&context->vk.vkFreeCommandBuffers, &symbol, sizeof(symbol));
    symbol = dlsym(context->library, "vkCreateFence");
    if (symbol == NULL) return DOE_VK_LOADER_UNAVAILABLE;
    _Static_assert(sizeof(context->vk.vkCreateFence) == sizeof(symbol), "POSIX function pointer size");
    memcpy(&context->vk.vkCreateFence, &symbol, sizeof(symbol));
    symbol = dlsym(context->library, "vkDestroyFence");
    if (symbol == NULL) return DOE_VK_LOADER_UNAVAILABLE;
    _Static_assert(sizeof(context->vk.vkDestroyFence) == sizeof(symbol), "POSIX function pointer size");
    memcpy(&context->vk.vkDestroyFence, &symbol, sizeof(symbol));
    symbol = dlsym(context->library, "vkGetFenceStatus");
    if (symbol == NULL) return DOE_VK_LOADER_UNAVAILABLE;
    _Static_assert(sizeof(context->vk.vkGetFenceStatus) == sizeof(symbol), "POSIX function pointer size");
    memcpy(&context->vk.vkGetFenceStatus, &symbol, sizeof(symbol));
    symbol = dlsym(context->library, "vkWaitForFences");
    if (symbol == NULL) return DOE_VK_LOADER_UNAVAILABLE;
    _Static_assert(sizeof(context->vk.vkWaitForFences) == sizeof(symbol), "POSIX function pointer size");
    memcpy(&context->vk.vkWaitForFences, &symbol, sizeof(symbol));
    symbol = dlsym(context->library, "vkQueueSubmit");
    if (symbol == NULL) return DOE_VK_LOADER_UNAVAILABLE;
    _Static_assert(sizeof(context->vk.vkQueueSubmit) == sizeof(symbol), "POSIX function pointer size");
    memcpy(&context->vk.vkQueueSubmit, &symbol, sizeof(symbol));
    symbol = dlsym(context->library, "vkQueueWaitIdle");
    if (symbol == NULL) return DOE_VK_LOADER_UNAVAILABLE;
    _Static_assert(sizeof(context->vk.vkQueueWaitIdle) == sizeof(symbol), "POSIX function pointer size");
    memcpy(&context->vk.vkQueueWaitIdle, &symbol, sizeof(symbol));
    symbol = dlsym(context->library, "vkBeginCommandBuffer");
    if (symbol == NULL) return DOE_VK_LOADER_UNAVAILABLE;
    _Static_assert(sizeof(context->vk.vkBeginCommandBuffer) == sizeof(symbol), "POSIX function pointer size");
    memcpy(&context->vk.vkBeginCommandBuffer, &symbol, sizeof(symbol));
    symbol = dlsym(context->library, "vkEndCommandBuffer");
    if (symbol == NULL) return DOE_VK_LOADER_UNAVAILABLE;
    _Static_assert(sizeof(context->vk.vkEndCommandBuffer) == sizeof(symbol), "POSIX function pointer size");
    memcpy(&context->vk.vkEndCommandBuffer, &symbol, sizeof(symbol));
    symbol = dlsym(context->library, "vkCmdBindPipeline");
    if (symbol == NULL) return DOE_VK_LOADER_UNAVAILABLE;
    _Static_assert(sizeof(context->vk.vkCmdBindPipeline) == sizeof(symbol), "POSIX function pointer size");
    memcpy(&context->vk.vkCmdBindPipeline, &symbol, sizeof(symbol));
    symbol = dlsym(context->library, "vkCmdDispatch");
    if (symbol == NULL) return DOE_VK_LOADER_UNAVAILABLE;
    _Static_assert(sizeof(context->vk.vkCmdDispatch) == sizeof(symbol), "POSIX function pointer size");
    memcpy(&context->vk.vkCmdDispatch, &symbol, sizeof(symbol));
    symbol = dlsym(context->library, "vkCmdDispatchIndirect");
    if (symbol == NULL) return DOE_VK_LOADER_UNAVAILABLE;
    _Static_assert(sizeof(context->vk.vkCmdDispatchIndirect) == sizeof(symbol), "POSIX function pointer size");
    memcpy(&context->vk.vkCmdDispatchIndirect, &symbol, sizeof(symbol));
    symbol = dlsym(context->library, "vkCmdCopyBuffer");
    if (symbol == NULL) return DOE_VK_LOADER_UNAVAILABLE;
    _Static_assert(sizeof(context->vk.vkCmdCopyBuffer) == sizeof(symbol), "POSIX function pointer size");
    memcpy(&context->vk.vkCmdCopyBuffer, &symbol, sizeof(symbol));
    symbol = dlsym(context->library, "vkCmdFillBuffer");
    if (symbol == NULL) return DOE_VK_LOADER_UNAVAILABLE;
    _Static_assert(sizeof(context->vk.vkCmdFillBuffer) == sizeof(symbol), "POSIX function pointer size");
    memcpy(&context->vk.vkCmdFillBuffer, &symbol, sizeof(symbol));
    symbol = dlsym(context->library, "vkCmdPipelineBarrier");
    if (symbol == NULL) return DOE_VK_LOADER_UNAVAILABLE;
    _Static_assert(sizeof(context->vk.vkCmdPipelineBarrier) == sizeof(symbol), "POSIX function pointer size");
    memcpy(&context->vk.vkCmdPipelineBarrier, &symbol, sizeof(symbol));
    symbol = dlsym(context->library, "vkCreateBuffer");
    if (symbol == NULL) return DOE_VK_LOADER_UNAVAILABLE;
    _Static_assert(sizeof(context->vk.vkCreateBuffer) == sizeof(symbol), "POSIX function pointer size");
    memcpy(&context->vk.vkCreateBuffer, &symbol, sizeof(symbol));
    symbol = dlsym(context->library, "vkDestroyBuffer");
    if (symbol == NULL) return DOE_VK_LOADER_UNAVAILABLE;
    _Static_assert(sizeof(context->vk.vkDestroyBuffer) == sizeof(symbol), "POSIX function pointer size");
    memcpy(&context->vk.vkDestroyBuffer, &symbol, sizeof(symbol));
    symbol = dlsym(context->library, "vkGetBufferMemoryRequirements");
    if (symbol == NULL) return DOE_VK_LOADER_UNAVAILABLE;
    _Static_assert(sizeof(context->vk.vkGetBufferMemoryRequirements) == sizeof(symbol), "POSIX function pointer size");
    memcpy(&context->vk.vkGetBufferMemoryRequirements, &symbol, sizeof(symbol));
    symbol = dlsym(context->library, "vkAllocateMemory");
    if (symbol == NULL) return DOE_VK_LOADER_UNAVAILABLE;
    _Static_assert(sizeof(context->vk.vkAllocateMemory) == sizeof(symbol), "POSIX function pointer size");
    memcpy(&context->vk.vkAllocateMemory, &symbol, sizeof(symbol));
    symbol = dlsym(context->library, "vkFreeMemory");
    if (symbol == NULL) return DOE_VK_LOADER_UNAVAILABLE;
    _Static_assert(sizeof(context->vk.vkFreeMemory) == sizeof(symbol), "POSIX function pointer size");
    memcpy(&context->vk.vkFreeMemory, &symbol, sizeof(symbol));
    symbol = dlsym(context->library, "vkBindBufferMemory");
    if (symbol == NULL) return DOE_VK_LOADER_UNAVAILABLE;
    _Static_assert(sizeof(context->vk.vkBindBufferMemory) == sizeof(symbol), "POSIX function pointer size");
    memcpy(&context->vk.vkBindBufferMemory, &symbol, sizeof(symbol));
    symbol = dlsym(context->library, "vkMapMemory");
    if (symbol == NULL) return DOE_VK_LOADER_UNAVAILABLE;
    _Static_assert(sizeof(context->vk.vkMapMemory) == sizeof(symbol), "POSIX function pointer size");
    memcpy(&context->vk.vkMapMemory, &symbol, sizeof(symbol));
    symbol = dlsym(context->library, "vkUnmapMemory");
    if (symbol == NULL) return DOE_VK_LOADER_UNAVAILABLE;
    _Static_assert(sizeof(context->vk.vkUnmapMemory) == sizeof(symbol), "POSIX function pointer size");
    memcpy(&context->vk.vkUnmapMemory, &symbol, sizeof(symbol));
    symbol = dlsym(context->library, "vkCreateShaderModule");
    if (symbol == NULL) return DOE_VK_LOADER_UNAVAILABLE;
    _Static_assert(sizeof(context->vk.vkCreateShaderModule) == sizeof(symbol), "POSIX function pointer size");
    memcpy(&context->vk.vkCreateShaderModule, &symbol, sizeof(symbol));
    symbol = dlsym(context->library, "vkDestroyShaderModule");
    if (symbol == NULL) return DOE_VK_LOADER_UNAVAILABLE;
    _Static_assert(sizeof(context->vk.vkDestroyShaderModule) == sizeof(symbol), "POSIX function pointer size");
    memcpy(&context->vk.vkDestroyShaderModule, &symbol, sizeof(symbol));
    symbol = dlsym(context->library, "vkCreateDescriptorSetLayout");
    if (symbol == NULL) return DOE_VK_LOADER_UNAVAILABLE;
    _Static_assert(sizeof(context->vk.vkCreateDescriptorSetLayout) == sizeof(symbol), "POSIX function pointer size");
    memcpy(&context->vk.vkCreateDescriptorSetLayout, &symbol, sizeof(symbol));
    symbol = dlsym(context->library, "vkDestroyDescriptorSetLayout");
    if (symbol == NULL) return DOE_VK_LOADER_UNAVAILABLE;
    _Static_assert(sizeof(context->vk.vkDestroyDescriptorSetLayout) == sizeof(symbol), "POSIX function pointer size");
    memcpy(&context->vk.vkDestroyDescriptorSetLayout, &symbol, sizeof(symbol));
    symbol = dlsym(context->library, "vkCreateDescriptorPool");
    if (symbol == NULL) return DOE_VK_LOADER_UNAVAILABLE;
    _Static_assert(sizeof(context->vk.vkCreateDescriptorPool) == sizeof(symbol), "POSIX function pointer size");
    memcpy(&context->vk.vkCreateDescriptorPool, &symbol, sizeof(symbol));
    symbol = dlsym(context->library, "vkDestroyDescriptorPool");
    if (symbol == NULL) return DOE_VK_LOADER_UNAVAILABLE;
    _Static_assert(sizeof(context->vk.vkDestroyDescriptorPool) == sizeof(symbol), "POSIX function pointer size");
    memcpy(&context->vk.vkDestroyDescriptorPool, &symbol, sizeof(symbol));
    symbol = dlsym(context->library, "vkAllocateDescriptorSets");
    if (symbol == NULL) return DOE_VK_LOADER_UNAVAILABLE;
    _Static_assert(sizeof(context->vk.vkAllocateDescriptorSets) == sizeof(symbol), "POSIX function pointer size");
    memcpy(&context->vk.vkAllocateDescriptorSets, &symbol, sizeof(symbol));
    symbol = dlsym(context->library, "vkUpdateDescriptorSets");
    if (symbol == NULL) return DOE_VK_LOADER_UNAVAILABLE;
    _Static_assert(sizeof(context->vk.vkUpdateDescriptorSets) == sizeof(symbol), "POSIX function pointer size");
    memcpy(&context->vk.vkUpdateDescriptorSets, &symbol, sizeof(symbol));
    symbol = dlsym(context->library, "vkCreatePipelineLayout");
    if (symbol == NULL) return DOE_VK_LOADER_UNAVAILABLE;
    _Static_assert(sizeof(context->vk.vkCreatePipelineLayout) == sizeof(symbol), "POSIX function pointer size");
    memcpy(&context->vk.vkCreatePipelineLayout, &symbol, sizeof(symbol));
    symbol = dlsym(context->library, "vkDestroyPipelineLayout");
    if (symbol == NULL) return DOE_VK_LOADER_UNAVAILABLE;
    _Static_assert(sizeof(context->vk.vkDestroyPipelineLayout) == sizeof(symbol), "POSIX function pointer size");
    memcpy(&context->vk.vkDestroyPipelineLayout, &symbol, sizeof(symbol));
    symbol = dlsym(context->library, "vkCreateComputePipelines");
    if (symbol == NULL) return DOE_VK_LOADER_UNAVAILABLE;
    _Static_assert(sizeof(context->vk.vkCreateComputePipelines) == sizeof(symbol), "POSIX function pointer size");
    memcpy(&context->vk.vkCreateComputePipelines, &symbol, sizeof(symbol));
    symbol = dlsym(context->library, "vkDestroyPipeline");
    if (symbol == NULL) return DOE_VK_LOADER_UNAVAILABLE;
    _Static_assert(sizeof(context->vk.vkDestroyPipeline) == sizeof(symbol), "POSIX function pointer size");
    memcpy(&context->vk.vkDestroyPipeline, &symbol, sizeof(symbol));
    symbol = dlsym(context->library, "vkCmdBindDescriptorSets");
    if (symbol == NULL) return DOE_VK_LOADER_UNAVAILABLE;
    _Static_assert(sizeof(context->vk.vkCmdBindDescriptorSets) == sizeof(symbol), "POSIX function pointer size");
    memcpy(&context->vk.vkCmdBindDescriptorSets, &symbol, sizeof(symbol));
    symbol = dlsym(context->library, "vkGetPhysicalDeviceFormatProperties");
    if (symbol == NULL) return DOE_VK_LOADER_UNAVAILABLE;
    _Static_assert(sizeof(context->vk.vkGetPhysicalDeviceFormatProperties) == sizeof(symbol), "POSIX function pointer size");
    memcpy(&context->vk.vkGetPhysicalDeviceFormatProperties, &symbol, sizeof(symbol));
    symbol = dlsym(context->library, "vkCreateImage");
    if (symbol == NULL) return DOE_VK_LOADER_UNAVAILABLE;
    _Static_assert(sizeof(context->vk.vkCreateImage) == sizeof(symbol), "POSIX function pointer size");
    memcpy(&context->vk.vkCreateImage, &symbol, sizeof(symbol));
    symbol = dlsym(context->library, "vkDestroyImage");
    if (symbol == NULL) return DOE_VK_LOADER_UNAVAILABLE;
    _Static_assert(sizeof(context->vk.vkDestroyImage) == sizeof(symbol), "POSIX function pointer size");
    memcpy(&context->vk.vkDestroyImage, &symbol, sizeof(symbol));
    symbol = dlsym(context->library, "vkGetImageMemoryRequirements");
    if (symbol == NULL) return DOE_VK_LOADER_UNAVAILABLE;
    _Static_assert(sizeof(context->vk.vkGetImageMemoryRequirements) == sizeof(symbol), "POSIX function pointer size");
    memcpy(&context->vk.vkGetImageMemoryRequirements, &symbol, sizeof(symbol));
    symbol = dlsym(context->library, "vkBindImageMemory");
    if (symbol == NULL) return DOE_VK_LOADER_UNAVAILABLE;
    _Static_assert(sizeof(context->vk.vkBindImageMemory) == sizeof(symbol), "POSIX function pointer size");
    memcpy(&context->vk.vkBindImageMemory, &symbol, sizeof(symbol));
    symbol = dlsym(context->library, "vkCreateImageView");
    if (symbol == NULL) return DOE_VK_LOADER_UNAVAILABLE;
    _Static_assert(sizeof(context->vk.vkCreateImageView) == sizeof(symbol), "POSIX function pointer size");
    memcpy(&context->vk.vkCreateImageView, &symbol, sizeof(symbol));
    symbol = dlsym(context->library, "vkDestroyImageView");
    if (symbol == NULL) return DOE_VK_LOADER_UNAVAILABLE;
    _Static_assert(sizeof(context->vk.vkDestroyImageView) == sizeof(symbol), "POSIX function pointer size");
    memcpy(&context->vk.vkDestroyImageView, &symbol, sizeof(symbol));
    symbol = dlsym(context->library, "vkCreateSampler");
    if (symbol == NULL) return DOE_VK_LOADER_UNAVAILABLE;
    _Static_assert(sizeof(context->vk.vkCreateSampler) == sizeof(symbol), "POSIX function pointer size");
    memcpy(&context->vk.vkCreateSampler, &symbol, sizeof(symbol));
    symbol = dlsym(context->library, "vkDestroySampler");
    if (symbol == NULL) return DOE_VK_LOADER_UNAVAILABLE;
    _Static_assert(sizeof(context->vk.vkDestroySampler) == sizeof(symbol), "POSIX function pointer size");
    memcpy(&context->vk.vkDestroySampler, &symbol, sizeof(symbol));
    symbol = dlsym(context->library, "vkCmdCopyBufferToImage");
    if (symbol == NULL) return DOE_VK_LOADER_UNAVAILABLE;
    _Static_assert(sizeof(context->vk.vkCmdCopyBufferToImage) == sizeof(symbol), "POSIX function pointer size");
    memcpy(&context->vk.vkCmdCopyBufferToImage, &symbol, sizeof(symbol));
    symbol = dlsym(context->library, "vkCmdCopyImageToBuffer");
    if (symbol == NULL) return DOE_VK_LOADER_UNAVAILABLE;
    _Static_assert(sizeof(context->vk.vkCmdCopyImageToBuffer) == sizeof(symbol), "POSIX function pointer size");
    memcpy(&context->vk.vkCmdCopyImageToBuffer, &symbol, sizeof(symbol));
    symbol = dlsym(context->library, "vkCmdCopyImage");
    if (symbol == NULL) return DOE_VK_LOADER_UNAVAILABLE;
    _Static_assert(sizeof(context->vk.vkCmdCopyImage) == sizeof(symbol), "POSIX function pointer size");
    memcpy(&context->vk.vkCmdCopyImage, &symbol, sizeof(symbol));
    return DOE_VK_OK;
}

static doe_vk_status choose_device(doe_vk_context *context,
                                   const doe_vk_options *options,
                                   doe_vk_error *error) {
    uint32_t count = 0;
    VkPhysicalDevice *devices = NULL;
    VkResult result = context->vk.vkEnumeratePhysicalDevices(context->instance, &count, NULL);
    if (result != 0) return doe_vk_result(context, result, "vkEnumeratePhysicalDevices", error);
    if (count == 0 || options->adapter_ordinal >= count)
        return doe_vk_fail(DOE_VK_UNSUPPORTED, "adapter ordinal unavailable", error);
    if ((uint64_t)count * sizeof(*devices) > SIZE_MAX)
        return doe_vk_fail(DOE_VK_OUT_OF_MEMORY, "physical device list", error);
    devices = calloc(count, sizeof(*devices));
    if (devices == NULL) return doe_vk_fail(DOE_VK_OUT_OF_MEMORY, "physical device list", error);
    uint32_t capacity = count;
    result = context->vk.vkEnumeratePhysicalDevices(context->instance, &count, devices);
    if (result == 0 && count <= capacity && options->adapter_ordinal < count)
        context->physical = devices[options->adapter_ordinal];
    free(devices);
    if (result != 0) return doe_vk_result(context, result, "vkEnumeratePhysicalDevices", error);
    if (context->physical == NULL)
        return doe_vk_fail(DOE_VK_UNSUPPORTED, "adapter enumeration changed", error);

    VkPhysicalDeviceProperties2 properties = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
    context->vk.vkGetPhysicalDeviceProperties2(context->physical, &properties);
    context->properties = properties.properties;
    if (properties.properties.apiVersion < VK_API_VERSION_1_2)
        return doe_vk_fail(DOE_VK_UNSUPPORTED, "Vulkan 1.2 device required", error);
    if (properties.properties.deviceType == 4 && !options->allow_cpu_adapter)
        return doe_vk_fail(DOE_VK_UNSUPPORTED, "software adapter not allowed", error);
    doe_vk_caps *caps = &context->caps;
    const VkPhysicalDeviceProperties *p = &context->properties;
    caps->api_version = p->apiVersion;
    caps->driver_version = p->driverVersion;
    caps->vendor_id = p->vendorID;
    caps->device_id = p->deviceID;
    caps->device_type = p->deviceType;
    caps->adapter_ordinal = options->adapter_ordinal;
    caps->software_adapter = p->deviceType == 4;
    memcpy(caps->device_name, p->deviceName, sizeof(caps->device_name));
    caps->device_name[sizeof(caps->device_name) - 1] = '\0';
    memcpy(caps->max_workgroup_count, p->limits.maxComputeWorkGroupCount, sizeof(caps->max_workgroup_count));
    memcpy(caps->max_workgroup_size, p->limits.maxComputeWorkGroupSize, sizeof(caps->max_workgroup_size));
    caps->max_workgroup_invocations = p->limits.maxComputeWorkGroupInvocations;
    caps->max_storage_buffer_range = p->limits.maxStorageBufferRange;
    caps->max_uniform_buffer_range = p->limits.maxUniformBufferRange;
    caps->storage_offset_alignment = p->limits.minStorageBufferOffsetAlignment;
    caps->uniform_offset_alignment = p->limits.minUniformBufferOffsetAlignment;
    context->vk.vkGetPhysicalDeviceMemoryProperties(context->physical, &context->memory);
    if (context->memory.memoryTypeCount > 32 || context->memory.memoryHeapCount > 16)
        return doe_vk_fail(DOE_VK_NATIVE_FAILURE, "invalid device memory properties", error);
    return DOE_VK_OK;
}

static doe_vk_status choose_queue(doe_vk_context *context, doe_vk_error *error) {
    uint32_t count = 0;
    context->vk.vkGetPhysicalDeviceQueueFamilyProperties(context->physical, &count, NULL);
    if (count == 0) return doe_vk_fail(DOE_VK_UNSUPPORTED, "no queue families", error);
    if ((uint64_t)count * sizeof(VkQueueFamilyProperties) > SIZE_MAX)
        return doe_vk_fail(DOE_VK_OUT_OF_MEMORY, "queue families", error);
    VkQueueFamilyProperties *queues = calloc(count, sizeof(*queues));
    if (queues == NULL) return doe_vk_fail(DOE_VK_OUT_OF_MEMORY, "queue families", error);
    uint32_t capacity = count;
    context->vk.vkGetPhysicalDeviceQueueFamilyProperties(context->physical, &count, queues);
    uint32_t selected = UINT32_MAX;
    if (count <= capacity) {
        for (uint32_t i = 0; i < count; ++i) {
            if (queues[i].queueCount == 0 || !(queues[i].queueFlags & VK_QUEUE_COMPUTE_BIT)) continue;
            if (selected == UINT32_MAX) selected = i;
            if (!(queues[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
                selected = i;
                break;
            }
        }
    }
    if (selected != UINT32_MAX) {
        context->caps.queue_family = selected;
        context->caps.queue_flags = queues[selected].queueFlags;
        context->caps.timestamp_valid_bits = queues[selected].timestampValidBits;
    }
    free(queues);
    return selected == UINT32_MAX ? doe_vk_fail(DOE_VK_UNSUPPORTED, "compute queue unavailable", error) : DOE_VK_OK;
}

doe_vk_status doe_vk_context_create(const doe_vk_options *options,
                                    doe_vk_context **out, doe_vk_error *error) {
    if (out == NULL) return doe_vk_fail(DOE_VK_INVALID_ARGUMENT, "context output", error);
    const doe_vk_options defaults = {0};
    if (options == NULL) options = &defaults;
    doe_vk_context *context = calloc(1, sizeof(*context));
    if (context == NULL) return doe_vk_fail(DOE_VK_OUT_OF_MEMORY, "context", error);
    context->references = 1;
    context->library = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
    doe_vk_status status = DOE_VK_LOADER_UNAVAILABLE;
    if (context->library == NULL) goto failed;
    status = load_functions(context);
    if (status != DOE_VK_OK) goto failed;
    const VkApplicationInfo application = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = (const uint8_t *)"Doe experimental C",
        .pEngineName = (const uint8_t *)"Doe",
        .apiVersion = VK_API_VERSION_1_2
    };
    const VkInstanceCreateInfo instance = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &application
    };
    status = doe_vk_result(context, context->vk.vkCreateInstance(&instance, NULL, &context->instance), "vkCreateInstance", error);
    if (status != DOE_VK_OK) goto failed;
    status = choose_device(context, options, error);
    if (status != DOE_VK_OK) goto failed;
    status = choose_queue(context, error);
    if (status != DOE_VK_OK) goto failed;
    VkPhysicalDeviceFeatures2 features = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
    context->vk.vkGetPhysicalDeviceFeatures2(context->physical, &features);
    if (!features.features.robustBufferAccess) {
        status = doe_vk_fail(DOE_VK_UNSUPPORTED, "robustBufferAccess required", error);
        goto failed;
    }
    VkPhysicalDeviceFeatures enabled = { .robustBufferAccess = 1 };
    float priority = 1.0f;
    const VkDeviceQueueCreateInfo queue = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = context->caps.queue_family,
        .queueCount = 1,
        .pQueuePriorities = &priority
    };
    const VkDeviceCreateInfo device = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queue,
        .pEnabledFeatures = &enabled
    };
    status = doe_vk_result(context, context->vk.vkCreateDevice(context->physical, &device, NULL, &context->device), "vkCreateDevice", error);
    if (status != DOE_VK_OK) goto failed;
    context->vk.vkGetDeviceQueue(context->device, context->caps.queue_family, 0, &context->queue);
    if (context->queue == NULL) {
        status = doe_vk_fail(DOE_VK_NATIVE_FAILURE, "vkGetDeviceQueue", error);
        goto failed;
    }
    context->caps.robust_buffer_access = true;
    *out = context;
    return doe_vk_fail(DOE_VK_OK, "context ready", error);
failed:
    if (status == DOE_VK_LOADER_UNAVAILABLE) doe_vk_fail(status, "Vulkan loader core symbols", error);
    doe_vk_context_release(context);
    return status;
}

doe_vk_status doe_vk_context_retain(doe_vk_context *context) {
    if (context == NULL) return DOE_VK_INVALID_ARGUMENT;
    if (context->references == UINT32_MAX) return DOE_VK_OUT_OF_MEMORY;
    ++context->references;
    return DOE_VK_OK;
}

void doe_vk_context_release(doe_vk_context *context) {
    if (context == NULL) return;
    if (--context->references != 0) return;
    /* Every recording/submission/resource owns a reference, so no native work
     * can exist here. Unknown submissions cannot be released. */
    if (context->device != NULL) context->vk.vkDestroyDevice(context->device, NULL);
    if (context->instance != NULL) context->vk.vkDestroyInstance(context->instance, NULL);
    if (context->library != NULL) dlclose(context->library);
    free(context);
}

bool doe_vk_context_alive(const doe_vk_context *context) {
    return context != NULL && context->device != NULL && !context->lost;
}

doe_vk_status doe_vk_context_caps(const doe_vk_context *context, doe_vk_caps *out) {
    if (context == NULL || out == NULL) return DOE_VK_INVALID_ARGUMENT;
    *out = context->caps;
    return DOE_VK_OK;
}
