/* SPDX-License-Identifier: MIT
 * Adapter info strings: vendor from the PCI/Khronos vendor ID (lowercase, empty
 * when unknown), no architecture family, and the driver's device name as both
 * device and description; adapter and device report the same. Without the
 * "subgroups" feature the subgroup sizes are the spec defaults 4 and 128. The
 * doeNativeAdapterGetPciIdentity extension reports the same vendor and device
 * IDs, a Vulkan driver version, and zeros for a NULL adapter. */
#include "consumer.h"
#include "doe_native_adapter.h"
#include "doe_native_legacy.h"
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

typedef struct feature_reply {
    WGPURequestDeviceStatus status;
    WGPUDevice device;
} feature_reply;
static void feature_device(WGPURequestDeviceStatus status, WGPUDevice device,
    WGPUStringView message, void *data, void *unused)
{
    (void)message; (void)unused;
    *(feature_reply *)data = (feature_reply){status,device};
}
static void required_feature_sets(state *s)
{
    WGPUSupportedFeatures offered = WGPU_SUPPORTED_FEATURES_INIT;
    wgpuAdapterGetFeatures(s->adapter,&offered);
    assert(offered.featureCount);
    WGPUFeatureName names[64];
    for (size_t i = 0; i < 64; ++i) names[i] = offered.features[0];
    WGPUDeviceDescriptor descriptor = WGPU_DEVICE_DESCRIPTOR_INIT;
    descriptor.requiredFeatures = names;
    descriptor.requiredFeatureCount = 1;
    WGPUDevice unique = wgpuAdapterCreateDevice(s->adapter,&descriptor);
    assert(unique);
    descriptor.requiredFeatureCount = 64;
    WGPUDevice duplicate = wgpuAdapterCreateDevice(s->adapter,&descriptor);
    assert(duplicate);
    WGPUSupportedFeatures first = WGPU_SUPPORTED_FEATURES_INIT, repeated = WGPU_SUPPORTED_FEATURES_INIT;
    wgpuDeviceGetFeatures(unique,&first);
    wgpuDeviceGetFeatures(duplicate,&repeated);
    assert(first.featureCount == repeated.featureCount);
    for (size_t i = 0; i < first.featureCount; ++i)
        assert(wgpuDeviceHasFeature(duplicate,first.features[i]));
    wgpuSupportedFeaturesFreeMembers(first);
    wgpuSupportedFeaturesFreeMembers(repeated);
    wgpuDeviceRelease(duplicate);
    wgpuDeviceRelease(unique);
    feature_reply reply = {0};
    WGPURequestDeviceCallbackInfo callback = {.mode=WGPUCallbackMode_AllowSpontaneous,
        .callback=feature_device,.userdata1=&reply};
    assert(wgpuAdapterRequestDevice(s->adapter,&descriptor,callback).id);
    assert(reply.status == WGPURequestDeviceStatus_Success && reply.device);
    wgpuDeviceRelease(reply.device);
    names[63] = WGPUFeatureName_Force32;
    reply = (feature_reply){0};
    assert(wgpuAdapterRequestDevice(s->adapter,&descriptor,callback).id);
    assert(reply.status == WGPURequestDeviceStatus_Error && !reply.device);
    assert(!wgpuAdapterCreateDevice(s->adapter,&descriptor));
    wgpuSupportedFeaturesFreeMembers(offered);
}

int main(void)
{
    state s = {0};
    consumer_init(&s);
    required_feature_sets(&s);
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
    printf("PASS repeated supported required-feature set admission and unknown-member rejection; adapter info vendor \"%.*s\", description \"%.*s\", PCI 0x%x:0x%x driver 0x%x %s\n",
        (int)adapter.vendor.length, adapter.vendor.data, (int)adapter.description.length, adapter.description.data,
        vendor_id, device_id, driver_version, adapter_class_scope());
    wgpuAdapterInfoFreeMembers(adapter);
    wgpuAdapterInfoFreeMembers(device);
    consumer_finish(&s);
    return 0;
}
