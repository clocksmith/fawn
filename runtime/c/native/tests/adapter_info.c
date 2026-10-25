/* SPDX-License-Identifier: MIT
 * Adapter info strings: vendor from the PCI/Khronos vendor ID (lowercase, empty
 * when unknown), no architecture family, and the driver's device name as both
 * device and description; adapter and device report the same. Without the
 * "subgroups" feature the subgroup sizes are the spec defaults 4 and 128. The
 * doeNativeAdapterGetPciIdentity extension reports the same vendor and device
 * IDs, a Vulkan driver version, and zeros for a NULL adapter. */
#include "consumer.h"
#include "doe_native_adapter.h"
#include <ctype.h>

static bool same(WGPUStringView a, WGPUStringView b)
{
    return a.length == b.length && (!a.length || !memcmp(a.data, b.data, a.length));
}

static void check(const WGPUAdapterInfo *info)
{
    assert(info->device.length && info->device.data);
    assert(same(info->description, info->device));
    assert(info->architecture.length == 0 && info->architecture.data);
    for (size_t i = 0; i < info->vendor.length; ++i)
        assert(islower((unsigned char)info->vendor.data[i]));
    if (info->vendorID == 0x10005) assert(same(info->vendor, (WGPUStringView){"mesa", 4}));
    if (info->vendorID == 0x1002) assert(same(info->vendor, (WGPUStringView){"amd", 3}));
}

static bool offers_subgroups(WGPUAdapter adapter)
{
    WGPUSupportedFeatures features = WGPU_SUPPORTED_FEATURES_INIT;
    wgpuAdapterGetFeatures(adapter, &features);
    bool found = false;
    for (size_t i = 0; i < features.featureCount; ++i) found |= features.features[i] == WGPUFeatureName_Subgroups;
    wgpuSupportedFeaturesFreeMembers(features);
    return found;
}

int main(void)
{
    state s = {0};
    consumer_init(&s);
    WGPUAdapterInfo adapter = WGPU_ADAPTER_INFO_INIT, device = WGPU_ADAPTER_INFO_INIT;
    assert(wgpuAdapterGetInfo(s.adapter, &adapter) == WGPUStatus_Success);
    assert(wgpuDeviceGetAdapterInfo(s.device, &device) == WGPUStatus_Success);
    check(&adapter);
    check(&device);
    /* Without "subgroups" the sizes are the spec defaults, not the driver's. */
    if (!offers_subgroups(s.adapter))
        assert(adapter.subgroupMinSize == 4 && adapter.subgroupMaxSize == 128 &&
            device.subgroupMinSize == 4 && device.subgroupMaxSize == 128);
    assert(same(adapter.vendor, device.vendor) && same(adapter.description, device.description) &&
        adapter.vendorID == device.vendorID && adapter.deviceID == device.deviceID);
    uint32_t vendor_id = 1, device_id = 1, driver_version = 1;
    doeNativeAdapterGetPciIdentity(NULL, &vendor_id, &device_id, &driver_version);
    assert(vendor_id == 0 && device_id == 0 && driver_version == 0);
    doeNativeAdapterGetPciIdentity(s.adapter, &vendor_id, &device_id, &driver_version);
    assert(vendor_id == adapter.vendorID && device_id == adapter.deviceID);
    if (adapter.backendType == WGPUBackendType_Vulkan) assert(driver_version != 0);
    doeNativeAdapterGetPciIdentity(s.adapter, NULL, NULL, NULL); /* NULL outputs are skipped */
    printf("PASS adapter info vendor \"%.*s\", description \"%.*s\", PCI 0x%x:0x%x driver 0x%x %s\n",
        (int)adapter.vendor.length, adapter.vendor.data, (int)adapter.description.length, adapter.description.data,
        vendor_id, device_id, driver_version, adapter_class_scope());
    wgpuAdapterInfoFreeMembers(adapter);
    wgpuAdapterInfoFreeMembers(device);
    consumer_finish(&s);
    return 0;
}
