/* SPDX-License-Identifier: MIT
 * Public depth/stencil sampling through the real compiler: rendered depth maps
 * read in compute via texture_depth_2d/_2d_array (automatic 'depth' layouts),
 * texture_2d<f32> over an explicit unfilterable-float layout, stencil as
 * texture_2d<u32> through a StencilOnly view, DepthOnly views of a combined
 * format, texture_depth_cube dimensions, and comparison-sampler binding
 * admission. Comparison sampling builtins are absent from the compiler, so
 * their pass/fail oracle is not part of this client. */
#include "consumer.h"

enum { WIDTH = 4, HEIGHT = 2, TEXELS = WIDTH * HEIGHT, SLOT = 256, SLOTS = 8 };
typedef struct fixture {
    WGPUBuffer vertices, output, readback;
    WGPURenderPipeline d32_writer, d24s8_writer;
} fixture;

static WGPUShaderModule module_of(state *s, const char *code)
{
    WGPUShaderSourceWGSL source = WGPU_SHADER_SOURCE_WGSL_INIT;
    source.code = (WGPUStringView){code,WGPU_STRLEN};
    WGPUShaderModuleDescriptor descriptor = {.nextInChain=&source.chain};
    WGPUShaderModule module = wgpuDeviceCreateShaderModule(s->device,&descriptor);
    assert(module);
    return module;
}
static WGPUTexture image(state *s, WGPUTextureFormat format, WGPUTextureUsage usage, uint32_t width,
    uint32_t height, uint32_t layers)
{
    WGPUTextureDescriptor descriptor = WGPU_TEXTURE_DESCRIPTOR_INIT;
    descriptor.format = format;
    descriptor.usage = usage;
    descriptor.size = (WGPUExtent3D){width,height,layers};
    WGPUTexture result = wgpuDeviceCreateTexture(s->device,&descriptor);
    assert(result);
    return result;
}
static WGPUTextureView view_of(WGPUTexture texture, WGPUTextureViewDimension dimension,
    WGPUTextureAspect aspect, uint32_t base_layer, uint32_t layers)
{
    WGPUTextureViewDescriptor descriptor = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    descriptor.dimension = dimension;
    descriptor.aspect = aspect;
    descriptor.baseArrayLayer = base_layer;
    descriptor.arrayLayerCount = layers;
    WGPUTextureView view = wgpuTextureCreateView(texture,&descriptor);
    assert(view);
    return view;
}
/* Vertex-only writer: depth Always/write, stencil Replace with the pass reference. */
static WGPURenderPipeline writer(state *s, WGPUTextureFormat format)
{
    WGPUShaderModule module = module_of(s,
        "@vertex fn vertex(@location(0) position:vec4f)->@builtin(position) vec4f{return position;}");
    WGPUVertexAttribute attribute = {.format=WGPUVertexFormat_Float32x4,.shaderLocation=0};
    WGPUVertexBufferLayout layout = {.arrayStride=16,.attributeCount=1,.attributes=&attribute};
    WGPUDepthStencilState depth = WGPU_DEPTH_STENCIL_STATE_INIT;
    depth.format = format;
    depth.depthWriteEnabled = WGPUOptionalBool_True;
    depth.depthCompare = WGPUCompareFunction_Always;
    if (format == WGPUTextureFormat_Depth24PlusStencil8) {
        depth.stencilFront.compare = depth.stencilBack.compare = WGPUCompareFunction_Always;
        depth.stencilFront.passOp = depth.stencilBack.passOp = WGPUStencilOperation_Replace;
    }
    WGPURenderPipelineDescriptor descriptor = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    descriptor.vertex = (WGPUVertexState){.module=module,.bufferCount=1,.buffers=&layout};
    descriptor.depthStencil = &depth;
    WGPURenderPipeline result = wgpuDeviceCreateRenderPipeline(s->device,&descriptor);
    wgpuShaderModuleRelease(module);
    assert(result);
    return result;
}
/* Draw quads (0 left z.25, 6 right z.75, 12 full z.25, 18 full z.75); stencil refs per quad. */
static void render(WGPUCommandEncoder encoder, const fixture *f, WGPUTextureView view, bool stencil,
    const uint32_t *quads, const uint32_t *references, size_t count)
{
    WGPURenderPassDepthStencilAttachment attachment = WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
    attachment.view = view;
    attachment.depthLoadOp = WGPULoadOp_Clear;
    attachment.depthStoreOp = WGPUStoreOp_Store;
    attachment.depthClearValue = 1;
    if (stencil) {
        attachment.stencilLoadOp = WGPULoadOp_Clear;
        attachment.stencilStoreOp = WGPUStoreOp_Store;
        attachment.stencilClearValue = 3;
    }
    WGPURenderPassDescriptor descriptor = {.depthStencilAttachment=&attachment};
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder,&descriptor);
    assert(pass);
    wgpuRenderPassEncoderSetPipeline(pass,stencil ? f->d24s8_writer : f->d32_writer);
    wgpuRenderPassEncoderSetVertexBuffer(pass,0,f->vertices,0,WGPU_WHOLE_SIZE);
    for (size_t i = 0; i < count; ++i) {
        if (stencil) wgpuRenderPassEncoderSetStencilReference(pass,references[i]);
        wgpuRenderPassEncoderDraw(pass,6,1,quads[i],0);
    }
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
}
/* Run one compute reader writing its results to output slot `slot`. Group 0:
 * binding 0 texture, optional binding 1 sampler, binding 2 storage output. */
static void read(state *s, const fixture *f, WGPUCommandEncoder encoder, WGPUComputePipeline pipeline,
    WGPUBindGroupLayout layout, WGPUTextureView view, WGPUSampler sampler, uint32_t slot)
{
    WGPUBindGroupLayout owned = layout ? NULL : wgpuComputePipelineGetBindGroupLayout(pipeline,0);
    WGPUBindGroupEntry entries[3] = {{.binding=0,.textureView=view},
        {.binding=2,.buffer=f->output,.offset=slot * SLOT,.size=SLOT}, {.binding=1,.sampler=sampler}};
    WGPUBindGroupDescriptor descriptor = {.layout=layout ? layout : owned,.entryCount=sampler ? 3 : 2,.entries=entries};
    WGPUBindGroup group = wgpuDeviceCreateBindGroup(s->device,&descriptor);
    assert(group);
    WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder,NULL);
    wgpuComputePassEncoderSetPipeline(pass,pipeline);
    wgpuComputePassEncoderSetBindGroup(pass,0,group,0,NULL);
    wgpuComputePassEncoderDispatchWorkgroups(pass,1,1,1);
    wgpuComputePassEncoderEnd(pass);
    wgpuComputePassEncoderRelease(pass);
    wgpuBindGroupRelease(group);
    if (owned) wgpuBindGroupLayoutRelease(owned);
}
static WGPUComputePipeline compute(state *s, const char *code, WGPUPipelineLayout layout)
{
    WGPUShaderModule module = module_of(s,code);
    WGPUComputePipelineDescriptor descriptor = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
    descriptor.compute.module = module;
    descriptor.layout = layout;
    WGPUComputePipeline result = wgpuDeviceCreateComputePipeline(s->device,&descriptor);
    wgpuShaderModuleRelease(module);
    assert(result);
    return result;
}
/* Explicit layout: binding 0 texture of `sample`, optional binding 1 sampler, binding 2 storage. */
static WGPUBindGroupLayout explicit_layout(state *s, WGPUTextureSampleType sample, WGPUSamplerBindingType sampler)
{
    WGPUBindGroupLayoutEntry entries[3] = {
        {.binding=0,.visibility=WGPUShaderStage_Compute,.texture={.sampleType=sample,
            .viewDimension=WGPUTextureViewDimension_2D}},
        {.binding=2,.visibility=WGPUShaderStage_Compute,.buffer={.type=WGPUBufferBindingType_Storage}},
        {.binding=1,.visibility=WGPUShaderStage_Compute,.sampler={.type=sampler}}};
    WGPUBindGroupLayoutDescriptor descriptor = {.entryCount=sampler ? 3 : 2,.entries=entries};
    WGPUBindGroupLayout layout = wgpuDeviceCreateBindGroupLayout(s->device,&descriptor);
    assert(layout);
    return layout;
}
static WGPUPipelineLayout pipeline_layout(state *s, WGPUBindGroupLayout group)
{
    WGPUPipelineLayoutDescriptor descriptor = {.bindGroupLayoutCount=1,.bindGroupLayouts=&group};
    WGPUPipelineLayout layout = wgpuDeviceCreatePipelineLayout(s->device,&descriptor);
    assert(layout);
    return layout;
}
static WGPUSampler sampler_of(state *s, WGPUCompareFunction compare)
{
    WGPUSamplerDescriptor descriptor = WGPU_SAMPLER_DESCRIPTOR_INIT;
    descriptor.compare = compare;
    WGPUSampler sampler = wgpuDeviceCreateSampler(s->device,&descriptor);
    assert(sampler);
    return sampler;
}
/* One validation error from bind-group creation. */
static void rejected_group(state *s, WGPUBindGroupLayout layout, WGPUTextureView view, WGPUSampler sampler,
    WGPUBuffer output)
{
    WGPUBindGroupEntry entries[3] = {{.binding=0,.textureView=view},{.binding=2,.buffer=output,.size=SLOT},
        {.binding=1,.sampler=sampler}};
    WGPUBindGroupDescriptor descriptor = {.layout=layout,.entryCount=sampler ? 3 : 2,.entries=entries};
    wgpuDevicePushErrorScope(s->device,WGPUErrorFilter_Validation);
    assert(!wgpuDeviceCreateBindGroup(s->device,&descriptor));
    expect_validation(s);
}
static void build(state *s, fixture *f)
{
    const float spans[4][3] = {{-1,0,0.25f},{0,1,0.75f},{-1,1,0.25f},{-1,1,0.75f}};
    float data[24][4];
    for (size_t q = 0; q < 4; ++q) {
        const float x0 = spans[q][0], x1 = spans[q][1];
        const float corners[6][2] = {{x0,-1},{x1,-1},{x0,1},{x1,-1},{x1,1},{x0,1}};
        for (size_t v = 0; v < 6; ++v) {
            data[q * 6 + v][0] = corners[v][0];
            data[q * 6 + v][1] = corners[v][1];
            data[q * 6 + v][2] = spans[q][2];
            data[q * 6 + v][3] = 1;
        }
    }
    f->vertices = buffer(s,sizeof(data),WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst);
    wgpuQueueWriteBuffer(s->queue,f->vertices,0,data,sizeof(data));
    f->output = buffer(s,SLOTS * SLOT,WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc);
    f->readback = buffer(s,SLOTS * SLOT,WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst);
    f->d32_writer = writer(s,WGPUTextureFormat_Depth32Float);
    f->d24s8_writer = writer(s,WGPUTextureFormat_Depth24PlusStencil8);
}
static const char *const load_depth =
    "@group(0) @binding(0) var t:texture_depth_2d;@group(0) @binding(2) var<storage,read_write> o:array<f32>;"
    "@compute @workgroup_size(1) fn main(){for(var i=0u;i<8u;i++){o[i]=textureLoad(t,vec2i(i32(i%4u),i32(i/4u)),0);}}";
int main(void)
{
    state s = {0};
    consumer_init(&s);
    fixture f = {0};
    build(&s,&f);
    const WGPUTextureUsage sampled = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding;
    WGPUTexture d32 = image(&s,WGPUTextureFormat_Depth32Float,sampled,WIDTH,HEIGHT,1);
    WGPUTexture d24s8 = image(&s,WGPUTextureFormat_Depth24PlusStencil8,sampled,WIDTH,HEIGHT,1);
    WGPUTexture layered = image(&s,WGPUTextureFormat_Depth32Float,sampled,WIDTH,HEIGHT,2);
    WGPUTexture cube = image(&s,WGPUTextureFormat_Depth32Float,WGPUTextureUsage_TextureBinding,2,2,6);

    /* Creation and view controls: no depth storage; DepthOnly needs a depth aspect. */
    WGPUTextureDescriptor storage = WGPU_TEXTURE_DESCRIPTOR_INIT;
    storage.format = WGPUTextureFormat_Depth32Float;
    storage.usage = WGPUTextureUsage_StorageBinding;
    storage.size = (WGPUExtent3D){WIDTH,HEIGHT,1};
    wgpuDevicePushErrorScope(s.device,WGPUErrorFilter_Validation);
    assert(!wgpuDeviceCreateTexture(s.device,&storage));
    expect_validation(&s);
    WGPUTexture stencil_only = image(&s,WGPUTextureFormat_Stencil8,sampled,WIDTH,HEIGHT,1);
    WGPUTextureViewDescriptor depth_of_stencil = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    depth_of_stencil.aspect = WGPUTextureAspect_DepthOnly;
    wgpuDevicePushErrorScope(s.device,WGPUErrorFilter_Validation);
    assert(!wgpuTextureCreateView(stencil_only,&depth_of_stencil));
    expect_validation(&s);
    wgpuTextureRelease(stencil_only);

    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s.device,NULL);
    WGPUTextureView attach = view_of(d32,WGPUTextureViewDimension_2D,WGPUTextureAspect_All,0,1);
    const uint32_t halves[2] = {0,6}, refs[2] = {7,9}, full25[1] = {12}, full75[1] = {18};
    render(encoder,&f,attach,false,halves,NULL,2);
    wgpuTextureViewRelease(attach);
    attach = view_of(d24s8,WGPUTextureViewDimension_2D,WGPUTextureAspect_All,0,1);
    render(encoder,&f,attach,true,halves,refs,2);
    wgpuTextureViewRelease(attach);
    for (uint32_t layer = 0; layer < 2; ++layer) {
        attach = view_of(layered,WGPUTextureViewDimension_2D,WGPUTextureAspect_All,layer,1);
        render(encoder,&f,attach,false,layer ? full75 : full25,NULL,1);
        wgpuTextureViewRelease(attach);
    }

    /* Automatic 'depth' layouts from texture_depth_* reflection. */
    WGPUComputePipeline depth_2d = compute(&s,load_depth,NULL);
    WGPUTextureView d32_view = view_of(d32,WGPUTextureViewDimension_2D,WGPUTextureAspect_All,0,1);
    WGPUTextureView d24_depth = view_of(d24s8,WGPUTextureViewDimension_2D,WGPUTextureAspect_DepthOnly,0,1);
    WGPUTextureView d24_stencil = view_of(d24s8,WGPUTextureViewDimension_2D,WGPUTextureAspect_StencilOnly,0,1);
    WGPUTextureView d24_all = view_of(d24s8,WGPUTextureViewDimension_2D,WGPUTextureAspect_All,0,1);
    read(&s,&f,encoder,depth_2d,NULL,d32_view,NULL,0);
    read(&s,&f,encoder,depth_2d,NULL,d24_depth,NULL,1);
    WGPUComputePipeline stencil_reader = compute(&s,
        "@group(0) @binding(0) var t:texture_2d<u32>;@group(0) @binding(2) var<storage,read_write> o:array<u32>;"
        "@compute @workgroup_size(1) fn main(){for(var i=0u;i<8u;i++){o[i]=textureLoad(t,vec2i(i32(i%4u),i32(i/4u)),0).x;}}",NULL);
    read(&s,&f,encoder,stencil_reader,NULL,d24_stencil,NULL,2);
    WGPUComputePipeline array_reader = compute(&s,
        "@group(0) @binding(0) var t:texture_depth_2d_array;@group(0) @binding(2) var<storage,read_write> o:array<f32>;"
        "@compute @workgroup_size(1) fn main(){for(var i=0u;i<16u;i++){"
        "o[i]=textureLoad(t,vec2i(i32(i%4u),i32((i/4u)%2u)),i32(i/8u),0);}}",NULL);
    WGPUTextureView layered_view = view_of(layered,WGPUTextureViewDimension_2DArray,WGPUTextureAspect_All,0,2);
    read(&s,&f,encoder,array_reader,NULL,layered_view,NULL,3);
    WGPUComputePipeline cube_reader = compute(&s,
        "@group(0) @binding(0) var t:texture_depth_cube;@group(0) @binding(2) var<storage,read_write> o:array<u32>;"
        "@compute @workgroup_size(1) fn main(){let d=textureDimensions(t);o[0]=d.x;o[1]=d.y;}",NULL);
    WGPUTextureView cube_view = view_of(cube,WGPUTextureViewDimension_Cube,WGPUTextureAspect_All,0,6);
    read(&s,&f,encoder,cube_reader,NULL,cube_view,NULL,4);

    /* Explicit unfilterable-float layout over a depth view, plus an unused
     * comparison-sampler binding: admission only, not comparison sampling. */
    WGPUBindGroupLayout unfilterable = explicit_layout(&s,WGPUTextureSampleType_UnfilterableFloat,
        WGPUSamplerBindingType_Comparison);
    WGPUPipelineLayout unfilterable_layout = pipeline_layout(&s,unfilterable);
    WGPUComputePipeline float_reader = compute(&s,
        "@group(0) @binding(0) var t:texture_2d<f32>;@group(0) @binding(2) var<storage,read_write> o:array<f32>;"
        "@compute @workgroup_size(1) fn main(){for(var i=0u;i<8u;i++){o[i]=textureLoad(t,vec2i(i32(i%4u),i32(i/4u)),0).x;}}",
        unfilterable_layout);
    WGPUSampler less = sampler_of(&s,WGPUCompareFunction_Less);
    WGPUSampler plain = sampler_of(&s,WGPUCompareFunction_Undefined);
    read(&s,&f,encoder,float_reader,unfilterable,d24_depth,less,5);

    /* Wrong-but-typed binding controls. */
    WGPUBindGroupLayout float_layout = explicit_layout(&s,WGPUTextureSampleType_Float,WGPUSamplerBindingType_BindingNotUsed);
    WGPUBindGroupLayout depth_layout = explicit_layout(&s,WGPUTextureSampleType_Depth,WGPUSamplerBindingType_BindingNotUsed);
    WGPUBindGroupLayout depth_filtering = explicit_layout(&s,WGPUTextureSampleType_Depth,WGPUSamplerBindingType_Filtering);
    rejected_group(&s,float_layout,d32_view,NULL,f.output);       /* depth is not filterable float */
    rejected_group(&s,depth_layout,d24_all,NULL,f.output);        /* combined view needs one aspect */
    rejected_group(&s,depth_layout,d24_stencil,NULL,f.output);    /* stencil samples as uint */
    rejected_group(&s,unfilterable,d24_depth,plain,f.output);     /* comparison entry needs compare */
    rejected_group(&s,depth_filtering,d24_depth,less,f.output);   /* filtering entry rejects compare */

    wgpuCommandEncoderCopyBufferToBuffer(encoder,f.output,0,f.readback,0,SLOTS * SLOT);
    WGPUTextureView views[] = {d32_view,d24_depth,d24_stencil,d24_all,layered_view,cube_view};
    for (size_t i = 0; i < sizeof(views) / sizeof(*views); ++i) wgpuTextureViewRelease(views[i]);
    WGPUTexture textures[] = {d32,d24s8,layered,cube};
    for (size_t i = 0; i < sizeof(textures) / sizeof(*textures); ++i) wgpuTextureRelease(textures[i]);
    WGPUComputePipeline pipelines[] = {depth_2d,stencil_reader,array_reader,cube_reader,float_reader};
    for (size_t i = 0; i < sizeof(pipelines) / sizeof(*pipelines); ++i) wgpuComputePipelineRelease(pipelines[i]);
    WGPUBindGroupLayout layouts[] = {unfilterable,float_layout,depth_layout,depth_filtering};
    for (size_t i = 0; i < sizeof(layouts) / sizeof(*layouts); ++i) wgpuBindGroupLayoutRelease(layouts[i]);
    wgpuPipelineLayoutRelease(unfilterable_layout);
    wgpuSamplerRelease(less);
    wgpuSamplerRelease(plain);
    submit(&s,encoder);

    const unsigned char *bytes = map(&s,f.readback,SLOTS * SLOT);
    float depth[16];
    uint32_t words[8];
    for (uint32_t slot = 0; slot < 6; ++slot) {
        if (slot == 2 || slot == 4) continue;
        memcpy(depth,bytes + slot * SLOT,sizeof(depth));
        for (size_t i = 0; i < TEXELS; ++i) assert(depth[i] == (i % WIDTH < 2 || slot == 3 ? 0.25f : 0.75f));
        if (slot == 3) for (size_t i = TEXELS; i < 2 * TEXELS; ++i) assert(depth[i] == 0.75f);
    }
    memcpy(words,bytes + 2 * SLOT,sizeof(words));
    for (size_t i = 0; i < TEXELS; ++i) assert(words[i] == (i % WIDTH < 2 ? 7u : 9u));
    memcpy(words,bytes + 4 * SLOT,2 * sizeof(uint32_t));
    assert(words[0] == 2 && words[1] == 2);
    wgpuBufferUnmap(f.readback);
    wgpuBufferRelease(f.readback);
    wgpuBufferRelease(f.output);
    wgpuBufferRelease(f.vertices);
    wgpuRenderPipelineRelease(f.d32_writer);
    wgpuRenderPipelineRelease(f.d24s8_writer);
    consumer_finish(&s);
    printf("PASS public depth/stencil sampling: rendered depth32float and depth24plus-stencil8 DepthOnly maps via texture_depth_2d (automatic depth layout) and texture_2d<f32> (explicit unfilterable-float), StencilOnly as texture_2d<u32>, two-layer texture_depth_2d_array, texture_depth_cube dimensions, comparison-sampler binding admission, 7 view/binding/creation controls, early releases; exact 48 depth values, 8 stencil values and 2 dimensions (no comparison sampling: compiler lacks the builtins) %s\n",
        adapter_class_scope());
}
