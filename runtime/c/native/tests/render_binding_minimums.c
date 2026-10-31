/* SPDX-License-Identifier: MIT
 * Render shaders' minimum buffer binding sizes. With a layout minBindingSize of
 * 0, every draw (pass or bundle) must reject a binding smaller than the stage's
 * reflected requirement as a validation error and never execute it; correctly
 * sized bindings render the bound values. */
#include "consumer.h"

static const char vertex_storage[] =
    "@group(0) @binding(0) var<storage,read> d:array<vec4f,4>;"
    "struct O{@builtin(position) p:vec4f,@location(0) c:vec4f}"
    "@vertex fn v(@builtin(vertex_index) i:u32)->O{"
    "return O(vec4f(f32(i32(i&1u)*4-1),f32(i32(i>>1u)*4-1),0.0,1.0),d[3]);}"
    "@fragment fn f(@location(0) c:vec4f)->@location(0) vec4f{return c;}";
static const char fragment_uniform[] =
    "struct U{a:vec4f,b:vec4f}"
    "@group(0) @binding(0) var<uniform> u:U;"
    "@vertex fn v(@builtin(vertex_index) i:u32)->@builtin(position) vec4f{"
    "return vec4f(f32(i32(i&1u)*4-1),f32(i32(i>>1u)*4-1),0.0,1.0);}"
    "@fragment fn f()->@location(0) vec4f{return u.b;}";

typedef struct render_case {
    const char *code;
    WGPUShaderStage visibility;
    WGPUBufferBindingType type;
    WGPUBufferUsage usage;
    uint64_t required, undersized;
    float value[4];
} render_case;

typedef struct fixture {
    WGPUBindGroupLayout layout;
    WGPURenderPipeline pipeline;
    WGPUTexture image;
    WGPUTextureView view;
    WGPUBuffer data;
} fixture;

static fixture setup(state *s, const render_case *test)
{
    fixture f = {0};
    WGPUShaderSourceWGSL source = WGPU_SHADER_SOURCE_WGSL_INIT;
    source.code = (WGPUStringView){test->code,WGPU_STRLEN};
    WGPUShaderModuleDescriptor shader_desc = {.nextInChain=&source.chain};
    WGPUShaderModule module = wgpuDeviceCreateShaderModule(s->device,&shader_desc);
    assert(module);
    /* minBindingSize 0 defers the shader's requirement to draw time. */
    WGPUBindGroupLayoutEntry entry = {.binding=0,.visibility=test->visibility,.buffer={.type=test->type}};
    WGPUBindGroupLayoutDescriptor layout_desc = {.entryCount=1,.entries=&entry};
    f.layout = wgpuDeviceCreateBindGroupLayout(s->device,&layout_desc);
    assert(f.layout);
    WGPUPipelineLayoutDescriptor pipeline_layout_desc = {.bindGroupLayoutCount=1,.bindGroupLayouts=&f.layout};
    WGPUPipelineLayout pipeline_layout = wgpuDeviceCreatePipelineLayout(s->device,&pipeline_layout_desc);
    assert(pipeline_layout);
    WGPUColorTargetState color = {.format=WGPUTextureFormat_RGBA8Unorm,.writeMask=WGPUColorWriteMask_All};
    WGPUFragmentState fragment = {.module=module,.targetCount=1,.targets=&color};
    WGPURenderPipelineDescriptor descriptor = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    descriptor.layout = pipeline_layout;
    descriptor.vertex.module = module;
    descriptor.fragment = &fragment;
    f.pipeline = wgpuDeviceCreateRenderPipeline(s->device,&descriptor);
    assert(f.pipeline);
    wgpuPipelineLayoutRelease(pipeline_layout);
    wgpuShaderModuleRelease(module);
    f.image = texture(s,WGPUTextureFormat_RGBA8Unorm,WGPUTextureUsage_RenderAttachment|WGPUTextureUsage_CopySrc,1);
    f.view = wgpuTextureCreateView(f.image,NULL);
    assert(f.view);
    /* The buffer itself is large; only the binding's size is undersized. */
    f.data = buffer(s,64,test->usage|WGPUBufferUsage_CopyDst);
    float words[16] = {0};
    memcpy(&words[(test->required - 16) / sizeof(float)],test->value,sizeof(test->value));
    wgpuQueueWriteBuffer(s->queue,f.data,0,words,sizeof(words));
    return f;
}

static WGPUBindGroup bind(state *s, const fixture *f, uint64_t size)
{
    WGPUBindGroupEntry entry = {.binding=0,.buffer=f->data,.offset=0,.size=size};
    WGPUBindGroupDescriptor descriptor = {.layout=f->layout,.entryCount=1,.entries=&entry};
    WGPUBindGroup group = wgpuDeviceCreateBindGroup(s->device,&descriptor);
    assert(group);
    return group;
}

static WGPURenderPassEncoder begin(WGPUCommandEncoder encoder, const fixture *f)
{
    WGPURenderPassColorAttachment attachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    attachment.view = f->view;
    attachment.loadOp = WGPULoadOp_Clear;
    attachment.storeOp = WGPUStoreOp_Store;
    WGPURenderPassDescriptor descriptor = {.colorAttachmentCount=1,.colorAttachments=&attachment};
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder,&descriptor);
    assert(pass);
    return pass;
}

static void reject_pass(state *s, const fixture *f, WGPUBindGroup group)
{
    wgpuDevicePushErrorScope(s->device,WGPUErrorFilter_Validation);
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device,NULL);
    WGPURenderPassEncoder pass = begin(encoder,f);
    wgpuRenderPassEncoderSetPipeline(pass,f->pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass,0,group,0,NULL);
    wgpuRenderPassEncoderDraw(pass,3,1,0,0);
    wgpuRenderPassEncoderEnd(pass);
    finish_invalid(encoder);
    expect_validation(s);
    wgpuRenderPassEncoderRelease(pass);
    wgpuCommandEncoderRelease(encoder);
}

static void reject_bundle(state *s, const fixture *f, WGPUBindGroup group)
{
    WGPUTextureFormat format = WGPUTextureFormat_RGBA8Unorm;
    WGPURenderBundleEncoderDescriptor descriptor = {.colorFormatCount=1,.colorFormats=&format,.sampleCount=1};
    wgpuDevicePushErrorScope(s->device,WGPUErrorFilter_Validation);
    WGPURenderBundleEncoder bundle = wgpuDeviceCreateRenderBundleEncoder(s->device,&descriptor);
    assert(bundle);
    wgpuRenderBundleEncoderSetPipeline(bundle,f->pipeline);
    wgpuRenderBundleEncoderSetBindGroup(bundle,0,group,0,NULL);
    wgpuRenderBundleEncoderDraw(bundle,3,1,0,0);
    WGPURenderBundle finished = wgpuRenderBundleEncoderFinish(bundle,NULL);
    expect_validation(s);
    wgpuRenderBundleRelease(finished);
    wgpuRenderBundleEncoderRelease(bundle);
}

static void accept_pass(state *s, const fixture *f, WGPUBindGroup group, const float value[4])
{
    WGPUBuffer readback = buffer(s,512,WGPUBufferUsage_CopyDst|WGPUBufferUsage_MapRead);
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device,NULL);
    WGPURenderPassEncoder pass = begin(encoder,f);
    wgpuRenderPassEncoderSetPipeline(pass,f->pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass,0,group,0,NULL);
    wgpuRenderPassEncoderDraw(pass,3,1,0,0);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
    texture_to_buffer(encoder,f->image,readback,0,2,2);
    submit(s,encoder);
    const unsigned char *pixels = map(s,readback,512);
    for (size_t row = 0; row < 2; ++row)
        for (size_t column = 0; column < 2; ++column)
            for (size_t channel = 0; channel < 4; ++channel)
                assert(pixels[row * 256 + column * 4 + channel] == (unsigned char)(value[channel] * 255.0f));
    wgpuBufferUnmap(readback);
    wgpuBufferRelease(readback);
}

static void run(state *s, const render_case *test)
{
    fixture f = setup(s,test);
    WGPUBindGroup small = bind(s,&f,test->undersized);
    WGPUBindGroup exact = bind(s,&f,test->required);
    WGPUBindGroup whole = bind(s,&f,WGPU_WHOLE_SIZE);
    reject_pass(s,&f,small);
    reject_bundle(s,&f,small);
    accept_pass(s,&f,exact,test->value);
    accept_pass(s,&f,whole,test->value);
    wgpuBindGroupRelease(whole);
    wgpuBindGroupRelease(exact);
    wgpuBindGroupRelease(small);
    wgpuBufferRelease(f.data);
    wgpuTextureViewRelease(f.view);
    wgpuTextureRelease(f.image);
    wgpuRenderPipelineRelease(f.pipeline);
    wgpuBindGroupLayoutRelease(f.layout);
}

int main(void)
{
    state s = {0};
    consumer_init(&s);
    const render_case cases[] = {
        {vertex_storage,WGPUShaderStage_Vertex,WGPUBufferBindingType_ReadOnlyStorage,WGPUBufferUsage_Storage,
            64,48,{0.0f,1.0f,0.0f,1.0f}},
        {fragment_uniform,WGPUShaderStage_Fragment,WGPUBufferBindingType_Uniform,WGPUBufferUsage_Uniform,
            32,16,{0.0f,0.0f,1.0f,1.0f}},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) run(&s,&cases[i]);
    work_done(&s);
    consumer_finish(&s);
    printf("PASS render binding minimums: undersized vertex storage and fragment uniform bindings reject at pass and bundle draws; sized bindings render %s\n",
        adapter_class_scope());
}
