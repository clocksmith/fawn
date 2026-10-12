/* Repository diagnostic layer: retain the exact shader bytes given to Vulkan. */
#include <vulkan/vulkan.h>
#include <vulkan/vk_layer.h>
#include <pthread.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { MAX_OWNERS = 64, PATH_BYTES = 4096 };
typedef struct { void* key; VkInstance instance; PFN_vkGetInstanceProcAddr next; } InstanceOwner;
typedef struct { void* key; PFN_vkGetDeviceProcAddr next; } DeviceOwner;
static InstanceOwner instances[MAX_OWNERS];
static DeviceOwner devices[MAX_OWNERS];
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static uint64_t sequence = 0;
static void* dispatch_key(const void* handle) { return handle ? *(void* const*)handle : NULL; }
static InstanceOwner instance_owner(const void* handle) {
    const void* key = dispatch_key(handle); InstanceOwner owner = {0};
    pthread_mutex_lock(&mutex);
    for (size_t i = 0; i < MAX_OWNERS; i++) if (key && instances[i].key == key) { owner = instances[i]; break; }
    pthread_mutex_unlock(&mutex); return owner;
}
static DeviceOwner device_owner(VkDevice device) {
    const void* key = dispatch_key(device); DeviceOwner owner = {0};
    pthread_mutex_lock(&mutex);
    for (size_t i = 0; i < MAX_OWNERS; i++) if (key && devices[i].key == key) { owner = devices[i]; break; }
    pthread_mutex_unlock(&mutex); return owner;
}

static void retain_capabilities(InstanceOwner instance, VkPhysicalDevice physical, const VkDeviceCreateInfo* info, VkResult status) {
    const char* folder = getenv("DOE_VULKAN_CUSTODY_DIR"); if (!folder || !*folder) return;
    VkPhysicalDeviceShaderIntegerDotProductFeatures dot = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_INTEGER_DOT_PRODUCT_FEATURES, NULL, VK_FALSE};
    VkPhysicalDeviceFeatures2 features = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &dot, {0}};
    VkPhysicalDeviceShaderIntegerDotProductProperties acceleration = {0};
    acceleration.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_INTEGER_DOT_PRODUCT_PROPERTIES;
    VkPhysicalDeviceProperties2 properties = {0}; properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2; properties.pNext = &acceleration;
    PFN_vkGetPhysicalDeviceFeatures2 get_features = (PFN_vkGetPhysicalDeviceFeatures2)instance.next(instance.instance, "vkGetPhysicalDeviceFeatures2");
    PFN_vkGetPhysicalDeviceProperties2 get_properties = (PFN_vkGetPhysicalDeviceProperties2)instance.next(instance.instance, "vkGetPhysicalDeviceProperties2");
    if (!get_features || !get_properties) return;
    get_features(physical, &features); get_properties(physical, &properties);
    bool enabled = false;
    const VkBaseInStructure* chain = (const VkBaseInStructure*)info->pNext;
    for (size_t count = 0; chain && count < MAX_OWNERS; count++, chain = chain->pNext) {
        if (chain->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_INTEGER_DOT_PRODUCT_FEATURES) enabled |= ((const VkPhysicalDeviceShaderIntegerDotProductFeatures*)chain)->shaderIntegerDotProduct;
        if (chain->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES) enabled |= ((const VkPhysicalDeviceVulkan13Features*)chain)->shaderIntegerDotProduct;
    }
    char path[PATH_BYTES];
    if (snprintf(path, sizeof(path), "%s/capabilities.jsonl", folder) >= (int)sizeof(path)) return;
    pthread_mutex_lock(&mutex); FILE* file = fopen(path, "a");
    if (file) {
        fprintf(file, "{\"result\":%d,\"vendorId\":%u,\"deviceId\":%u,\"integerDotAvailable\":%s,\"packedSignedAccelerated\":%s,\"packedUnsignedAccelerated\":%s,\"integerDotEnabled\":%s}\n", status, properties.properties.vendorID, properties.properties.deviceID, dot.shaderIntegerDotProduct ? "true" : "false", acceleration.integerDotProduct4x8BitPackedSignedAccelerated ? "true" : "false", acceleration.integerDotProduct4x8BitPackedUnsignedAccelerated ? "true" : "false", enabled ? "true" : "false");
        fclose(file);
    }
    pthread_mutex_unlock(&mutex);
}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance, const char*);
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(VkDevice, const char*);

static VKAPI_ATTR VkResult VKAPI_CALL create_instance(const VkInstanceCreateInfo* info, const VkAllocationCallbacks* alloc, VkInstance* result) {
    VkLayerInstanceCreateInfo* chain = (VkLayerInstanceCreateInfo*)info->pNext;
    while (chain && (chain->sType != VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO || chain->function != VK_LAYER_LINK_INFO)) chain = (VkLayerInstanceCreateInfo*)chain->pNext;
    if (!chain || !chain->u.pLayerInfo) return VK_ERROR_INITIALIZATION_FAILED;
    PFN_vkGetInstanceProcAddr next = chain->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    PFN_vkCreateInstance create = (PFN_vkCreateInstance)next(VK_NULL_HANDLE, "vkCreateInstance");
    chain->u.pLayerInfo = chain->u.pLayerInfo->pNext;
    VkResult status = create(info, alloc, result);
    if (status == VK_SUCCESS) {
        bool stored = false; pthread_mutex_lock(&mutex);
        for (size_t i = 0; i < MAX_OWNERS; i++) if (!instances[i].key) {
            instances[i] = (InstanceOwner){dispatch_key(*result), *result, next}; stored = true; break;
        }
        pthread_mutex_unlock(&mutex);
        if (!stored) {
            ((PFN_vkDestroyInstance)next(*result, "vkDestroyInstance"))(*result, alloc);
            return VK_ERROR_INITIALIZATION_FAILED;
        }
    }
    return status;
}
static VKAPI_ATTR void VKAPI_CALL destroy_instance(VkInstance instance, const VkAllocationCallbacks* alloc) {
    InstanceOwner owner = instance_owner(instance); if (!owner.next) return;
    ((PFN_vkDestroyInstance)owner.next(instance, "vkDestroyInstance"))(instance, alloc);
    pthread_mutex_lock(&mutex);
    for (size_t i = 0; i < MAX_OWNERS; i++) if (instances[i].key == owner.key) instances[i] = (InstanceOwner){0};
    pthread_mutex_unlock(&mutex);
}
static VKAPI_ATTR VkResult VKAPI_CALL create_device(VkPhysicalDevice physical, const VkDeviceCreateInfo* info, const VkAllocationCallbacks* alloc, VkDevice* result) {
    InstanceOwner instance = instance_owner(physical); if (!instance.next) return VK_ERROR_INITIALIZATION_FAILED;
    VkLayerDeviceCreateInfo* chain = (VkLayerDeviceCreateInfo*)info->pNext;
    while (chain && (chain->sType != VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO || chain->function != VK_LAYER_LINK_INFO)) chain = (VkLayerDeviceCreateInfo*)chain->pNext;
    if (!chain || !chain->u.pLayerInfo) return VK_ERROR_INITIALIZATION_FAILED;
    PFN_vkGetDeviceProcAddr next = chain->u.pLayerInfo->pfnNextGetDeviceProcAddr;
    PFN_vkCreateDevice create = (PFN_vkCreateDevice)chain->u.pLayerInfo->pfnNextGetInstanceProcAddr(instance.instance, "vkCreateDevice");
    chain->u.pLayerInfo = chain->u.pLayerInfo->pNext;
    VkResult status = create(physical, info, alloc, result);
    retain_capabilities(instance, physical, info, status);
    if (status == VK_SUCCESS) {
        bool stored = false; pthread_mutex_lock(&mutex);
        for (size_t i = 0; i < MAX_OWNERS; i++) if (!devices[i].key) {
            devices[i] = (DeviceOwner){dispatch_key(*result), next}; stored = true; break;
        }
        pthread_mutex_unlock(&mutex);
        if (!stored) {
            ((PFN_vkDestroyDevice)next(*result, "vkDestroyDevice"))(*result, alloc);
            return VK_ERROR_INITIALIZATION_FAILED;
        }
    }
    return status;
}
static VKAPI_ATTR void VKAPI_CALL destroy_device(VkDevice device, const VkAllocationCallbacks* alloc) {
    DeviceOwner owner = device_owner(device); if (!owner.next) return;
    ((PFN_vkDestroyDevice)owner.next(device, "vkDestroyDevice"))(device, alloc);
    pthread_mutex_lock(&mutex);
    for (size_t i = 0; i < MAX_OWNERS; i++) if (devices[i].key == owner.key) devices[i] = (DeviceOwner){0};
    pthread_mutex_unlock(&mutex);
}
static VKAPI_ATTR VkResult VKAPI_CALL create_shader(VkDevice device, const VkShaderModuleCreateInfo* info, const VkAllocationCallbacks* alloc, VkShaderModule* result) {
    DeviceOwner owner = device_owner(device); if (!owner.next) return VK_ERROR_INITIALIZATION_FAILED;
    VkResult status = ((PFN_vkCreateShaderModule)owner.next(device, "vkCreateShaderModule"))(device, info, alloc, result);
    const char* folder = getenv("DOE_VULKAN_CUSTODY_DIR");
    if (folder && *folder) {
        pthread_mutex_lock(&mutex);
        const unsigned long long id = ++sequence;
        char path[PATH_BYTES]; bool retained = false;
        if (snprintf(path, sizeof(path), "%s/module-%llu.spv", folder, id) < (int)sizeof(path)) {
            FILE* file = fopen(path, "wb");
            if (file) { retained = fwrite(info->pCode, 1, info->codeSize, file) == info->codeSize; retained &= fclose(file) == 0; }
        }
        if (snprintf(path, sizeof(path), "%s/custody.jsonl", folder) < (int)sizeof(path)) {
            FILE* file = fopen(path, "a");
            if (file) { fprintf(file, "{\"sequence\":%llu,\"result\":%d,\"sizeBytes\":%zu,\"artifact\":\"module-%llu.spv\",\"retained\":%s}\n", id, status, info->codeSize, id, retained ? "true" : "false"); fclose(file); }
        }
        pthread_mutex_unlock(&mutex);
    }
    return status;
}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance instance, const char* name) {
    if (!strcmp(name, "vkGetInstanceProcAddr")) return (PFN_vkVoidFunction)vkGetInstanceProcAddr;
    if (!strcmp(name, "vkGetDeviceProcAddr")) return (PFN_vkVoidFunction)vkGetDeviceProcAddr;
    if (!strcmp(name, "vkCreateInstance")) return (PFN_vkVoidFunction)create_instance;
    if (!strcmp(name, "vkDestroyInstance")) return (PFN_vkVoidFunction)destroy_instance;
    if (!strcmp(name, "vkCreateDevice")) return (PFN_vkVoidFunction)create_device;
    if (!strcmp(name, "vkDestroyDevice")) return (PFN_vkVoidFunction)destroy_device;
    if (!strcmp(name, "vkCreateShaderModule")) return (PFN_vkVoidFunction)create_shader;
    InstanceOwner owner = instance_owner(instance);
    return owner.next ? owner.next(instance, name) : NULL;
}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(VkDevice device, const char* name) {
    if (!strcmp(name, "vkGetDeviceProcAddr")) return (PFN_vkVoidFunction)vkGetDeviceProcAddr;
    if (!strcmp(name, "vkDestroyDevice")) return (PFN_vkVoidFunction)destroy_device;
    if (!strcmp(name, "vkCreateShaderModule")) return (PFN_vkVoidFunction)create_shader;
    DeviceOwner owner = device_owner(device); return owner.next ? owner.next(device, name) : NULL;
}
VKAPI_ATTR VkResult VKAPI_CALL vkNegotiateLoaderLayerInterfaceVersion(VkNegotiateLayerInterface* interface) {
    if (interface->loaderLayerInterfaceVersion < 2) return VK_ERROR_INITIALIZATION_FAILED;
    interface->loaderLayerInterfaceVersion = 2;
    interface->pfnGetInstanceProcAddr = vkGetInstanceProcAddr;
    interface->pfnGetDeviceProcAddr = vkGetDeviceProcAddr;
    interface->pfnGetPhysicalDeviceProcAddr = NULL;
    return VK_SUCCESS;
}
