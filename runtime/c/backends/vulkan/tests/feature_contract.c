#include "../src/feature_internal.h"
#include <assert.h>
#include <stdio.h>

static bool extension, demote, robust, unterminated;
static uint32_t enumerations, queries, chains;
static VkResult count_result, list_result;
static VkResult enumerate(VkPhysicalDevice device, const uint8_t *layer,
    uint32_t *count, VkExtensionProperties *properties) {
    (void)device;
    assert(layer == NULL);
    ++enumerations;
    if (properties == NULL) {
        *count = 1;
        return count_result;
    }
    assert(*count == 1);
    memset(properties, 0, sizeof(*properties));
    const char *name = extension ? "VK_EXT_shader_demote_to_helper_invocation" : "VK_EXT_other";
    memcpy(properties->extensionName, name, strlen(name) + 1);
    if (unterminated) memset(properties->extensionName, 'x', sizeof(properties->extensionName));
    return list_result;
}
static void features(VkPhysicalDevice device, VkPhysicalDeviceFeatures2 *out) {
    (void)device;
    ++queries;
    assert(out->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2);
    out->features.robustBufferAccess = robust;
    /* Other supported features must never be accidentally enabled. */
    out->features.shaderInt64 = 1;
    if (out->pNext != NULL) {
        ++chains;
        VkPhysicalDeviceShaderDemoteToHelperInvocationFeatures *optional = out->pNext;
        assert(optional->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DEMOTE_TO_HELPER_INVOCATION_FEATURES);
        assert(optional->pNext == NULL);
        optional->shaderDemoteToHelperInvocation = demote;
    }
}
int main(void) {
    doe_vk_context context = {0};
    context.vk.vkEnumerateDeviceExtensionProperties = enumerate;
    context.vk.vkGetPhysicalDeviceFeatures2 = features;
    doe_vk_options options = {0};
    doe_vk_feature_selection selection;
    doe_vk_error error;
    robust = extension = demote = true;
    assert(doe_vk_prepare_features(&context, &options, &selection, &error) == DOE_VK_OK);
    assert(enumerations == 0 && queries == 1 && chains == 0);
    assert(selection.extension == NULL && selection.base.robustBufferAccess == 1);
    assert(selection.base.shaderInt64 == 0);
    options.enable_shader_demote_if_supported = true;
    assert(doe_vk_prepare_features(&context, &options, &selection, &error) == DOE_VK_OK);
    assert(enumerations == 2 && queries == 2 && chains == 1);
    assert(selection.extension != NULL && selection.demote.shaderDemoteToHelperInvocation == 1);
    assert(selection.demote.pNext == NULL && selection.base.shaderInt64 == 0);
    extension = false;
    assert(doe_vk_prepare_features(&context, &options, &selection, &error) == DOE_VK_OK);
    assert(selection.extension == NULL && chains == 1);
    extension = true;
    demote = false;
    assert(doe_vk_prepare_features(&context, &options, &selection, &error) == DOE_VK_OK);
    assert(selection.extension == NULL && chains == 2);
    unterminated = true;
    assert(doe_vk_prepare_features(&context, &options, &selection, &error) == DOE_VK_OK);
    assert(selection.extension == NULL && chains == 2);
    unterminated = false;
    doe_vk_feature_selection saved = selection;
    robust = false;
    assert(doe_vk_prepare_features(&context, &options, &selection, &error) == DOE_VK_UNSUPPORTED);
    assert(memcmp(&selection, &saved, sizeof(saved)) == 0);
    robust = true;
    count_result = -1;
    assert(doe_vk_prepare_features(&context, &options, &selection, &error) != DOE_VK_OK);
    assert(memcmp(&selection, &saved, sizeof(saved)) == 0);
    count_result = 0;
    list_result = 5;
    assert(doe_vk_prepare_features(&context, &options, &selection, &error) != DOE_VK_OK);
    assert(memcmp(&selection, &saved, sizeof(saved)) == 0);
    assert(doe_vk_prepare_features(NULL, &options, &selection, &error) == DOE_VK_INVALID_ARGUMENT);
    assert(memcmp(&selection, &saved, sizeof(saved)) == 0);
    puts("PASS explicit optional demote selection, absent/false/malformed extension controls, robust-only baseline, failed/incomplete enumeration and unchanged failure outputs");
    return 0;
}
