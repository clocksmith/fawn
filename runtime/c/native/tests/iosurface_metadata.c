/* SPDX-License-Identifier: MIT
 * Real packed IOSurface metadata, before any texture creation. No GPU work. */
#include "consumer.h"
#include "doe_native_shared_memory.h"
#include "iosurface_fixture.h"

static void packed_metadata(state *s, uint32_t code, WGPUTextureFormat expected,
    WGPUTextureFormat srgb)
{
    IOSurfaceRef surface = doe_test_iosurface_create(code);
    assert(surface && IOSurfaceGetPixelFormat(surface) == code);
    WGPUSharedTextureMemoryIOSurfaceDescriptor chain = {
        .chain={.sType=DOE_SHARED_STYPE_IOSURFACE},.ioSurface=surface};
    WGPUSharedTextureMemoryDescriptor descriptor = {.nextInChain=&chain.chain};
    WGPUSharedTextureMemory memory = wgpuDeviceImportSharedTextureMemory(s->device,&descriptor);
    assert(memory);
    CFRelease(surface); /* Import now owns its independent native surface lease. */
    WGPUSharedTextureMemoryProperties properties = {0};
    assert(wgpuSharedTextureMemoryGetProperties(memory,&properties) == WGPUStatus_Success);
    assert(properties.format == expected && properties.size.width == 2 &&
        properties.size.height == 2 && properties.size.depthOrArrayLayers == 1);
    WGPUTextureDescriptor texture_descriptor = WGPU_TEXTURE_DESCRIPTOR_INIT;
    texture_descriptor.dimension = WGPUTextureDimension_2D;
    texture_descriptor.size = properties.size;
    texture_descriptor.format = expected;
    texture_descriptor.usage = WGPUTextureUsage_TextureBinding;
    texture_descriptor.viewFormatCount = 1;
    texture_descriptor.viewFormats = &srgb;
    WGPUTexture texture = wgpuSharedTextureMemoryCreateTexture(memory,&texture_descriptor);
    assert(texture);
    WGPUTextureViewDescriptor view_descriptor = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    view_descriptor.format = srgb;
    WGPUTextureView view = wgpuTextureCreateView(texture,&view_descriptor);
    assert(view);
    wgpuTextureViewRelease(view);
    assert(wgpuSharedTextureMemoryGetProperties(memory,&properties) == WGPUStatus_Success &&
        properties.format == expected);
    /* A compatible sRGB view does not change the physical memory's format.
     * SharedTextureMemory textures must match that format exactly. */
    texture_descriptor.format = srgb;
    texture_descriptor.viewFormatCount = 0;
    texture_descriptor.viewFormats = NULL;
    wgpuDevicePushErrorScope(s->device,WGPUErrorFilter_Validation);
    assert(!wgpuSharedTextureMemoryCreateTexture(memory,&texture_descriptor));
    expect_validation(s);
    wgpuSharedTextureMemoryRelease(memory); /* Texture still retains the memory. */
    wgpuTextureRelease(texture);
}
int main(void)
{
    state s = {0}; consumer_init(&s);
    packed_metadata(&s,UINT32_C(0x42475241),WGPUTextureFormat_BGRA8Unorm,WGPUTextureFormat_BGRA8UnormSrgb);
    packed_metadata(&s,UINT32_C(0x52474241),WGPUTextureFormat_RGBA8Unorm,WGPUTextureFormat_RGBA8UnormSrgb);
    consumer_finish(&s);
    printf("PASS actual BGRA/RGBA IOSurface properties before texture creation, stable physical format, sRGB views, exact-format admission and early native/public releases; no GPU submission %s\n",adapter_class_scope());
    return 0;
}
