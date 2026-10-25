/* SPDX-License-Identifier: MIT
 * Vulkan core/EXT subgroup records, checked against the official SDK ABI. */
#ifndef DOE_VK_ABI_SUBGROUP_H
#define DOE_VK_ABI_SUBGROUP_H
#include "vk_abi_types.h"
enum { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_SUBGROUP_EXTENDED_TYPES_FEATURES = 1000175000 };
typedef struct VkPhysicalDeviceSubgroupProperties {
    VkStructureType sType;
    void *pNext;
    uint32_t subgroupSize, supportedStages, supportedOperations;
    VkBool32 quadOperationsInAllStages;
} VkPhysicalDeviceSubgroupProperties;
typedef struct VkPhysicalDeviceSubgroupSizeControlFeatures {
    VkStructureType sType;
    void *pNext;
    VkBool32 subgroupSizeControl, computeFullSubgroups;
} VkPhysicalDeviceSubgroupSizeControlFeatures;
typedef struct VkPhysicalDeviceSubgroupSizeControlProperties {
    VkStructureType sType;
    void *pNext;
    uint32_t minSubgroupSize, maxSubgroupSize, maxComputeWorkgroupSubgroups, requiredSubgroupSizeStages;
} VkPhysicalDeviceSubgroupSizeControlProperties;
typedef struct VkPipelineShaderStageRequiredSubgroupSizeCreateInfo {
    VkStructureType sType;
    void *pNext;
    uint32_t requiredSubgroupSize;
} VkPipelineShaderStageRequiredSubgroupSizeCreateInfo;
typedef struct VkPhysicalDeviceShaderSubgroupExtendedTypesFeatures {
    VkStructureType sType;
    void *pNext;
    VkBool32 shaderSubgroupExtendedTypes;
} VkPhysicalDeviceShaderSubgroupExtendedTypesFeatures;
#endif
