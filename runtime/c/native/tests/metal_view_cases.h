/* SPDX-License-Identifier: MIT */
#ifndef DOE_NATIVE_TEST_METAL_VIEW_CASES_H
#define DOE_NATIVE_TEST_METAL_VIEW_CASES_H
static void metal_swizzle_case(state *s, mock_native *mock, bool enabled)
{
    WGPUTextureDescriptor image = WGPU_TEXTURE_DESCRIPTOR_INIT;
    image.format = WGPUTextureFormat_RGBA8Unorm;
    image.size = (WGPUExtent3D){4,4,1};
    image.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_StorageBinding |
        WGPUTextureUsage_RenderAttachment;
    WGPUTexture texture = wgpuDeviceCreateTexture(s->device, &image);
    assert(texture);
    WGPUTextureComponentSwizzleDescriptor swizzle = WGPU_TEXTURE_COMPONENT_SWIZZLE_DESCRIPTOR_INIT;
    WGPUTextureViewDescriptor descriptor = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    descriptor.nextInChain = &swizzle.chain;
    WGPUTextureView view = wgpuTextureCreateView(texture, &descriptor);
    assert(view && !doe_resource_view_description((doe_resource_view *)view)->swizzled);
    wgpuTextureViewRelease(view);
    swizzle.swizzle.r = WGPUComponentSwizzle_Force32;
    wgpuDevicePushErrorScope(s->device, WGPUErrorFilter_Validation);
    assert(!wgpuTextureCreateView(texture, &descriptor));
    expect_validation(s);
    swizzle.swizzle.r = WGPUComponentSwizzle_Undefined;
    swizzle.chain.next = &swizzle.chain;
    wgpuDevicePushErrorScope(s->device, WGPUErrorFilter_Validation);
    assert(!wgpuTextureCreateView(texture, &descriptor));
    expect_validation(s);
    swizzle.chain.next = NULL;
    swizzle.swizzle = (WGPUTextureComponentSwizzle){WGPUComponentSwizzle_B,WGPUComponentSwizzle_G,
        WGPUComponentSwizzle_R,WGPUComponentSwizzle_One};
    if (!enabled) wgpuDevicePushErrorScope(s->device, WGPUErrorFilter_Validation);
    view = wgpuTextureCreateView(texture, &descriptor);
    if (!enabled) {
        assert(!view);
        expect_validation(s);
    } else {
        assert(view && doe_resource_view_description((doe_resource_view *)view)->swizzled);
        assert(mock->view_desc.red == DOE_MTL_SWIZZLE_BLUE && mock->view_desc.green == DOE_MTL_SWIZZLE_GREEN &&
            mock->view_desc.blue == DOE_MTL_SWIZZLE_RED && mock->view_desc.alpha == DOE_MTL_SWIZZLE_ONE);
        WGPUBindGroupLayoutEntry binding = {.binding=0,.visibility=WGPUShaderStage_Compute};
        binding.storageTexture = (WGPUStorageTextureBindingLayout){.access=WGPUStorageTextureAccess_WriteOnly,
            .format=WGPUTextureFormat_RGBA8Unorm,.viewDimension=WGPUTextureViewDimension_2D};
        WGPUBindGroupLayoutDescriptor layout_desc = {.entryCount=1,.entries=&binding};
        WGPUBindGroupLayout layout = wgpuDeviceCreateBindGroupLayout(s->device, &layout_desc);
        assert(layout);
        WGPUBindGroupEntry entry = {.binding=0,.textureView=view};
        WGPUBindGroupDescriptor group_desc = {.layout=layout,.entryCount=1,.entries=&entry};
        wgpuDevicePushErrorScope(s->device, WGPUErrorFilter_Validation);
        assert(!wgpuDeviceCreateBindGroup(s->device, &group_desc));
        expect_validation(s);
        wgpuBindGroupLayoutRelease(layout);
        WGPURenderPassColorAttachment color = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
        color.view = view; color.loadOp = WGPULoadOp_Clear; color.storeOp = WGPUStoreOp_Store;
        WGPURenderPassDescriptor pass_desc = {.colorAttachmentCount=1,.colorAttachments=&color};
        wgpuDevicePushErrorScope(s->device, WGPUErrorFilter_Validation);
        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device, NULL);
        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &pass_desc);
        if (pass) {
            wgpuRenderPassEncoderEnd(pass);
            wgpuRenderPassEncoderRelease(pass);
        }
        WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, NULL);
        wgpuCommandBufferRelease(commands);
        wgpuCommandEncoderRelease(encoder);
        expect_validation(s);
        wgpuTextureRelease(texture);
        texture = NULL;
        wgpuTextureViewRelease(view);
    }
    wgpuTextureRelease(texture);
    assert(!s->errors);
    if (enabled) puts("PASS public Metal shared swizzle channels, feature/chain/use admission and early-release ownership; host ledger only");
}
#endif
