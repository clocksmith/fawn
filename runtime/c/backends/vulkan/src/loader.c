/* SPDX-License-Identifier: MIT */
#include "internal.h"
#include "feature_internal.h"
#include "surface_internal.h"
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
_Static_assert(sizeof(FARPROC) == sizeof(void *), "Win64 loader symbol storage");
static doe_vk_status win32_loader_error(const char *operation, doe_vk_error *error) {
    DWORD code = GetLastError();
    _Static_assert(sizeof(code) == sizeof(error->native_result), "Win32 loader error storage");
    doe_vk_status status = doe_vk_fail(DOE_VK_LOADER_UNAVAILABLE, operation, error);
    if (error != NULL) memcpy(&error->native_result, &code, sizeof(code));
    return status;
}
#else
#include <dlfcn.h>
#endif

/* POSIX dlsym and Win64 GetProcAddress have distinct native result types.
 * Copy only checked native symbol storage; failed lookup leaves the typed
 * destination intact. No object/function-pointer cast or procedure call is made. */
doe_vk_status doe_vk_load_symbol(void *library, const char *name,
    void *destination, size_t destination_size, doe_vk_error *error) {
    if (library == NULL || name == NULL || destination == NULL ||
        destination_size != sizeof(void *))
        return doe_vk_fail(DOE_VK_INVALID_ARGUMENT, "loader symbol storage", error);
#if defined(_WIN32)
    FARPROC symbol = GetProcAddress((HMODULE)library, name);
#else
    void *symbol = dlsym(library, name);
#endif
    if (symbol == NULL) {
#if defined(_WIN32)
        return win32_loader_error(name, error);
#else
        return doe_vk_fail(DOE_VK_LOADER_UNAVAILABLE, name, error);
#endif
    }
    memcpy(destination, &symbol, destination_size);
    return DOE_VK_OK;
}

static doe_vk_status load_functions(doe_vk_context *context, doe_vk_error *error) {
    _Static_assert(sizeof(context->vk.vkCreateInstance) == sizeof(void *), "vkCreateInstance POSIX pointer size");
    _Static_assert(sizeof(context->vk.vkDestroyInstance) == sizeof(void *), "vkDestroyInstance POSIX pointer size");
    _Static_assert(sizeof(context->vk.vkEnumeratePhysicalDevices) == sizeof(void *), "vkEnumeratePhysicalDevices POSIX pointer size");
    _Static_assert(sizeof(context->vk.vkGetPhysicalDeviceQueueFamilyProperties) == sizeof(void *), "vkGetPhysicalDeviceQueueFamilyProperties POSIX pointer size");
    _Static_assert(sizeof(context->vk.vkGetPhysicalDeviceMemoryProperties) == sizeof(void *), "vkGetPhysicalDeviceMemoryProperties POSIX pointer size");
    _Static_assert(sizeof(context->vk.vkGetPhysicalDeviceProperties2) == sizeof(void *), "vkGetPhysicalDeviceProperties2 POSIX pointer size");
    _Static_assert(sizeof(context->vk.vkGetPhysicalDeviceFeatures2) == sizeof(void *), "vkGetPhysicalDeviceFeatures2 POSIX pointer size");
    _Static_assert(sizeof(context->vk.vkEnumerateDeviceExtensionProperties) == sizeof(void *), "vkEnumerateDeviceExtensionProperties POSIX pointer size");
    _Static_assert(sizeof(context->vk.vkCreateDevice) == sizeof(void *), "vkCreateDevice POSIX pointer size");
    _Static_assert(sizeof(context->vk.vkDestroyDevice) == sizeof(void *), "vkDestroyDevice POSIX pointer size");
    _Static_assert(sizeof(context->vk.vkGetDeviceQueue) == sizeof(void *), "vkGetDeviceQueue POSIX pointer size");
    _Static_assert(sizeof(context->vk.vkCreateCommandPool) == sizeof(void *), "vkCreateCommandPool POSIX pointer size");
    _Static_assert(sizeof(context->vk.vkDestroyCommandPool) == sizeof(void *), "vkDestroyCommandPool POSIX pointer size");
    _Static_assert(sizeof(context->vk.vkAllocateCommandBuffers) == sizeof(void *), "vkAllocateCommandBuffers POSIX pointer size");
    _Static_assert(sizeof(context->vk.vkFreeCommandBuffers) == sizeof(void *), "vkFreeCommandBuffers POSIX pointer size");
    _Static_assert(sizeof(context->vk.vkCreateFence) == sizeof(void *), "vkCreateFence POSIX pointer size");
    _Static_assert(sizeof(context->vk.vkDestroyFence) == sizeof(void *), "vkDestroyFence POSIX pointer size");
    _Static_assert(sizeof(context->vk.vkGetFenceStatus) == sizeof(void *), "vkGetFenceStatus POSIX pointer size");
    _Static_assert(sizeof(context->vk.vkWaitForFences) == sizeof(void *), "vkWaitForFences POSIX pointer size");
    _Static_assert(sizeof(context->vk.vkResetFences) == sizeof(void *), "vkResetFences POSIX pointer size");
    _Static_assert(sizeof(context->vk.vkQueueSubmit) == sizeof(void *), "vkQueueSubmit POSIX pointer size");
    _Static_assert(sizeof(context->vk.vkQueueWaitIdle) == sizeof(void *), "vkQueueWaitIdle POSIX pointer size");
    _Static_assert(sizeof(context->vk.vkBeginCommandBuffer) == sizeof(void *), "vkBeginCommandBuffer POSIX pointer size");
    _Static_assert(sizeof(context->vk.vkEndCommandBuffer) == sizeof(void *), "vkEndCommandBuffer POSIX pointer size");
    _Static_assert(sizeof(context->vk.vkCmdBindPipeline) == sizeof(void *), "vkCmdBindPipeline POSIX pointer size");
    _Static_assert(sizeof(context->vk.vkCmdDispatch) == sizeof(void *), "vkCmdDispatch POSIX pointer size");
    _Static_assert(sizeof(context->vk.vkCmdDispatchIndirect) == sizeof(void *), "vkCmdDispatchIndirect POSIX pointer size");
    _Static_assert(sizeof(context->vk.vkCmdCopyBuffer) == sizeof(void *), "vkCmdCopyBuffer POSIX pointer size");
    _Static_assert(sizeof(context->vk.vkCmdFillBuffer) == sizeof(void *), "vkCmdFillBuffer POSIX pointer size");
    _Static_assert(sizeof(context->vk.vkCmdPipelineBarrier) == sizeof(void *), "vkCmdPipelineBarrier POSIX pointer size");
    _Static_assert(sizeof(context->vk.vkCreateBuffer) == sizeof(void *), "vkCreateBuffer POSIX pointer size");
    _Static_assert(sizeof(context->vk.vkDestroyBuffer) == sizeof(void *), "vkDestroyBuffer POSIX pointer size");
    _Static_assert(sizeof(context->vk.vkGetBufferMemoryRequirements) == sizeof(void *), "vkGetBufferMemoryRequirements POSIX pointer size");
    _Static_assert(sizeof(context->vk.vkAllocateMemory) == sizeof(void *), "vkAllocateMemory POSIX pointer size");
    _Static_assert(sizeof(context->vk.vkFreeMemory) == sizeof(void *), "vkFreeMemory POSIX pointer size");
    _Static_assert(sizeof(context->vk.vkBindBufferMemory) == sizeof(void *), "vkBindBufferMemory POSIX pointer size");
    _Static_assert(sizeof(context->vk.vkMapMemory) == sizeof(void *), "vkMapMemory POSIX pointer size");
    _Static_assert(sizeof(context->vk.vkUnmapMemory) == sizeof(void *), "vkUnmapMemory POSIX pointer size");
    _Static_assert(sizeof(context->vk.vkCreateShaderModule) == sizeof(void *), "vkCreateShaderModule POSIX pointer size");
    _Static_assert(sizeof(context->vk.vkDestroyShaderModule) == sizeof(void *), "vkDestroyShaderModule POSIX pointer size");
    _Static_assert(sizeof(context->vk.vkCreateDescriptorSetLayout) == sizeof(void *), "vkCreateDescriptorSetLayout POSIX pointer size");
    _Static_assert(sizeof(context->vk.vkDestroyDescriptorSetLayout) == sizeof(void *), "vkDestroyDescriptorSetLayout POSIX pointer size");
    _Static_assert(sizeof(context->vk.vkCreateDescriptorPool) == sizeof(void *), "vkCreateDescriptorPool POSIX pointer size");
    _Static_assert(sizeof(context->vk.vkDestroyDescriptorPool) == sizeof(void *), "vkDestroyDescriptorPool POSIX pointer size");
    _Static_assert(sizeof(context->vk.vkAllocateDescriptorSets) == sizeof(void *), "vkAllocateDescriptorSets POSIX pointer size");
    _Static_assert(sizeof(context->vk.vkUpdateDescriptorSets) == sizeof(void *), "vkUpdateDescriptorSets POSIX pointer size");
    _Static_assert(sizeof(context->vk.vkCreatePipelineLayout) == sizeof(void *), "vkCreatePipelineLayout POSIX pointer size");
    _Static_assert(sizeof(context->vk.vkDestroyPipelineLayout) == sizeof(void *), "vkDestroyPipelineLayout POSIX pointer size");
    _Static_assert(sizeof(context->vk.vkCreateComputePipelines) == sizeof(void *), "vkCreateComputePipelines POSIX pointer size");
    _Static_assert(sizeof(context->vk.vkDestroyPipeline) == sizeof(void *), "vkDestroyPipeline POSIX pointer size");
    _Static_assert(sizeof(context->vk.vkCmdBindDescriptorSets) == sizeof(void *), "vkCmdBindDescriptorSets POSIX pointer size");
    _Static_assert(sizeof(context->vk.vkGetPhysicalDeviceFormatProperties) == sizeof(void *), "vkGetPhysicalDeviceFormatProperties POSIX pointer size");
    _Static_assert(sizeof(context->vk.vkGetPhysicalDeviceImageFormatProperties) == sizeof(void *), "vkGetPhysicalDeviceImageFormatProperties POSIX pointer size");
    _Static_assert(sizeof(context->vk.vkCreateImage) == sizeof(void *), "vkCreateImage POSIX pointer size");
    _Static_assert(sizeof(context->vk.vkDestroyImage) == sizeof(void *), "vkDestroyImage POSIX pointer size");
    _Static_assert(sizeof(context->vk.vkGetImageMemoryRequirements) == sizeof(void *), "vkGetImageMemoryRequirements POSIX pointer size");
    _Static_assert(sizeof(context->vk.vkBindImageMemory) == sizeof(void *), "vkBindImageMemory POSIX pointer size");
    _Static_assert(sizeof(context->vk.vkCreateImageView) == sizeof(void *), "vkCreateImageView POSIX pointer size");
    _Static_assert(sizeof(context->vk.vkDestroyImageView) == sizeof(void *), "vkDestroyImageView POSIX pointer size");
    _Static_assert(sizeof(context->vk.vkCreateSampler) == sizeof(void *), "vkCreateSampler POSIX pointer size");
    _Static_assert(sizeof(context->vk.vkDestroySampler) == sizeof(void *), "vkDestroySampler POSIX pointer size");
    _Static_assert(sizeof(context->vk.vkCmdCopyBufferToImage) == sizeof(void *), "vkCmdCopyBufferToImage POSIX pointer size");
    _Static_assert(sizeof(context->vk.vkCmdCopyImageToBuffer) == sizeof(void *), "vkCmdCopyImageToBuffer POSIX pointer size");
    _Static_assert(sizeof(context->vk.vkCmdCopyImage) == sizeof(void *), "vkCmdCopyImage POSIX pointer size");
    _Static_assert(sizeof(context->vk.vkCreateRenderPass) == sizeof(void *), "vkCreateRenderPass POSIX pointer size");
    _Static_assert(sizeof(context->vk.vkDestroyRenderPass) == sizeof(void *), "vkDestroyRenderPass POSIX pointer size");
    _Static_assert(sizeof(context->vk.vkCreateFramebuffer) == sizeof(void *), "vkCreateFramebuffer POSIX pointer size");
    _Static_assert(sizeof(context->vk.vkDestroyFramebuffer) == sizeof(void *), "vkDestroyFramebuffer POSIX pointer size");
    _Static_assert(sizeof(context->vk.vkCreateGraphicsPipelines) == sizeof(void *), "vkCreateGraphicsPipelines POSIX pointer size");
    _Static_assert(sizeof(context->vk.vkCmdBeginRenderPass) == sizeof(void *), "vkCmdBeginRenderPass POSIX pointer size");
    _Static_assert(sizeof(context->vk.vkCmdEndRenderPass) == sizeof(void *), "vkCmdEndRenderPass POSIX pointer size");
    _Static_assert(sizeof(context->vk.vkCmdSetViewport) == sizeof(void *), "vkCmdSetViewport POSIX pointer size");
    _Static_assert(sizeof(context->vk.vkCmdSetScissor) == sizeof(void *), "vkCmdSetScissor POSIX pointer size");
    _Static_assert(sizeof(context->vk.vkCmdBindVertexBuffers) == sizeof(void *), "vkCmdBindVertexBuffers POSIX pointer size");
    _Static_assert(sizeof(context->vk.vkCmdBeginQuery) == sizeof(void *), "vkCmdBeginQuery POSIX pointer size");
    _Static_assert(sizeof(context->vk.vkCmdEndQuery) == sizeof(void *), "vkCmdEndQuery POSIX pointer size");
    _Static_assert(sizeof(context->vk.vkCmdSetStencilReference) == sizeof(void *), "vkCmdSetStencilReference POSIX pointer size");
    _Static_assert(sizeof(context->vk.vkCmdSetBlendConstants) == sizeof(void *), "vkCmdSetBlendConstants POSIX pointer size");
    _Static_assert(sizeof(context->vk.vkCmdDraw) == sizeof(void *), "vkCmdDraw POSIX pointer size");
    _Static_assert(sizeof(context->vk.vkCmdDrawIndirect) == sizeof(void *), "vkCmdDrawIndirect POSIX pointer size");
    _Static_assert(sizeof(context->vk.vkCmdBindIndexBuffer) == sizeof(void *), "vkCmdBindIndexBuffer POSIX pointer size");
    _Static_assert(sizeof(context->vk.vkCmdDrawIndexed) == sizeof(void *), "vkCmdDrawIndexed POSIX pointer size");
    _Static_assert(sizeof(context->vk.vkCmdDrawIndexedIndirect) == sizeof(void *), "vkCmdDrawIndexedIndirect POSIX pointer size");
    _Static_assert(sizeof(context->vk.vkCreateQueryPool) == sizeof(void *), "vkCreateQueryPool POSIX pointer size");
    _Static_assert(sizeof(context->vk.vkDestroyQueryPool) == sizeof(void *), "vkDestroyQueryPool POSIX pointer size");
    _Static_assert(sizeof(context->vk.vkCmdWriteTimestamp) == sizeof(void *), "vkCmdWriteTimestamp POSIX pointer size");
    _Static_assert(sizeof(context->vk.vkCmdResetQueryPool) == sizeof(void *), "vkCmdResetQueryPool POSIX pointer size");
    _Static_assert(sizeof(context->vk.vkCmdCopyQueryPoolResults) == sizeof(void *), "vkCmdCopyQueryPoolResults POSIX pointer size");
    const struct {
        const char *name;
        void *destination;
        size_t destination_size;
    } symbols[] = {
        { "vkCreateInstance", &context->vk.vkCreateInstance, sizeof(context->vk.vkCreateInstance) },
        { "vkDestroyInstance", &context->vk.vkDestroyInstance, sizeof(context->vk.vkDestroyInstance) },
        { "vkEnumeratePhysicalDevices", &context->vk.vkEnumeratePhysicalDevices, sizeof(context->vk.vkEnumeratePhysicalDevices) },
        { "vkGetPhysicalDeviceQueueFamilyProperties", &context->vk.vkGetPhysicalDeviceQueueFamilyProperties, sizeof(context->vk.vkGetPhysicalDeviceQueueFamilyProperties) },
        { "vkGetPhysicalDeviceMemoryProperties", &context->vk.vkGetPhysicalDeviceMemoryProperties, sizeof(context->vk.vkGetPhysicalDeviceMemoryProperties) },
        { "vkGetPhysicalDeviceProperties2", &context->vk.vkGetPhysicalDeviceProperties2, sizeof(context->vk.vkGetPhysicalDeviceProperties2) },
        { "vkGetPhysicalDeviceFeatures2", &context->vk.vkGetPhysicalDeviceFeatures2, sizeof(context->vk.vkGetPhysicalDeviceFeatures2) },
        { "vkEnumerateDeviceExtensionProperties", &context->vk.vkEnumerateDeviceExtensionProperties, sizeof(context->vk.vkEnumerateDeviceExtensionProperties) },
        { "vkCreateDevice", &context->vk.vkCreateDevice, sizeof(context->vk.vkCreateDevice) },
        { "vkDestroyDevice", &context->vk.vkDestroyDevice, sizeof(context->vk.vkDestroyDevice) },
        { "vkGetDeviceQueue", &context->vk.vkGetDeviceQueue, sizeof(context->vk.vkGetDeviceQueue) },
        { "vkCreateCommandPool", &context->vk.vkCreateCommandPool, sizeof(context->vk.vkCreateCommandPool) },
        { "vkDestroyCommandPool", &context->vk.vkDestroyCommandPool, sizeof(context->vk.vkDestroyCommandPool) },
        { "vkAllocateCommandBuffers", &context->vk.vkAllocateCommandBuffers, sizeof(context->vk.vkAllocateCommandBuffers) },
        { "vkFreeCommandBuffers", &context->vk.vkFreeCommandBuffers, sizeof(context->vk.vkFreeCommandBuffers) },
        { "vkCreateFence", &context->vk.vkCreateFence, sizeof(context->vk.vkCreateFence) },
        { "vkDestroyFence", &context->vk.vkDestroyFence, sizeof(context->vk.vkDestroyFence) },
        { "vkGetFenceStatus", &context->vk.vkGetFenceStatus, sizeof(context->vk.vkGetFenceStatus) },
        { "vkWaitForFences", &context->vk.vkWaitForFences, sizeof(context->vk.vkWaitForFences) },
        { "vkResetFences", &context->vk.vkResetFences, sizeof(context->vk.vkResetFences) },
        { "vkQueueSubmit", &context->vk.vkQueueSubmit, sizeof(context->vk.vkQueueSubmit) },
        { "vkQueueWaitIdle", &context->vk.vkQueueWaitIdle, sizeof(context->vk.vkQueueWaitIdle) },
        { "vkBeginCommandBuffer", &context->vk.vkBeginCommandBuffer, sizeof(context->vk.vkBeginCommandBuffer) },
        { "vkEndCommandBuffer", &context->vk.vkEndCommandBuffer, sizeof(context->vk.vkEndCommandBuffer) },
        { "vkCmdBindPipeline", &context->vk.vkCmdBindPipeline, sizeof(context->vk.vkCmdBindPipeline) },
        { "vkCmdDispatch", &context->vk.vkCmdDispatch, sizeof(context->vk.vkCmdDispatch) },
        { "vkCmdDispatchIndirect", &context->vk.vkCmdDispatchIndirect, sizeof(context->vk.vkCmdDispatchIndirect) },
        { "vkCmdCopyBuffer", &context->vk.vkCmdCopyBuffer, sizeof(context->vk.vkCmdCopyBuffer) },
        { "vkCmdFillBuffer", &context->vk.vkCmdFillBuffer, sizeof(context->vk.vkCmdFillBuffer) },
        { "vkCmdPipelineBarrier", &context->vk.vkCmdPipelineBarrier, sizeof(context->vk.vkCmdPipelineBarrier) },
        { "vkCreateBuffer", &context->vk.vkCreateBuffer, sizeof(context->vk.vkCreateBuffer) },
        { "vkDestroyBuffer", &context->vk.vkDestroyBuffer, sizeof(context->vk.vkDestroyBuffer) },
        { "vkGetBufferMemoryRequirements", &context->vk.vkGetBufferMemoryRequirements, sizeof(context->vk.vkGetBufferMemoryRequirements) },
        { "vkAllocateMemory", &context->vk.vkAllocateMemory, sizeof(context->vk.vkAllocateMemory) },
        { "vkFreeMemory", &context->vk.vkFreeMemory, sizeof(context->vk.vkFreeMemory) },
        { "vkBindBufferMemory", &context->vk.vkBindBufferMemory, sizeof(context->vk.vkBindBufferMemory) },
        { "vkMapMemory", &context->vk.vkMapMemory, sizeof(context->vk.vkMapMemory) },
        { "vkUnmapMemory", &context->vk.vkUnmapMemory, sizeof(context->vk.vkUnmapMemory) },
        { "vkCreateShaderModule", &context->vk.vkCreateShaderModule, sizeof(context->vk.vkCreateShaderModule) },
        { "vkDestroyShaderModule", &context->vk.vkDestroyShaderModule, sizeof(context->vk.vkDestroyShaderModule) },
        { "vkCreateDescriptorSetLayout", &context->vk.vkCreateDescriptorSetLayout, sizeof(context->vk.vkCreateDescriptorSetLayout) },
        { "vkDestroyDescriptorSetLayout", &context->vk.vkDestroyDescriptorSetLayout, sizeof(context->vk.vkDestroyDescriptorSetLayout) },
        { "vkCreateDescriptorPool", &context->vk.vkCreateDescriptorPool, sizeof(context->vk.vkCreateDescriptorPool) },
        { "vkDestroyDescriptorPool", &context->vk.vkDestroyDescriptorPool, sizeof(context->vk.vkDestroyDescriptorPool) },
        { "vkAllocateDescriptorSets", &context->vk.vkAllocateDescriptorSets, sizeof(context->vk.vkAllocateDescriptorSets) },
        { "vkUpdateDescriptorSets", &context->vk.vkUpdateDescriptorSets, sizeof(context->vk.vkUpdateDescriptorSets) },
        { "vkCreatePipelineLayout", &context->vk.vkCreatePipelineLayout, sizeof(context->vk.vkCreatePipelineLayout) },
        { "vkDestroyPipelineLayout", &context->vk.vkDestroyPipelineLayout, sizeof(context->vk.vkDestroyPipelineLayout) },
        { "vkCreateComputePipelines", &context->vk.vkCreateComputePipelines, sizeof(context->vk.vkCreateComputePipelines) },
        { "vkDestroyPipeline", &context->vk.vkDestroyPipeline, sizeof(context->vk.vkDestroyPipeline) },
        { "vkCmdBindDescriptorSets", &context->vk.vkCmdBindDescriptorSets, sizeof(context->vk.vkCmdBindDescriptorSets) },
        { "vkGetPhysicalDeviceFormatProperties", &context->vk.vkGetPhysicalDeviceFormatProperties, sizeof(context->vk.vkGetPhysicalDeviceFormatProperties) },
        { "vkGetPhysicalDeviceImageFormatProperties", &context->vk.vkGetPhysicalDeviceImageFormatProperties, sizeof(context->vk.vkGetPhysicalDeviceImageFormatProperties) },
        { "vkCreateImage", &context->vk.vkCreateImage, sizeof(context->vk.vkCreateImage) },
        { "vkDestroyImage", &context->vk.vkDestroyImage, sizeof(context->vk.vkDestroyImage) },
        { "vkGetImageMemoryRequirements", &context->vk.vkGetImageMemoryRequirements, sizeof(context->vk.vkGetImageMemoryRequirements) },
        { "vkBindImageMemory", &context->vk.vkBindImageMemory, sizeof(context->vk.vkBindImageMemory) },
        { "vkCreateImageView", &context->vk.vkCreateImageView, sizeof(context->vk.vkCreateImageView) },
        { "vkDestroyImageView", &context->vk.vkDestroyImageView, sizeof(context->vk.vkDestroyImageView) },
        { "vkCreateSampler", &context->vk.vkCreateSampler, sizeof(context->vk.vkCreateSampler) },
        { "vkDestroySampler", &context->vk.vkDestroySampler, sizeof(context->vk.vkDestroySampler) },
        { "vkCmdCopyBufferToImage", &context->vk.vkCmdCopyBufferToImage, sizeof(context->vk.vkCmdCopyBufferToImage) },
        { "vkCmdCopyImageToBuffer", &context->vk.vkCmdCopyImageToBuffer, sizeof(context->vk.vkCmdCopyImageToBuffer) },
        { "vkCmdCopyImage", &context->vk.vkCmdCopyImage, sizeof(context->vk.vkCmdCopyImage) },
        { "vkCreateRenderPass", &context->vk.vkCreateRenderPass, sizeof(context->vk.vkCreateRenderPass) },
        { "vkDestroyRenderPass", &context->vk.vkDestroyRenderPass, sizeof(context->vk.vkDestroyRenderPass) },
        { "vkCreateFramebuffer", &context->vk.vkCreateFramebuffer, sizeof(context->vk.vkCreateFramebuffer) },
        { "vkDestroyFramebuffer", &context->vk.vkDestroyFramebuffer, sizeof(context->vk.vkDestroyFramebuffer) },
        { "vkCreateGraphicsPipelines", &context->vk.vkCreateGraphicsPipelines, sizeof(context->vk.vkCreateGraphicsPipelines) },
        { "vkCmdBeginRenderPass", &context->vk.vkCmdBeginRenderPass, sizeof(context->vk.vkCmdBeginRenderPass) },
        { "vkCmdEndRenderPass", &context->vk.vkCmdEndRenderPass, sizeof(context->vk.vkCmdEndRenderPass) },
        { "vkCmdSetViewport", &context->vk.vkCmdSetViewport, sizeof(context->vk.vkCmdSetViewport) },
        { "vkCmdSetScissor", &context->vk.vkCmdSetScissor, sizeof(context->vk.vkCmdSetScissor) },
        { "vkCmdBindVertexBuffers", &context->vk.vkCmdBindVertexBuffers, sizeof(context->vk.vkCmdBindVertexBuffers) },
        { "vkCmdBeginQuery", &context->vk.vkCmdBeginQuery, sizeof(context->vk.vkCmdBeginQuery) },
        { "vkCmdEndQuery", &context->vk.vkCmdEndQuery, sizeof(context->vk.vkCmdEndQuery) },
        { "vkCmdSetStencilReference", &context->vk.vkCmdSetStencilReference, sizeof(context->vk.vkCmdSetStencilReference) },
        { "vkCmdSetBlendConstants", &context->vk.vkCmdSetBlendConstants, sizeof(context->vk.vkCmdSetBlendConstants) },
        { "vkCmdDraw", &context->vk.vkCmdDraw, sizeof(context->vk.vkCmdDraw) },
        { "vkCmdDrawIndirect", &context->vk.vkCmdDrawIndirect, sizeof(context->vk.vkCmdDrawIndirect) },
        { "vkCmdBindIndexBuffer", &context->vk.vkCmdBindIndexBuffer, sizeof(context->vk.vkCmdBindIndexBuffer) },
        { "vkCmdDrawIndexed", &context->vk.vkCmdDrawIndexed, sizeof(context->vk.vkCmdDrawIndexed) },
        { "vkCmdDrawIndexedIndirect", &context->vk.vkCmdDrawIndexedIndirect, sizeof(context->vk.vkCmdDrawIndexedIndirect) },
        { "vkCreateQueryPool", &context->vk.vkCreateQueryPool, sizeof(context->vk.vkCreateQueryPool) },
        { "vkDestroyQueryPool", &context->vk.vkDestroyQueryPool, sizeof(context->vk.vkDestroyQueryPool) },
        { "vkCmdWriteTimestamp", &context->vk.vkCmdWriteTimestamp, sizeof(context->vk.vkCmdWriteTimestamp) },
        { "vkCmdResetQueryPool", &context->vk.vkCmdResetQueryPool, sizeof(context->vk.vkCmdResetQueryPool) },
        { "vkCmdCopyQueryPoolResults", &context->vk.vkCmdCopyQueryPoolResults, sizeof(context->vk.vkCmdCopyQueryPoolResults) },
    };
    for (size_t i = 0; i < sizeof(symbols) / sizeof(symbols[0]); ++i) {
        doe_vk_status status = doe_vk_load_symbol(context->library, symbols[i].name,
            symbols[i].destination, symbols[i].destination_size, error);
        if (status != DOE_VK_OK) return status;
    }
    return DOE_VK_OK;
}

static doe_vk_status choose_queue(doe_vk_context *context, const doe_vk_options *options, doe_vk_error *error) {
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
            if ((options->require_graphics_queue || options->surface_platforms != 0) && !(queues[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) continue;
            if (selected == UINT32_MAX) selected = i;
            if (!(queues[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
                selected = i;
                break;
            }
        }
    }
    if (selected != UINT32_MAX) {
        context->transfer_granularity = queues[selected].minImageTransferGranularity;
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
#if defined(_WIN32)
    /* The system loader is installed by the Vulkan runtime. Never search the
     * working directory or silently substitute another implementation. */
    const char *library_name = "vulkan-1.dll";
    context->library = LoadLibraryExW(L"vulkan-1.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
#else
    const char *library_name = "libvulkan.so.1";
    context->library = dlopen(library_name, RTLD_NOW | RTLD_LOCAL);
#endif
    doe_vk_status status = DOE_VK_LOADER_UNAVAILABLE;
    if (context->library == NULL) {
#if defined(_WIN32)
        win32_loader_error(library_name, error);
#else
        doe_vk_fail(status, library_name, error);
#endif
        goto failed;
    }
    status = load_functions(context, error);
    if (status != DOE_VK_OK) goto failed;
    doe_vk_instance_extensions instance_extensions;
    status = doe_vk_surface_instance_extensions(context, options->surface_platforms, &instance_extensions, error);
    if (status != DOE_VK_OK) goto failed;
    const VkApplicationInfo application = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = (const uint8_t *)"Doe experimental C",
        .pEngineName = (const uint8_t *)"Doe",
        .apiVersion = VK_API_VERSION_1_2
    };
    const VkInstanceCreateInfo instance = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &application,
        .enabledExtensionCount = instance_extensions.count,
        .ppEnabledExtensionNames = instance_extensions.count != 0 ? instance_extensions.names : NULL
    };
    status = doe_vk_result(context, context->vk.vkCreateInstance(&instance, NULL, &context->instance), "vkCreateInstance", error);
    if (status != DOE_VK_OK) goto failed;
    status = doe_vk_surface_load_functions(context, options->surface_platforms, false, error);
    if (status != DOE_VK_OK) goto failed;
    status = doe_vk_choose_device(context, options, error);
    if (status != DOE_VK_OK) goto failed;
    status = choose_queue(context, options, error);
    if (status != DOE_VK_OK) goto failed;
    doe_vk_feature_selection features;
    status = doe_vk_prepare_features(context, options, &features, error);
    if (status != DOE_VK_OK) goto failed;
    float priority = 1.0f;
    const VkDeviceQueueCreateInfo queue = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = context->caps.queue_family,
        .queueCount = 1,
        .pQueuePriorities = &priority
    };
    const uint8_t *device_extensions[5];
    uint32_t extension_count = 0;
    void *feature_chain = NULL;
    if (features.extension != NULL) {
        device_extensions[extension_count++] = features.extension;
        feature_chain = &features.demote;
    }
    if (features.maintenance5_enabled) {
        device_extensions[extension_count++] = (const uint8_t *)"VK_KHR_dynamic_rendering";
        device_extensions[extension_count++] = (const uint8_t *)"VK_KHR_maintenance5";
        features.maintenance5.pNext = feature_chain;
        feature_chain = &features.maintenance5;
    }
    if (features.surface_enabled) {
        device_extensions[extension_count++] = (const uint8_t *)"VK_KHR_swapchain";
        device_extensions[extension_count++] = (const uint8_t *)"VK_EXT_swapchain_maintenance1";
        features.swapchain.pNext = feature_chain;
        feature_chain = &features.swapchain;
    }
    const VkDeviceCreateInfo device = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queue,
        .pEnabledFeatures = &features.base,
        .pNext = feature_chain,
        .enabledExtensionCount = extension_count,
        .ppEnabledExtensionNames = extension_count != 0 ? device_extensions : NULL
    };
    status = doe_vk_result(context, context->vk.vkCreateDevice(context->physical, &device, NULL, &context->device), "vkCreateDevice", error);
    if (status != DOE_VK_OK) goto failed;
    status = doe_vk_surface_load_functions(context, options->surface_platforms, true, error);
    if (status != DOE_VK_OK) goto failed;
    context->vk.vkGetDeviceQueue(context->device, context->caps.queue_family, 0, &context->queue);
    if (context->queue == NULL) {
        status = doe_vk_fail(DOE_VK_NATIVE_FAILURE, "vkGetDeviceQueue", error);
        goto failed;
    }
    context->caps.surface_platforms = options->surface_platforms;
    context->caps.robust_buffer_access = true;
    context->caps.shader_demote_to_helper_invocation = features.extension != NULL;
    context->caps.maintenance5 = features.maintenance5_enabled;
    context->caps.full_draw_index_uint32 = features.base.fullDrawIndexUint32 != 0;
    context->caps.image_cube_array = features.base.imageCubeArray != 0;
    context->caps.independent_blend = features.base.independentBlend != 0;
    context->caps.sampler_anisotropy = features.base.samplerAnisotropy != 0;
    context->caps.storage_image_extended_formats = features.base.shaderStorageImageExtendedFormats != 0;
    context->caps.fragment_stores_and_atomics = features.base.fragmentStoresAndAtomics != 0;
    context->caps.draw_indirect_first_instance = features.base.drawIndirectFirstInstance != 0;
    context->caps.depth_clamp = features.base.depthClamp != 0;
    context->caps.texture_compression_bc = features.base.textureCompressionBC != 0;
    context->caps.max_sampler_anisotropy = context->caps.sampler_anisotropy ?
        context->properties.limits.maxSamplerAnisotropy : 0.0f;
    *out = context;
    return doe_vk_fail(DOE_VK_OK, "context ready", error);
failed:
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
    if (context->library != NULL) {
#if defined(_WIN32)
        (void)FreeLibrary((HMODULE)context->library);
#else
        (void)dlclose(context->library);
#endif
    }
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
