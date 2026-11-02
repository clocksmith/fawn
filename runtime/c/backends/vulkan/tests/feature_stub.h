/* SPDX-License-Identifier: MIT
 * Explicitly unsupported optional probes for narrow fake-adapter controls. */
#ifndef DOE_VK_TEST_FEATURE_STUB_H
#define DOE_VK_TEST_FEATURE_STUB_H
static inline bool unsupported_feature_query(VkPhysicalDeviceFeatures2 *query) {
    if (!query->pNext) return false;
    const VkStructureType type=*(const VkStructureType *)query->pNext;
    return type==VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_SUBGROUP_EXTENDED_TYPES_FEATURES ||
        type==VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VARIABLE_POINTERS_FEATURES;
}
static inline void unsupported_subgroup_properties(VkPhysicalDevice device,VkPhysicalDeviceProperties2 *query) {
    (void)device;
    assert(query->sType==VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 && query->pNext);
    VkPhysicalDeviceSubgroupProperties *subgroup=query->pNext;
    assert(subgroup->sType==VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES && !subgroup->pNext);
    /* Zero-initialized fields retain explicitly absent subgroup capabilities. */
}
#endif
