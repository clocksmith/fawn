#include "feature_internal.h"

static const uint8_t demote_extension[] = "VK_EXT_shader_demote_to_helper_invocation";

static doe_vk_status has_demote_extension(doe_vk_context *context, bool *out, doe_vk_error *error) {
    uint32_t count = 0;
    VkResult result = context->vk.vkEnumerateDeviceExtensionProperties(context->physical, NULL, &count, NULL);
    if (result != 0) return doe_vk_result(context, result, "vkEnumerateDeviceExtensionProperties count", error);
    if (count == 0) {
        *out = false;
        return DOE_VK_OK;
    }
    if ((uint64_t)count * sizeof(VkExtensionProperties) > SIZE_MAX)
        return doe_vk_fail(DOE_VK_OUT_OF_MEMORY, "device extension list", error);
    VkExtensionProperties *properties = calloc(count, sizeof(*properties));
    if (properties == NULL) return doe_vk_fail(DOE_VK_OUT_OF_MEMORY, "device extension list", error);
    uint32_t capacity = count;
    result = context->vk.vkEnumerateDeviceExtensionProperties(context->physical, NULL, &count, properties);
    bool found = false;
    if (result == 0 && count <= capacity) {
        for (uint32_t i = 0; i < count; ++i) {
            if (memchr(properties[i].extensionName, 0, sizeof(properties[i].extensionName)) != NULL &&
                strcmp((const char *)properties[i].extensionName, (const char *)demote_extension) == 0) found = true;
        }
    }
    free(properties);
    if (result != 0) return doe_vk_result(context, result, "vkEnumerateDeviceExtensionProperties", error);
    if (count > capacity) return doe_vk_fail(DOE_VK_NATIVE_FAILURE, "device extension list changed", error);
    *out = found;
    return DOE_VK_OK;
}

doe_vk_status doe_vk_prepare_features(doe_vk_context *context, const doe_vk_options *options,
    doe_vk_feature_selection *out, doe_vk_error *error) {
    if (context == NULL || options == NULL || out == NULL)
        return doe_vk_fail(DOE_VK_INVALID_ARGUMENT, "device feature selection", error);
    bool available = false;
    if (options->enable_shader_demote_if_supported) {
        doe_vk_status status = has_demote_extension(context, &available, error);
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
    *out = selection;
    return DOE_VK_OK;
}
