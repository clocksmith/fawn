/* Selected ABI translated from the retained Doe Zig Vulkan declarations.
 * Source commit: 7b709f49. See ABI_PROVENANCE.json. Linux LP64 only.
 * No SDK/header dependency or runtime Zig dependency. Repository license applies. */
#ifndef DOE_VK_ABI_TYPES_H
#define DOE_VK_ABI_TYPES_H
#include <stddef.h>
#include <stdint.h>

typedef int32_t VkResult;
typedef uint32_t VkBool32;
typedef uint32_t VkFlags;
typedef uint64_t VkDeviceSize;
typedef int32_t VkStructureType;
typedef struct VkInstance_T *VkInstance;
typedef struct VkPhysicalDevice_T *VkPhysicalDevice;
typedef struct VkDevice_T *VkDevice;
typedef struct VkQueue_T *VkQueue;
typedef struct VkCommandBuffer_T *VkCommandBuffer;
typedef struct VkAllocationCallbacks_T VkAllocationCallbacks;
typedef uint64_t VkImage;
typedef uint64_t VkImageView;
typedef uint64_t VkSampler;
typedef uint64_t VkRenderPass;
typedef uint64_t VkFramebuffer;
typedef uint64_t VkPipelineCache;
typedef uint64_t VkCommandPool;
typedef uint64_t VkFence;
typedef uint64_t VkBuffer;
typedef uint64_t VkDeviceMemory;
typedef uint64_t VkShaderModule;
typedef uint64_t VkPipelineLayout;
typedef uint64_t VkPipeline;
typedef uint64_t VkDescriptorSetLayout;
typedef uint64_t VkDescriptorPool;
typedef uint64_t VkDescriptorSet;

_Static_assert(sizeof(void *) == 8 && sizeof(size_t) == 8, "Vulkan ABI requires Linux LP64");
#endif
