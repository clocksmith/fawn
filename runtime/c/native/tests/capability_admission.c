/* SPDX-License-Identifier: MIT
 * Pure feature-composition control; this does not query or emulate a device. */
#include "../src/internal.h"
#include "../src/feature_formats.h"
#include "../src/d3d12_frontend.h"
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
    WGPUFeatureName duplicate[2] = {request,request};
    descriptor.requiredFeatures = duplicate;
    descriptor.requiredFeatureCount = 2;
    enabled = 0xa5a5a5a5;
    assert(!native_requested_features(&backend,&descriptor,&enabled) && enabled == 0xa5a5a5a5);
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

static void optional_format_features(void)
{
    native_backend backend={.kind=WGPUBackendType_D3D12};
    doe_dx_caps *caps=&backend.value.d3d12.caps;
    const uint32_t tier1=NATIVE_FEATURE(WGPUFeatureName_TextureFormatsTier1);
    const uint32_t tier2=NATIVE_FEATURE(WGPUFeatureName_TextureFormatsTier2);
    const uint32_t rg11=NATIVE_FEATURE(WGPUFeatureName_RG11B10UfloatRenderable);
    caps->texture_formats_tier1=true;
    caps->texture_formats_tier2=true;
    assert(!(native_adapter_features(&backend) & (tier1|tier2)));
    caps->rg11b10ufloat_renderable=true;
    assert((native_adapter_features(&backend) & (tier1|tier2|rg11))==(tier1|tier2|rg11));
    WGPUFeatureName request=WGPUFeatureName_TextureFormatsTier2;
    WGPUDeviceDescriptor descriptor={.requiredFeatureCount=1,.requiredFeatures=&request};
    uint32_t enabled=0;
    assert(native_requested_features(&backend,&descriptor,&enabled));
    assert(enabled==(tier1|tier2|rg11));
    assert(native_requested_features(&backend,NULL,&enabled) && !enabled);
    caps->texture_formats_tier1=false;
    enabled=0xa5a5a5a5;
    assert(!native_requested_features(&backend,&descriptor,&enabled) && enabled==0xa5a5a5a5);
    caps->bgra8unorm_storage=true;
    assert(native_adapter_features(&backend) & NATIVE_FEATURE(WGPUFeatureName_BGRA8UnormStorage));
    request=WGPUFeatureName_BGRA8UnormStorage;
    assert(native_requested_features(&backend,&descriptor,&enabled));
    assert(enabled==NATIVE_FEATURE(request));
    assert(!native_d3d12_core_supported(&backend));
    assert(!native_limits_support_core(NULL));
    WGPULimits zero={0};
    assert(!native_limits_support_core(&zero));
    caps->feature_level=0xb000;
    caps->limits=(doe_dx_limits){.max_buffer_size=268435456,.max_descriptors=64,
        .max_cbvs=14,.max_srvs=128,.max_uavs=64,.uniform_offset_alignment=256,
        .max_uniform_bytes=65536,.max_compute_shared_bytes=32768,.max_compute_threads=1024,
        .max_compute_size={1024,1024,64},.max_dispatch_groups={65535,65535,65535}};
    assert(native_d3d12_limits(caps,&zero)==WGPUStatus_Success);
    assert(native_limits_support_core(&zero));
    zero.maxStorageBuffersPerShaderStage=7;
    assert(!native_limits_support_core(&zero));
    zero.maxStorageBuffersPerShaderStage=8;
    zero.minUniformBufferOffsetAlignment=512;
    assert(!native_limits_support_core(&zero));
    zero.minUniformBufferOffsetAlignment=128;
    assert(native_limits_support_core(&zero));
    zero.maxImmediateSize=WGPU_LIMIT_U32_UNDEFINED;
    assert(!native_limits_support_core(&zero));
}

int main(void)
{
    pointer_features();
    d3d_features();
    metal_graphics_features();
    optional_format_features();
    puts("PASS independent pointer capability gates and queried D3D12 half/format feature composition; host only");
    return 0;
}
