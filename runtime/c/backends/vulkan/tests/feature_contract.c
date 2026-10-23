/* SPDX-License-Identifier: MIT */
#include "../src/feature_internal.h"
#include <assert.h>
#include <stdio.h>

static bool extension, demote, robust, unterminated;
static bool dynamic_rendering, maintenance_extension, maintenance_feature;
static uint32_t enumerations, queries, chains, maintenance_chains;
static VkResult count_result, list_result;
/* Lists the configured device extensions; demote/other and both maintenance5
 * dependencies are independently present or absent. */
static uint32_t listed(const char *names[4]) {
    uint32_t count = 0;
    names[count++] = extension ? "VK_EXT_shader_demote_to_helper_invocation" : "VK_EXT_other";
    if (dynamic_rendering) names[count++] = "VK_KHR_dynamic_rendering";
    if (maintenance_extension) names[count++] = "VK_KHR_maintenance5";
    return count;
}
static VkResult enumerate(VkPhysicalDevice device, const uint8_t *layer,
    uint32_t *count, VkExtensionProperties *properties) {
    (void)device;
    assert(layer == NULL);
    ++enumerations;
    const char *names[4];
    uint32_t total = listed(names);
    if (properties == NULL) {
        *count = total;
        return count_result;
    }
    assert(*count == total);
    for (uint32_t i = 0; i < total; ++i) {
        memset(&properties[i], 0, sizeof(properties[i]));
        memcpy(properties[i].extensionName, names[i], strlen(names[i]) + 1);
        if (unterminated) memset(properties[i].extensionName, 'x', sizeof(properties[i].extensionName));
    }
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
        const int32_t *type = out->pNext;
        if (*type == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_5_FEATURES_KHR) {
            ++maintenance_chains;
            VkPhysicalDeviceMaintenance5FeaturesKHR *optional = out->pNext;
            assert(optional->pNext == NULL);
            optional->maintenance5 = maintenance_feature;
            return;
        }
        ++chains;
        VkPhysicalDeviceShaderDemoteToHelperInvocationFeatures *optional = out->pNext;
        assert(optional->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DEMOTE_TO_HELPER_INVOCATION_FEATURES);
        assert(optional->pNext == NULL);
        optional->shaderDemoteToHelperInvocation = demote;
    }
}
/* maintenance5 is default-on when offered: both extensions and the feature. */
static void maintenance5(doe_vk_context *context) {
    doe_vk_options options = {0};
    doe_vk_feature_selection selection;
    doe_vk_error error;
    count_result = list_result = 0; /* Earlier controls injected enumeration failures. */
    robust = true;
    dynamic_rendering = maintenance_extension = maintenance_feature = true;
    uint32_t before = maintenance_chains;
    assert(doe_vk_prepare_features(context, &options, &selection, &error) == DOE_VK_OK);
    assert(selection.maintenance5_enabled && selection.maintenance5.maintenance5 == 1 && maintenance_chains == before + 1);
    assert(selection.maintenance5.sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_5_FEATURES_KHR);
    assert(selection.maintenance5.pNext == NULL && selection.base.shaderInt64 == 0);
    maintenance_feature = false; /* Advertised extension, feature not reported. */
    assert(doe_vk_prepare_features(context, &options, &selection, &error) == DOE_VK_OK);
    assert(!selection.maintenance5_enabled && selection.maintenance5.maintenance5 == 0);
    maintenance_feature = true;
    dynamic_rendering = false; /* Missing dependency at API 1.2: never enabled or queried. */
    before = maintenance_chains;
    assert(doe_vk_prepare_features(context, &options, &selection, &error) == DOE_VK_OK);
    assert(!selection.maintenance5_enabled && maintenance_chains == before);
    dynamic_rendering = true;
    maintenance_extension = false;
    assert(doe_vk_prepare_features(context, &options, &selection, &error) == DOE_VK_OK);
    assert(!selection.maintenance5_enabled && maintenance_chains == before);
    dynamic_rendering = maintenance_extension = false;
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
    /* One maintenance5 presence enumeration (count + list); nothing to chain. */
    assert(enumerations == 2 && queries == 1 && chains == 0 && !selection.maintenance5_enabled);
    assert(selection.extension == NULL && selection.base.robustBufferAccess == 1);
    assert(selection.base.shaderInt64 == 0);
    options.enable_shader_demote_if_supported = true;
    assert(doe_vk_prepare_features(&context, &options, &selection, &error) == DOE_VK_OK);
    assert(enumerations == 6 && queries == 2 && chains == 1);
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
    maintenance5(&context);
    puts("PASS default-on maintenance5 only with dynamic_rendering, maintenance5 and its reported feature, explicit optional demote selection, absent/false/malformed extension controls, robust-only baseline, failed/incomplete enumeration and unchanged failure outputs");
    return 0;
}
