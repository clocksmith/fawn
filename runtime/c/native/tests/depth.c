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
    for (size_t i = 0; i < 2; ++i) {
        WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        desc.format = formats[i];
        desc.size = (WGPUExtent3D){4,4,3};
        desc.mipLevelCount = 2;
        desc.usage = WGPUTextureUsage_RenderAttachment;
        WGPUTexture image = wgpuDeviceCreateTexture(s->device,&desc);
        assert(image);
        WGPUTextureViewDescriptor selected = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
        selected.dimension = WGPUTextureViewDimension_2D;
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
    options.forceFallbackAdapter = WGPU_TRUE;
    WGPURequestAdapterCallbackInfo adapter = {.mode=WGPUCallbackMode_AllowSpontaneous,.callback=adapter_reply,.userdata1=&s};
    assert(wgpuInstanceRequestAdapter(s.instance,&options,adapter).id && s.adapter);
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
    assert(!s.errors && s.validation_errors == 4);
    wgpuBufferRelease(vertices);
    wgpuShaderModuleRelease(module);
    wgpuQueueRelease(s.queue);
    wgpuDeviceRelease(s.device);
    wgpuAdapterRelease(s.adapter);
    wgpuInstanceRelease(s.instance);
    puts("PASS public depth/stencil: zero publication, genuine vertex-only depth draw, depth ordering/read-only bundles, selected array/mip aspects, dynamic stencil, culled writes, same-batch discard/reinitialize preserving the other aspect, early releases and admission negatives (CPU Vulkan only)");
    return 0;
}
