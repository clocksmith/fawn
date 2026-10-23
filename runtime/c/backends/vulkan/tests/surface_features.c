/* SPDX-License-Identifier: MIT */
#include "../src/surface_internal.h"
#include "../src/feature_internal.h"
#include <assert.h>
#include <stdio.h>

static bool maintenance_extension = true, maintenance_feature = true;
static bool truncated, unterminated, missing_proc;
static unsigned queried, resolved;
static VkResult enumerate(VkPhysicalDevice device, const uint8_t *layer, uint32_t *count,
    VkExtensionProperties *properties) {
    (void)device;
    assert(layer == NULL);
    if (properties == NULL) { *count = 3; return 0; }
    assert(*count == 3);
    memset(properties, 0, 3 * sizeof(*properties));
    const char *names[3] = {"VK_EXT_shader_demote_to_helper_invocation", "VK_KHR_swapchain",
        maintenance_extension ? "VK_EXT_swapchain_maintenance1" : "VK_EXT_not_maintenance"};
    for (unsigned i = 0; i < 3; ++i) memcpy(properties[i].extensionName, names[i], strlen(names[i]) + 1);
    if (unterminated) memset(properties[2].extensionName, 'x', sizeof(properties[2].extensionName));
    return truncated ? 5 : 0;
}
static void features(VkPhysicalDevice device, VkPhysicalDeviceFeatures2 *info) {
    (void)device;
    info->features.robustBufferAccess = 1;
    if (info->pNext == NULL) return;
    VkStructureType type;
    memcpy(&type, info->pNext, sizeof(type));
    if (type == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DEMOTE_TO_HELPER_INVOCATION_FEATURES) {
        VkPhysicalDeviceShaderDemoteToHelperInvocationFeatures *demote = info->pNext;
        demote->shaderDemoteToHelperInvocation = 1;
    } else {
        assert(type == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_EXT);
        VkPhysicalDeviceSwapchainMaintenance1FeaturesEXT *maintenance = info->pNext;
        assert(maintenance->pNext == NULL);
        maintenance->swapchainMaintenance1 = maintenance_feature;
        ++queried;
    }
}
static void unused_proc(void) { assert(false); }
static doe_vk_proc get_proc(VkDevice device, const char *name) {
    (void)device;
    ++resolved;
    if (missing_proc && strcmp(name, "vkReleaseSwapchainImagesEXT") == 0) return NULL;
    return unused_proc;
}
int main(void) {
    doe_vk_context context = {.references = 1};
    context.vk.vkEnumerateDeviceExtensionProperties = enumerate;
    context.vk.vkGetPhysicalDeviceFeatures2 = features;
    context.vk.vkGetDeviceProcAddr = get_proc;
    doe_vk_options options = {.surface_platforms = DOE_VK_SURFACE_XCB, .enable_shader_demote_if_supported = true};
    doe_vk_feature_selection selection;
    assert(doe_vk_prepare_features(&context, &options, &selection, NULL) == DOE_VK_OK);
    assert(selection.surface_enabled && selection.swapchain.swapchainMaintenance1 == 1 && selection.extension != NULL && queried == 1);
    assert(selection.base.robustBufferAccess == 1 && selection.base.fullDrawIndexUint32 == 0);
    doe_vk_feature_selection saved = selection;
    maintenance_feature = false;
    assert(doe_vk_prepare_features(&context, &options, &selection, NULL) == DOE_VK_UNSUPPORTED);
    assert(memcmp(&saved, &selection, sizeof(saved)) == 0);
    maintenance_feature = true;
    maintenance_extension = false;
    assert(doe_vk_prepare_features(&context, &options, &selection, NULL) == DOE_VK_UNSUPPORTED);
    maintenance_extension = true;
    unterminated = true;
    assert(doe_vk_prepare_features(&context, &options, &selection, NULL) == DOE_VK_UNSUPPORTED);
    unterminated = false;
    truncated = true;
    assert(doe_vk_prepare_features(&context, &options, &selection, NULL) != DOE_VK_OK);
    assert(memcmp(&saved, &selection, sizeof(saved)) == 0);
    doe_vk_instance_extensions instance = {0};
    assert(doe_vk_surface_instance_extensions(&context, 8, &instance, NULL) == DOE_VK_INVALID_ARGUMENT);
    assert(doe_vk_surface_instance_extensions(&context, 0, &instance, NULL) == DOE_VK_OK && instance.count == 0);
    missing_proc = true;
    assert(doe_vk_surface_load_functions(&context, DOE_VK_SURFACE_XCB, true, NULL) == DOE_VK_UNSUPPORTED);
    assert(resolved == 8 && context.caps.surface_platforms == 0);
    missing_proc = false;
    assert(doe_vk_surface_load_functions(&context, DOE_VK_SURFACE_XCB, true, NULL) == DOE_VK_OK);
    assert(resolved == 16 && context.vk.vkReleaseSwapchainImagesEXT != NULL);
    puts("PASS explicit WSI extension/feature admission, simultaneous demote selection, absent/false/truncated/unterminated controls, missing native release procedure and unchanged capability/output state; host only");
    return 0;
}
