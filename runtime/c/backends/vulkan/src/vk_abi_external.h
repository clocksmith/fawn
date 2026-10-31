/* SPDX-License-Identifier: MIT */
/* Official core1.1/1.2 and VK_KHR_external_{memory,semaphore}_fd declarations,
 * separate from retained provenance; see ABI_PROVENANCE.json official_structs.
 * Source: Khronos Vulkan-Headers v1.3.290 vulkan_core.h. Linux LP64 only. */
#ifndef DOE_VK_ABI_EXTERNAL_H
#define DOE_VK_ABI_EXTERNAL_H
#include "vk_abi_images.h"
#include "vk_abi_surface.h"
#include "vk_abi_constants.h"

#define VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2 1000059003
#define VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2 1000059004
#define VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO 1000071000
#define VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES 1000071001
#define VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO 1000072001
#define VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR 1000074000
#define VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_SEMAPHORE_INFO 1000076000
#define VK_STRUCTURE_TYPE_EXTERNAL_SEMAPHORE_PROPERTIES 1000076001
#define VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO 1000077000
#define VK_STRUCTURE_TYPE_IMPORT_SEMAPHORE_FD_INFO_KHR 1000079000
#define VK_STRUCTURE_TYPE_SEMAPHORE_GET_FD_INFO_KHR 1000079001
#define VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO 1000127001
#define VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT 0x00000001u
#define VK_EXTERNAL_MEMORY_FEATURE_DEDICATED_ONLY_BIT 0x00000001u
#define VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT 0x00000004u
#define VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT 0x00000010u
#define VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT 0x00000001u
#define VK_EXTERNAL_SEMAPHORE_FEATURE_EXPORTABLE_BIT 0x00000001u
#define VK_EXTERNAL_SEMAPHORE_FEATURE_IMPORTABLE_BIT 0x00000002u
#define VK_SEMAPHORE_IMPORT_TEMPORARY_BIT 0x00000001u
#define VK_QUEUE_FAMILY_EXTERNAL (~1u)

typedef struct VkPhysicalDeviceExternalImageFormatInfo {
    VkStructureType sType;
    const void *pNext;
    uint32_t handleType;
} VkPhysicalDeviceExternalImageFormatInfo;
typedef struct VkPhysicalDeviceImageFormatInfo2 {
    VkStructureType sType;
    const void *pNext;
    uint32_t format;
    uint32_t type;
    uint32_t tiling;
    VkFlags usage;
    VkFlags flags;
} VkPhysicalDeviceImageFormatInfo2;
typedef struct VkExternalMemoryProperties {
    VkFlags externalMemoryFeatures;
    VkFlags exportFromImportedHandleTypes;
    VkFlags compatibleHandleTypes;
} VkExternalMemoryProperties;
typedef struct VkExternalImageFormatProperties {
    VkStructureType sType;
    void *pNext;
    VkExternalMemoryProperties externalMemoryProperties;
} VkExternalImageFormatProperties;
typedef struct VkImageFormatProperties2 {
    VkStructureType sType;
    void *pNext;
    VkImageFormatProperties imageFormatProperties;
} VkImageFormatProperties2;
typedef struct VkPhysicalDeviceExternalSemaphoreInfo {
    VkStructureType sType;
    const void *pNext;
    uint32_t handleType;
} VkPhysicalDeviceExternalSemaphoreInfo;
typedef struct VkExternalSemaphoreProperties {
    VkStructureType sType;
    void *pNext;
    VkFlags exportFromImportedHandleTypes;
    VkFlags compatibleHandleTypes;
    VkFlags externalSemaphoreFeatures;
} VkExternalSemaphoreProperties;
typedef struct VkExportSemaphoreCreateInfo {
    VkStructureType sType;
    const void *pNext;
    VkFlags handleTypes;
} VkExportSemaphoreCreateInfo;
typedef struct VkImportSemaphoreFdInfoKHR {
    VkStructureType sType;
    const void *pNext;
    VkSemaphore semaphore;
    VkFlags flags;
    uint32_t handleType;
    int fd;
} VkImportSemaphoreFdInfoKHR;
typedef struct VkSemaphoreGetFdInfoKHR {
    VkStructureType sType;
    const void *pNext;
    VkSemaphore semaphore;
    uint32_t handleType;
} VkSemaphoreGetFdInfoKHR;
typedef struct VkExternalMemoryImageCreateInfo {
    VkStructureType sType;
    const void *pNext;
    VkFlags handleTypes;
} VkExternalMemoryImageCreateInfo;
typedef struct VkImportMemoryFdInfoKHR {
    VkStructureType sType;
    const void *pNext;
    uint32_t handleType;
    int fd;
} VkImportMemoryFdInfoKHR;
typedef struct VkMemoryDedicatedAllocateInfo {
    VkStructureType sType;
    const void *pNext;
    VkImage image;
    VkBuffer buffer;
} VkMemoryDedicatedAllocateInfo;

/* Resolved only when doe_vk_options.enable_external_fd selects them; NULL otherwise. */
typedef struct doe_vk_external_functions {
    VkResult (*vkGetPhysicalDeviceImageFormatProperties2)(VkPhysicalDevice,
        const VkPhysicalDeviceImageFormatInfo2 *, VkImageFormatProperties2 *);
    void (*vkGetPhysicalDeviceExternalSemaphoreProperties)(VkPhysicalDevice,
        const VkPhysicalDeviceExternalSemaphoreInfo *, VkExternalSemaphoreProperties *);
    VkResult (*vkImportSemaphoreFdKHR)(VkDevice, const VkImportSemaphoreFdInfoKHR *);
    VkResult (*vkGetSemaphoreFdKHR)(VkDevice, const VkSemaphoreGetFdInfoKHR *, int *);
} doe_vk_external_functions;
#endif
