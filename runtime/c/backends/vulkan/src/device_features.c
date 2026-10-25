/* SPDX-License-Identifier: MIT */
#include "feature_internal.h"
#include "surface_internal.h"

static const uint8_t demote_extension[] = "VK_EXT_shader_demote_to_helper_invocation";


doe_vk_status doe_vk_prepare_features(doe_vk_context *context, const doe_vk_options *options,
    doe_vk_feature_selection *out, doe_vk_error *error) {
    if (context == NULL || options == NULL || out == NULL)
        return doe_vk_fail(DOE_VK_INVALID_ARGUMENT, "device feature selection", error);
    if (options->subgroup_selection < DOE_VK_SUBGROUP_LEGACY ||
        options->subgroup_selection > DOE_VK_SUBGROUP_SUPPRESS_MEMORY256_OR_SINGLE)
        return doe_vk_fail(DOE_VK_INVALID_ARGUMENT, "subgroup policy", error);
    VkPhysicalDeviceSubgroupProperties subgroup = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES};
    VkPhysicalDeviceProperties2 subgroup_query = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
        .pNext = &subgroup};
    context->vk.vkGetPhysicalDeviceProperties2(context->physical, &subgroup_query);
    context->caps.subgroup_size = subgroup.subgroupSize;
    context->caps.subgroup_min_size = context->caps.subgroup_max_size = subgroup.subgroupSize;
    context->caps.subgroup_stages = subgroup.supportedStages;
    context->caps.subgroup_operations = subgroup.supportedOperations;
    context->caps.subgroup_quad_all_stages = subgroup.quadOperationsInAllStages != 0;
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
    if (options->subgroup_selection != DOE_VK_SUBGROUP_LEGACY) {
        const uint8_t *name = (const uint8_t *)"VK_EXT_subgroup_size_control";
        bool present = false;
        doe_vk_status status = doe_vk_extensions_present(context, true, &name, 1, &present, error);
        if (status != DOE_VK_OK) return status;
        if (present) {
            VkPhysicalDeviceSubgroupSizeControlFeatures control = {
                .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_FEATURES};
            VkPhysicalDeviceFeatures2 query = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
                .pNext = &control};
            context->vk.vkGetPhysicalDeviceFeatures2(context->physical, &query);
            VkPhysicalDeviceSubgroupSizeControlProperties limits = {
                .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_PROPERTIES};
            VkPhysicalDeviceProperties2 properties = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
                .pNext = &limits};
            context->vk.vkGetPhysicalDeviceProperties2(context->physical, &properties);
            if (control.subgroupSizeControl) {
                selection.subgroup = (VkPhysicalDeviceSubgroupSizeControlFeatures){
                    .sType = control.sType, .subgroupSizeControl = 1};
                context->caps.subgroup_size_control = true;
                context->caps.subgroup_min_size = limits.minSubgroupSize;
                context->caps.subgroup_max_size = limits.maxSubgroupSize;
                context->caps.subgroup_required_stages = limits.requiredSubgroupSizeStages;
                context->caps.max_compute_workgroup_subgroups = limits.maxComputeWorkgroupSubgroups;
            }
        }
    }
    /* The selected device is Vulkan 1.2. Arithmetic and buffer storage
     * capabilities are independent: never expose half arithmetic alone as the
     * admitted shader-f16 buffer contract. */
    VkPhysicalDevice16BitStorageFeatures storage16 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES};
    VkPhysicalDeviceShaderFloat16Int8Features float16 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES, .pNext = &storage16};
    VkPhysicalDeviceFeatures2 half_query = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext = &float16};
    context->vk.vkGetPhysicalDeviceFeatures2(context->physical, &half_query);
    if (float16.shaderFloat16 && storage16.storageBuffer16BitAccess &&
        storage16.uniformAndStorageBuffer16BitAccess) {
        selection.float16 = (VkPhysicalDeviceShaderFloat16Int8Features){
            .sType = float16.sType, .shaderFloat16 = 1};
        selection.storage16 = (VkPhysicalDevice16BitStorageFeatures){.sType = storage16.sType,
            .storageBuffer16BitAccess = 1, .uniformAndStorageBuffer16BitAccess = 1,
            .storagePushConstant16 = storage16.storagePushConstant16};
        context->caps.storage_push_constant16 = storage16.storagePushConstant16 != 0;
        selection.shader_f16 = true;
    }
    VkPhysicalDeviceShaderSubgroupExtendedTypesFeatures extended = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_SUBGROUP_EXTENDED_TYPES_FEATURES};
    VkPhysicalDeviceFeatures2 extended_query = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext = &extended};
    context->vk.vkGetPhysicalDeviceFeatures2(context->physical, &extended_query);
    if (extended.shaderSubgroupExtendedTypes) {
        selection.subgroup_extended = extended;
        context->caps.subgroup_extended_types = true;
    }
    /* The backend requires Vulkan1.2. Core promotion does not imply optional
     * feature support: query and explicitly enable only the returned features. */
    VkPhysicalDeviceVariablePointersFeatures pointers = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VARIABLE_POINTERS_FEATURES};
    VkPhysicalDeviceFeatures2 pointers_query = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext = &pointers};
    context->vk.vkGetPhysicalDeviceFeatures2(context->physical, &pointers_query);
    if (pointers.variablePointersStorageBuffer) {
        selection.pointers = pointers;
        selection.pointers.variablePointers = pointers.variablePointers ? 1u : 0u;
        context->caps.variable_pointers_storage = true;
        context->caps.variable_pointers = pointers.variablePointers != 0;
    }
    /* Optional full uint32 index range; uint32 index draws reject without it. */
    if (features.features.fullDrawIndexUint32) selection.base.fullDrawIndexUint32 = 1;
    /* Optional cube-array views; rejected when the device does not offer them. */
    if (features.features.imageCubeArray) selection.base.imageCubeArray = 1;
    /* Optional per-target blend and anisotropic filtering; absent features stay
     * disabled and the corresponding requests reject at admission. */
    if (features.features.independentBlend) selection.base.independentBlend = 1;
    if (features.features.samplerAnisotropy) selection.base.samplerAnisotropy = 1;
    /* Optional stage/output semantics. Geometry capability is needed for
     * fragment PrimitiveId even without a geometry-stage pipeline. */
    if (features.features.shaderClipDistance) selection.base.shaderClipDistance = 1;
    if (features.features.geometryShader) selection.base.geometryShader = 1;
    if (features.features.dualSrcBlend) selection.base.dualSrcBlend = 1;
    /* Optional storage-image formats and fragment stores, enabled only when offered. */
    if (features.features.shaderStorageImageExtendedFormats) selection.base.shaderStorageImageExtendedFormats = 1;
    if (features.features.fragmentStoresAndAtomics) selection.base.fragmentStoresAndAtomics = 1;
    /* Optional nonzero firstInstance in indirect draws and depth clamping without
     * clipping (WebGPU indirect-first-instance and unclippedDepth). */
    if (features.features.drawIndirectFirstInstance) selection.base.drawIndirectFirstInstance = 1;
    if (features.features.depthClamp) selection.base.depthClamp = 1;
    /* Optional BC block compression (WebGPU texture-compression-bc). */
    if (features.features.textureCompressionBC) selection.base.textureCompressionBC = 1;
    if (features.features.textureCompressionETC2) selection.base.textureCompressionETC2 = 1;
    if (features.features.textureCompressionASTC_LDR) selection.base.textureCompressionASTC_LDR = 1;
    /* Optional per-sample shading (WebGPU core sample_index and sample interpolation). */
    if (features.features.sampleRateShading) selection.base.sampleRateShading = 1;
    /* Optional clamped depth bias (WebGPU depthBiasClamp). */
    if (features.features.depthBiasClamp) selection.base.depthBiasClamp = 1;
    if (available && demote.shaderDemoteToHelperInvocation) {
        selection.demote = demote;
        selection.extension = demote_extension;
    }
    /* Optional maintenance5 (default PointSize 1.0 among others). The 1.2
     * instance needs its VK_KHR_dynamic_rendering dependency too; the feature
     * is enabled only when both extensions exist and the device reports it. */
    const uint8_t *maintenance_names[] = {(const uint8_t *)"VK_KHR_dynamic_rendering",
        (const uint8_t *)"VK_KHR_maintenance5"};
    bool maintenance_present[2] = {false, false};
    doe_vk_status maintenance_status = doe_vk_extensions_present(context, true, maintenance_names, 2,
        maintenance_present, error);
    if (maintenance_status != DOE_VK_OK) return maintenance_status;
    if (maintenance_present[0] && maintenance_present[1]) {
        VkPhysicalDeviceMaintenance5FeaturesKHR maintenance5 = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_5_FEATURES_KHR
        };
        VkPhysicalDeviceFeatures2 query = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = &maintenance5};
        context->vk.vkGetPhysicalDeviceFeatures2(context->physical, &query);
        if (maintenance5.maintenance5) {
            selection.maintenance5 = (VkPhysicalDeviceMaintenance5FeaturesKHR){
                .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_5_FEATURES_KHR, .maintenance5 = 1};
            selection.maintenance5_enabled = true;
        }
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
