/* SPDX-License-Identifier: MIT
 * Count real discovery-boundary calls; adapter queries consume frozen facts. */
#include "../src/feature_formats.h"
#include <assert.h>
#include <stdio.h>

typedef struct format_driver {
    unsigned calls[NATIVE_FORMAT_QUERY_COUNT];
    bool fail_float32;
    bool reject_queries;
} format_driver;

doe_vk_status __wrap_doe_vk_texture_format_caps(doe_vk_context *, doe_vk_texture_format, doe_vk_format_caps *);
doe_vk_status __wrap_doe_vk_texture_format_caps(doe_vk_context *context,
    doe_vk_texture_format format, doe_vk_format_caps *out)
{
    format_driver *driver = (format_driver *)context;
    assert(driver && !driver->reject_queries);
    uint32_t index = (uint32_t)format;
    assert(index < NATIVE_FORMAT_QUERY_COUNT);
    driver->calls[index]++;
    if (driver->fail_float32 && format == DOE_VK_FORMAT_R32_FLOAT) return DOE_VK_NATIVE_FAILURE;
    *out = (doe_vk_format_caps){.sampled=true,.storage=true,.color_attachment=true,
        .blendable=true,.filterable=true,.copy_src=true,.copy_dst=true,
        .depth_stencil_attachment=true,.attachment_sample_counts=4,.volume=true};
    return DOE_VK_OK;
}

static native_adapter discover(format_driver *driver)
{
    native_adapter adapter = {.backend={.kind=WGPUBackendType_Vulkan}};
    adapter.backend.value.vulkan.context = (doe_vk_context *)driver;
    adapter.backend.value.vulkan.caps = (doe_vk_caps){.shader_f16=true,
        .texture_compression_bc=true,.texture_compression_etc2=true,
        .texture_compression_astc=true,.storage_image_extended_formats=true};
    adapter.features = native_adapter_features(&adapter.backend);
    assert(driver->calls[DOE_VK_FORMAT_R32_FLOAT] == 1);
    for (unsigned i = 0; i < NATIVE_FORMAT_QUERY_COUNT; ++i) assert(driver->calls[i] <= 1);
    return adapter;
}

int main(void)
{
    format_driver first = {.fail_float32=true}, second = {0};
    native_adapter a = discover(&first), b = discover(&second);
    assert(!wgpuAdapterHasFeature((WGPUAdapter)&a, WGPUFeatureName_Float32Filterable));
    assert(wgpuAdapterHasFeature((WGPUAdapter)&b, WGPUFeatureName_Float32Filterable));
    first.reject_queries = second.reject_queries = true;
    for (unsigned repetition = 0; repetition < 16; ++repetition) {
        WGPUSupportedFeatures list = {0};
        wgpuAdapterGetFeatures((WGPUAdapter)&a, &list);
        for (size_t i = 0; i < list.featureCount; ++i)
            assert(wgpuAdapterHasFeature((WGPUAdapter)&a, list.features[i]));
        wgpuSupportedFeaturesFreeMembers(list);
        uint32_t enabled = 0;
        assert(native_requested_features(a.features, NULL, &enabled));
        assert(enabled == NATIVE_FEATURE(WGPUFeatureName_CoreFeaturesAndLimits));
        WGPUFeatureName half = WGPUFeatureName_ShaderF16;
        WGPUDeviceDescriptor desc = {.requiredFeatureCount=1,.requiredFeatures=&half};
        assert(native_requested_features(a.features, &desc, &enabled));
        native_device device = {.features=enabled};
        assert(wgpuDeviceHasFeature((WGPUDevice)&device, half));
        assert(!wgpuDeviceHasFeature((WGPUDevice)&device, WGPUFeatureName_TextureCompressionBC));
    }
    puts("PASS one query per Vulkan format including failed formats; independent adapter snapshots; enabled device subset; host only");
    return 0;
}
