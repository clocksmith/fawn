/* SPDX-License-Identifier: MIT */
#ifndef DOE_VK_ABI_TIMELINE_H
#define DOE_VK_ABI_TIMELINE_H
#include "vk_abi_surface.h"
/* Vulkan 1.2 core declarations, checked against the SDK ABI by focused tests. */
typedef struct VkPhysicalDeviceTimelineSemaphoreFeatures {
    VkStructureType sType; void *pNext; VkBool32 timelineSemaphore;
} VkPhysicalDeviceTimelineSemaphoreFeatures;
typedef struct VkPhysicalDeviceTimelineSemaphoreProperties {
    VkStructureType sType; void *pNext; uint64_t maxTimelineSemaphoreValueDifference;
} VkPhysicalDeviceTimelineSemaphoreProperties;
typedef struct VkSemaphoreTypeCreateInfo {
    VkStructureType sType; const void *pNext; int32_t semaphoreType; uint64_t initialValue;
} VkSemaphoreTypeCreateInfo;
typedef struct VkTimelineSemaphoreSubmitInfo {
    VkStructureType sType; const void *pNext;
    uint32_t waitSemaphoreValueCount; const uint64_t *pWaitSemaphoreValues;
    uint32_t signalSemaphoreValueCount; const uint64_t *pSignalSemaphoreValues;
} VkTimelineSemaphoreSubmitInfo;
typedef struct VkSemaphoreWaitInfo {
    VkStructureType sType; const void *pNext; VkFlags flags;
    uint32_t semaphoreCount; const VkSemaphore *pSemaphores; const uint64_t *pValues;
} VkSemaphoreWaitInfo;
enum {
    DOE_VK_TIMELINE_FEATURES = 1000207000, DOE_VK_TIMELINE_PROPERTIES = 1000207001,
    DOE_VK_SEMAPHORE_TYPE = 1000207002, DOE_VK_TIMELINE_SUBMIT = 1000207003,
    DOE_VK_SEMAPHORE_WAIT = 1000207004, DOE_VK_SEMAPHORE_TIMELINE = 1
};
#endif
