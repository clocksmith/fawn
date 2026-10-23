/* SPDX-License-Identifier: MIT */
#ifndef DOE_VK_ADAPTER_IDENTITY_FIXTURE_H
#define DOE_VK_ADAPTER_IDENTITY_FIXTURE_H
#include "../src/internal.h"
#include <assert.h>
#include <stdio.h>

/* Inject only enumeration/property procedures, never device/shader execution. */
static unsigned handles[2][3], instances[2];
static uint32_t generation, order[3] = {0, 1, 2}, available = 3, enumerations;
static VkResult count_result, list_result;
static bool oversized, duplicate;
static uint32_t device_version = VK_API_VERSION_1_2;

static VkPhysicalDevice handle(uint32_t index) {
    return (VkPhysicalDevice)&handles[generation][index];
}
static VkResult enumerate(VkInstance instance, uint32_t *count, VkPhysicalDevice *devices) {
    assert(instance == (VkInstance)&instances[generation]);
    ++enumerations;
    if (devices == NULL) {
        *count = available;
        return count_result;
    }
    assert(*count == available);
    for (uint32_t i = 0; i < available; ++i) devices[i] = handle(order[i]);
    *count = available + (oversized ? 1u : 0u);
    return list_result;
}
static void properties(VkPhysicalDevice device, VkPhysicalDeviceProperties2 *out) {
    uint32_t index = 0;
    while (index < 3 && device != handle(index)) ++index;
    assert(index < 3 && out->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2);
    VkStructureType chained;
    memcpy(&chained, out->pNext, sizeof(chained));
    if (chained == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_3_PROPERTIES) {
        /* The selected device's allocation limit is queried separately. */
        VkPhysicalDeviceMaintenance3Properties *maintenance = out->pNext;
        assert(!maintenance->pNext);
        maintenance->maxMemoryAllocationSize = UINT64_C(1) << 30;
        return;
    }
    VkPhysicalDeviceIDProperties *ids = out->pNext;
    assert(ids && ids->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES && !ids->pNext);
    out->properties = (VkPhysicalDeviceProperties){.apiVersion=device_version,
        .driverVersion=7,.vendorID=11,.deviceID=13,.deviceType=index == 2 ? 4u : 2u};
    memcpy(out->properties.deviceName, "same reported device", 21);
    memset(out->properties.pipelineCacheUUID, 0x77, 16);
    memset(ids->deviceUUID, (int)(duplicate && index == 1 ? 1u : index + 1u), 16);
    memset(ids->driverUUID, 0x99, 16);
    /* Optional LUID fields are deliberately unusable. */
    memset(ids->deviceLUID, 0xa5, 8);
    ids->deviceLUIDValid = 0;
    ids->deviceNodeMask = UINT32_MAX;
}
static void memory(VkPhysicalDevice device, VkPhysicalDeviceMemoryProperties *out) {
    (void)device;
    *out = (VkPhysicalDeviceMemoryProperties){.memoryTypeCount=1,.memoryHeapCount=1};
}
static doe_vk_context context(void) {
    doe_vk_context result = {.instance=(VkInstance)&instances[generation]};
    result.vk.vkEnumeratePhysicalDevices = enumerate;
    result.vk.vkGetPhysicalDeviceProperties2 = properties;
    result.vk.vkGetPhysicalDeviceMemoryProperties = memory;
    return result;
}
#endif
