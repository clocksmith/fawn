/* SPDX-License-Identifier: MIT
 * Public ABI C-WGSL graphics through one real native pass, not Tint or emulation. */
#include "consumer.h"
#include <math.h>

static WGPUShaderModule shader(state *s, const char *code)
{
    WGPUShaderSourceWGSL source = WGPU_SHADER_SOURCE_WGSL_INIT;
    source.code = (WGPUStringView){code,WGPU_STRLEN};
    WGPUShaderModuleDescriptor descriptor = {.nextInChain=&source.chain};
    WGPUShaderModule result = wgpuDeviceCreateShaderModule(s->device,&descriptor);
    assert(result);
    return result;
}
static void main_graphics(state *s)
{
    WGPUShaderModule module = shader(s,
        "struct Params{offset:vec2f,color:vec4f}"
        "@group(2) @binding(3) var<uniform> params:Params;"
        "struct Output{@builtin(position) position:vec4f,@location(0) @interpolate(flat) tag:u32}"
        "@vertex fn vertex(@location(0) point:vec4f)->Output{"
        "return Output(vec4f(point.xy+params.offset,point.z,point.w),7u);}"
        "@fragment fn fragment(input:Output)->@location(0) vec4f{"
        "if(input.tag==7u){return params.color;}return vec4f(0.0);}");
    WGPUVertexAttribute attribute = {.format=WGPUVertexFormat_Float32x2,.shaderLocation=0};
    WGPUVertexBufferLayout vertex[2] = {{0},{.arrayStride=8,.attributeCount=1,.attributes=&attribute}};
    WGPUBlendState blend = {.color={.srcFactor=WGPUBlendFactor_Constant,.dstFactor=WGPUBlendFactor_Zero},
        .alpha={.srcFactor=WGPUBlendFactor_One,.dstFactor=WGPUBlendFactor_Zero}};
    WGPUColorTargetState color = {.format=WGPUTextureFormat_RGBA8Unorm,.blend=&blend,.writeMask=WGPUColorWriteMask_All};
    WGPUFragmentState fragment = {.module=module,.targetCount=1,.targets=&color};
    WGPURenderPipelineDescriptor descriptor = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    descriptor.vertex = (WGPUVertexState){.module=module,.bufferCount=2,.buffers=vertex};
    descriptor.fragment = &fragment;
    descriptor.primitive.cullMode = WGPUCullMode_Back;
    WGPURenderPipeline pipeline = wgpuDeviceCreateRenderPipeline(s->device,&descriptor);
    assert(pipeline);
    wgpuRenderPipelineSetLabel(pipeline,(WGPUStringView){"actual C graphics",WGPU_STRLEN});
    WGPUBindGroupLayout layout = wgpuRenderPipelineGetBindGroupLayout(pipeline,2);
    WGPUBindGroupLayout trailing = wgpuRenderPipelineGetBindGroupLayout(pipeline,3);
    assert(layout && trailing);
    wgpuBindGroupLayoutRelease(trailing);
    float params[8] = {0,0,0,0,1,0,1,1};
    WGPUBuffer uniform = buffer(s,sizeof(params),WGPUBufferUsage_Uniform|WGPUBufferUsage_CopyDst);
    wgpuQueueWriteBuffer(s->queue,uniform,0,params,sizeof(params));
    WGPUBindGroupEntry binding = {.binding=3,.buffer=uniform,.size=sizeof(params)};
    WGPUBindGroupDescriptor group_desc = {.layout=layout,.entryCount=1,.entries=&binding};
    WGPUBindGroup bound = wgpuDeviceCreateBindGroup(s->device,&group_desc);
    assert(bound);
    const float points[6][2] = {{-1,0},{1,0},{-1,1},{-1,1},{1,0},{1,1}};
    WGPUBuffer vertices = buffer(s,sizeof(points),WGPUBufferUsage_Vertex|WGPUBufferUsage_CopyDst);
    wgpuQueueWriteBuffer(s->queue,vertices,0,points,sizeof(points));
    WGPUTextureDescriptor image_desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    image_desc.format = WGPUTextureFormat_RGBA8Unorm;
    image_desc.size = (WGPUExtent3D){8,8,1};
    image_desc.usage = WGPUTextureUsage_RenderAttachment|WGPUTextureUsage_CopySrc;
    WGPUTexture image = wgpuDeviceCreateTexture(s->device,&image_desc);
    WGPUTextureView view = wgpuTextureCreateView(image,NULL);
    WGPUQuerySetDescriptor query_desc = {.type=WGPUQueryType_Occlusion,.count=4};
    WGPUQuerySet queries = wgpuDeviceCreateQuerySet(s->device,&query_desc);
    WGPUBuffer results = buffer(s,32,WGPUBufferUsage_QueryResolve|WGPUBufferUsage_CopySrc);
    WGPUBuffer readback = buffer(s,2080,WGPUBufferUsage_MapRead|WGPUBufferUsage_CopyDst);
    assert(view && queries);
    WGPURenderPassColorAttachment attachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    attachment.view = view;
    attachment.loadOp = WGPULoadOp_Clear;
    attachment.storeOp = WGPUStoreOp_Store;
    attachment.clearValue = (WGPUColor){0,0,1,1};
    WGPURenderPassDescriptor pass_desc = {.colorAttachmentCount=1,.colorAttachments=&attachment,.occlusionQuerySet=queries};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device,NULL);
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder,&pass_desc);
    assert(pass);
    wgpuRenderPassEncoderSetPipeline(pass,pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass,2,bound,0,NULL);
    wgpuRenderPassEncoderSetVertexBuffer(pass,1,vertices,0,sizeof(points));
    wgpuRenderPassEncoderSetStencilReference(pass,17); /* No stencil attachment: inert state. */
    wgpuRenderPassEncoderSetViewport(pass,0,0,8,8,0,1);
    wgpuRenderPassEncoderSetScissorRect(pass,0,0,4,8);
    wgpuRenderPassEncoderSetBlendConstant(pass,&(WGPUColor){1,0,0,1});
    wgpuRenderPassEncoderBeginOcclusionQuery(pass,0);
    wgpuRenderPassEncoderDraw(pass,6,1,0,0);
    wgpuRenderPassEncoderEndOcclusionQuery(pass);
    WGPURenderBundleEncoderDescriptor bundle_desc = WGPU_RENDER_BUNDLE_ENCODER_DESCRIPTOR_INIT;
    bundle_desc.colorFormatCount = 1;
    bundle_desc.colorFormats = &color.format;
    WGPURenderBundleEncoder bundle_encoder = wgpuDeviceCreateRenderBundleEncoder(s->device,&bundle_desc);
    assert(bundle_encoder);
    wgpuRenderBundleEncoderSetPipeline(bundle_encoder,pipeline);
    wgpuRenderBundleEncoderSetBindGroup(bundle_encoder,2,bound,0,NULL);
    wgpuRenderBundleEncoderSetVertexBuffer(bundle_encoder,1,vertices,0,sizeof(points));
    wgpuRenderBundleEncoderDraw(bundle_encoder,6,1,0,0);
    WGPURenderBundle bundle = wgpuRenderBundleEncoderFinish(bundle_encoder,NULL);
    assert(bundle);
    wgpuRenderBundleEncoderRelease(bundle_encoder);
    wgpuRenderPassEncoderSetScissorRect(pass,4,0,4,8);
    wgpuRenderPassEncoderSetBlendConstant(pass,&(WGPUColor){1,1,1,1});
    wgpuRenderPassEncoderExecuteBundles(pass,1,&bundle);
    wgpuRenderBundleRelease(bundle);
    /* Bundle execution clears pipeline/group/vertex state, not dynamic state. */
    wgpuRenderPassEncoderSetPipeline(pass,pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass,2,bound,0,NULL);
    wgpuRenderPassEncoderSetVertexBuffer(pass,1,vertices,0,sizeof(points));
    wgpuRenderPassEncoderSetScissorRect(pass,0,0,0,0);
    wgpuRenderPassEncoderBeginOcclusionQuery(pass,1);
    wgpuRenderPassEncoderDraw(pass,6,1,0,0);
    wgpuRenderPassEncoderEndOcclusionQuery(pass);
    wgpuRenderPassEncoderBeginOcclusionQuery(pass,2);
    wgpuRenderPassEncoderEndOcclusionQuery(pass);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
    /* A second empty real LOAD pass must preserve all pixels; ignored clear
     * values are not converted or used when loadOp is Load. */
    attachment.loadOp = WGPULoadOp_Load;
    attachment.clearValue = (WGPUColor){NAN,INFINITY,0,0};
    pass = wgpuCommandEncoderBeginRenderPass(encoder,&pass_desc);
    assert(pass);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
    wgpuCommandEncoderResolveQuerySet(encoder,queries,0,4,results,0);
    wgpuCommandEncoderCopyBufferToBuffer(encoder,results,0,readback,2048,32);
    texture_to_buffer(encoder,image,readback,0,8,8);
    wgpuShaderModuleRelease(module);
    wgpuRenderPipelineRelease(pipeline);
    wgpuBindGroupRelease(bound);
    wgpuBindGroupLayoutRelease(layout);
    wgpuBufferRelease(uniform);
    wgpuBufferRelease(vertices);
    wgpuTextureViewRelease(view);
    wgpuTextureRelease(image);
    wgpuQuerySetRelease(queries);
    wgpuBufferRelease(results);
    submit(s,encoder);
    const unsigned char *bytes = map(s,readback,2080);
    for (size_t y = 0; y < 8; ++y) for (size_t x = 0; x < 8; ++x) {
        const unsigned char expected[4] = {y < 4 ? 255 : 0,0,y < 4 && x < 4 ? 0 : 255,255};
        assert(!memcmp(bytes+y*256+x*4,expected,4));
    }
    const uint64_t *hits = (const uint64_t *)(bytes+2048);
    assert(hits[0] != 0 && !hits[1] && !hits[2] && !hits[3]);
    wgpuBufferUnmap(readback);
    wgpuBufferRelease(readback);
}
static void integer_clear(state *s)
{
    const WGPUTextureFormat formats[] = {WGPUTextureFormat_RGBA32Uint,WGPUTextureFormat_RGBA32Sint};
    const WGPUColor colors[] = {{4294967295.75,-0.75,7.9,1.0},{-2147483648.75,2147483647.75,-7.9,1.0}};
    const uint32_t expected[2][4] = {{UINT32_MAX,0,7,1},{UINT32_C(0x80000000),INT32_MAX,UINT32_C(0xfffffff9),1}};
    for (size_t i = 0; i < 2; ++i) {
        WGPUTexture image = texture(s,formats[i],WGPUTextureUsage_RenderAttachment|WGPUTextureUsage_CopySrc,1);
        WGPUTextureView view = wgpuTextureCreateView(image,NULL);
        WGPUBuffer readback = buffer(s,512,WGPUBufferUsage_MapRead|WGPUBufferUsage_CopyDst);
        WGPURenderPassColorAttachment attachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
        attachment.view = view;
        attachment.loadOp = WGPULoadOp_Clear;
        attachment.storeOp = WGPUStoreOp_Store;
        attachment.clearValue = colors[i];
        WGPURenderPassDescriptor descriptor = {.colorAttachmentCount=1,.colorAttachments=&attachment};
        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device,NULL);
        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder,&descriptor);
        assert(pass);
        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);
        texture_to_buffer(encoder,image,readback,0,2,2);
        submit(s,encoder);
        const unsigned char *bytes = map(s,readback,512);
        for (size_t y = 0; y < 2; ++y) for (size_t x = 0; x < 2; ++x)
            assert(!memcmp(bytes+y*256+x*16,expected[i],16));
        wgpuBufferUnmap(readback);
        wgpuBufferRelease(readback);
        attachment.clearValue.r = i ? -2147483649.0 : 4294967296.0;
        encoder = wgpuDeviceCreateCommandEncoder(s->device,NULL);
        wgpuDevicePushErrorScope(s->device,WGPUErrorFilter_Validation);
        /* WebGPU invalid pass: commands on it record nothing; Finish fails. */
        WGPURenderPassEncoder rejected = wgpuCommandEncoderBeginRenderPass(encoder,&descriptor);
        assert(rejected);
        wgpuRenderPassEncoderDraw(rejected,3,1,0,0);
        wgpuRenderPassEncoderEnd(rejected);
        wgpuRenderPassEncoderRelease(rejected);
        finish_invalid(encoder);
        expect_validation(s);
        wgpuCommandEncoderRelease(encoder);
        wgpuTextureViewRelease(view);
        wgpuTextureRelease(image);
    }
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
    WGPURequestDeviceCallbackInfo requested = {.mode=WGPUCallbackMode_AllowSpontaneous,.callback=device_reply,.userdata1=&s};
    assert(wgpuAdapterRequestDevice(s.adapter,&descriptor,requested).id && s.device);
    s.queue = wgpuDeviceGetQueue(s.device);
    main_graphics(&s);
    integer_clear(&s);
    assert(!s.errors);
    wgpuQueueRelease(s.queue);
    wgpuDeviceRelease(s.device);
    wgpuAdapterRelease(s.adapter);
    wgpuInstanceRelease(s.instance);
    printf("PASS public C graphics: genuine WGSL stage IO, sparse shared uniform, empty vertex slot, default fetched components, dynamic blend/scissor/culling, bundle replay, one native pass, integer clears, empty LOAD, real occlusion and early releases %s\n", adapter_class_scope());
}
