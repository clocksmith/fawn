/* SPDX-License-Identifier: MIT
 * Unchanged WGSL over rendered depth maps, sampled through the public C API. */
#include "consumer.h"
#include "shader_fixture.h"

enum { CASES = 4, RESULT_WORDS = 21, SUFFIX_WORDS = 2 };
#define CANARY UINT32_C(0xdeadbeef)
#define QUARTER UINT32_C(0x3e800000)
#define HALF UINT32_C(0x3f000000)
#define THREE_QUARTERS UINT32_C(0x3f400000)
#define ONE UINT32_C(0x3f800000)

/* Independent finite oracle: WebGPU GPUCompareFunction compares the supplied
 * reference to sampled depth (Less means reference < depth), before filtering.
 * https://www.w3.org/TR/webgpu/#enumdef-gpucomparefunction
 * Nearest filtering makes every comparison exactly zero or one. No expected
 * value comes from a shader/compiler evaluator or another target's output. */
/* Words 16 and 20: output[0xffffffffu]=99u clamps to the last bound word, 20
 * (overwriting the 134 trace), and output[16] reads it back where buffer indices
 * clamp (HLSL, SPIR-V: compiler/targets/hlsl/AGENTS.md, compiler/src/spirv_clamp.c).
 * The MSL guard drops the store and loads zero: words 16/20 are 0/134 there. */
static const uint32_t expected[CASES][RESULT_WORDS] = {
    {QUARTER,THREE_QUARTERS,HALF,0,ONE,0,ONE,THREE_QUARTERS,ONE,
        THREE_QUARTERS,THREE_QUARTERS,0,0,12,QUARTER,134,99,CANARY,CANARY,CANARY,99},
    {QUARTER,THREE_QUARTERS,HALF,ONE,0,ONE,0,THREE_QUARTERS,0,
        THREE_QUARTERS,THREE_QUARTERS,0,ONE,12,QUARTER,134,99,CANARY,CANARY,CANARY,99},
    {QUARTER,THREE_QUARTERS,HALF,0,0,0,0,THREE_QUARTERS,0,
        THREE_QUARTERS,THREE_QUARTERS,ONE,0,12,QUARTER,134,99,CANARY,CANARY,CANARY,99},
    {QUARTER,THREE_QUARTERS,HALF,0,ONE,0,ONE,THREE_QUARTERS,ONE,
        THREE_QUARTERS,QUARTER,0,0,12,QUARTER,134,99,CANARY,CANARY,CANARY,99}
};
static const struct {
    const char *name;
    WGPUCompareFunction comparison;
    WGPUAddressMode address;
} cases[CASES] = {
    {"less-clamp",WGPUCompareFunction_Less,WGPUAddressMode_ClampToEdge},
    {"greater-clamp",WGPUCompareFunction_Greater,WGPUAddressMode_ClampToEdge},
    {"equal-clamp",WGPUCompareFunction_Equal,WGPUAddressMode_ClampToEdge},
    {"less-repeat",WGPUCompareFunction_Less,WGPUAddressMode_Repeat}
};

static WGPUTexture depth_image(state *s,uint32_t width,uint32_t layers,uint32_t mips)
{
    WGPUTextureDescriptor descriptor = WGPU_TEXTURE_DESCRIPTOR_INIT;
    descriptor.format = WGPUTextureFormat_Depth32Float;
    descriptor.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding;
    descriptor.size = (WGPUExtent3D){width,1,layers};
    descriptor.mipLevelCount = mips;
    WGPUTexture image = wgpuDeviceCreateTexture(s->device,&descriptor);
    assert(image);
    return image;
}
static WGPUTextureView depth_view(WGPUTexture image,WGPUTextureViewDimension dimension,
    uint32_t mip,uint32_t mips,uint32_t layer,uint32_t layers)
{
    WGPUTextureViewDescriptor descriptor = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    descriptor.dimension = dimension;
    descriptor.aspect = WGPUTextureAspect_DepthOnly;
    descriptor.baseMipLevel = mip;
    descriptor.mipLevelCount = mips;
    descriptor.baseArrayLayer = layer;
    descriptor.arrayLayerCount = layers;
    WGPUTextureView view = wgpuTextureCreateView(image,&descriptor);
    assert(view);
    return view;
}
static WGPURenderPipeline depth_writer(state *s)
{
    static const char code[] = "@vertex fn vertex(@location(0) position:vec4f)"
        "->@builtin(position) vec4f{return position;}";
    WGPUShaderSourceWGSL source = WGPU_SHADER_SOURCE_WGSL_INIT;
    source.code = (WGPUStringView){code,WGPU_STRLEN};
    WGPUShaderModuleDescriptor shader_descriptor = {.nextInChain=&source.chain};
    WGPUShaderModule module = wgpuDeviceCreateShaderModule(s->device,&shader_descriptor);
    assert(module);
    WGPUVertexAttribute attribute = {.format=WGPUVertexFormat_Float32x4,.shaderLocation=0};
    WGPUVertexBufferLayout vertices = {.arrayStride=16,.attributeCount=1,.attributes=&attribute};
    WGPUDepthStencilState depth = WGPU_DEPTH_STENCIL_STATE_INIT;
    depth.format = WGPUTextureFormat_Depth32Float;
    depth.depthWriteEnabled = WGPUOptionalBool_True;
    depth.depthCompare = WGPUCompareFunction_Always;
    WGPURenderPipelineDescriptor descriptor = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    descriptor.vertex = (WGPUVertexState){.module=module,.bufferCount=1,.buffers=&vertices};
    descriptor.depthStencil = &depth;
    WGPURenderPipeline result = wgpuDeviceCreateRenderPipeline(s->device,&descriptor);
    wgpuShaderModuleRelease(module);
    assert(result);
    return result;
}
/* Clear every attachment through a real render pass. The base mip starts at
 * .25; one vertex-only triangle at z=.75 writes only its right-hand texel.
 * Scissoring selects that texel without fragment_depth, copies or CPU emulation. */
static void render_depth(WGPUCommandEncoder encoder,WGPUTextureView view,float clear,
    WGPURenderPipeline writer,WGPUBuffer vertices)
{
    WGPURenderPassDepthStencilAttachment depth = WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
    depth.view = view;
    depth.depthLoadOp = WGPULoadOp_Clear;
    depth.depthStoreOp = WGPUStoreOp_Store;
    depth.depthClearValue = clear;
    WGPURenderPassDescriptor descriptor = {.depthStencilAttachment=&depth};
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder,&descriptor);
    assert(pass);
    if (writer) {
        wgpuRenderPassEncoderSetPipeline(pass,writer);
        wgpuRenderPassEncoderSetVertexBuffer(pass,0,vertices,0,WGPU_WHOLE_SIZE);
        wgpuRenderPassEncoderSetScissorRect(pass,1,0,1,1);
        wgpuRenderPassEncoderDraw(pass,3,1,0,0);
    }
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
}
static WGPUSampler sampling(state *s,WGPUCompareFunction comparison,WGPUAddressMode address)
{
    WGPUSamplerDescriptor descriptor = WGPU_SAMPLER_DESCRIPTOR_INIT;
    descriptor.addressModeU = address;
    descriptor.addressModeV = WGPUAddressMode_ClampToEdge;
    descriptor.addressModeW = WGPUAddressMode_ClampToEdge;
    descriptor.magFilter = WGPUFilterMode_Nearest;
    descriptor.minFilter = WGPUFilterMode_Nearest;
    descriptor.mipmapFilter = WGPUMipmapFilterMode_Nearest;
    descriptor.compare = comparison;
    WGPUSampler result = wgpuDeviceCreateSampler(s->device,&descriptor);
    assert(result);
    return result;
}
static size_t prefix_words(state *s)
{
    WGPULimits limits = WGPU_LIMITS_INIT;
    assert(wgpuDeviceGetLimits(s->device,&limits) == WGPUStatus_Success);
    size_t alignment = limits.minStorageBufferOffsetAlignment;
    assert(alignment);
    if (alignment < 8) alignment = 8;
    assert(alignment % sizeof(uint32_t) == 0 && !(alignment & (alignment - 1)));
    return alignment / sizeof(uint32_t);
}
static WGPUBuffer record_case(state *s,WGPUCommandEncoder encoder,WGPUComputePipeline program,
    WGPUTexture depth,WGPUTexture layered,size_t prefix,size_t bytes,uint32_t index)
{
    uint32_t *seed = malloc(bytes);
    assert(seed);
    for (size_t i = 0; i < bytes / sizeof(*seed); ++i) seed[i] = CANARY;
    seed[prefix + 20] = 0;
    WGPUBuffer output = buffer(s,bytes,WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst);
    WGPUBuffer readback = buffer(s,bytes,WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst);
    wgpuQueueWriteBuffer(s->queue,output,0,seed,bytes);
    free(seed);
    WGPUTextureView depth_sample = depth_view(depth,WGPUTextureViewDimension_2D,0,2,0,1);
    WGPUTextureView layer_sample = depth_view(layered,WGPUTextureViewDimension_2DArray,0,1,0,2);
    WGPUSampler regular = sampling(s,WGPUCompareFunction_Undefined,cases[index].address);
    WGPUSampler comparison = sampling(s,cases[index].comparison,WGPUAddressMode_ClampToEdge);
    WGPUBindGroupEntry entries[5] = {
        {.binding=0,.textureView=depth_sample},
        {.binding=1,.textureView=layer_sample},
        {.binding=2,.sampler=regular},
        {.binding=3,.sampler=comparison},
        {.binding=4,.buffer=output,.offset=prefix * sizeof(uint32_t),.size=RESULT_WORDS * sizeof(uint32_t)}
    };
    WGPUBindGroup bindings = group(s,program,entries,5);
    WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder,NULL);
    assert(pass);
    wgpuComputePassEncoderSetPipeline(pass,program);
    wgpuComputePassEncoderSetBindGroup(pass,0,bindings,0,NULL);
    wgpuComputePassEncoderDispatchWorkgroups(pass,1,1,1);
    wgpuComputePassEncoderEnd(pass);
    wgpuComputePassEncoderRelease(pass);
    wgpuCommandEncoderCopyBufferToBuffer(encoder,output,0,readback,0,bytes);
    wgpuBindGroupRelease(bindings);
    wgpuTextureViewRelease(depth_sample);
    wgpuTextureViewRelease(layer_sample);
    wgpuSamplerRelease(regular);
    wgpuSamplerRelease(comparison);
    wgpuBufferRelease(output);
    return readback;
}
int main(int argc,char **argv)
{
    if (argc > 2) {
        fprintf(stderr,"usage: %s [depth_sampling.wgsl]\n",argv[0]);
        return 2;
    }
    char *source = native_test_shader(argc == 2 ? argv[1] : "../compiler/tests/depth_sampling.wgsl");
    state s = {0};
    consumer_init(&s);
    WGPUComputePipeline program = pipeline(&s,source);
    free(source);
    WGPURenderPipeline writer = depth_writer(&s);
    static const float positions[3][4] = {{-1,-1,0.75f,1},{3,-1,0.75f,1},{-1,3,0.75f,1}};
    WGPUBuffer vertices = buffer(&s,sizeof(positions),WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst);
    wgpuQueueWriteBuffer(s.queue,vertices,0,positions,sizeof(positions));
    WGPUTexture depth = depth_image(&s,2,1,2);
    WGPUTexture layered = depth_image(&s,1,2,1);
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s.device,NULL);
    assert(encoder);
    WGPUTextureView base = depth_view(depth,WGPUTextureViewDimension_2D,0,1,0,1);
    WGPUTextureView mip = depth_view(depth,WGPUTextureViewDimension_2D,1,1,0,1);
    WGPUTextureView first = depth_view(layered,WGPUTextureViewDimension_2D,0,1,0,1);
    WGPUTextureView last = depth_view(layered,WGPUTextureViewDimension_2D,0,1,1,1);
    render_depth(encoder,base,0.25f,writer,vertices);
    render_depth(encoder,mip,0.5f,NULL,NULL);
    render_depth(encoder,first,0.25f,NULL,NULL);
    render_depth(encoder,last,0.75f,NULL,NULL);
    wgpuTextureViewRelease(base);
    wgpuTextureViewRelease(mip);
    wgpuTextureViewRelease(first);
    wgpuTextureViewRelease(last);
    const size_t prefix = prefix_words(&s);
    assert(prefix <= SIZE_MAX / sizeof(uint32_t) - RESULT_WORDS - SUFFIX_WORDS);
    const size_t total = prefix + RESULT_WORDS + SUFFIX_WORDS;
    const size_t bytes = total * sizeof(uint32_t);
    WGPUBuffer readbacks[CASES];
    for (uint32_t i = 0; i < CASES; ++i)
        readbacks[i] = record_case(&s,encoder,program,depth,layered,prefix,bytes,i);
    /* Only readback public handles survive submission. Existing command owners
     * must retain every referenced pipeline, group, sampler, view and allocation. */
    wgpuComputePipelineRelease(program);
    wgpuRenderPipelineRelease(writer);
    wgpuBufferRelease(vertices);
    wgpuTextureRelease(depth);
    wgpuTextureRelease(layered);
    submit(&s,encoder);
    for (uint32_t c = 0; c < CASES; ++c) {
        const uint32_t *actual = map(&s,readbacks[c],bytes);
        for (size_t i = 0; i < total; ++i) {
            uint32_t wanted = i < prefix || i >= prefix + RESULT_WORDS ? CANARY : expected[c][i - prefix];
            if (adapter_class_backend() == WGPUBackendType_Metal && i == prefix + 16) wanted = 0;
            if (adapter_class_backend() == WGPUBackendType_Metal && i == prefix + 20) wanted = 134;
            if (actual[i] != wanted)
                fprintf(stderr,"depth comparison %s allocation word%zu: %08x != %08x\n",cases[c].name,i,actual[i],wanted);
            assert(actual[i] == wanted);
        }
        wgpuBufferUnmap(readbacks[c]);
        wgpuBufferRelease(readbacks[c]);
    }
    assert(s.mapped == CASES);
    consumer_finish(&s);
    printf("PASS public depth comparison: unchanged WGSL, rendered depth32float mip0 [.25,.75]/mip1 [.5], two array layers, Less/Greater/Equal, Clamp/Repeat, const offsets, layer clamping, eager traces12/134, 84 exact result words and full allocation guards, early releases %s\n",
        adapter_class_scope());
    return 0;
}
