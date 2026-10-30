/* Diagnostic Vulkan layer: counts selected device-level calls and the memory
 * type of every vkAllocateMemory, printing totals at vkDestroyDevice to the
 * file named by COUNT_LAYER_OUT. Not part of Doe. */
#define _GNU_SOURCE
#include <vulkan/vulkan.h>
#include <vulkan/vk_layer.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FN_LIST(X) \
    X(QueueSubmit) X(BeginCommandBuffer) X(EndCommandBuffer) X(AllocateCommandBuffers) \
    X(ResetCommandPool) X(ResetCommandBuffer) X(CreateCommandPool) X(DestroyCommandPool) X(CreateFence) X(DestroyFence) \
    X(ResetFences) X(WaitForFences) X(GetFenceStatus) X(CreateDescriptorPool) X(DestroyDescriptorPool) \
    X(AllocateDescriptorSets) X(FreeDescriptorSets) X(UpdateDescriptorSets) X(ResetDescriptorPool) \
    X(CmdPipelineBarrier) X(CmdBindPipeline) X(CmdBindDescriptorSets) X(CmdDispatch) X(CmdFillBuffer) \
    X(CmdCopyBuffer) X(CreateBuffer) X(DestroyBuffer) X(AllocateMemory) X(FreeMemory) X(MapMemory) X(UnmapMemory) \
    X(FlushMappedMemoryRanges) X(InvalidateMappedMemoryRanges) X(GetSemaphoreCounterValue) X(WaitSemaphores) \
    X(CmdPushConstants) X(CreateSemaphore) X(QueueWaitIdle) X(DeviceWaitIdle) X(CreateComputePipelines) \
    X(CreateShaderModule) X(CreatePipelineLayout) X(CreateDescriptorSetLayout) X(CmdPipelineBarrier2) X(QueueSubmit2)

#define DECL(n) static PFN_vk##n next_##n; static unsigned long count_##n;
FN_LIST(DECL)
static PFN_vkGetDeviceProcAddr next_gdpa;
static PFN_vkGetInstanceProcAddr next_gipa;
static PFN_vkDestroyDevice next_DestroyDevice;
static PFN_vkGetPhysicalDeviceMemoryProperties next_GPDMP;
static VkPhysicalDeviceMemoryProperties memory_props;
static unsigned long alloc_by_flags[64];
static VkDeviceSize alloc_bytes_by_flags[64];

#define FWD_VOID(n, params, args) static VKAPI_ATTR void VKAPI_CALL w_##n params { ++count_##n; next_##n args; }
#define FWD_RES(n, params, args) static VKAPI_ATTR VkResult VKAPI_CALL w_##n params { ++count_##n; return next_##n args; }
FWD_RES(QueueSubmit, (VkQueue q, uint32_t c, const VkSubmitInfo *s, VkFence f), (q, c, s, f))
FWD_RES(QueueSubmit2, (VkQueue q, uint32_t c, const VkSubmitInfo2 *s, VkFence f), (q, c, s, f))
FWD_RES(BeginCommandBuffer, (VkCommandBuffer b, const VkCommandBufferBeginInfo *i), (b, i))
FWD_RES(EndCommandBuffer, (VkCommandBuffer b), (b))
FWD_RES(AllocateCommandBuffers, (VkDevice d, const VkCommandBufferAllocateInfo *i, VkCommandBuffer *o), (d, i, o))
FWD_RES(ResetCommandPool, (VkDevice d, VkCommandPool p, VkCommandPoolResetFlags f), (d, p, f))
FWD_RES(ResetCommandBuffer, (VkCommandBuffer b, VkCommandBufferResetFlags f), (b, f))
FWD_RES(CreateCommandPool, (VkDevice d, const VkCommandPoolCreateInfo *i, const VkAllocationCallbacks *a, VkCommandPool *o), (d, i, a, o))
FWD_VOID(DestroyCommandPool, (VkDevice d, VkCommandPool p, const VkAllocationCallbacks *a), (d, p, a))
FWD_RES(CreateFence, (VkDevice d, const VkFenceCreateInfo *i, const VkAllocationCallbacks *a, VkFence *o), (d, i, a, o))
FWD_VOID(DestroyFence, (VkDevice d, VkFence f, const VkAllocationCallbacks *a), (d, f, a))
FWD_RES(ResetFences, (VkDevice d, uint32_t c, const VkFence *f), (d, c, f))
FWD_RES(WaitForFences, (VkDevice d, uint32_t c, const VkFence *f, VkBool32 all, uint64_t t), (d, c, f, all, t))
FWD_RES(GetFenceStatus, (VkDevice d, VkFence f), (d, f))
FWD_RES(CreateDescriptorPool, (VkDevice d, const VkDescriptorPoolCreateInfo *i, const VkAllocationCallbacks *a, VkDescriptorPool *o), (d, i, a, o))
FWD_VOID(DestroyDescriptorPool, (VkDevice d, VkDescriptorPool p, const VkAllocationCallbacks *a), (d, p, a))
FWD_RES(AllocateDescriptorSets, (VkDevice d, const VkDescriptorSetAllocateInfo *i, VkDescriptorSet *o), (d, i, o))
FWD_RES(FreeDescriptorSets, (VkDevice d, VkDescriptorPool p, uint32_t c, const VkDescriptorSet *s), (d, p, c, s))
FWD_VOID(UpdateDescriptorSets, (VkDevice d, uint32_t wc, const VkWriteDescriptorSet *w, uint32_t cc, const VkCopyDescriptorSet *c), (d, wc, w, cc, c))
FWD_RES(ResetDescriptorPool, (VkDevice d, VkDescriptorPool p, VkDescriptorPoolResetFlags f), (d, p, f))
FWD_VOID(CmdPipelineBarrier, (VkCommandBuffer b, VkPipelineStageFlags s, VkPipelineStageFlags t, VkDependencyFlags f, uint32_t mc, const VkMemoryBarrier *m, uint32_t bc, const VkBufferMemoryBarrier *bb, uint32_t ic, const VkImageMemoryBarrier *ib), (b, s, t, f, mc, m, bc, bb, ic, ib))
FWD_VOID(CmdPipelineBarrier2, (VkCommandBuffer b, const VkDependencyInfo *d), (b, d))
FWD_VOID(CmdBindPipeline, (VkCommandBuffer b, VkPipelineBindPoint p, VkPipeline l), (b, p, l))
FWD_VOID(CmdBindDescriptorSets, (VkCommandBuffer b, VkPipelineBindPoint p, VkPipelineLayout l, uint32_t f, uint32_t c, const VkDescriptorSet *s, uint32_t dc, const uint32_t *o), (b, p, l, f, c, s, dc, o))
FWD_VOID(CmdDispatch, (VkCommandBuffer b, uint32_t x, uint32_t y, uint32_t z), (b, x, y, z))
FWD_VOID(CmdFillBuffer, (VkCommandBuffer b, VkBuffer d, VkDeviceSize o, VkDeviceSize s, uint32_t v), (b, d, o, s, v))
FWD_VOID(CmdCopyBuffer, (VkCommandBuffer b, VkBuffer s, VkBuffer d, uint32_t c, const VkBufferCopy *r), (b, s, d, c, r))
FWD_RES(CreateBuffer, (VkDevice d, const VkBufferCreateInfo *i, const VkAllocationCallbacks *a, VkBuffer *o), (d, i, a, o))
FWD_VOID(DestroyBuffer, (VkDevice d, VkBuffer b, const VkAllocationCallbacks *a), (d, b, a))
FWD_VOID(FreeMemory, (VkDevice d, VkDeviceMemory m, const VkAllocationCallbacks *a), (d, m, a))
FWD_RES(MapMemory, (VkDevice d, VkDeviceMemory m, VkDeviceSize o, VkDeviceSize s, VkMemoryMapFlags f, void **p), (d, m, o, s, f, p))
FWD_VOID(UnmapMemory, (VkDevice d, VkDeviceMemory m), (d, m))
FWD_RES(FlushMappedMemoryRanges, (VkDevice d, uint32_t c, const VkMappedMemoryRange *r), (d, c, r))
FWD_RES(InvalidateMappedMemoryRanges, (VkDevice d, uint32_t c, const VkMappedMemoryRange *r), (d, c, r))
FWD_RES(GetSemaphoreCounterValue, (VkDevice d, VkSemaphore s, uint64_t *v), (d, s, v))
FWD_RES(WaitSemaphores, (VkDevice d, const VkSemaphoreWaitInfo *i, uint64_t t), (d, i, t))
FWD_VOID(CmdPushConstants, (VkCommandBuffer b, VkPipelineLayout l, VkShaderStageFlags s, uint32_t o, uint32_t z, const void *v), (b, l, s, o, z, v))
FWD_RES(CreateSemaphore, (VkDevice d, const VkSemaphoreCreateInfo *i, const VkAllocationCallbacks *a, VkSemaphore *o), (d, i, a, o))
FWD_RES(QueueWaitIdle, (VkQueue q), (q))
FWD_RES(DeviceWaitIdle, (VkDevice d), (d))
FWD_RES(CreateComputePipelines, (VkDevice d, VkPipelineCache c, uint32_t n, const VkComputePipelineCreateInfo *i, const VkAllocationCallbacks *a, VkPipeline *o), (d, c, n, i, a, o))
FWD_RES(CreateShaderModule, (VkDevice d, const VkShaderModuleCreateInfo *i, const VkAllocationCallbacks *a, VkShaderModule *o), (d, i, a, o))
FWD_RES(CreatePipelineLayout, (VkDevice d, const VkPipelineLayoutCreateInfo *i, const VkAllocationCallbacks *a, VkPipelineLayout *o), (d, i, a, o))
FWD_RES(CreateDescriptorSetLayout, (VkDevice d, const VkDescriptorSetLayoutCreateInfo *i, const VkAllocationCallbacks *a, VkDescriptorSetLayout *o), (d, i, a, o))

static VKAPI_ATTR VkResult VKAPI_CALL w_AllocateMemory(VkDevice d, const VkMemoryAllocateInfo *i,
    const VkAllocationCallbacks *a, VkDeviceMemory *o) {
    ++count_AllocateMemory;
    if (i->memoryTypeIndex < memory_props.memoryTypeCount) {
        uint32_t flags = memory_props.memoryTypes[i->memoryTypeIndex].propertyFlags & 0x1f;
        uint32_t heap = memory_props.memoryTypes[i->memoryTypeIndex].heapIndex & 1;
        ++alloc_by_flags[flags | heap << 5];
        alloc_bytes_by_flags[flags | heap << 5] += i->allocationSize;
    }
    return next_AllocateMemory(d, i, a, o);
}

static VKAPI_ATTR void VKAPI_CALL w_DestroyDevice(VkDevice d, const VkAllocationCallbacks *a) {
    const char *path = getenv("COUNT_LAYER_OUT");
    FILE *f = path ? fopen(path, "a") : stderr;
    if (f) {
        fprintf(f, "{");
#define PRINT(n) fprintf(f, "\"%s\":%lu,", #n, count_##n);
        FN_LIST(PRINT)
        fprintf(f, "\"alloc\":{");
        int first = 1;
        for (int k = 0; k < 64; ++k) if (alloc_by_flags[k]) {
            fprintf(f, "%s\"heap%d_flags0x%x\":[%lu,%llu]", first ? "" : ",", k >> 5, k & 0x1f, alloc_by_flags[k],
                (unsigned long long)alloc_bytes_by_flags[k]);
            first = 0;
        }
        fprintf(f, "}}\n");
        if (f != stderr) fclose(f);
    }
    next_DestroyDevice(d, a);
}

static PFN_vkVoidFunction lookup(const char *name) {
#define MATCH(n) if (!strcmp(name, "vk" #n)) return next_##n ? (PFN_vkVoidFunction)w_##n : NULL;
    FN_LIST(MATCH)
    if (!strcmp(name, "vkDestroyDevice")) return (PFN_vkVoidFunction)w_DestroyDevice;
    return NULL;
}

static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL layer_GetDeviceProcAddr(VkDevice d, const char *name);
static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL layer_GetInstanceProcAddr(VkInstance i, const char *name);

static VKAPI_ATTR VkResult VKAPI_CALL layer_CreateInstance(const VkInstanceCreateInfo *ci,
    const VkAllocationCallbacks *a, VkInstance *out) {
    VkLayerInstanceCreateInfo *chain = (VkLayerInstanceCreateInfo *)ci->pNext;
    while (chain && !(chain->sType == VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO && chain->function == VK_LAYER_LINK_INFO))
        chain = (VkLayerInstanceCreateInfo *)chain->pNext;
    if (!chain) return VK_ERROR_INITIALIZATION_FAILED;
    next_gipa = chain->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    chain->u.pLayerInfo = chain->u.pLayerInfo->pNext;
    PFN_vkCreateInstance create = (PFN_vkCreateInstance)next_gipa(NULL, "vkCreateInstance");
    VkResult r = create(ci, a, out);
    if (r == VK_SUCCESS) next_GPDMP = (PFN_vkGetPhysicalDeviceMemoryProperties)next_gipa(*out, "vkGetPhysicalDeviceMemoryProperties");
    return r;
}

static VKAPI_ATTR VkResult VKAPI_CALL layer_CreateDevice(VkPhysicalDevice p, const VkDeviceCreateInfo *ci,
    const VkAllocationCallbacks *a, VkDevice *out) {
    VkLayerDeviceCreateInfo *chain = (VkLayerDeviceCreateInfo *)ci->pNext;
    while (chain && !(chain->sType == VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO && chain->function == VK_LAYER_LINK_INFO))
        chain = (VkLayerDeviceCreateInfo *)chain->pNext;
    if (!chain) return VK_ERROR_INITIALIZATION_FAILED;
    PFN_vkGetInstanceProcAddr gipa = chain->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    next_gdpa = chain->u.pLayerInfo->pfnNextGetDeviceProcAddr;
    chain->u.pLayerInfo = chain->u.pLayerInfo->pNext;
    PFN_vkCreateDevice create = (PFN_vkCreateDevice)gipa(NULL, "vkCreateDevice");
    VkResult r = create(p, ci, a, out);
    if (r != VK_SUCCESS) return r;
    if (next_GPDMP) next_GPDMP(p, &memory_props);
#define LOAD(n) next_##n = (PFN_vk##n)next_gdpa(*out, "vk" #n);
    FN_LIST(LOAD)
    next_DestroyDevice = (PFN_vkDestroyDevice)next_gdpa(*out, "vkDestroyDevice");
    return r;
}

static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL layer_GetDeviceProcAddr(VkDevice d, const char *name) {
    if (!strcmp(name, "vkGetDeviceProcAddr")) return (PFN_vkVoidFunction)layer_GetDeviceProcAddr;
    PFN_vkVoidFunction f = lookup(name);
    return f ? f : next_gdpa(d, name);
}

static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL layer_GetInstanceProcAddr(VkInstance i, const char *name) {
    if (!strcmp(name, "vkGetInstanceProcAddr")) return (PFN_vkVoidFunction)layer_GetInstanceProcAddr;
    if (!strcmp(name, "vkCreateInstance")) return (PFN_vkVoidFunction)layer_CreateInstance;
    if (!strcmp(name, "vkCreateDevice")) return (PFN_vkVoidFunction)layer_CreateDevice;
    if (!strcmp(name, "vkGetDeviceProcAddr")) return (PFN_vkVoidFunction)layer_GetDeviceProcAddr;
    PFN_vkVoidFunction f = lookup(name);
    if (f) return f;
    return next_gipa ? next_gipa(i, name) : NULL;
}

__attribute__((visibility("default"))) VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance i, const char *n) {
    return layer_GetInstanceProcAddr(i, n);
}
__attribute__((visibility("default"))) VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(VkDevice d, const char *n) {
    return layer_GetDeviceProcAddr(d, n);
}
