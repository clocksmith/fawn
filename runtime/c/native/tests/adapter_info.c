/* SPDX-License-Identifier: MIT
 * Adapter info strings: vendor from the PCI/Khronos vendor ID (lowercase, empty
 * when unknown), Metal's highest supported family as the architecture (Vulkan and
 * D3D12 hardware adapters: the family their PCI IDs map to, "" when unlisted or
 * software), and the driver's device name as both
 * device and description; adapter and device report the same. Without the
 * "subgroups" feature the subgroup sizes are the spec defaults 4 and 128. The
 * doeNativeAdapterGetPciIdentity extension reports the same vendor and device
 * IDs, a Vulkan driver version, and zeros for a NULL adapter. */
#include "consumer.h"
#include "doe_native_adapter.h"
#include "doe_native_legacy.h"
#include <ctype.h>
#include <stdlib.h>
/* The mapping is private to the library; compile its one source here to check
 * listed, neighbouring and unlisted IDs without the hardware. */
#include "../src/gpu_architecture.c"

static bool same(WGPUStringView a, WGPUStringView b)
{
    return a.length == b.length && (!a.length || !memcmp(a.data, b.data, a.length));
}

static bool contains(WGPUStringView text, const char *mark)
{
    const size_t size = strlen(mark);
    for (size_t i = 0; i + size <= text.length; ++i)
        if (!memcmp(text.data + i, mark, size)) return true;
    return false;
}

static void check(const WGPUAdapterInfo *info)
{
    assert(info->device.length && info->device.data);
    assert(same(info->description, info->device));
    assert(info->architecture.data);
    if (info->backendType == WGPUBackendType_Metal) {
        static const char *families[] = {"metal-3", "common-3", "common-2", "common-1"};
        bool known = false;
        for (size_t i = 0; i < sizeof(families) / sizeof(families[0]); ++i)
            known |= same(info->architecture, (WGPUStringView){families[i], strlen(families[i])});
        assert(known);
        assert(info->adapterType == WGPUAdapterType_IntegratedGPU ||
            info->adapterType == WGPUAdapterType_DiscreteGPU);
        if (contains(info->device, "Apple"))
            assert(info->vendorID == 0x106B && same(info->vendor, (WGPUStringView){"apple", 5}));
    } else {
        /* Vulkan and D3D12 keep the PCI IDs and derive the family from them alone;
         * software adapters (lavapipe 0x10005, WARP 0x1414) have no table entry. */
        assert(info->vendorID != 0);
        const char *expected = native_gpu_architecture(info->vendorID, info->deviceID);
        assert(same(info->architecture, (WGPUStringView){expected, strlen(expected)}));
    }
    for (size_t i = 0; i < info->vendor.length; ++i)
        assert(islower((unsigned char)info->vendor.data[i]));
    if (info->vendorID == 0x10005) assert(same(info->vendor, (WGPUStringView){"mesa", 4}));
    if (info->vendorID == 0x1002) assert(same(info->vendor, (WGPUStringView){"amd", 3}));
}

static void expect_architecture(uint32_t vendor_id, uint32_t device_id, const char *family)
{
    const char *actual = native_gpu_architecture(vendor_id, device_id);
    if (strcmp(actual, family)) {
        fprintf(stderr, "FAIL architecture 0x%x:0x%x is \"%s\", expected \"%s\"\n",
            vendor_id, device_id, actual, family);
        abort();
    }
}

static void architecture_mapping(void)
{
    expect_architecture(0x1002, 0x1586, "rdna-3.5"); /* Strix Halo: RDNA 3.5, not RDNA 3 */
    expect_architecture(0x1002, 0x150E, "rdna-3.5");
    expect_architecture(0x1002, 0x744C, "rdna-3");
    expect_architecture(0x1002, 0x15BF, "rdna-3");
    expect_architecture(0x1002, 0x73F0, "rdna-3"); /* Navi 33 among Navi 2x IDs */
    expect_architecture(0x1002, 0x73FF, "rdna-2");
    expect_architecture(0x1002, 0x1681, "rdna-2");
    expect_architecture(0x1002, 0x7550, "rdna-4");
    expect_architecture(0x8086, 0x9A49, "gen-12lp");
    /* Unlisted: unknown AMD device, a listed device ID under another vendor,
     * NVIDIA (no table entries), lavapipe and WARP all report unknown. */
    expect_architecture(0x1002, 0x1587, "");
    expect_architecture(0x1002, 0x0000, "");
    expect_architecture(0x8086, 0x1586, "");
    expect_architecture(0x1022, 0x1586, "");
    expect_architecture(0x10DE, 0x2684, "");
    expect_architecture(0x10005, 0x0000, "");
    expect_architecture(0x1414, 0x008C, "");
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
    architecture_mapping();
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
    printf("PASS architecture mapping; repeated supported required-feature set admission and unknown-member rejection; adapter info vendor \"%.*s\", architecture \"%.*s\", description \"%.*s\", PCI 0x%x:0x%x driver 0x%x %s\n",
        (int)adapter.vendor.length, adapter.vendor.data, (int)adapter.architecture.length,
        adapter.architecture.data, (int)adapter.description.length, adapter.description.data,
        vendor_id, device_id, driver_version, adapter_class_scope());
    wgpuAdapterInfoFreeMembers(adapter);
    wgpuAdapterInfoFreeMembers(device);
    consumer_finish(&s);
    return 0;
}
