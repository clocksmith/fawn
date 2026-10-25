/* SPDX-License-Identifier: MIT */
#include "internal.h"

doe_vk_status doe_vk_timeline_prepare(doe_vk_context *context, const doe_vk_options *options,
    VkPhysicalDeviceTimelineSemaphoreFeatures *feature, doe_vk_error *error) {
    *feature = (VkPhysicalDeviceTimelineSemaphoreFeatures){.sType=DOE_VK_TIMELINE_FEATURES};
    if (options->sync_selection != DOE_VK_SYNC_PREFER_TIMELINE) return DOE_VK_OK;
    VkPhysicalDeviceFeatures2 query = {.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,.pNext=feature};
    context->vk.vkGetPhysicalDeviceFeatures2(context->physical, &query);
    if (!feature->timelineSemaphore) return DOE_VK_OK; /* A preference, not a requirement. */
    VkPhysicalDeviceTimelineSemaphoreProperties limits = {.sType=DOE_VK_TIMELINE_PROPERTIES};
    VkPhysicalDeviceProperties2 properties = {.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,.pNext=&limits};
    context->vk.vkGetPhysicalDeviceProperties2(context->physical, &properties);
    if (!limits.maxTimelineSemaphoreValueDifference)
        return doe_vk_fail(DOE_VK_UNSUPPORTED, "invalid timeline semaphore limit", error);
    context->timeline_max_difference = limits.maxTimelineSemaphoreValueDifference;
    if (options->adapter_only) return DOE_VK_OK;
    struct symbol { const char *name; void *output; size_t size; } symbols[] = {
        {"vkCreateSemaphore", &context->vk.vkCreateSemaphore, sizeof(context->vk.vkCreateSemaphore)},
        {"vkDestroySemaphore", &context->vk.vkDestroySemaphore, sizeof(context->vk.vkDestroySemaphore)},
        {"vkWaitSemaphores", &context->vk.vkWaitSemaphores, sizeof(context->vk.vkWaitSemaphores)},
        {"vkGetSemaphoreCounterValue", &context->vk.vkGetSemaphoreCounterValue, sizeof(context->vk.vkGetSemaphoreCounterValue)}
    };
    for (size_t i = 0; i < sizeof(symbols)/sizeof(symbols[0]); ++i) {
        doe_vk_status status = doe_vk_load_symbol(context->library, symbols[i].name, symbols[i].output, symbols[i].size, error);
        if (status != DOE_VK_OK) return status;
    }
    return DOE_VK_OK;
}

doe_vk_status doe_vk_timeline_create(doe_vk_context *context, doe_vk_error *error) {
    const VkSemaphoreTypeCreateInfo type = {.sType=DOE_VK_SEMAPHORE_TYPE,.semaphoreType=DOE_VK_SEMAPHORE_TIMELINE};
    const VkSemaphoreCreateInfo create = {.sType=VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,.pNext=&type};
    doe_vk_status status = doe_vk_result(context, context->vk.vkCreateSemaphore(context->device, &create, NULL,
        &context->timeline), "vkCreateSemaphore(timeline)", error);
    if (status == DOE_VK_OK) context->caps.timeline_enabled = true;
    return status;
}

/* The existing command owner retains completion/failure and resource leases.
 * This is only the native wait primitive; no second pending queue or state. */
VkResult doe_vk_command_wait_raw(const doe_vk_commands *commands, uint64_t timeout_ns, bool poll) {
    const doe_vk_context *context = commands->context;
    if (!context->timeline) return poll ? context->vk.vkGetFenceStatus(context->device, commands->fence) :
        context->vk.vkWaitForFences(context->device, 1, &commands->fence, 1, timeout_ns);
    if (poll) {
        uint64_t value = 0;
        VkResult result = context->vk.vkGetSemaphoreCounterValue(context->device, context->timeline, &value);
        return result != 0 ? result : value >= commands->timeline_value ? 0 : 1; /* VK_NOT_READY */
    }
    const VkSemaphoreWaitInfo wait = {.sType=DOE_VK_SEMAPHORE_WAIT,.semaphoreCount=1,
        .pSemaphores=&context->timeline,.pValues=&commands->timeline_value};
    return context->vk.vkWaitSemaphores(context->device, &wait, timeout_ns);
}
