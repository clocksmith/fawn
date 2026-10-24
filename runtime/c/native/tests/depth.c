/* SPDX-License-Identifier: MIT
 * Actual public depth/stencil pixels, initialized aspects and discard recovery. */
#include "consumer.h"
#include <math.h>

typedef struct vertex { float position[4], color[4]; } vertex;
static WGPURenderPipeline graphics(state *s, WGPUShaderModule module, WGPUDepthStencilState *depth,
    WGPUCullMode cull)
{
    WGPUVertexAttribute attributes[2] = {{.format=WGPUVertexFormat_Float32x4,.shaderLocation=0},
        {.format=WGPUVertexFormat_Float32x4,.offset=16,.shaderLocation=1}};
    WGPUVertexBufferLayout vertices = {.arrayStride=sizeof(vertex),.attributeCount=2,.attributes=attributes};
    WGPUColorTargetState color = {.format=WGPUTextureFormat_RGBA8Unorm,.writeMask=WGPUColorWriteMask_All};
    WGPUFragmentState fragment = {.module=module,.targetCount=1,.targets=&color};
    WGPURenderPipelineDescriptor descriptor = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    descriptor.vertex = (WGPUVertexState){.module=module,.bufferCount=1,.buffers=&vertices};
    descriptor.fragment = &fragment;
    descriptor.depthStencil = depth;
    descriptor.primitive.cullMode = cull;
    return wgpuDeviceCreateRenderPipeline(s->device,&descriptor);
}
static WGPURenderPassEncoder begin(state *s, WGPUCommandEncoder encoder, WGPUTextureView color,
    WGPURenderPassDepthStencilAttachment *depth)
{
    (void)s;
    WGPURenderPassColorAttachment attachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    attachment.view = color;
    attachment.loadOp = WGPULoadOp_Clear;
    attachment.storeOp = WGPUStoreOp_Store;
    attachment.clearValue = (WGPUColor){0,0,0,1};
    WGPURenderPassDescriptor descriptor = {.colorAttachmentCount=1,.colorAttachments=&attachment,
        .depthStencilAttachment=depth};
    return wgpuCommandEncoderBeginRenderPass(encoder,&descriptor);
}
static void draw(WGPURenderPassEncoder pass, WGPURenderPipeline pipeline, WGPUBuffer vertices, uint32_t first)
{
    wgpuRenderPassEncoderSetPipeline(pass,pipeline);
    wgpuRenderPassEncoderSetVertexBuffer(pass,0,vertices,0,WGPU_WHOLE_SIZE);
    wgpuRenderPassEncoderDraw(pass,3,1,first,0);
}
static void end(WGPURenderPassEncoder pass)
{
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
}
static void read_pixels(state *s, WGPUCommandEncoder encoder, WGPUTexture image, unsigned char red,
    unsigned char green, unsigned char blue)
{
    WGPUBuffer readback = buffer(s,512,WGPUBufferUsage_MapRead|WGPUBufferUsage_CopyDst);
    texture_to_buffer(encoder,image,readback,0,2,2);
    submit(s,encoder);
    const unsigned char *bytes = map(s,readback,512);
    const unsigned char expected[4] = {red,green,blue,255};
    for (size_t y = 0; y < 2; ++y) for (size_t x = 0; x < 2; ++x)
        assert(!memcmp(bytes+y*256+x*4,expected,4));
    wgpuBufferUnmap(readback);
    wgpuBufferRelease(readback);
}
static void depth_pixels(state *s, WGPUShaderModule module, WGPUBuffer vertices)
{
    WGPUTexture color = texture(s,WGPUTextureFormat_RGBA8Unorm,WGPUTextureUsage_RenderAttachment|WGPUTextureUsage_CopySrc,1);
    WGPUTexture image = texture(s,WGPUTextureFormat_Depth32Float,WGPUTextureUsage_RenderAttachment,1);
    WGPUTextureView view = wgpuTextureCreateView(color,NULL);
    WGPUTextureViewDescriptor view_desc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    view_desc.aspect = WGPUTextureAspect_DepthOnly;
    WGPUTextureView depth_view = wgpuTextureCreateView(image,&view_desc);
    assert(view && depth_view);
    WGPUDepthStencilState depth = WGPU_DEPTH_STENCIL_STATE_INIT;
    depth.format = WGPUTextureFormat_Depth32Float;
    depth.depthWriteEnabled = WGPUOptionalBool_True;
    depth.depthCompare = WGPUCompareFunction_Less;
    WGPURenderPipeline writes = graphics(s,module,&depth,WGPUCullMode_None);
    depth.depthWriteEnabled = WGPUOptionalBool_False;
    WGPURenderPipeline readonly = graphics(s,module,&depth,WGPUCullMode_None);
    assert(writes && readonly);
    WGPURenderPassDepthStencilAttachment attachment = WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
    attachment.view = depth_view;
    attachment.depthLoadOp = WGPULoadOp_Load;
    attachment.depthStoreOp = WGPUStoreOp_Store;
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device,NULL);
    WGPURenderPassEncoder pass = begin(s,encoder,view,&attachment);
    assert(pass);
    draw(pass,writes,vertices,0); /* Newly published depth is real zero, not allocator contents. */
    end(pass);
    read_pixels(s,encoder,color,0,0,0);
    attachment.depthLoadOp = WGPULoadOp_Clear;
    attachment.depthClearValue = 1;
    encoder = wgpuDeviceCreateCommandEncoder(s->device,NULL);
    pass = begin(s,encoder,view,&attachment);
    assert(pass);
    draw(pass,writes,vertices,0); /* red .75 */
    draw(pass,writes,vertices,3); /* green .25 */
    draw(pass,writes,vertices,6); /* blue .5 fails */
    end(pass);
    read_pixels(s,encoder,color,0,255,0);
    attachment.depthLoadOp = WGPULoadOp_Undefined;
    attachment.depthStoreOp = WGPUStoreOp_Undefined;
    attachment.depthReadOnly = WGPU_TRUE;
    attachment.depthClearValue = NAN;
    encoder = wgpuDeviceCreateCommandEncoder(s->device,NULL);
    pass = begin(s,encoder,view,&attachment);
    assert(pass);
    WGPUTextureFormat color_format = WGPUTextureFormat_RGBA8Unorm;
    WGPURenderBundleEncoderDescriptor bundle_desc = WGPU_RENDER_BUNDLE_ENCODER_DESCRIPTOR_INIT;
    bundle_desc.colorFormatCount = 1;
    bundle_desc.colorFormats = &color_format;
    bundle_desc.depthStencilFormat = WGPUTextureFormat_Depth32Float;
    bundle_desc.depthReadOnly = WGPU_TRUE;
    WGPURenderBundleEncoder builder = wgpuDeviceCreateRenderBundleEncoder(s->device,&bundle_desc);
    assert(builder);
    wgpuRenderBundleEncoderSetPipeline(builder,readonly);
    wgpuRenderBundleEncoderSetVertexBuffer(builder,0,vertices,0,WGPU_WHOLE_SIZE);
    wgpuRenderBundleEncoderDraw(builder,3,1,9,0); /* blue .1 without writing depth. */
    WGPURenderBundle bundle = wgpuRenderBundleEncoderFinish(builder,NULL);
    assert(bundle);
    wgpuRenderBundleEncoderRelease(builder);
    wgpuRenderPassEncoderExecuteBundles(pass,1,&bundle);
    wgpuRenderBundleRelease(bundle);
    draw(pass,readonly,vertices,0); /* red .75 still fails. */
    end(pass);
    read_pixels(s,encoder,color,0,0,255);
    /* Discard and LOAD in the same command owner must re-zero on GPU. */
    attachment.depthReadOnly = WGPU_FALSE;
    attachment.depthLoadOp = WGPULoadOp_Load;
    attachment.depthStoreOp = WGPUStoreOp_Discard;
    encoder = wgpuDeviceCreateCommandEncoder(s->device,NULL);
    pass = begin(s,encoder,view,&attachment);
    assert(pass);
    end(pass);
    attachment.depthStoreOp = WGPUStoreOp_Store;
    pass = begin(s,encoder,view,&attachment);
    assert(pass);
    draw(pass,writes,vertices,0);
    end(pass);
    wgpuRenderPipelineRelease(writes);
    wgpuRenderPipelineRelease(readonly);
    wgpuTextureViewRelease(depth_view);
    wgpuTextureRelease(image);
    wgpuTextureViewRelease(view);
    read_pixels(s,encoder,color,0,0,0);
    wgpuTextureRelease(color);
}
static void stencil_pixels(state *s, WGPUShaderModule module, WGPUBuffer vertices)
{
    WGPUTexture color = texture(s,WGPUTextureFormat_RGBA8Unorm,WGPUTextureUsage_RenderAttachment|WGPUTextureUsage_CopySrc,1);
    WGPUTexture image = texture(s,WGPUTextureFormat_Depth24PlusStencil8,WGPUTextureUsage_RenderAttachment,1);
    WGPUTextureView view = wgpuTextureCreateView(color,NULL), depth_view = wgpuTextureCreateView(image,NULL);
    assert(view && depth_view);
    WGPUDepthStencilState depth = WGPU_DEPTH_STENCIL_STATE_INIT;
    depth.format = WGPUTextureFormat_Depth24PlusStencil8;
    depth.depthWriteEnabled = WGPUOptionalBool_False;
    depth.stencilFront.compare = depth.stencilBack.compare = WGPUCompareFunction_Equal;
    WGPURenderPipeline compare = graphics(s,module,&depth,WGPUCullMode_None);
    /* Writes on a culled face do not violate the public read-only summary. */
    depth.stencilBack.passOp = WGPUStencilOperation_Replace;
    WGPURenderPipeline culled = graphics(s,module,&depth,WGPUCullMode_Back);
    assert(compare && culled);
    WGPURenderPassDepthStencilAttachment attachment = WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
    attachment.view = depth_view;
    attachment.depthLoadOp = WGPULoadOp_Clear;
    attachment.depthStoreOp = WGPUStoreOp_Discard;
    attachment.depthClearValue = 1;
    attachment.stencilLoadOp = WGPULoadOp_Clear;
    attachment.stencilStoreOp = WGPUStoreOp_Store;
    attachment.stencilClearValue = 7;
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device,NULL);
    WGPURenderPassEncoder pass = begin(s,encoder,view,&attachment);
    assert(pass);
    end(pass);
    attachment.depthLoadOp = attachment.stencilLoadOp = WGPULoadOp_Undefined;
    attachment.depthStoreOp = attachment.stencilStoreOp = WGPUStoreOp_Undefined;
    attachment.depthReadOnly = attachment.stencilReadOnly = WGPU_TRUE;
    attachment.depthClearValue = NAN;
    pass = begin(s,encoder,view,&attachment);
    assert(pass);
    wgpuRenderPassEncoderSetStencilReference(pass,7);
    draw(pass,compare,vertices,0); /* Initializer must preserve stencil7 while zeroing discarded depth. */
    wgpuRenderPassEncoderSetStencilReference(pass,8);
    draw(pass,compare,vertices,6); /* Wrong reference must not overwrite red. */
    wgpuRenderPassEncoderSetStencilReference(pass,7);
    draw(pass,culled,vertices,0); /* Canonical native culled-write summary agrees. */
    end(pass);
    wgpuRenderPipelineRelease(compare);
    wgpuRenderPipelineRelease(culled);
    wgpuTextureViewRelease(depth_view);
    wgpuTextureRelease(image);
    wgpuTextureViewRelease(view);
    read_pixels(s,encoder,color,255,0,0);
    wgpuTextureRelease(color);
}
static void depth_only_draw(state *s, WGPUShaderModule module, WGPUBuffer vertices)
{
    WGPUShaderSourceWGSL source = WGPU_SHADER_SOURCE_WGSL_INIT;
    source.code = (WGPUStringView){"@vertex fn vertex(@location(0) position:vec4f)->@builtin(position) vec4f{return position;}",WGPU_STRLEN};
    WGPUShaderModuleDescriptor shader_desc = {.nextInChain=&source.chain};
    WGPUShaderModule vertex_shader = wgpuDeviceCreateShaderModule(s->device,&shader_desc);
    assert(vertex_shader);
    WGPUVertexAttribute attribute = {.format=WGPUVertexFormat_Float32x4,.shaderLocation=0};
    WGPUVertexBufferLayout vertex_layout = {.arrayStride=sizeof(vertex),.attributeCount=1,.attributes=&attribute};
    WGPUDepthStencilState depth = WGPU_DEPTH_STENCIL_STATE_INIT;
    depth.format = WGPUTextureFormat_Depth32Float;
    depth.depthWriteEnabled = WGPUOptionalBool_True;
    depth.depthCompare = WGPUCompareFunction_Less;
    WGPURenderPipelineDescriptor desc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    desc.vertex = (WGPUVertexState){.module=vertex_shader,.bufferCount=1,.buffers=&vertex_layout};
    desc.depthStencil = &depth;
    WGPURenderPipeline only_depth = wgpuDeviceCreateRenderPipeline(s->device,&desc);
    assert(only_depth);
    depth.depthWriteEnabled = WGPUOptionalBool_False;
    WGPURenderPipeline observer = graphics(s,module,&depth,WGPUCullMode_None);
    assert(observer);
    WGPUTexture image = texture(s,WGPUTextureFormat_Depth32Float,WGPUTextureUsage_RenderAttachment,1);
    WGPUTextureView depth_view = wgpuTextureCreateView(image,NULL);
    WGPUTexture color = texture(s,WGPUTextureFormat_RGBA8Unorm,WGPUTextureUsage_RenderAttachment|WGPUTextureUsage_CopySrc,1);
    WGPUTextureView view = wgpuTextureCreateView(color,NULL);
    assert(depth_view && view);
    WGPURenderPassDepthStencilAttachment attachment = WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
    attachment.view = depth_view;
    attachment.depthLoadOp = WGPULoadOp_Clear;
    attachment.depthStoreOp = WGPUStoreOp_Store;
    attachment.depthClearValue = 1;
    WGPURenderPassDescriptor pass_desc = {.depthStencilAttachment=&attachment};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device,NULL);
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder,&pass_desc);
    assert(pass);
    draw(pass,only_depth,vertices,3); /* Actual vertex-only raster writes .25 with no color attachment. */
    end(pass);
    wgpuRenderPipelineRelease(only_depth);
    wgpuShaderModuleRelease(vertex_shader);
    attachment.depthLoadOp = WGPULoadOp_Undefined;
    attachment.depthStoreOp = WGPUStoreOp_Undefined;
    attachment.depthReadOnly = WGPU_TRUE;
    pass = begin(s,encoder,view,&attachment);
    assert(pass);
    draw(pass,observer,vertices,0);
    end(pass); /* .75 must fail against .25. */
    read_pixels(s,encoder,color,0,0,0);
    encoder = wgpuDeviceCreateCommandEncoder(s->device,NULL);
    pass = begin(s,encoder,view,&attachment);
    assert(pass);
    draw(pass,observer,vertices,9);
    end(pass); /* .1 must pass. */
    wgpuRenderPipelineRelease(observer);
    wgpuTextureViewRelease(depth_view);
    wgpuTextureRelease(image);
    wgpuTextureViewRelease(view);
    read_pixels(s,encoder,color,0,0,255);
    wgpuTextureRelease(color);
}
static void array_aspects(state *s, WGPUShaderModule module, WGPUBuffer vertices)
{
    const WGPUTextureFormat formats[] = {WGPUTextureFormat_Depth24Plus,WGPUTextureFormat_Stencil8};
    /* A single-layer 2D array view is a renderable view like a 2D one. */
    const WGPUTextureViewDimension dimensions[] = {WGPUTextureViewDimension_2D,WGPUTextureViewDimension_2DArray};
    for (size_t case_index = 0; case_index < 4; ++case_index) {
        const size_t i = case_index % 2;
        WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        desc.format = formats[i];
        desc.size = (WGPUExtent3D){4,4,3};
        desc.mipLevelCount = 2;
        desc.usage = WGPUTextureUsage_RenderAttachment;
        WGPUTexture image = wgpuDeviceCreateTexture(s->device,&desc);
        assert(image);
        WGPUTextureViewDescriptor selected = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
        selected.dimension = dimensions[case_index / 2];
        selected.arrayLayerCount = 1;
        selected.baseMipLevel = 1;
        selected.baseArrayLayer = 2;
        selected.aspect = i ? WGPUTextureAspect_StencilOnly : WGPUTextureAspect_DepthOnly;
        WGPUTextureView depth_view = wgpuTextureCreateView(image,&selected);
        assert(depth_view);
        WGPUTexture color = texture(s,WGPUTextureFormat_RGBA8Unorm,WGPUTextureUsage_RenderAttachment|WGPUTextureUsage_CopySrc,1);
        WGPUTextureView view = wgpuTextureCreateView(color,NULL);
        WGPUDepthStencilState depth = WGPU_DEPTH_STENCIL_STATE_INIT;
        depth.format = formats[i];
        if (i) depth.stencilFront.compare = depth.stencilBack.compare = WGPUCompareFunction_Equal;
        else {
            depth.depthWriteEnabled = WGPUOptionalBool_False;
            depth.depthCompare = WGPUCompareFunction_Less;
        }
        WGPURenderPipeline pipeline = graphics(s,module,&depth,WGPUCullMode_None);
        assert(pipeline);
        WGPURenderPassDepthStencilAttachment attachment = WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
        attachment.view = depth_view;
        if (i) {
            attachment.stencilLoadOp = WGPULoadOp_Load;
            attachment.stencilStoreOp = WGPUStoreOp_Store;
        } else {
            attachment.depthLoadOp = WGPULoadOp_Load;
            attachment.depthStoreOp = WGPUStoreOp_Store;
        }
        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device,NULL);
        WGPURenderPassEncoder pass = begin(s,encoder,view,&attachment);
        assert(pass);
        draw(pass,pipeline,vertices,0);
        if (i) {
            wgpuRenderPassEncoderSetStencilReference(pass,1);
            draw(pass,pipeline,vertices,6);
        }
        end(pass);
        wgpuRenderPipelineRelease(pipeline);
        wgpuTextureViewRelease(depth_view);
        wgpuTextureRelease(image);
        wgpuTextureViewRelease(view);
        read_pixels(s,encoder,color,i ? 255 : 0,0,0);
        wgpuTextureRelease(color);
    }
}
static WGPUShaderModule wgsl(state *s, const char *fragment)
{
    char code[1024];
    int length = snprintf(code,sizeof(code),"struct Output{@builtin(position) position:vec4f,@location(0) color:vec4f}"
        "@vertex fn vertex(@location(0) point:vec4f,@location(1) color:vec4f)->Output{return Output(point,color);}%s",
        fragment);
    assert(length > 0 && (size_t)length < sizeof(code));
    WGPUShaderSourceWGSL source = WGPU_SHADER_SOURCE_WGSL_INIT;
    source.code = (WGPUStringView){code,WGPU_STRLEN};
    WGPUShaderModuleDescriptor descriptor = {.nextInChain=&source.chain};
    WGPUShaderModule module = wgpuDeviceCreateShaderModule(s->device,&descriptor);
    assert(module);
    return module;
}
/* frag_depth replaces the rasterized depth; sample_mask 0 writes no sample;
 * WebGPU rejects frag_depth without a depth aspect and sample_mask output with
 * alpha-to-coverage. */
static void fragment_builtins(state *s, WGPUShaderModule plain, WGPUBuffer vertices)
{
    WGPUShaderModule replaced = wgsl(s,"struct Fragment{@location(0) color:vec4f,@builtin(frag_depth) depth:f32}"
        "@fragment fn fragment(input:Output)->Fragment{return Fragment(input.color,.6);}");
    WGPUShaderModule masked = wgsl(s,"struct Fragment{@location(0) color:vec4f,@builtin(sample_mask) mask:u32}"
        "@fragment fn fragment(input:Output)->Fragment{return Fragment(input.color,0u);}");
    WGPUShaderModule indexed = wgsl(s,"@fragment fn fragment(input:Output,@builtin(sample_index) index:u32,"
        "@builtin(sample_mask) mask:u32)->@location(0) vec4f{return input.color*f32(1u+index+(mask&0u));}");
    WGPUTexture color = texture(s,WGPUTextureFormat_RGBA8Unorm,WGPUTextureUsage_RenderAttachment|WGPUTextureUsage_CopySrc,1);
    WGPUTexture image = texture(s,WGPUTextureFormat_Depth32Float,WGPUTextureUsage_RenderAttachment,1);
    WGPUTextureView view = wgpuTextureCreateView(color,NULL);
    WGPUTextureView depth_view = wgpuTextureCreateView(image,NULL);
    assert(view && depth_view);
    WGPUDepthStencilState depth = WGPU_DEPTH_STENCIL_STATE_INIT;
    depth.format = WGPUTextureFormat_Depth32Float;
    depth.depthWriteEnabled = WGPUOptionalBool_True;
    depth.depthCompare = WGPUCompareFunction_Less;
    WGPURenderPipeline writes = graphics(s,replaced,&depth,WGPUCullMode_None);
    WGPURenderPipeline ordinary = graphics(s,plain,&depth,WGPUCullMode_None);
    WGPURenderPipeline sampled = graphics(s,indexed,&depth,WGPUCullMode_None);
    assert(writes && ordinary && sampled);
    WGPURenderPassDepthStencilAttachment attachment = WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
    attachment.view = depth_view;
    attachment.depthLoadOp = WGPULoadOp_Clear;
    attachment.depthStoreOp = WGPUStoreOp_Store;
    attachment.depthClearValue = 1;
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device,NULL);
    WGPURenderPassEncoder pass = begin(s,encoder,view,&attachment);
    assert(pass);
    draw(pass,writes,vertices,3); /* green at .25 writes depth .6 */
    draw(pass,ordinary,vertices,6); /* blue .5 passes only against .6 */
    draw(pass,ordinary,vertices,0); /* red .75 fails */
    end(pass);
    read_pixels(s,encoder,color,0,0,255);
    WGPURenderPipeline none = graphics(s,masked,NULL,WGPUCullMode_None);
    assert(none);
    encoder = wgpuDeviceCreateCommandEncoder(s->device,NULL);
    pass = begin(s,encoder,view,NULL);
    assert(pass);
    draw(pass,none,vertices,0); /* red with an empty sample mask */
    end(pass);
    read_pixels(s,encoder,color,0,0,0);
    wgpuDevicePushErrorScope(s->device,WGPUErrorFilter_Validation);
    assert(!graphics(s,replaced,NULL,WGPUCullMode_None));
    expect_validation(s);
    depth.format = WGPUTextureFormat_Stencil8;
    depth.depthWriteEnabled = WGPUOptionalBool_False;
    depth.depthCompare = WGPUCompareFunction_Undefined;
    wgpuDevicePushErrorScope(s->device,WGPUErrorFilter_Validation);
    assert(!graphics(s,replaced,&depth,WGPUCullMode_None));
    expect_validation(s);
    WGPUVertexAttribute attributes[2] = {{.format=WGPUVertexFormat_Float32x4,.shaderLocation=0},
        {.format=WGPUVertexFormat_Float32x4,.offset=16,.shaderLocation=1}};
    WGPUVertexBufferLayout layout = {.arrayStride=sizeof(vertex),.attributeCount=2,.attributes=attributes};
    WGPUColorTargetState target = {.format=WGPUTextureFormat_RGBA8Unorm,.writeMask=WGPUColorWriteMask_All};
    WGPUFragmentState fragment = {.module=masked,.targetCount=1,.targets=&target};
    WGPURenderPipelineDescriptor coverage = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    coverage.vertex = (WGPUVertexState){.module=masked,.bufferCount=1,.buffers=&layout};
    coverage.fragment = &fragment;
    coverage.multisample.count = 4;
    coverage.multisample.alphaToCoverageEnabled = WGPU_TRUE;
    wgpuDevicePushErrorScope(s->device,WGPUErrorFilter_Validation);
    assert(!wgpuDeviceCreateRenderPipeline(s->device,&coverage));
    expect_validation(s);
    wgpuRenderPipelineRelease(none);
    wgpuRenderPipelineRelease(sampled);
    wgpuRenderPipelineRelease(ordinary);
    wgpuRenderPipelineRelease(writes);
    wgpuTextureViewRelease(depth_view);
    wgpuTextureViewRelease(view);
    wgpuTextureRelease(image);
    wgpuTextureRelease(color);
    wgpuShaderModuleRelease(indexed);
    wgpuShaderModuleRelease(masked);
    wgpuShaderModuleRelease(replaced);
}
/* A constant bias of 2^20 at z .5 in depth32float (r = 2^-24) writes .5625, so
 * a later .55 draw passes Less; a .03 clamp writes .53 and the .55 draw fails. */
static void depth_bias(state *s, WGPUShaderModule module)
{
    vertex data[6];
    const float positions[3][2] = {{-1,-1},{3,-1},{-1,3}};
    for (size_t i = 0; i < 6; ++i)
        data[i] = (vertex){.position={positions[i%3][0],positions[i%3][1],i < 3 ? .5f : .55f,1},
            .color={0,i < 3 ? 1.0f : 0.0f,i < 3 ? 0.0f : 1.0f,1}};
    WGPUBuffer vertices = buffer(s,sizeof(data),WGPUBufferUsage_Vertex|WGPUBufferUsage_CopyDst);
    wgpuQueueWriteBuffer(s->queue,vertices,0,data,sizeof(data));
    WGPUTexture color = texture(s,WGPUTextureFormat_RGBA8Unorm,WGPUTextureUsage_RenderAttachment|WGPUTextureUsage_CopySrc,1);
    WGPUTexture image = texture(s,WGPUTextureFormat_Depth32Float,WGPUTextureUsage_RenderAttachment,1);
    WGPUTextureView view = wgpuTextureCreateView(color,NULL);
    WGPUTextureView depth_view = wgpuTextureCreateView(image,NULL);
    assert(view && depth_view);
    WGPUDepthStencilState depth = WGPU_DEPTH_STENCIL_STATE_INIT;
    depth.format = WGPUTextureFormat_Depth32Float;
    depth.depthWriteEnabled = WGPUOptionalBool_True;
    depth.depthCompare = WGPUCompareFunction_Less;
    WGPURenderPipeline plain = graphics(s,module,&depth,WGPUCullMode_None);
    depth.depthBias = 1 << 20;
    WGPURenderPipeline biased = graphics(s,module,&depth,WGPUCullMode_None);
    depth.depthBiasClamp = .03f;
    WGPURenderPipeline clamped = graphics(s,module,&depth,WGPUCullMode_None);
    assert(plain && biased && clamped);
    WGPURenderPassDepthStencilAttachment attachment = WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
    attachment.view = depth_view;
    attachment.depthLoadOp = WGPULoadOp_Clear;
    attachment.depthStoreOp = WGPUStoreOp_Store;
    attachment.depthClearValue = 1;
    const WGPURenderPipeline first[2] = {biased,clamped};
    const unsigned char blue[2] = {255,0};
    for (size_t i = 0; i < 2; ++i) {
        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device,NULL);
        WGPURenderPassEncoder pass = begin(s,encoder,view,&attachment);
        assert(pass);
        draw(pass,first[i],vertices,0);
        draw(pass,plain,vertices,3);
        end(pass);
        read_pixels(s,encoder,color,0,blue[i] ? 0 : 255,blue[i]);
    }
    WGPUVertexAttribute attributes[2] = {{.format=WGPUVertexFormat_Float32x4,.shaderLocation=0},
        {.format=WGPUVertexFormat_Float32x4,.offset=16,.shaderLocation=1}};
    WGPUVertexBufferLayout layout = {.arrayStride=sizeof(vertex),.attributeCount=2,.attributes=attributes};
    WGPUColorTargetState target = {.format=WGPUTextureFormat_RGBA8Unorm,.writeMask=WGPUColorWriteMask_All};
    WGPUFragmentState fragment = {.module=module,.targetCount=1,.targets=&target};
    WGPURenderPipelineDescriptor lines = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    lines.vertex = (WGPUVertexState){.module=module,.bufferCount=1,.buffers=&layout};
    lines.fragment = &fragment;
    lines.depthStencil = &depth;
    lines.primitive.topology = WGPUPrimitiveTopology_LineList;
    wgpuDevicePushErrorScope(s->device,WGPUErrorFilter_Validation);
    assert(!wgpuDeviceCreateRenderPipeline(s->device,&lines));
    expect_validation(s);
    wgpuRenderPipelineRelease(clamped);
    wgpuRenderPipelineRelease(biased);
    wgpuRenderPipelineRelease(plain);
    wgpuTextureViewRelease(depth_view);
    wgpuTextureViewRelease(view);
    wgpuTextureRelease(image);
    wgpuTextureRelease(color);
    wgpuBufferRelease(vertices);
}
static void negative_states(state *s, WGPUShaderModule module)
{
    WGPUDepthStencilState depth = WGPU_DEPTH_STENCIL_STATE_INIT;
    depth.format = WGPUTextureFormat_Depth32Float;
    wgpuDevicePushErrorScope(s->device,WGPUErrorFilter_Validation);
    assert(!graphics(s,module,&depth,WGPUCullMode_None));
    expect_validation(s);
    depth.depthWriteEnabled = WGPUOptionalBool_True;
    wgpuDevicePushErrorScope(s->device,WGPUErrorFilter_Validation);
    assert(!graphics(s,module,&depth,WGPUCullMode_None));
    expect_validation(s);
    depth.depthCompare = WGPUCompareFunction_Less;
    depth.stencilFront.passOp = WGPUStencilOperation_Replace;
    wgpuDevicePushErrorScope(s->device,WGPUErrorFilter_Validation);
    assert(!graphics(s,module,&depth,WGPUCullMode_None));
    expect_validation(s);
    depth.format = WGPUTextureFormat_Stencil8;
    wgpuDevicePushErrorScope(s->device,WGPUErrorFilter_Validation);
    assert(!graphics(s,module,&depth,WGPUCullMode_None));
    expect_validation(s);
}
int main(void)
{
    state s = {0};
    s.instance = wgpuCreateInstance(NULL);
    WGPURequestAdapterOptions options = WGPU_REQUEST_ADAPTER_OPTIONS_INIT;
    adapter_class_options(&options);
    WGPURequestAdapterCallbackInfo adapter = {.mode=WGPUCallbackMode_AllowSpontaneous,.callback=adapter_reply,.userdata1=&s};
    assert(wgpuInstanceRequestAdapter(s.instance,&options,adapter).id && s.adapter);
    adapter_class_require(s.adapter);
    WGPUDeviceDescriptor descriptor = WGPU_DEVICE_DESCRIPTOR_INIT;
    descriptor.uncapturedErrorCallbackInfo = (WGPUUncapturedErrorCallbackInfo){.callback=error_reply,.userdata1=&s};
    WGPURequestDeviceCallbackInfo device = {.mode=WGPUCallbackMode_AllowSpontaneous,.callback=device_reply,.userdata1=&s};
    assert(wgpuAdapterRequestDevice(s.adapter,&descriptor,device).id && s.device);
    s.queue = wgpuDeviceGetQueue(s.device);
    WGPUShaderSourceWGSL source = WGPU_SHADER_SOURCE_WGSL_INIT;
    source.code = (WGPUStringView){"struct Output{@builtin(position) position:vec4f,@location(0) color:vec4f}"
        "@vertex fn vertex(@location(0) point:vec4f,@location(1) color:vec4f)->Output{return Output(point,color);}"
        "@fragment fn fragment(input:Output)->@location(0) vec4f{return input.color;}",WGPU_STRLEN};
    WGPUShaderModuleDescriptor shader_desc = {.nextInChain=&source.chain};
    WGPUShaderModule module = wgpuDeviceCreateShaderModule(s.device,&shader_desc);
    assert(module);
    vertex data[12];
    const float positions[3][2] = {{-1,-1},{3,-1},{-1,3}};
    const float z[4] = {.75f,.25f,.5f,.1f};
    const float colors[4][4] = {{1,0,0,1},{0,1,0,1},{0,0,1,1},{0,0,1,1}};
    for (size_t i = 0; i < 12; ++i) {
        data[i] = (vertex){.position={positions[i%3][0],positions[i%3][1],z[i/3],1}};
        memcpy(data[i].color,colors[i/3],sizeof(data[i].color));
    }
    WGPUBuffer vertices = buffer(&s,sizeof(data),WGPUBufferUsage_Vertex|WGPUBufferUsage_CopyDst);
    wgpuQueueWriteBuffer(s.queue,vertices,0,data,sizeof(data));
    depth_pixels(&s,module,vertices);
    stencil_pixels(&s,module,vertices);
    depth_only_draw(&s,module,vertices);
    array_aspects(&s,module,vertices);
    negative_states(&s,module);
    fragment_builtins(&s,module,vertices);
    depth_bias(&s,module);
    assert(!s.errors && s.validation_errors == 8);
    wgpuBufferRelease(vertices);
    wgpuShaderModuleRelease(module);
    wgpuQueueRelease(s.queue);
    wgpuDeviceRelease(s.device);
    wgpuAdapterRelease(s.adapter);
    wgpuInstanceRelease(s.instance);
    printf("PASS public depth/stencil: zero publication, genuine vertex-only depth draw, depth ordering/read-only bundles, selected array/mip aspects through 2D and single-layer 2D array views, dynamic stencil, culled writes, same-batch discard/reinitialize preserving the other aspect, frag_depth/sample_mask/sample_index fragment builtins, constant and clamped depth bias, early releases and admission negatives %s\n", adapter_class_scope());
    return 0;
}
