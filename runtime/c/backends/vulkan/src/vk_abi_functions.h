/* Selected ABI translated from the retained Doe Zig Vulkan declarations.
 * Source commit: 7b709f49. See ABI_PROVENANCE.json. Linux LP64 only.
 * No SDK/header dependency or runtime Zig dependency. Repository license applies. */
#ifndef DOE_VK_ABI_FUNCTIONS_H
#define DOE_VK_ABI_FUNCTIONS_H
#include "vk_abi_structs.h"
#include "vk_abi_caps.h"
#include "vk_abi_images.h"
#include "vk_abi_render.h"

typedef struct doe_vk_functions {
    VkResult (*vkCreateInstance)(const VkInstanceCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkInstance *pInstance);
    void (*vkDestroyInstance)(VkInstance instance, const VkAllocationCallbacks *pAllocator);
    VkResult (*vkEnumeratePhysicalDevices)(VkInstance instance, uint32_t *pPhysicalDeviceCount, VkPhysicalDevice *pPhysicalDevices);
    void (*vkGetPhysicalDeviceQueueFamilyProperties)(VkPhysicalDevice physicalDevice, uint32_t *pQueueFamilyPropertyCount, VkQueueFamilyProperties *pQueueFamilyProperties);
    VkResult (*vkEnumerateDeviceExtensionProperties)(VkPhysicalDevice physicalDevice, const uint8_t *pLayerName, uint32_t *pPropertyCount, VkExtensionProperties *pProperties);
    VkResult (*vkCreateDevice)(VkPhysicalDevice physicalDevice, const VkDeviceCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkDevice *pDevice);
    void (*vkDestroyDevice)(VkDevice device, const VkAllocationCallbacks *pAllocator);
    void (*vkGetDeviceQueue)(VkDevice device, uint32_t queueFamilyIndex, uint32_t queueIndex, VkQueue *pQueue);
    VkResult (*vkCreateCommandPool)(VkDevice device, const VkCommandPoolCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkCommandPool *pCommandPool);
    void (*vkDestroyCommandPool)(VkDevice device, VkCommandPool commandPool, const VkAllocationCallbacks *pAllocator);
    VkResult (*vkAllocateCommandBuffers)(VkDevice device, const VkCommandBufferAllocateInfo *pAllocateInfo, VkCommandBuffer *pCommandBuffers);
    void (*vkFreeCommandBuffers)(VkDevice device, VkCommandPool commandPool, uint32_t commandBufferCount, const VkCommandBuffer *pCommandBuffers);
    VkResult (*vkCreateFence)(VkDevice device, const VkFenceCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkFence *pFence);
    void (*vkDestroyFence)(VkDevice device, VkFence fence, const VkAllocationCallbacks *pAllocator);
    VkResult (*vkGetFenceStatus)(VkDevice device, VkFence fence);
    VkResult (*vkWaitForFences)(VkDevice device, uint32_t fenceCount, const VkFence *pFences, VkBool32 waitAll, uint64_t timeout);
    VkResult (*vkQueueSubmit)(VkQueue queue, uint32_t submitCount, const VkSubmitInfo *pSubmits, VkFence fence);
    VkResult (*vkQueueWaitIdle)(VkQueue queue);
    VkResult (*vkBeginCommandBuffer)(VkCommandBuffer commandBuffer, const VkCommandBufferBeginInfo *pBeginInfo);
    VkResult (*vkEndCommandBuffer)(VkCommandBuffer commandBuffer);
    void (*vkCmdBindPipeline)(VkCommandBuffer commandBuffer, int32_t pipelineBindPoint, VkPipeline pipeline);
    void (*vkCmdDispatch)(VkCommandBuffer commandBuffer, uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ);
    void (*vkCmdDispatchIndirect)(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset);
    void (*vkCmdCopyBuffer)(VkCommandBuffer commandBuffer, VkBuffer srcBuffer, VkBuffer dstBuffer, uint32_t regionCount, const VkBufferCopy *pRegions);
    void (*vkCmdFillBuffer)(VkCommandBuffer commandBuffer, VkBuffer dstBuffer, VkDeviceSize dstOffset, VkDeviceSize size, uint32_t data);
    void (*vkCmdPipelineBarrier)(VkCommandBuffer commandBuffer, VkFlags srcStageMask, VkFlags dstStageMask, VkFlags dependencyFlags, uint32_t memoryBarrierCount, const void *pMemoryBarriers, uint32_t bufferMemoryBarrierCount, const void *pBufferMemoryBarriers, uint32_t imageMemoryBarrierCount, const void *pImageMemoryBarriers);
    VkResult (*vkCreateBuffer)(VkDevice device, const VkBufferCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkBuffer *pBuffer);
    void (*vkDestroyBuffer)(VkDevice device, VkBuffer buffer, const VkAllocationCallbacks *pAllocator);
    void (*vkGetBufferMemoryRequirements)(VkDevice device, VkBuffer buffer, VkMemoryRequirements *pMemoryRequirements);
    VkResult (*vkAllocateMemory)(VkDevice device, const VkMemoryAllocateInfo *pAllocateInfo, const VkAllocationCallbacks *pAllocator, VkDeviceMemory *pMemory);
    void (*vkFreeMemory)(VkDevice device, VkDeviceMemory memory, const VkAllocationCallbacks *pAllocator);
    VkResult (*vkBindBufferMemory)(VkDevice device, VkBuffer buffer, VkDeviceMemory memory, VkDeviceSize memoryOffset);
    VkResult (*vkMapMemory)(VkDevice device, VkDeviceMemory memory, VkDeviceSize offset, VkDeviceSize size, VkFlags flags, void **ppData);
    void (*vkUnmapMemory)(VkDevice device, VkDeviceMemory memory);
    void (*vkGetPhysicalDeviceMemoryProperties)(VkPhysicalDevice physicalDevice, VkPhysicalDeviceMemoryProperties *pMemoryProperties);
    void (*vkGetPhysicalDeviceFeatures2)(VkPhysicalDevice physicalDevice, VkPhysicalDeviceFeatures2 *pFeatures);
    void (*vkGetPhysicalDeviceProperties2)(VkPhysicalDevice physicalDevice, VkPhysicalDeviceProperties2 *pProperties);
    VkResult (*vkCreateShaderModule)(VkDevice device, const VkShaderModuleCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkShaderModule *pShaderModule);
    void (*vkDestroyShaderModule)(VkDevice device, VkShaderModule shaderModule, const VkAllocationCallbacks *pAllocator);
    VkResult (*vkCreateDescriptorSetLayout)(VkDevice device, const VkDescriptorSetLayoutCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkDescriptorSetLayout *pSetLayout);
    void (*vkDestroyDescriptorSetLayout)(VkDevice device, VkDescriptorSetLayout descriptorSetLayout, const VkAllocationCallbacks *pAllocator);
    VkResult (*vkCreateDescriptorPool)(VkDevice device, const VkDescriptorPoolCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkDescriptorPool *pDescriptorPool);
    void (*vkDestroyDescriptorPool)(VkDevice device, VkDescriptorPool descriptorPool, const VkAllocationCallbacks *pAllocator);
    VkResult (*vkAllocateDescriptorSets)(VkDevice device, const VkDescriptorSetAllocateInfo *pAllocateInfo, VkDescriptorSet *pDescriptorSets);
    void (*vkUpdateDescriptorSets)(VkDevice device, uint32_t descriptorWriteCount, const VkWriteDescriptorSet *pDescriptorWrites, uint32_t descriptorCopyCount, const void *pDescriptorCopies);
    VkResult (*vkCreatePipelineLayout)(VkDevice device, const VkPipelineLayoutCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkPipelineLayout *pPipelineLayout);
    void (*vkDestroyPipelineLayout)(VkDevice device, VkPipelineLayout pipelineLayout, const VkAllocationCallbacks *pAllocator);
    VkResult (*vkCreateComputePipelines)(VkDevice device, VkPipelineCache pipelineCache, uint32_t createInfoCount, const VkComputePipelineCreateInfo *pCreateInfos, const VkAllocationCallbacks *pAllocator, VkPipeline *pPipelines);
    void (*vkDestroyPipeline)(VkDevice device, VkPipeline pipeline, const VkAllocationCallbacks *pAllocator);
    void (*vkCmdBindDescriptorSets)(VkCommandBuffer commandBuffer, int32_t pipelineBindPoint, VkPipelineLayout layout, uint32_t firstSet, uint32_t descriptorSetCount, const VkDescriptorSet *pDescriptorSets, uint32_t dynamicOffsetCount, const uint32_t *pDynamicOffsets);
    void (*vkCmdCopyBufferToImage)(VkCommandBuffer commandBuffer, VkBuffer srcBuffer, VkImage dstImage, uint32_t dstImageLayout, uint32_t regionCount, const VkBufferImageCopy *pRegions);
    void (*vkCmdCopyImageToBuffer)(VkCommandBuffer commandBuffer, VkImage srcImage, uint32_t srcImageLayout, VkBuffer dstBuffer, uint32_t regionCount, const VkBufferImageCopy *pRegions);
    void (*vkCmdCopyImage)(VkCommandBuffer commandBuffer, VkImage srcImage, uint32_t srcImageLayout, VkImage dstImage, uint32_t dstImageLayout, uint32_t regionCount, const VkImageCopy *pRegions);
    VkResult (*vkCreateImage)(VkDevice device, const VkImageCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkImage *pImage);
    void (*vkDestroyImage)(VkDevice device, VkImage image, const VkAllocationCallbacks *pAllocator);
    void (*vkGetImageMemoryRequirements)(VkDevice device, VkImage image, VkMemoryRequirements *pMemoryRequirements);
    VkResult (*vkBindImageMemory)(VkDevice device, VkImage image, VkDeviceMemory memory, VkDeviceSize memoryOffset);
    VkResult (*vkCreateImageView)(VkDevice device, const VkImageViewCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkImageView *pView);
    void (*vkDestroyImageView)(VkDevice device, VkImageView imageView, const VkAllocationCallbacks *pAllocator);
    void (*vkGetPhysicalDeviceFormatProperties)(VkPhysicalDevice physicalDevice, uint32_t format, VkFormatProperties *pFormatProperties);
    VkResult (*vkCreateSampler)(VkDevice device, const VkSamplerCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkSampler *pSampler);
    void (*vkDestroySampler)(VkDevice device, VkSampler sampler, const VkAllocationCallbacks *pAllocator);
    VkResult (*vkCreateRenderPass)(VkDevice device, const VkRenderPassCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkRenderPass *pRenderPass);
    void (*vkDestroyRenderPass)(VkDevice device, VkRenderPass renderPass, const VkAllocationCallbacks *pAllocator);
    VkResult (*vkCreateFramebuffer)(VkDevice device, const VkFramebufferCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkFramebuffer *pFramebuffer);
    void (*vkDestroyFramebuffer)(VkDevice device, VkFramebuffer framebuffer, const VkAllocationCallbacks *pAllocator);
    VkResult (*vkCreateGraphicsPipelines)(VkDevice device, VkPipelineCache pipelineCache, uint32_t createInfoCount, const VkGraphicsPipelineCreateInfo *pCreateInfos, const VkAllocationCallbacks *pAllocator, VkPipeline *pPipelines);
    void (*vkCmdBeginRenderPass)(VkCommandBuffer commandBuffer, const VkRenderPassBeginInfo *pRenderPassBegin, uint32_t contents);
    void (*vkCmdEndRenderPass)(VkCommandBuffer commandBuffer);
    void (*vkCmdSetViewport)(VkCommandBuffer commandBuffer, uint32_t firstViewport, uint32_t viewportCount, const VkViewport *pViewports);
    void (*vkCmdSetScissor)(VkCommandBuffer commandBuffer, uint32_t firstScissor, uint32_t scissorCount, const VkRect2D *pScissors);
    void (*vkCmdBindVertexBuffers)(VkCommandBuffer commandBuffer, uint32_t firstBinding, uint32_t bindingCount, const VkBuffer *pBuffers, const VkDeviceSize *pOffsets);
    /* Core1.0 signature verified against Khronos; see ABI_PROVENANCE.json. */
    void (*vkCmdSetBlendConstants)(VkCommandBuffer commandBuffer, const float blendConstants[4]);
    void (*vkCmdDraw)(VkCommandBuffer commandBuffer, uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance);
    VkResult (*vkCreateQueryPool)(VkDevice device, const VkQueryPoolCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkQueryPool *pQueryPool);
    void (*vkDestroyQueryPool)(VkDevice device, VkQueryPool queryPool, const VkAllocationCallbacks *pAllocator);
    void (*vkCmdBeginQuery)(VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t query, VkFlags flags);
    void (*vkCmdEndQuery)(VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t query);
    void (*vkCmdWriteTimestamp)(VkCommandBuffer commandBuffer, VkFlags pipelineStage, VkQueryPool queryPool, uint32_t query);
    void (*vkCmdResetQueryPool)(VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t firstQuery, uint32_t queryCount);
    void (*vkCmdCopyQueryPoolResults)(VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t firstQuery, uint32_t queryCount, VkBuffer dstBuffer, VkDeviceSize dstOffset, VkDeviceSize stride, VkFlags flags);
} doe_vk_functions;
#endif
