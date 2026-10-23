/* Selected ABI translated from the retained Doe Zig Vulkan declarations.
 * Source commit: 7b709f49. See ABI_PROVENANCE.json. Linux LP64 only.
 * No SDK/header dependency or runtime Zig dependency. Repository license applies. */
#ifndef VK_ABI_STRUCTS_H
#define VK_ABI_STRUCTS_H
#include "vk_abi_types.h"

typedef struct VkApplicationInfo VkApplicationInfo;
typedef struct VkInstanceCreateInfo VkInstanceCreateInfo;
typedef struct VkDeviceQueueCreateInfo VkDeviceQueueCreateInfo;
typedef struct VkDeviceCreateInfo VkDeviceCreateInfo;
typedef struct VkCommandPoolCreateInfo VkCommandPoolCreateInfo;
typedef struct VkCommandBufferAllocateInfo VkCommandBufferAllocateInfo;
typedef struct VkFenceCreateInfo VkFenceCreateInfo;
typedef struct VkBufferCreateInfo VkBufferCreateInfo;
typedef struct VkExtent3D VkExtent3D;
typedef struct VkMemoryAllocateInfo VkMemoryAllocateInfo;
typedef struct VkMemoryRequirements VkMemoryRequirements;
typedef struct VkCommandBufferInheritanceInfo VkCommandBufferInheritanceInfo;
typedef struct VkCommandBufferBeginInfo VkCommandBufferBeginInfo;
typedef struct VkSubmitInfo VkSubmitInfo;
typedef struct VkShaderModuleCreateInfo VkShaderModuleCreateInfo;
typedef struct VkPipelineLayoutCreateInfo VkPipelineLayoutCreateInfo;
typedef struct VkPipelineShaderStageCreateInfo VkPipelineShaderStageCreateInfo;
typedef struct VkDescriptorSetLayoutBinding VkDescriptorSetLayoutBinding;
typedef struct VkDescriptorSetLayoutCreateInfo VkDescriptorSetLayoutCreateInfo;
typedef struct VkDescriptorPoolSize VkDescriptorPoolSize;
typedef struct VkDescriptorPoolCreateInfo VkDescriptorPoolCreateInfo;
typedef struct VkDescriptorSetAllocateInfo VkDescriptorSetAllocateInfo;
typedef struct VkDescriptorBufferInfo VkDescriptorBufferInfo;
typedef struct VkWriteDescriptorSet VkWriteDescriptorSet;
typedef struct VkComputePipelineCreateInfo VkComputePipelineCreateInfo;
typedef struct VkBufferCopy VkBufferCopy;
typedef struct VkMemoryBarrier VkMemoryBarrier;
typedef struct VkQueueFamilyProperties VkQueueFamilyProperties;
typedef struct VkMemoryType VkMemoryType;
typedef struct VkMemoryHeap VkMemoryHeap;
typedef struct VkPhysicalDeviceMemoryProperties VkPhysicalDeviceMemoryProperties;
typedef struct VkExtensionProperties VkExtensionProperties;

struct VkApplicationInfo {
    VkStructureType sType;
    const void *pNext;
    const uint8_t *pApplicationName;
    uint32_t applicationVersion;
    const uint8_t *pEngineName;
    uint32_t engineVersion;
    uint32_t apiVersion;
};

struct VkInstanceCreateInfo {
    VkStructureType sType;
    const void *pNext;
    VkFlags flags;
    const VkApplicationInfo *pApplicationInfo;
    uint32_t enabledLayerCount;
    const uint8_t *const *ppEnabledLayerNames;
    uint32_t enabledExtensionCount;
    const uint8_t *const *ppEnabledExtensionNames;
};

struct VkDeviceQueueCreateInfo {
    VkStructureType sType;
    const void *pNext;
    VkFlags flags;
    uint32_t queueFamilyIndex;
    uint32_t queueCount;
    const float *pQueuePriorities;
};

struct VkDeviceCreateInfo {
    VkStructureType sType;
    const void *pNext;
    VkFlags flags;
    uint32_t queueCreateInfoCount;
    const VkDeviceQueueCreateInfo *pQueueCreateInfos;
    uint32_t enabledLayerCount;
    const uint8_t *const *ppEnabledLayerNames;
    uint32_t enabledExtensionCount;
    const uint8_t *const *ppEnabledExtensionNames;
    const void *pEnabledFeatures;
};

struct VkCommandPoolCreateInfo {
    VkStructureType sType;
    const void *pNext;
    VkFlags flags;
    uint32_t queueFamilyIndex;
};

struct VkCommandBufferAllocateInfo {
    VkStructureType sType;
    const void *pNext;
    VkCommandPool commandPool;
    int32_t level;
    uint32_t commandBufferCount;
};

struct VkFenceCreateInfo {
    VkStructureType sType;
    const void *pNext;
    VkFlags flags;
};

struct VkBufferCreateInfo {
    VkStructureType sType;
    const void *pNext;
    VkFlags flags;
    VkDeviceSize size;
    VkFlags usage;
    int32_t sharingMode;
    uint32_t queueFamilyIndexCount;
    const uint32_t *pQueueFamilyIndices;
};

struct VkExtent3D {
    uint32_t width;
    uint32_t height;
    uint32_t depth;
};

struct VkMemoryAllocateInfo {
    VkStructureType sType;
    const void *pNext;
    VkDeviceSize allocationSize;
    uint32_t memoryTypeIndex;
};

struct VkMemoryRequirements {
    VkDeviceSize size;
    VkDeviceSize alignment;
    uint32_t memoryTypeBits;
};

struct VkCommandBufferInheritanceInfo {
    VkStructureType sType;
    const void *pNext;
    VkRenderPass renderPass;
    uint32_t subpass;
    VkFramebuffer framebuffer;
    VkBool32 occlusionQueryEnable;
    VkFlags queryFlags;
    VkFlags pipelineStatistics;
};

struct VkCommandBufferBeginInfo {
    VkStructureType sType;
    const void *pNext;
    VkFlags flags;
    const VkCommandBufferInheritanceInfo *pInheritanceInfo;
};

struct VkSubmitInfo {
    VkStructureType sType;
    const void *pNext;
    uint32_t waitSemaphoreCount;
    const uint64_t *pWaitSemaphores;
    const VkFlags *pWaitDstStageMask;
    uint32_t commandBufferCount;
    const VkCommandBuffer *pCommandBuffers;
    uint32_t signalSemaphoreCount;
    const uint64_t *pSignalSemaphores;
};

struct VkShaderModuleCreateInfo {
    VkStructureType sType;
    const void *pNext;
    VkFlags flags;
    size_t codeSize;
    const uint32_t *pCode;
};

struct VkPipelineLayoutCreateInfo {
    VkStructureType sType;
    const void *pNext;
    VkFlags flags;
    uint32_t setLayoutCount;
    const uint64_t *pSetLayouts;
    uint32_t pushConstantRangeCount;
    const void *pPushConstantRanges;
};

struct VkPipelineShaderStageCreateInfo {
    VkStructureType sType;
    const void *pNext;
    VkFlags flags;
    VkFlags stage;
    VkShaderModule module;
    const uint8_t *pName;
    const void *pSpecializationInfo;
};

struct VkDescriptorSetLayoutBinding {
    uint32_t binding;
    uint32_t descriptorType;
    uint32_t descriptorCount;
    VkFlags stageFlags;
    const void *pImmutableSamplers;
};

struct VkDescriptorSetLayoutCreateInfo {
    VkStructureType sType;
    const void *pNext;
    VkFlags flags;
    uint32_t bindingCount;
    const VkDescriptorSetLayoutBinding *pBindings;
};

struct VkDescriptorPoolSize {
    uint32_t type;
    uint32_t descriptorCount;
};

struct VkDescriptorPoolCreateInfo {
    VkStructureType sType;
    const void *pNext;
    VkFlags flags;
    uint32_t maxSets;
    uint32_t poolSizeCount;
    const VkDescriptorPoolSize *pPoolSizes;
};

struct VkDescriptorSetAllocateInfo {
    VkStructureType sType;
    const void *pNext;
    VkDescriptorPool descriptorPool;
    uint32_t descriptorSetCount;
    const VkDescriptorSetLayout *pSetLayouts;
};

struct VkDescriptorBufferInfo {
    VkBuffer buffer;
    VkDeviceSize offset;
    VkDeviceSize range;
};

struct VkWriteDescriptorSet {
    VkStructureType sType;
    const void *pNext;
    VkDescriptorSet dstSet;
    uint32_t dstBinding;
    uint32_t dstArrayElement;
    uint32_t descriptorCount;
    uint32_t descriptorType;
    const void *pImageInfo;
    const VkDescriptorBufferInfo *pBufferInfo;
    const void *pTexelBufferView;
};

struct VkComputePipelineCreateInfo {
    VkStructureType sType;
    const void *pNext;
    VkFlags flags;
    VkPipelineShaderStageCreateInfo stage;
    VkPipelineLayout layout;
    VkPipeline basePipelineHandle;
    int32_t basePipelineIndex;
};

struct VkBufferCopy {
    VkDeviceSize srcOffset;
    VkDeviceSize dstOffset;
    VkDeviceSize size;
};

struct VkMemoryBarrier {
    VkStructureType sType;
    const void *pNext;
    VkFlags srcAccessMask;
    VkFlags dstAccessMask;
};

struct VkQueueFamilyProperties {
    VkFlags queueFlags;
    uint32_t queueCount;
    uint32_t timestampValidBits;
    VkExtent3D minImageTransferGranularity;
};

struct VkMemoryType {
    VkFlags propertyFlags;
    uint32_t heapIndex;
};

struct VkMemoryHeap {
    VkDeviceSize size;
    VkFlags flags;
};

struct VkPhysicalDeviceMemoryProperties {
    uint32_t memoryTypeCount;
    VkMemoryType memoryTypes[32];
    uint32_t memoryHeapCount;
    VkMemoryHeap memoryHeaps[16];
};

struct VkExtensionProperties {
    uint8_t extensionName[256];
    uint32_t specVersion;
};

typedef struct VkQueryPoolCreateInfo {
    VkStructureType sType;
    const void *pNext;
    VkFlags flags;
    uint32_t queryType;
    uint32_t queryCount;
    VkFlags pipelineStatistics;
} VkQueryPoolCreateInfo;
#endif
