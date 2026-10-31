/* Diagnostic, not part of Doe: median host memcpy time of 1 MiB into a persistently
 * mapped allocation of each host-visible memory type on the first physical device. */
#define _CRT_SECURE_NO_WARNINGS
#include <vulkan/vulkan.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int cmp(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return x < y ? -1 : x > y; }
int main(void) {
    VkApplicationInfo app = {VK_STRUCTURE_TYPE_APPLICATION_INFO, 0, "memprobe", 0, "", 0, VK_API_VERSION_1_2};
    VkInstanceCreateInfo ici = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, 0, 0, &app};
    VkInstance inst; if (vkCreateInstance(&ici, 0, &inst)) return 1;
    uint32_t n = 8; VkPhysicalDevice pds[8]; vkEnumeratePhysicalDevices(inst, &n, pds);
    VkPhysicalDevice pd = 0; VkPhysicalDeviceProperties props;
    for (uint32_t i = 0; i < n; ++i) { vkGetPhysicalDeviceProperties(pds[i], &props); if (props.deviceType != VK_PHYSICAL_DEVICE_TYPE_CPU) { pd = pds[i]; break; } }
    if (!pd) return 2;
    VkPhysicalDeviceMemoryProperties mp; vkGetPhysicalDeviceMemoryProperties(pd, &mp);
    float prio = 1; VkDeviceQueueCreateInfo q = {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, 0, 0, 0, 1, &prio};
    VkDeviceCreateInfo dci = {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, 0, 0, 1, &q};
    VkDevice dev; if (vkCreateDevice(pd, &dci, 0, &dev)) return 3;
    const size_t size = 1 << 20; char *src = malloc(size); for (size_t i = 0; i < size; ++i) src[i] = (char)(i * 131);
    LARGE_INTEGER f; QueryPerformanceFrequency(&f);
    printf("{\"device\":\"%s\",\"types\":[", props.deviceName);
    int first = 1;
    for (uint32_t t = 0; t < mp.memoryTypeCount && t < 8; ++t) {
        VkMemoryPropertyFlags fl = mp.memoryTypes[t].propertyFlags;
        if (!(fl & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) continue;
        VkMemoryAllocateInfo ai = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, 0, size, t};
        VkDeviceMemory mem; if (vkAllocateMemory(dev, &ai, 0, &mem)) continue;
        void *p; vkMapMemory(dev, mem, 0, size, 0, &p);
        double us[201], zus[51];
        for (int r = 0; r < 201; ++r) { LARGE_INTEGER a, b; QueryPerformanceCounter(&a); memcpy(p, src, size); QueryPerformanceCounter(&b); us[r] = (b.QuadPart - a.QuadPart) * 1e6 / f.QuadPart; }
        for (int r = 0; r < 51; ++r) { LARGE_INTEGER a, b; QueryPerformanceCounter(&a); memset(p, 0, size); QueryPerformanceCounter(&b); zus[r] = (b.QuadPart - a.QuadPart) * 1e6 / f.QuadPart; }
        qsort(us, 201, sizeof(double), cmp); qsort(zus, 51, sizeof(double), cmp);
        printf("%s{\"type\":%u,\"heap\":%u,\"flags\":\"0x%x\",\"memcpy1MiBMedianUs\":%.1f,\"memcpyP95Us\":%.1f,\"memset1MiBMedianUs\":%.1f}", first ? "" : ",", t, mp.memoryTypes[t].heapIndex, fl, us[100], us[190], zus[25]);
        first = 0;
        vkUnmapMemory(dev, mem); vkFreeMemory(dev, mem, 0);
    }
    printf("]}\n");
    vkDestroyDevice(dev, 0); vkDestroyInstance(inst, 0); return 0;
}
