/* Diagnostic, not part of Doe: median submit-to-fence time of one vkCmdCopyBuffer of
 * 4 MiB between two buffers allocated from the same memory type, per type. */
#define _CRT_SECURE_NO_WARNINGS
#include <vulkan/vulkan.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
static int cmp(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return x < y ? -1 : x > y; }
int main(void) {
    VkApplicationInfo app = {VK_STRUCTURE_TYPE_APPLICATION_INFO, 0, "gpuprobe", 0, "", 0, VK_API_VERSION_1_2};
    VkInstanceCreateInfo ici = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, 0, 0, &app};
    VkInstance inst; if (vkCreateInstance(&ici, 0, &inst)) return 1;
    uint32_t n = 8; VkPhysicalDevice pds[8]; vkEnumeratePhysicalDevices(inst, &n, pds);
    VkPhysicalDevice pd = 0; VkPhysicalDeviceProperties props;
    for (uint32_t i = 0; i < n; ++i) { vkGetPhysicalDeviceProperties(pds[i], &props); if (props.deviceType != VK_PHYSICAL_DEVICE_TYPE_CPU) { pd = pds[i]; break; } }
    if (!pd) return 2;
    VkPhysicalDeviceMemoryProperties mp; vkGetPhysicalDeviceMemoryProperties(pd, &mp);
    uint32_t qn = 16; VkQueueFamilyProperties qf[16]; vkGetPhysicalDeviceQueueFamilyProperties(pd, &qn, qf);
    uint32_t family = 0; for (uint32_t i = 0; i < qn; ++i) if (qf[i].queueFlags & VK_QUEUE_COMPUTE_BIT) { family = i; break; }
    float prio = 1; VkDeviceQueueCreateInfo q = {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, 0, 0, family, 1, &prio};
    VkDeviceCreateInfo dci = {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, 0, 0, 1, &q};
    VkDevice dev; if (vkCreateDevice(pd, &dci, 0, &dev)) return 3;
    VkQueue queue; vkGetDeviceQueue(dev, family, 0, &queue);
    VkCommandPoolCreateInfo pci = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, 0, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, family};
    VkCommandPool pool; vkCreateCommandPool(dev, &pci, 0, &pool);
    VkCommandBufferAllocateInfo cai = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, 0, pool, VK_COMMAND_BUFFER_LEVEL_PRIMARY, 1};
    VkCommandBuffer cmd; vkAllocateCommandBuffers(dev, &cai, &cmd);
    VkFenceCreateInfo fci = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO}; VkFence fence; vkCreateFence(dev, &fci, 0, &fence);
    const VkDeviceSize size = 4u << 20; LARGE_INTEGER f; QueryPerformanceFrequency(&f);
    printf("{\"device\":\"%s\",\"copy4MiB\":[", props.deviceName);
    const uint32_t types[] = {0, 2, 3}; int first = 1;
    for (int k = 0; k < 3; ++k) {
        uint32_t t = types[k]; VkBuffer b[2]; VkDeviceMemory m[2]; int ok = 1;
        for (int j = 0; j < 2; ++j) {
            VkBufferCreateInfo bci = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, 0, 0, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT};
            vkCreateBuffer(dev, &bci, 0, &b[j]); VkMemoryRequirements r; vkGetBufferMemoryRequirements(dev, b[j], &r);
            if (!(r.memoryTypeBits & (1u << t))) ok = 0;
            VkMemoryAllocateInfo ai = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, 0, r.size, t};
            if (!ok || vkAllocateMemory(dev, &ai, 0, &m[j])) { ok = 0; break; }
            vkBindBufferMemory(dev, b[j], m[j], 0);
        }
        if (!ok) continue;
        double us[61];
        for (int r = 0; r < 61; ++r) {
            VkCommandBufferBeginInfo bi = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, 0, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
            vkBeginCommandBuffer(cmd, &bi); VkBufferCopy c = {0, 0, size}; vkCmdCopyBuffer(cmd, b[0], b[1], 1, &c); vkEndCommandBuffer(cmd);
            VkSubmitInfo si = {VK_STRUCTURE_TYPE_SUBMIT_INFO, 0, 0, 0, 0, 1, &cmd};
            LARGE_INTEGER a, z; QueryPerformanceCounter(&a);
            vkQueueSubmit(queue, 1, &si, fence); vkWaitForFences(dev, 1, &fence, VK_TRUE, UINT64_MAX);
            QueryPerformanceCounter(&z); us[r] = (z.QuadPart - a.QuadPart) * 1e6 / f.QuadPart;
            vkResetFences(dev, 1, &fence); vkResetCommandBuffer(cmd, 0);
        }
        qsort(us + 1, 60, sizeof(double), cmp);
        printf("%s{\"type\":%u,\"heap\":%u,\"flags\":\"0x%x\",\"submitToFenceMedianUs\":%.1f,\"p95Us\":%.1f}", first ? "" : ",", t, mp.memoryTypes[t].heapIndex, mp.memoryTypes[t].propertyFlags, us[31], us[58]);
        first = 0;
        for (int j = 0; j < 2; ++j) { vkDestroyBuffer(dev, b[j], 0); vkFreeMemory(dev, m[j], 0); }
    }
    printf("]}\n");
    vkDestroyFence(dev, fence, 0); vkDestroyCommandPool(dev, pool, 0); vkDestroyDevice(dev, 0); vkDestroyInstance(inst, 0);
    return 0;
}
