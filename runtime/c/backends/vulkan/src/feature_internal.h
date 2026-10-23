#ifndef DOE_VK_FEATURE_INTERNAL_H
#define DOE_VK_FEATURE_INTERNAL_H
#include "internal.h"
typedef struct doe_vk_feature_selection {
    VkPhysicalDeviceFeatures base;
    VkPhysicalDeviceShaderDemoteToHelperInvocationFeatures demote;
    const uint8_t *extension;
    VkPhysicalDeviceSwapchainMaintenance1FeaturesEXT swapchain;
    bool surface_enabled;
} doe_vk_feature_selection;
/* Caller-owned immediate device-create data, no retained pointer or handles.
 * Failure leaves output unchanged. Optional unsupported demote stays disabled. */
doe_vk_status doe_vk_prepare_features(doe_vk_context *context, const doe_vk_options *options,
    doe_vk_feature_selection *out, doe_vk_error *error);
#endif
