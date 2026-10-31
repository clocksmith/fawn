/* Diagnostic, not part of Doe: median submit-to-fence time of the image-invert dispatch
 * (16384 x 64 invocations over 4 MiB) for a captured SPIR-V module, with the two storage
 * buffers in a chosen memory type. Usage: dispatchprobe <module.spv> <memory type>...
 * Binding 0 is the 16-byte uniform (pixel_count = 1048576, host-visible); every other
 * descriptor binding is a 4 MiB storage buffer in the chosen type. */
#define _CRT_SECURE_NO_WARNINGS
#include <vulkan/vulkan.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int cmp(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return x < y ? -1 : x > y; }
typedef struct { uint32_t binding, set, var, ptr_class; } binding_t;
int main(int argc, char **argv) {
    if (argc < 3) return 2;
    FILE *f = fopen(argv[1], "rb"); if (!f) return 3;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    uint32_t *code = malloc((size_t)n); fread(code, 1, (size_t)n, f); fclose(f);
    size_t words = (size_t)n / 4; char entry[64] = "main";
    binding_t b[16] = {0}; uint32_t nb = 0; uint32_t ptrclass[4096] = {0}, varptr[4096] = {0};
    uint32_t setof[4096], bindof[4096]; memset(setof, 0xff, sizeof setof); memset(bindof, 0xff, sizeof bindof);
    for (size_t i = 5; i < words;) {
        uint32_t op = code[i] & 0xffff, len = code[i] >> 16; if (!len) break;
        if (op == 15) strncpy(entry, (const char *)&code[i + 3], 63);
        if (op == 71 && len >= 4 && code[i + 1] < 4096) { if (code[i + 2] == 33) bindof[code[i + 1]] = code[i + 3]; if (code[i + 2] == 34) setof[code[i + 1]] = code[i + 3]; }
        if (op == 32 && code[i + 1] < 4096) ptrclass[code[i + 1]] = code[i + 2];
        if (op == 59 && code[i + 2] < 4096) varptr[code[i + 2]] = code[i + 1];
        i += len;
    }
    for (uint32_t id = 0; id < 4096; ++id) if (bindof[id] != 0xffffffffu && nb < 16) b[nb++] = (binding_t){bindof[id], setof[id], id, ptrclass[varptr[id]]};
    VkApplicationInfo app = {VK_STRUCTURE_TYPE_APPLICATION_INFO, 0, "dispatchprobe", 0, "", 0, VK_API_VERSION_1_2};
    VkInstanceCreateInfo ici = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, 0, 0, &app};
    VkInstance inst; if (vkCreateInstance(&ici, 0, &inst)) return 4;
    uint32_t pn = 8; VkPhysicalDevice pds[8]; vkEnumeratePhysicalDevices(inst, &pn, pds);
    VkPhysicalDevice pd = 0; VkPhysicalDeviceProperties props;
    for (uint32_t i = 0; i < pn; ++i) { vkGetPhysicalDeviceProperties(pds[i], &props); if (props.deviceType != VK_PHYSICAL_DEVICE_TYPE_CPU) { pd = pds[i]; break; } }
    VkPhysicalDeviceMemoryProperties mp; vkGetPhysicalDeviceMemoryProperties(pd, &mp);
    uint32_t qn = 16; VkQueueFamilyProperties qf[16]; vkGetPhysicalDeviceQueueFamilyProperties(pd, &qn, qf);
    uint32_t family = 0; for (uint32_t i = 0; i < qn; ++i) if (qf[i].queueFlags & VK_QUEUE_COMPUTE_BIT) { family = i; break; }
    float prio = 1; VkDeviceQueueCreateInfo q = {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, 0, 0, family, 1, &prio};
    /* Enabled identically for both modules: Dawn's declares VulkanMemoryModel and
     * SPV_KHR_maximal_reconvergence; both providers enable robustBufferAccess. */
    VkPhysicalDeviceShaderMaximalReconvergenceFeaturesKHR rec = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_MAXIMAL_RECONVERGENCE_FEATURES_KHR, 0, VK_TRUE};
    VkPhysicalDeviceVulkan12Features v12 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, &rec};
    v12.vulkanMemoryModel = VK_TRUE; v12.vulkanMemoryModelDeviceScope = VK_TRUE;
    VkPhysicalDeviceFeatures2 feat2 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &v12};
    feat2.features.robustBufferAccess = VK_TRUE;
    const char *exts[] = {"VK_KHR_shader_maximal_reconvergence"};
    VkDeviceCreateInfo dci = {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, &feat2, 0, 1, &q, 0, 0, 1, exts, 0};
    VkDevice dev; if (vkCreateDevice(pd, &dci, 0, &dev)) return 5;
    VkQueue queue; vkGetDeviceQueue(dev, family, 0, &queue);
    VkShaderModuleCreateInfo smi = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, 0, 0, (size_t)n, code};
    VkShaderModule sm; if (vkCreateShaderModule(dev, &smi, 0, &sm)) return 6;
    VkDescriptorSetLayoutBinding lb[16];
    for (uint32_t i = 0; i < nb; ++i) lb[i] = (VkDescriptorSetLayoutBinding){b[i].binding, b[i].ptr_class == 2 ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, 0};
    VkDescriptorSetLayoutCreateInfo dli = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, 0, 0, nb, lb};
    VkDescriptorSetLayout dsl; vkCreateDescriptorSetLayout(dev, &dli, 0, &dsl);
    VkPipelineLayoutCreateInfo pli = {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, 0, 0, 1, &dsl};
    VkPipelineLayout pl; vkCreatePipelineLayout(dev, &pli, 0, &pl);
    VkComputePipelineCreateInfo cpi = {VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, 0, 0, {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, 0, 0, VK_SHADER_STAGE_COMPUTE_BIT, sm, entry}, pl};
    VkPipeline pipe; if (vkCreateComputePipelines(dev, 0, 1, &cpi, 0, &pipe)) return 7;
    VkDescriptorPoolSize ps[2] = {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 4}, {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 8}};
    VkDescriptorPoolCreateInfo dpi = {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, 0, 0, 1, 2, ps};
    VkDescriptorPool dp; vkCreateDescriptorPool(dev, &dpi, 0, &dp);
    VkCommandPoolCreateInfo pci = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, 0, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, family};
    VkCommandPool pool; vkCreateCommandPool(dev, &pci, 0, &pool);
    VkCommandBufferAllocateInfo cai = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, 0, pool, VK_COMMAND_BUFFER_LEVEL_PRIMARY, 1};
    VkCommandBuffer cmd; vkAllocateCommandBuffers(dev, &cai, &cmd);
    VkFenceCreateInfo fci = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO}; VkFence fence; vkCreateFence(dev, &fci, 0, &fence);
    LARGE_INTEGER fq; QueryPerformanceFrequency(&fq);
    printf("{\"module\":\"%s\",\"entry\":\"%s\",\"bindings\":%u,\"runs\":[", argv[1], entry, nb);
    for (int a = 2; a < argc; ++a) {
        uint32_t type = (uint32_t)atoi(argv[a]); VkBuffer buf[16]; VkDeviceMemory mem[16]; VkDescriptorBufferInfo bi[16]; VkWriteDescriptorSet wr[16];
        for (uint32_t i = 0; i < nb; ++i) {
            const int uniform = lb[i].descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            VkDeviceSize size = uniform ? 256 : (4u << 20);
            VkBufferCreateInfo bci = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, 0, 0, size, uniform ? VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT : VK_BUFFER_USAGE_STORAGE_BUFFER_BIT};
            vkCreateBuffer(dev, &bci, 0, &buf[i]); VkMemoryRequirements r; vkGetBufferMemoryRequirements(dev, buf[i], &r);
            uint32_t t = uniform ? 3 : type;
            VkMemoryAllocateInfo ai = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, 0, r.size, t};
            if (vkAllocateMemory(dev, &ai, 0, &mem[i])) return 8;
            vkBindBufferMemory(dev, buf[i], mem[i], 0);
            if (uniform) { uint32_t *p; vkMapMemory(dev, mem[i], 0, 256, 0, (void **)&p); memset(p, 0, 256); p[0] = 1048576u; vkUnmapMemory(dev, mem[i]); }
            bi[i] = (VkDescriptorBufferInfo){buf[i], 0, VK_WHOLE_SIZE};
            wr[i] = (VkWriteDescriptorSet){VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, 0, 0, lb[i].binding, 0, 1, lb[i].descriptorType, 0, &bi[i], 0};
        }
        VkDescriptorSetAllocateInfo dsa = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, 0, dp, 1, &dsl};
        VkDescriptorSet ds; vkResetDescriptorPool(dev, dp, 0); vkAllocateDescriptorSets(dev, &dsa, &ds);
        for (uint32_t i = 0; i < nb; ++i) wr[i].dstSet = ds;
        vkUpdateDescriptorSets(dev, nb, wr, 0, 0);
        double us[81];
        for (int r = 0; r < 81; ++r) {
            VkCommandBufferBeginInfo cbi = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, 0, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
            vkBeginCommandBuffer(cmd, &cbi); vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipe);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pl, 0, 1, &ds, 0, 0);
            vkCmdDispatch(cmd, 16384, 1, 1); vkEndCommandBuffer(cmd);
            VkSubmitInfo si = {VK_STRUCTURE_TYPE_SUBMIT_INFO, 0, 0, 0, 0, 1, &cmd};
            LARGE_INTEGER s0, s1; QueryPerformanceCounter(&s0);
            vkQueueSubmit(queue, 1, &si, fence); vkWaitForFences(dev, 1, &fence, VK_TRUE, UINT64_MAX);
            QueryPerformanceCounter(&s1); us[r] = (s1.QuadPart - s0.QuadPart) * 1e6 / fq.QuadPart;
            vkResetFences(dev, 1, &fence); vkResetCommandBuffer(cmd, 0);
        }
        qsort(us + 1, 80, sizeof(double), cmp);
        printf("%s{\"storageType\":%u,\"flags\":\"0x%x\",\"submitToFenceMedianUs\":%.1f,\"p95Us\":%.1f}", a > 2 ? "," : "", type, mp.memoryTypes[type].propertyFlags, us[41], us[77]);
        for (uint32_t i = 0; i < nb; ++i) { vkDestroyBuffer(dev, buf[i], 0); vkFreeMemory(dev, mem[i], 0); }
    }
    printf("]}\n");
    return 0;
}
