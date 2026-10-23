#include "feature_internal.h"
#include "surface_internal.h"

static const uint8_t demote_extension[] = "VK_EXT_shader_demote_to_helper_invocation";


doe_vk_status doe_vk_prepare_features(doe_vk_context *context, const doe_vk_options *options,
    doe_vk_feature_selection *out, doe_vk_error *error) {
    if (context == NULL || options == NULL || out == NULL)
        return doe_vk_fail(DOE_VK_INVALID_ARGUMENT, "device feature selection", error);
    bool available = false;
    if (options->enable_shader_demote_if_supported) {
        doe_vk_status status = doe_vk_extensions_present(context, true, (const uint8_t *const[]){demote_extension}, 1, &available, error);
        if (status != DOE_VK_OK) return status;
    }
    VkPhysicalDeviceShaderDemoteToHelperInvocationFeatures demote = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DEMOTE_TO_HELPER_INVOCATION_FEATURES
    };
    VkPhysicalDeviceFeatures2 features = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext = available ? &demote : NULL};
    context->vk.vkGetPhysicalDeviceFeatures2(context->physical, &features);
    if (!features.features.robustBufferAccess)
        return doe_vk_fail(DOE_VK_UNSUPPORTED, "robustBufferAccess required", error);
    doe_vk_feature_selection selection = {.base = {.robustBufferAccess = 1}};
    if (available && demote.shaderDemoteToHelperInvocation) {
        selection.demote = demote;
        selection.extension = demote_extension;
    }
    if (options->surface_platforms != 0) {
        const uint8_t *names[] = {(const uint8_t *)"VK_KHR_swapchain", (const uint8_t *)"VK_EXT_swapchain_maintenance1"};
        bool present[2];
        doe_vk_status status = doe_vk_extensions_present(context, true, names, 2, present, error);
        if (status != DOE_VK_OK) return status;
        if (!present[0] || !present[1]) return doe_vk_fail(DOE_VK_UNSUPPORTED, "swapchain maintenance extensions required", error);
        VkPhysicalDeviceSwapchainMaintenance1FeaturesEXT swapchain = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_EXT
        };
        VkPhysicalDeviceFeatures2 query = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = &swapchain};
        context->vk.vkGetPhysicalDeviceFeatures2(context->physical, &query);
        if (!swapchain.swapchainMaintenance1) return doe_vk_fail(DOE_VK_UNSUPPORTED, "swapchainMaintenance1 required", error);
        selection.swapchain = swapchain;
        selection.surface_enabled = true;
    }
    *out = selection;
    return DOE_VK_OK;
}
