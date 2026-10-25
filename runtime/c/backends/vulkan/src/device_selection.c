/* SPDX-License-Identifier: MIT */
#include "internal.h"

doe_vk_status doe_vk_validate_queue_policy(const doe_vk_options *options, doe_vk_error *error) {
    if (!options) return doe_vk_fail(DOE_VK_INVALID_ARGUMENT, "queue policy arguments", error);
    switch (options->queue_selection) {
        case DOE_VK_QUEUE_LEGACY:
        case DOE_VK_QUEUE_PREFER_GRAPHICS_COMPUTE: return DOE_VK_OK;
        case DOE_VK_QUEUE_REQUIRE_COMPUTE_ONLY:
            if (!options->require_graphics_queue && !options->surface_platforms) return DOE_VK_OK;
            return doe_vk_fail(DOE_VK_INVALID_ARGUMENT, "compute-only queue conflicts with graphics or surface", error);
    }
    return doe_vk_fail(DOE_VK_INVALID_ARGUMENT, "unknown queue selection", error);
}

doe_vk_status doe_vk_select_queue(const VkQueueFamilyProperties *queues, uint32_t count,
    const doe_vk_options *options, uint32_t *out, doe_vk_error *error) {
    doe_vk_status status = doe_vk_validate_queue_policy(options, error);
    if (status != DOE_VK_OK) return status;
    if (!out || (count && !queues)) return doe_vk_fail(DOE_VK_INVALID_ARGUMENT, "queue family arguments", error);
    uint32_t selected = UINT32_MAX;
    for (uint32_t i = 0; i < count; ++i) {
        if (!queues[i].queueCount || !(queues[i].queueFlags & VK_QUEUE_COMPUTE_BIT)) continue;
        bool graphics = (queues[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0;
        if ((options->require_graphics_queue || options->surface_platforms) && !graphics) continue;
        if (options->queue_selection == DOE_VK_QUEUE_REQUIRE_COMPUTE_ONLY && graphics) continue;
        if (selected == UINT32_MAX) selected = i;
        bool preferred = options->queue_selection == DOE_VK_QUEUE_PREFER_GRAPHICS_COMPUTE ? graphics : !graphics;
        if (preferred) { selected = i; break; }
    }
    if (selected == UINT32_MAX) return doe_vk_fail(DOE_VK_UNSUPPORTED,
        options->queue_selection == DOE_VK_QUEUE_REQUIRE_COMPUTE_ONLY ?
        "required compute-only queue unavailable" : "compute queue unavailable", error);
    *out = selected;
    return DOE_VK_OK;
}

static bool valid_selection(const doe_vk_options *options, doe_vk_error *error) {
    switch (options->adapter_selection) {
        case DOE_VK_SELECT_ORDINAL:
        case DOE_VK_SELECT_HARDWARE:
        case DOE_VK_SELECT_IDENTITY: return true;
        case DOE_VK_SELECT_SOFTWARE:
        case DOE_VK_SELECT_HARDWARE_ELSE_SOFTWARE:
            if (options->allow_cpu_adapter) return true;
            (void)doe_vk_fail(DOE_VK_INVALID_ARGUMENT, "software selection requires CPU permission", error);
            return false;
    }
    (void)doe_vk_fail(DOE_VK_INVALID_ARGUMENT, "unknown adapter selection", error);
    return false;
}

static bool matches(const doe_vk_options *options, doe_vk_adapter_selection mode, uint32_t ordinal,
                    const VkPhysicalDeviceProperties *properties,
                    const doe_vk_adapter_identity *identity) {
    switch (mode) {
        case DOE_VK_SELECT_HARDWARE_ELSE_SOFTWARE: return false; /* Resolved to one class per pass. */
        case DOE_VK_SELECT_ORDINAL: return ordinal == options->adapter_ordinal;
        case DOE_VK_SELECT_SOFTWARE: return properties->deviceType == 4u;
        case DOE_VK_SELECT_HARDWARE: return properties->deviceType != 4u;
        case DOE_VK_SELECT_IDENTITY:
            return memcmp(identity, &options->required_identity, sizeof(*identity)) == 0;
    }
    return false;
}

doe_vk_status doe_vk_choose_device(doe_vk_context *context,
                                   const doe_vk_options *options,
                                   doe_vk_error *error) {
    if (context == NULL || options == NULL)
        return doe_vk_fail(DOE_VK_INVALID_ARGUMENT, "adapter selection arguments", error);
    if (!valid_selection(options, error)) return DOE_VK_INVALID_ARGUMENT;
    uint32_t count = 0;
    VkResult result = context->vk.vkEnumeratePhysicalDevices(context->instance, &count, NULL);
    if (result != 0) return doe_vk_result(context, result, "vkEnumeratePhysicalDevices", error);
    if (count == 0 || (options->adapter_selection == DOE_VK_SELECT_ORDINAL && options->adapter_ordinal >= count))
        return doe_vk_fail(DOE_VK_UNSUPPORTED, "adapter unavailable", error);
    if ((uint64_t)count * sizeof(VkPhysicalDevice) > SIZE_MAX)
        return doe_vk_fail(DOE_VK_OUT_OF_MEMORY, "physical device list", error);
    VkPhysicalDevice *devices = calloc(count, sizeof(*devices));
    if (devices == NULL) return doe_vk_fail(DOE_VK_OUT_OF_MEMORY, "physical device list", error);
    const uint32_t capacity = count;
    result = context->vk.vkEnumeratePhysicalDevices(context->instance, &count, devices);
    if (result != 0 || count > capacity) {
        free(devices);
        return result != 0 ? doe_vk_result(context, result, "vkEnumeratePhysicalDevices", error) :
            doe_vk_fail(DOE_VK_UNSUPPORTED, "adapter enumeration changed", error);
    }
    VkPhysicalDevice selected = NULL;
    VkPhysicalDeviceProperties properties = {0};
    doe_vk_adapter_identity identity = {0};
    uint32_t selected_ordinal = 0;
    /* HARDWARE_ELSE_SOFTWARE is the HARDWARE class, then the SOFTWARE class
     * when no non-CPU device exists: two explicit class passes over one
     * enumeration, never an ordinal. */
    const bool both = options->adapter_selection == DOE_VK_SELECT_HARDWARE_ELSE_SOFTWARE;
    for (uint32_t pass = 0; pass < (both ? 2u : 1u) && selected == NULL; ++pass) {
        const doe_vk_adapter_selection mode = !both ? options->adapter_selection :
            pass == 0 ? DOE_VK_SELECT_HARDWARE : DOE_VK_SELECT_SOFTWARE;
        for (uint32_t i = 0; i < count; ++i) {
            if (options->adapter_selection == DOE_VK_SELECT_ORDINAL && i != options->adapter_ordinal) continue;
            if (devices[i] == NULL) {
                free(devices);
                return doe_vk_fail(DOE_VK_NATIVE_FAILURE, "invalid physical device handle", error);
            }
            VkPhysicalDeviceIDProperties ids = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES};
            VkPhysicalDeviceProperties2 candidate = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
                                                    .pNext = &ids};
            context->vk.vkGetPhysicalDeviceProperties2(devices[i], &candidate);
            doe_vk_adapter_identity observed;
            memcpy(observed.device_uuid, ids.deviceUUID, sizeof(observed.device_uuid));
            memcpy(observed.driver_uuid, ids.driverUUID, sizeof(observed.driver_uuid));
            if (!matches(options, mode, i, &candidate.properties, &observed)) continue;
            if (selected != NULL) {
                free(devices);
                return doe_vk_fail(DOE_VK_UNSUPPORTED, "ambiguous adapter identity", error);
            }
            selected = devices[i];
            properties = candidate.properties;
            identity = observed;
            selected_ordinal = i;
            if (options->adapter_selection != DOE_VK_SELECT_IDENTITY) break;
        }
    }
    free(devices);
    if (selected == NULL)
        return doe_vk_fail(DOE_VK_UNSUPPORTED, "selected adapter unavailable", error);
    if (properties.apiVersion < VK_API_VERSION_1_2)
        return doe_vk_fail(DOE_VK_UNSUPPORTED, "Vulkan 1.2 device required", error);
    if (properties.deviceType == 4 && !options->allow_cpu_adapter)
        return doe_vk_fail(DOE_VK_UNSUPPORTED, "software adapter not allowed", error);
    context->physical = selected;
    context->properties = properties;
    VkPhysicalDeviceMaintenance3Properties maintenance = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_3_PROPERTIES};
    VkPhysicalDeviceProperties2 extended = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, .pNext = &maintenance};
    context->vk.vkGetPhysicalDeviceProperties2(selected, &extended);
    context->caps.max_memory_allocation_size = maintenance.maxMemoryAllocationSize;
    doe_vk_caps *caps = &context->caps;
    const VkPhysicalDeviceProperties *p = &context->properties;
    caps->api_version = p->apiVersion;
    caps->driver_version = p->driverVersion;
    caps->vendor_id = p->vendorID;
    caps->device_id = p->deviceID;
    caps->device_type = p->deviceType;
    caps->adapter_ordinal = selected_ordinal;
    caps->identity = identity;
    caps->software_adapter = p->deviceType == 4;
    caps->timestamp_period_ns = p->limits.timestampPeriod;
    caps->max_workgroup_storage_size = p->limits.maxComputeSharedMemorySize;
    caps->max_bind_groups = p->limits.maxBoundDescriptorSets < DOE_VK_MAX_GROUPS ?
        p->limits.maxBoundDescriptorSets : DOE_VK_MAX_GROUPS;
    caps->max_bindings_per_group = DOE_VK_MAX_BINDINGS;
    caps->max_storage_buffers_per_stage = p->limits.maxPerStageDescriptorStorageBuffers;
    caps->max_uniform_buffers_per_stage = p->limits.maxPerStageDescriptorUniformBuffers;
    caps->max_sampled_textures_per_stage = p->limits.maxPerStageDescriptorSampledImages;
    caps->max_storage_textures_per_stage = p->limits.maxPerStageDescriptorStorageImages;
    caps->max_samplers_per_stage = p->limits.maxPerStageDescriptorSamplers;
    caps->max_texture_dimension_1d = p->limits.maxImageDimension1D;
    caps->max_texture_dimension_2d = p->limits.maxImageDimension2D;
    caps->max_texture_dimension_3d = p->limits.maxImageDimension3D;
    caps->max_texture_array_layers = p->limits.maxImageArrayLayers;
    caps->max_vertex_input_bindings = p->limits.maxVertexInputBindings;
    caps->max_vertex_input_attributes = p->limits.maxVertexInputAttributes;
    caps->max_vertex_input_binding_stride = p->limits.maxVertexInputBindingStride;
    caps->max_vertex_input_attribute_offset = p->limits.maxVertexInputAttributeOffset;
    caps->max_vertex_output_components = p->limits.maxVertexOutputComponents;
    caps->max_fragment_input_components = p->limits.maxFragmentInputComponents;
    caps->max_color_attachments = p->limits.maxColorAttachments;
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

