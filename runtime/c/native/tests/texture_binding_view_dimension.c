/* SPDX-License-Identifier: MIT
 * Core-mode textureBindingViewDimension: the pinned header requires accepting the
 * WGPUTextureBindingViewDimension chain, whose contents are ignored on a device
 * with flexible (core) texture views. The getter therefore reports Undefined and
 * the chained value never restricts later view dimensions. */
#include "consumer.h"

static WGPUTexture create(state *s, uint32_t layers, const WGPUChainedStruct *chain)
{
    WGPUTextureDescriptor descriptor = WGPU_TEXTURE_DESCRIPTOR_INIT;
    descriptor.nextInChain = (WGPUChainedStruct *)chain;
    descriptor.format = WGPUTextureFormat_RGBA8Unorm;
    descriptor.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
    descriptor.size = (WGPUExtent3D){4, 4, layers};
    return wgpuDeviceCreateTexture(s->device, &descriptor);
}

static void view(WGPUTexture texture, WGPUTextureViewDimension dimension, uint32_t layers)
{
    WGPUTextureViewDescriptor descriptor = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    descriptor.dimension = dimension;
    descriptor.arrayLayerCount = layers;
    WGPUTextureView result = wgpuTextureCreateView(texture, &descriptor);
    assert(result);
    wgpuTextureViewRelease(result);
}

int main(void)
{
    state s = {0};
    consumer_init(&s);
    WGPUTexture plain = create(&s, 1, NULL);
    assert(plain && wgpuTextureGetTextureBindingViewDimension(plain) == WGPUTextureViewDimension_Undefined);
    wgpuTextureRelease(plain);

    /* A compatibility-mode request for a 2D binding of a six-layer texture is
     * ignored: cube, 2D-array and single-layer 2D views all remain available. */
    WGPUTextureBindingViewDimension binding = WGPU_TEXTURE_BINDING_VIEW_DIMENSION_INIT;
    binding.textureBindingViewDimension = WGPUTextureViewDimension_2D;
    WGPUTexture layered = create(&s, 6, &binding.chain);
    assert(layered && wgpuTextureGetTextureBindingViewDimension(layered) == WGPUTextureViewDimension_Undefined);
    view(layered, WGPUTextureViewDimension_Cube, 6);
    view(layered, WGPUTextureViewDimension_2DArray, 6);
    view(layered, WGPUTextureViewDimension_2D, 1);
    wgpuTextureRelease(layered);
    binding.textureBindingViewDimension = WGPUTextureViewDimension_Cube;
    WGPUTexture cube = create(&s, 6, &binding.chain);
    assert(cube && wgpuTextureGetTextureBindingViewDimension(cube) == WGPUTextureViewDimension_Undefined);
    view(cube, WGPUTextureViewDimension_2D, 1);
    wgpuTextureRelease(cube);

    /* Unknown or repeated chain nodes still reject explicitly. */
    WGPUShaderSourceWGSL unrelated = WGPU_SHADER_SOURCE_WGSL_INIT;
    wgpuDevicePushErrorScope(s.device, WGPUErrorFilter_Validation);
    assert(!create(&s, 1, &unrelated.chain));
    expect_validation(&s);
    WGPUTextureBindingViewDimension second = WGPU_TEXTURE_BINDING_VIEW_DIMENSION_INIT;
    binding.chain.next = &second.chain;
    wgpuDevicePushErrorScope(s.device, WGPUErrorFilter_Validation);
    assert(!create(&s, 1, &binding.chain));
    expect_validation(&s);
    assert(wgpuTextureGetTextureBindingViewDimension(NULL) == WGPUTextureViewDimension_Undefined);
    consumer_finish(&s);
    printf("PASS public texture binding view dimension: accepted ignored core-mode chain, Undefined getter, "
        "unrestricted cube/2D-array/2D views and unknown/repeated chain rejection %s\n", adapter_class_scope());
    return 0;
}
