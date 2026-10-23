/* SPDX-License-Identifier: MIT
 * Public graphics stage, callback and canonical recording rejection controls. */
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
static void reject_pipeline(state *s, const WGPURenderPipelineDescriptor *descriptor)
{
    wgpuDevicePushErrorScope(s->device,WGPUErrorFilter_Validation);
    assert(!wgpuDeviceCreateRenderPipeline(s->device,descriptor));
    expect_validation(s);
}
typedef struct delivered {
    WGPUInstance instance;
    WGPUCreatePipelineAsyncStatus expected;
    unsigned calls;
} delivered;
static void pipeline_reply(WGPUCreatePipelineAsyncStatus status, WGPURenderPipeline pipeline,
    WGPUStringView message, void *userdata, void *unused)
{
    (void)message;
    (void)unused;
    delivered *value = userdata;
    assert(status == value->expected);
    assert((pipeline != NULL) == (status == WGPUCreatePipelineAsyncStatus_Success));
    ++value->calls;
    wgpuRenderPipelineRelease(pipeline);
    wgpuInstanceProcessEvents(value->instance); /* Reentry is outside device locks. */
}
static void stage_controls(state *s)
{
    WGPUShaderModule module = shader(s,
        "@vertex fn v()->@builtin(position) vec4f{return vec4f(0.0,0.0,0.0,1.0);}"
        "@fragment fn f()->@location(0) vec4f{return vec4f(1.0);}");
    WGPUColorTargetState color = {.format=WGPUTextureFormat_RGBA8Unorm,.writeMask=WGPUColorWriteMask_All};
    WGPUFragmentState fragment = {.module=module,.targets=&color,.targetCount=1};
    WGPURenderPipelineDescriptor descriptor = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    descriptor.vertex.module = module;
    descriptor.fragment = &fragment;
    WGPURenderPipeline pipeline = wgpuDeviceCreateRenderPipeline(s->device,&descriptor);
    assert(pipeline);
    wgpuRenderPipelineRelease(pipeline);
    descriptor.vertex.entryPoint = (WGPUStringView){"f",1};
    reject_pipeline(s,&descriptor);
    descriptor.vertex.entryPoint = (WGPUStringView){0};
    color.format = WGPUTextureFormat_RGBA8Uint;
    reject_pipeline(s,&descriptor);
    color.format = WGPUTextureFormat_RGBA8Unorm;
    /* PointList is admitted with maintenance5 (point_list.c); an unknown
     * topology value still rejects. */
    descriptor.primitive.topology = (WGPUPrimitiveTopology)0x7F;
    reject_pipeline(s,&descriptor);
    descriptor.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    const WGPUCallbackMode modes[] = {WGPUCallbackMode_WaitAnyOnly,WGPUCallbackMode_AllowProcessEvents,
        WGPUCallbackMode_AllowSpontaneous};
    for (size_t i = 0; i < 3; ++i) {
        delivered value = {.instance=s->instance,.expected=WGPUCreatePipelineAsyncStatus_Success};
        WGPUCreateRenderPipelineAsyncCallbackInfo callback = {.mode=modes[i],.callback=pipeline_reply,.userdata1=&value};
        WGPUFuture future = wgpuDeviceCreateRenderPipelineAsync(s->device,&descriptor,callback);
        assert(future.id && value.calls == (modes[i] == WGPUCallbackMode_AllowSpontaneous));
        WGPUFutureWaitInfo wait = {.future=future};
        assert(wgpuInstanceWaitAny(s->instance,1,&wait,0) == WGPUWaitStatus_Success && value.calls == 1);
        descriptor.vertex.entryPoint = (WGPUStringView){"absent",6};
        value.expected = WGPUCreatePipelineAsyncStatus_ValidationError;
        value.calls = 0;
        future = wgpuDeviceCreateRenderPipelineAsync(s->device,&descriptor,callback);
        assert(future.id);
        wait = (WGPUFutureWaitInfo){.future=future};
        assert(wgpuInstanceWaitAny(s->instance,1,&wait,0) == WGPUWaitStatus_Success && value.calls == 1);
        descriptor.vertex.entryPoint = (WGPUStringView){0};
    }
    WGPUShaderModule missing = shader(s,"@fragment fn empty(){}");
    fragment.module = missing;
    reject_pipeline(s,&descriptor); /* A written target must have a fragment output. */
    color.writeMask = 0;
    pipeline = wgpuDeviceCreateRenderPipeline(s->device,&descriptor);
    assert(pipeline);
    wgpuRenderPipelineRelease(pipeline);
    wgpuShaderModuleRelease(missing);
    WGPUShaderModule scalar = shader(s,"@fragment fn scalar()->@location(0) f32{return 1.0;}");
    fragment.module = scalar;
    color.format = WGPUTextureFormat_R8Unorm;
    color.writeMask = WGPUColorWriteMask_All;
    WGPUBlendState blend = {.color={.srcFactor=WGPUBlendFactor_SrcAlpha,.dstFactor=WGPUBlendFactor_Zero}};
    color.blend = &blend;
    reject_pipeline(s,&descriptor); /* Source-alpha blending requires an actual alpha output. */
    color.blend = NULL;
    pipeline = wgpuDeviceCreateRenderPipeline(s->device,&descriptor);
    assert(pipeline);
    wgpuRenderPipelineRelease(pipeline);
    wgpuShaderModuleRelease(scalar);
    WGPUShaderModule extra = shader(s,
        "struct O{@location(0) color:vec4f,@location(1) ignored:vec4u}"
        "@fragment fn extra()->O{return O(vec4f(1.0),vec4u(7u));}");
    fragment.module = extra;
    color.format = WGPUTextureFormat_RGBA8Unorm;
    pipeline = wgpuDeviceCreateRenderPipeline(s->device,&descriptor);
    assert(pipeline); /* Additional shader outputs not targeted by the pipeline are ignored. */
    wgpuRenderPipelineRelease(pipeline);
    wgpuShaderModuleRelease(extra);
    wgpuShaderModuleRelease(module);
}
static void draw_controls(state *s)
{
    WGPUShaderModule module = shader(s,
        "@vertex fn v(@location(0) p:vec2f)->@builtin(position) vec4f{return vec4f(p,0.0,1.0);}"
        "@fragment fn f()->@location(0) vec4f{return vec4f(1.0);}");
    WGPUVertexAttribute attribute = {.format=WGPUVertexFormat_Float32x2};
    WGPUVertexBufferLayout vertices[2] = {{.stepMode=WGPUVertexStepMode_Vertex},
        {.arrayStride=8,.attributeCount=1,.attributes=&attribute}};
    WGPUColorTargetState color = {.format=WGPUTextureFormat_RGBA8Unorm,.writeMask=WGPUColorWriteMask_All};
    WGPUFragmentState fragment = {.module=module,.targetCount=1,.targets=&color};
    WGPURenderPipelineDescriptor descriptor = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    descriptor.vertex = (WGPUVertexState){.module=module,.bufferCount=2,.buffers=vertices};
    descriptor.fragment = &fragment;
    WGPURenderPipeline pipeline = wgpuDeviceCreateRenderPipeline(s->device,&descriptor);
    assert(pipeline);
    WGPUTexture image = texture(s,WGPUTextureFormat_RGBA8Unorm,WGPUTextureUsage_RenderAttachment,1);
    WGPUTextureView view = wgpuTextureCreateView(image,NULL);
    WGPUBuffer data = buffer(s,8,WGPUBufferUsage_Vertex);
    WGPURenderPassColorAttachment attachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    attachment.view = view;
    attachment.loadOp = WGPULoadOp_Clear;
    attachment.storeOp = WGPUStoreOp_Store;
    WGPURenderPassDescriptor pass_desc = {.colorAttachmentCount=1,.colorAttachments=&attachment};
    for (unsigned test = 0; test < 3; ++test) {
        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device,NULL);
        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder,&pass_desc);
        assert(pass);
        wgpuRenderPassEncoderSetPipeline(pass,pipeline);
        wgpuRenderPassEncoderSetVertexBuffer(pass,1,data,0,8);
        if (test) wgpuRenderPassEncoderSetVertexBuffer(pass,0,data,0,8);
        wgpuDevicePushErrorScope(s->device,WGPUErrorFilter_Validation);
        if (test == 2) wgpuRenderPassEncoderExecuteBundles(pass,0,NULL);
        wgpuRenderPassEncoderDraw(pass,0,1,test == 1 ? 2 : 0,0);
        wgpuRenderPassEncoderEnd(pass);
        assert(!wgpuCommandEncoderFinish(encoder,NULL));
        expect_validation(s);
        wgpuRenderPassEncoderRelease(pass);
        wgpuCommandEncoderRelease(encoder);
    }
    wgpuBufferRelease(data);
    wgpuTextureViewRelease(view);
    wgpuTextureRelease(image);
    wgpuRenderPipelineRelease(pipeline);
    wgpuShaderModuleRelease(module);
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
    stage_controls(&s);
    draw_controls(&s);
    assert(!s.errors);
    wgpuQueueRelease(s.queue);
    wgpuDeviceRelease(s.device);
    wgpuAdapterRelease(s.adapter);
    wgpuInstanceRelease(s.instance);
    printf("PASS public graphics admission: stage/output identity, async modes/reentry, explicit empty vertex slot, zero-count range and bundle state reset %s\n", adapter_class_scope());
}
