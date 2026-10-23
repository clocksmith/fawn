#include "surface_internal.h"

/* One bounded enumeration owner for optional features and explicit WSI. No
 * cached second capability registry; failed enumeration publishes no flags. */
doe_vk_status doe_vk_extensions_present(doe_vk_context *context, bool device,
    const uint8_t *const *names, size_t count, bool *present, doe_vk_error *error) {
    if (context == NULL || names == NULL || present == NULL || count == 0 || count > 8)
        return doe_vk_fail(DOE_VK_INVALID_ARGUMENT, "extension query", error);
    for (size_t i = 0; i < count; ++i)
        if (names[i] == NULL) return doe_vk_fail(DOE_VK_INVALID_ARGUMENT, "extension name", error);
    uint32_t length = 0;
    VkResult result = device ? context->vk.vkEnumerateDeviceExtensionProperties(context->physical, NULL, &length, NULL) :
        context->vk.vkEnumerateInstanceExtensionProperties(NULL, &length, NULL);
    if (result != 0) return doe_vk_result(context, result, "extension count", error);
    if (length > 4096 || (uint64_t)length * sizeof(VkExtensionProperties) > SIZE_MAX)
        return doe_vk_fail(DOE_VK_UNSUPPORTED, "extension enumeration budget", error);
    VkExtensionProperties *properties = length != 0 ? calloc(length, sizeof(*properties)) : NULL;
    if (length != 0 && properties == NULL) return doe_vk_fail(DOE_VK_OUT_OF_MEMORY, "extension properties", error);
    uint32_t capacity = length;
    if (length != 0) result = device ?
        context->vk.vkEnumerateDeviceExtensionProperties(context->physical, NULL, &length, properties) :
        context->vk.vkEnumerateInstanceExtensionProperties(NULL, &length, properties);
    bool found[8] = {false};
    if (result == 0 && length <= capacity) {
        for (uint32_t i = 0; i < length; ++i) {
            if (memchr(properties[i].extensionName, 0, sizeof(properties[i].extensionName)) == NULL) continue;
            for (size_t j = 0; j < count; ++j)
                if (strcmp((const char *)properties[i].extensionName, (const char *)names[j]) == 0) found[j] = true;
        }
    }
    free(properties);
    if (result != 0) return doe_vk_result(context, result, "extension enumeration", error);
    if (length > capacity) return doe_vk_fail(DOE_VK_NATIVE_FAILURE, "extension enumeration changed", error);
    memcpy(present, found, count * sizeof(*present));
    return DOE_VK_OK;
}
