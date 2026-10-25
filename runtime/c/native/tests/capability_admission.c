/* SPDX-License-Identifier: MIT
 * Pure feature-composition control; this does not query or emulate a device. */
#include "../src/internal.h"
#include <assert.h>
#include <stdio.h>

static void pointer_features(void)
{
    uint32_t words[] = {0x07230203,0x00010300,0,1,0,0x00020011,1,0x00020011,4441};
    doe_shader_view view = {.words=words,.counts={.words=sizeof(words)/sizeof(words[0])}};
    doe_vk_caps caps = {0};
    assert(!native_shader_capabilities(&view, &caps));
    caps.variable_pointers_storage = true;
    assert(native_shader_capabilities(&view, &caps));
    words[8] = 4442;
    assert(!native_shader_capabilities(&view, &caps));
    caps.variable_pointers = true;
    assert(native_shader_capabilities(&view, &caps));
    words[8] = 4436; /* Variable pointers never enable 16-bit stage storage. */
    assert(!native_shader_capabilities(&view, &caps));
}

static void d3d_features(void)
{
    native_backend backend = {.kind=WGPUBackendType_D3D12};
    doe_dx_caps *caps = &backend.value.d3d12.caps;
    uint32_t enabled = UINT32_MAX;
    assert(native_requested_features(&backend, NULL, &enabled) && !enabled);
    WGPUFeatureName request = WGPUFeatureName_CoreFeaturesAndLimits;
    WGPUDeviceDescriptor descriptor = {.requiredFeatureCount=1,.requiredFeatures=&request};
    enabled = 0xa5a5a5a5;
    assert(!native_requested_features(&backend, &descriptor, &enabled) && enabled == 0xa5a5a5a5);
    request = (WGPUFeatureName)-1;
    assert(!native_requested_features(&backend, &descriptor, &enabled) && enabled == 0xa5a5a5a5);
    native_device unconfigured = {.features=UINT32_MAX};
    assert(!native_feature_enabled(&unconfigured, (WGPUFeatureName)-1));
    const uint32_t half = NATIVE_FEATURE(WGPUFeatureName_ShaderF16);
    assert(!(native_adapter_features(&backend) & half));
    caps->shader_model = 0x62;
    assert(!(native_adapter_features(&backend) & half));
    caps->native_16bit_shader_ops = true;
    assert(native_adapter_features(&backend) & half);
    caps->shader_model = 0x61;
    assert(!(native_adapter_features(&backend) & half));
    const uint32_t formats = NATIVE_FEATURE(WGPUFeatureName_TextureCompressionBC) |
        NATIVE_FEATURE(WGPUFeatureName_Depth32FloatStencil8);
    assert(!(native_adapter_features(&backend) & formats));
    caps->texture_compression_bc = true;
    assert((native_adapter_features(&backend) & formats) == NATIVE_FEATURE(WGPUFeatureName_TextureCompressionBC));
    caps->depth32float_stencil8 = true;
    assert((native_adapter_features(&backend) & formats) == formats);
    const uint32_t graphics = NATIVE_FEATURE(WGPUFeatureName_ClipDistances) |
        NATIVE_FEATURE(WGPUFeatureName_PrimitiveIndex) | NATIVE_FEATURE(WGPUFeatureName_DualSourceBlending);
    assert(!(native_adapter_features(&backend) & graphics));
    caps->feature_level = 0xb000;
    assert((native_adapter_features(&backend) & graphics) == graphics);
    assert(native_adapter_features(&backend) & NATIVE_FEATURE(WGPUFeatureName_IndirectFirstInstance));
    caps->shader_model = 0x51;
    assert(!(native_adapter_features(&backend) & NATIVE_FEATURE(WGPUFeatureName_IndirectFirstInstance)));
    caps->shader_model = 0x61;
    request = WGPUFeatureName_PrimitiveIndex;
    assert(native_requested_features(&backend, &descriptor, &enabled));
    assert(enabled == NATIVE_FEATURE(request));
}

static void metal_graphics_features(void)
{
    native_backend backend = {.kind=WGPUBackendType_Metal};
    const uint32_t primitive = NATIVE_FEATURE(WGPUFeatureName_PrimitiveIndex);
    const uint32_t blend = NATIVE_FEATURE(WGPUFeatureName_DualSourceBlending);
    assert(native_adapter_features(&backend) & NATIVE_FEATURE(WGPUFeatureName_ClipDistances));
    assert(!(native_adapter_features(&backend) & (primitive | blend)));
    backend.value.metal.caps.primitive_index = true;
    assert((native_adapter_features(&backend) & (primitive | blend)) == primitive);
    backend.value.metal.caps.dual_source_blending = true;
    assert((native_adapter_features(&backend) & (primitive | blend)) == (primitive | blend));
}

int main(void)
{
    pointer_features();
    d3d_features();
    metal_graphics_features();
    puts("PASS independent pointer capability gates and queried D3D12 half/format feature composition; host only");
    return 0;
}
