/* SPDX-License-Identifier: MIT
 * Real public/compiler/private Metal admission and native-call identity only.
 * The unchanged depth_sampling.c owns the independent GPU output oracle. */
#ifndef DOE_NATIVE_TEST_METAL_DEPTH_CASES_H
#define DOE_NATIVE_TEST_METAL_DEPTH_CASES_H

static const char *const metal_depth_sources[] = {
    "@group(0) @binding(0) var t:texture_depth_2d;"
    "@group(0) @binding(1) var<storage,read_write> o:array<f32>;"
    "@compute @workgroup_size(1) fn main(){o[0]=textureLoad(t,vec2i(0),0);}",
    "@group(0) @binding(0) var t:texture_2d<u32>;"
    "@group(0) @binding(1) var<storage,read_write> o:array<u32>;"
    "@compute @workgroup_size(1) fn main(){o[0]=textureLoad(t,vec2i(0),0).x;}",
    "@group(0) @binding(0) var t:texture_depth_2d_array;"
    "@group(0) @binding(1) var<storage,read_write> o:array<f32>;"
    "@compute @workgroup_size(1) fn main(){o[0]=textureLoad(t,vec2i(0),1,0);}",
    "@group(0) @binding(0) var t:texture_depth_cube;"
    "@group(0) @binding(1) var<storage,read_write> o:array<u32>;"
    "@compute @workgroup_size(1) fn main(){o[0]=textureDimensions(t).x;}",
    "@group(0) @binding(0) var t:texture_2d<f32>;"
    "@group(0) @binding(1) var<storage,read_write> o:array<f32>;"
    "@compute @workgroup_size(1) fn main(){o[0]=textureLoad(t,vec2i(0),0).x;}"
};

static WGPUTexture metal_depth_image(state *s, WGPUTextureFormat format, uint32_t layers)
{
    WGPUTextureDescriptor descriptor = WGPU_TEXTURE_DESCRIPTOR_INIT;
    descriptor.format = format;
    descriptor.size = (WGPUExtent3D){2,2,layers};
    descriptor.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_RenderAttachment;
    return wgpuDeviceCreateTexture(s->device,&descriptor);
}

static WGPUTextureView metal_depth_view(WGPUTexture texture, WGPUTextureAspect aspect,
    WGPUTextureViewDimension dimension, uint32_t layers)
{
    WGPUTextureViewDescriptor descriptor = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    descriptor.aspect = aspect;
    descriptor.dimension = dimension;
    descriptor.arrayLayerCount = layers;
    return wgpuTextureCreateView(texture,&descriptor);
}

static WGPUComputePipeline metal_float_depth_pipeline(state *s)
{
    WGPUBindGroupLayoutEntry entries[2] = {
        {.binding=0,.visibility=WGPUShaderStage_Compute,.texture={
            .sampleType=WGPUTextureSampleType_UnfilterableFloat,.viewDimension=WGPUTextureViewDimension_2D}},
        {.binding=1,.visibility=WGPUShaderStage_Compute,.buffer={.type=WGPUBufferBindingType_Storage}}};
    WGPUBindGroupLayoutDescriptor layout_descriptor = {.entries=entries,.entryCount=2};
    WGPUBindGroupLayout layout = wgpuDeviceCreateBindGroupLayout(s->device,&layout_descriptor);
    WGPUPipelineLayoutDescriptor pipeline_layout = {.bindGroupLayoutCount=1,.bindGroupLayouts=&layout};
    WGPUPipelineLayout owner = wgpuDeviceCreatePipelineLayout(s->device,&pipeline_layout);
    WGPUShaderSourceWGSL source = WGPU_SHADER_SOURCE_WGSL_INIT;
    source.code = (WGPUStringView){metal_depth_sources[4],WGPU_STRLEN};
    WGPUShaderModuleDescriptor module_descriptor = {.nextInChain=&source.chain};
    WGPUShaderModule shader = wgpuDeviceCreateShaderModule(s->device,&module_descriptor);
    WGPUComputePipelineDescriptor descriptor = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
    descriptor.layout = owner;
    descriptor.compute.module = shader;
    WGPUComputePipeline result = wgpuDeviceCreateComputePipeline(s->device,&descriptor);
    wgpuShaderModuleRelease(shader);
    wgpuPipelineLayoutRelease(owner);
    wgpuBindGroupLayoutRelease(layout);
    return result;
}

static WGPUBindGroup metal_depth_group(state *s, WGPUComputePipeline pipeline,
    WGPUTextureView view, WGPUBuffer output)
{
    WGPUBindGroupEntry entries[2] = {{.binding=0,.textureView=view},
        {.binding=1,.buffer=output,.size=16}};
    return group(s,pipeline,entries,2);
}

static void metal_depth_discard(WGPUCommandEncoder encoder, WGPUTextureView view)
{
    WGPURenderPassDepthStencilAttachment attachment = WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
    attachment.view = view;
    attachment.depthLoadOp = attachment.stencilLoadOp = WGPULoadOp_Clear;
    attachment.depthStoreOp = WGPUStoreOp_Discard;
    attachment.stencilStoreOp = WGPUStoreOp_Store;
    attachment.depthClearValue = 0.75f;
    attachment.stencilClearValue = 59;
    WGPURenderPassDescriptor descriptor = {.depthStencilAttachment=&attachment};
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder,&descriptor);
    assert(pass);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
}

static void metal_depth_admission(state *s, mock_native *mock)
{
    unsigned baseline = mock->live;
    const WGPUTextureFormat formats[] = {WGPUTextureFormat_Depth16Unorm,WGPUTextureFormat_Depth32Float,
        WGPUTextureFormat_Depth24Plus,WGPUTextureFormat_Depth24PlusStencil8,WGPUTextureFormat_Stencil8};
    for (size_t i = 0; i < sizeof(formats)/sizeof(formats[0]); ++i) {
        WGPUTexture image = metal_depth_image(s,formats[i],1);
        assert(image);
        WGPUTextureView view = wgpuTextureCreateView(image,NULL);
        assert(view && (mock->texture_desc.usage & DOE_MTL_TEXTURE_SAMPLED));
        wgpuTextureViewRelease(view);
        wgpuTextureRelease(image);
        for (size_t copy = 0; copy < 2; ++copy) {
            WGPUTextureDescriptor descriptor = WGPU_TEXTURE_DESCRIPTOR_INIT;
            descriptor.format = formats[i];
            descriptor.size = (WGPUExtent3D){2,2,1};
            descriptor.usage = copy ? WGPUTextureUsage_CopyDst : WGPUTextureUsage_CopySrc;
            wgpuDevicePushErrorScope(s->device,WGPUErrorFilter_Validation);
            assert(!wgpuDeviceCreateTexture(s->device,&descriptor));
            pop(s,WGPUErrorType_Validation);
        }
    }
    WGPUTexture combined = metal_depth_image(s,WGPUTextureFormat_Depth24PlusStencil8,1);
    assert(combined && mock->texture_desc.allow_format_views);
    WGPUTextureView depth = metal_depth_view(combined,WGPUTextureAspect_DepthOnly,WGPUTextureViewDimension_2D,1);
    assert(depth && mock->view_desc.format == DOE_MTL_DEPTH32_STENCIL8);
    WGPUTextureView stencil = metal_depth_view(combined,WGPUTextureAspect_StencilOnly,WGPUTextureViewDimension_2D,1);
    assert(stencil && mock->view_desc.format == DOE_MTL_X32_STENCIL8);
    WGPUTextureView all = wgpuTextureCreateView(combined,NULL);
    assert(all);
    WGPUTexture layered = metal_depth_image(s,WGPUTextureFormat_Depth32Float,2);
    WGPUTextureView array = metal_depth_view(layered,WGPUTextureAspect_DepthOnly,WGPUTextureViewDimension_2DArray,2);
    WGPUTexture cube = metal_depth_image(s,WGPUTextureFormat_Depth32Float,6);
    WGPUTextureView cube_view = metal_depth_view(cube,WGPUTextureAspect_DepthOnly,WGPUTextureViewDimension_Cube,6);
    WGPUBuffer output = buffer(s,16,WGPUBufferUsage_Storage);
    WGPUComputePipeline pipelines[5];
    WGPUBindGroup groups[5];
    WGPUTextureView views[5] = {depth,stencil,array,cube_view,depth};
    void *identities[5];
    for (size_t i = 0; i < 5; ++i) {
        pipelines[i] = i == 4 ? metal_float_depth_pipeline(s) : pipeline(s,metal_depth_sources[i]);
        assert(pipelines[i]);
        groups[i] = metal_depth_group(s,pipelines[i],views[i],output);
        assert(groups[i]);
        native_view *view = doe_resource_view_native((doe_resource_view *)views[i]);
        identities[i] = doe_mtl_texture_view_native(view->metal);
    }
    /* A missing depth aspect is cleared through a combined parent attachment;
     * the stored stencil companion must LOAD rather than lose its value. */
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device,NULL);
    metal_depth_discard(encoder,all);
    dispatch(encoder,pipelines[1],groups[1]);
    submit(s,encoder);
    assert(mock->render_pass.depth.load == DOE_MTL_CLEAR && mock->render_pass.stencil.load == DOE_MTL_LOAD);
    assert(mock->view_desc.format == DOE_MTL_DEPTH32_STENCIL8);
    assert(mock->resources.textures[0] == identities[1]);
    /* Fault while creating the temporary parent clear view: exact detail is
     * copied into deferred public delivery, and the recording is discarded. */
    encoder = wgpuDeviceCreateCommandEncoder(s->device,NULL);
    metal_depth_discard(encoder,all);
    dispatch(encoder,pipelines[1],groups[1]);
    mock->fail = MOCK_VIEW;
    exact_message expected = {"Metal: injected native failure (native=-71): owned fault boundary",0};
    WGPUPopErrorScopeCallbackInfo scope = {.mode=WGPUCallbackMode_AllowProcessEvents,
        .callback=exact_scope,.userdata1=&expected};
    wgpuDevicePushErrorScope(s->device,WGPUErrorFilter_Internal);
    submit(s,encoder);
    mock->fail = MOCK_NONE;
    assert(wgpuDevicePopErrorScope(s->device,scope).id);
    overwrite_stack();
    wgpuInstanceProcessEvents(s->instance);
    assert(expected.calls == 1);
    for (size_t i = 0; i < 5; ++i) {
        encoder = wgpuDeviceCreateCommandEncoder(s->device,NULL);
        dispatch(encoder,pipelines[i],groups[i]);
        submit(s,encoder);
        assert(mock->resources.textures[0] == identities[i]);
    }
    encoder = wgpuDeviceCreateCommandEncoder(s->device,NULL);
    dispatch(encoder,pipelines[1],groups[1]);
    for (size_t i = 0; i < 5; ++i) {
        wgpuBindGroupRelease(groups[i]);
        wgpuComputePipelineRelease(pipelines[i]);
    }
    wgpuTextureViewRelease(depth); wgpuTextureViewRelease(stencil); wgpuTextureViewRelease(all);
    wgpuTextureViewRelease(array); wgpuTextureViewRelease(cube_view);
    wgpuTextureRelease(combined); wgpuTextureRelease(layered); wgpuTextureRelease(cube);
    wgpuBufferRelease(output);
    native_device *device = (native_device *)s->device;
    native_lock(device);
    hold = true;
    native_unlock(device);
    wgpuDevicePushErrorScope(s->device,WGPUErrorFilter_Internal);
    submit(s,encoder);
    pop(s,WGPUErrorType_Internal);
    native_lock(device);
    assert(device->pending && mock->live > baseline);
    hold = false;
    native_queue_settlement settled = native_settle_pending(device);
    assert(settled.complete && settled.failure.status == NATIVE_OK && !device->pending);
    native_unlock(device);
    assert(mock->live == baseline && !s->errors);
    puts("PASS public Metal depth/stencil sampling host composition: canonical aspect views, dense MSL bindings, parent-format zero clears preserving companions, copied errors, copy rejection and pending early-release leases; no GPU execution");
}
#endif
