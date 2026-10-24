/* SPDX-License-Identifier: MIT
 * Unchanged fragment WGSL over real color/depth mip chains and runtime samplers. */
#include "consumer.h"
#include "shader_fixture.h"

enum { WIDTH=4, HEIGHT=4, CASES=10, ROW_BYTES=256, FRAME_BYTES=ROW_BYTES*HEIGHT,
    PREFIX_BYTES=8, TOTAL_BYTES=PREFIX_BYTES+CASES*FRAME_BYTES+8 };
static const struct {
    const char *name;
    float scale,max_lod;
    WGPUCompareFunction compare;
    unsigned char rgba[4];
} cases[CASES]={
    {"less-mip0",0.125f,32,WGPUCompareFunction_Less,{255,0,0,0}},
    {"less-mip1",0.25f,32,WGPUCompareFunction_Less,{0,255,0,255}},
    {"less-mip2",0.5f,32,WGPUCompareFunction_Less,{0,0,255,0}},
    {"greater-mip0",0.125f,32,WGPUCompareFunction_Greater,{255,0,0,255}},
    {"greater-mip1",0.25f,32,WGPUCompareFunction_Greater,{0,255,0,0}},
    {"greater-mip2",0.5f,32,WGPUCompareFunction_Greater,{0,0,255,0}},
    {"equal-mip0",0.125f,32,WGPUCompareFunction_Equal,{255,0,0,0}},
    {"equal-mip1",0.25f,32,WGPUCompareFunction_Equal,{0,255,0,0}},
    {"equal-mip2",0.5f,32,WGPUCompareFunction_Equal,{0,0,255,255}},
    {"less-lod-clamp",0.5f,1,WGPUCompareFunction_Less,{0,255,0,255}}
};

static WGPUShaderModule shader(state *s,const char *code)
{
    WGPUShaderSourceWGSL source=WGPU_SHADER_SOURCE_WGSL_INIT;
    source.code=(WGPUStringView){code,WGPU_STRLEN};
    WGPUShaderModuleDescriptor desc={.nextInChain=&source.chain};
    WGPUShaderModule result=wgpuDeviceCreateShaderModule(s->device,&desc);
    assert(result);
    return result;
}
static WGPURenderPipeline render_program(state *s,const char *fragment_source)
{
    static const char vertex_source[]="@vertex fn vertex(@builtin(vertex_index) i:u32)"
        "->@builtin(position) vec4f{let p=array<vec2f,3>(vec2f(-1,-1),vec2f(3,-1),vec2f(-1,3));"
        "return vec4f(p[i],0,1);}";
    WGPUShaderModule vertex=shader(s,vertex_source),fragment=shader(s,fragment_source);
    WGPUColorTargetState color=WGPU_COLOR_TARGET_STATE_INIT;
    color.format=WGPUTextureFormat_RGBA8Unorm;
    WGPUFragmentState pixel=WGPU_FRAGMENT_STATE_INIT;
    pixel.module=fragment; pixel.targetCount=1; pixel.targets=&color;
    WGPURenderPipelineDescriptor desc=WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    desc.vertex.module=vertex; desc.fragment=&pixel;
    WGPURenderPipeline result=wgpuDeviceCreateRenderPipeline(s->device,&desc);
    wgpuShaderModuleRelease(vertex); wgpuShaderModuleRelease(fragment);
    assert(result);
    return result;
}
static WGPUTexture image(state *s,WGPUTextureFormat format,WGPUTextureUsage usage,uint32_t width,uint32_t mips)
{
    WGPUTextureDescriptor desc=WGPU_TEXTURE_DESCRIPTOR_INIT;
    desc.size=(WGPUExtent3D){width,width,1}; desc.format=format;
    desc.usage=usage; desc.mipLevelCount=mips;
    WGPUTexture result=wgpuDeviceCreateTexture(s->device,&desc);
    assert(result);
    return result;
}
static void upload_colors(state *s,WGPUTexture color)
{
    static const unsigned char rgba[4][4]={{255,0,0,255},{0,255,0,255},
        {0,0,255,255},{255,255,255,255}};
    unsigned char data[8*8*4];
    for (uint32_t mip=0;mip<4;++mip) {
        uint32_t width=8u>>mip;
        for (size_t i=0;i<(size_t)width*width;++i) memcpy(data+4*i,rgba[mip],4);
        WGPUTexelCopyTextureInfo target={.texture=color,.mipLevel=mip};
        WGPUTexelCopyBufferLayout layout={.bytesPerRow=width*4,.rowsPerImage=width};
        WGPUExtent3D extent={width,width,1};
        wgpuQueueWriteTexture(s->queue,&target,data,(size_t)width*width*4,&layout,&extent);
    }
}
static void clear_depth(WGPUCommandEncoder encoder,WGPUTexture depth)
{
    static const float values[4]={0.25f,0.75f,0.5f,0.125f};
    for (uint32_t mip=0;mip<4;++mip) {
        WGPUTextureViewDescriptor view_desc=WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
        view_desc.aspect=WGPUTextureAspect_DepthOnly;
        view_desc.baseMipLevel=mip; view_desc.mipLevelCount=1;
        WGPUTextureView view=wgpuTextureCreateView(depth,&view_desc);
        assert(view);
        WGPURenderPassDepthStencilAttachment attachment=WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
        attachment.view=view; attachment.depthLoadOp=WGPULoadOp_Clear;
        attachment.depthStoreOp=WGPUStoreOp_Store; attachment.depthClearValue=values[mip];
        WGPURenderPassDescriptor desc={.depthStencilAttachment=&attachment};
        WGPURenderPassEncoder pass=wgpuCommandEncoderBeginRenderPass(encoder,&desc);
        assert(pass);
        wgpuRenderPassEncoderEnd(pass); wgpuRenderPassEncoderRelease(pass);
        wgpuTextureViewRelease(view);
    }
}
static WGPUSampler sampler(state *s,size_t index,bool comparison)
{
    WGPUSamplerDescriptor desc=WGPU_SAMPLER_DESCRIPTOR_INIT;
    desc.magFilter=desc.minFilter=WGPUFilterMode_Nearest;
    desc.mipmapFilter=WGPUMipmapFilterMode_Nearest;
    desc.addressModeU=desc.addressModeV=desc.addressModeW=WGPUAddressMode_ClampToEdge;
    desc.lodMinClamp=0; desc.lodMaxClamp=cases[index].max_lod;
    if (comparison) desc.compare=cases[index].compare;
    WGPUSampler result=wgpuDeviceCreateSampler(s->device,&desc);
    assert(result);
    return result;
}
static void record_case(state *s,WGPUCommandEncoder encoder,WGPURenderPipeline program,
    WGPUTextureView color,WGPUTextureView depth,WGPUTextureView target,WGPUBuffer readback,
    WGPUTexture target_texture,size_t index)
{
    const float parameters[4]={cases[index].scale,0.5f,0,0};
    WGPUBuffer params=buffer(s,sizeof(parameters),WGPUBufferUsage_Uniform|WGPUBufferUsage_CopyDst);
    wgpuQueueWriteBuffer(s->queue,params,0,parameters,sizeof(parameters));
    WGPUSampler regular=sampler(s,index,false),comparison=sampler(s,index,true);
    WGPUBindGroupEntry entries[5]={{.binding=0,.textureView=color},{.binding=1,.sampler=regular},
        {.binding=2,.textureView=depth},{.binding=3,.sampler=comparison},
        {.binding=4,.buffer=params,.size=sizeof(parameters)}};
    WGPUBindGroupLayout layout=wgpuRenderPipelineGetBindGroupLayout(program,0);
    assert(layout);
    WGPUBindGroupDescriptor group_desc={.layout=layout,.entries=entries,.entryCount=5};
    WGPUBindGroup bindings=wgpuDeviceCreateBindGroup(s->device,&group_desc);
    wgpuBindGroupLayoutRelease(layout);
    assert(bindings);
    WGPURenderPassColorAttachment attachment=WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    attachment.view=target; attachment.loadOp=WGPULoadOp_Clear; attachment.storeOp=WGPUStoreOp_Store;
    WGPURenderPassDescriptor desc={.colorAttachmentCount=1,.colorAttachments=&attachment};
    WGPURenderPassEncoder pass=wgpuCommandEncoderBeginRenderPass(encoder,&desc);
    assert(pass);
    wgpuRenderPassEncoderSetPipeline(pass,program);
    wgpuRenderPassEncoderSetBindGroup(pass,0,bindings,0,NULL);
    wgpuRenderPassEncoderDraw(pass,3,1,0,0);
    wgpuRenderPassEncoderEnd(pass); wgpuRenderPassEncoderRelease(pass);
    WGPUTexelCopyTextureInfo from={.texture=target_texture};
    WGPUTexelCopyBufferInfo to={.buffer=readback,.layout={.offset=PREFIX_BYTES+index*FRAME_BYTES,
        .bytesPerRow=ROW_BYTES,.rowsPerImage=HEIGHT}};
    WGPUExtent3D extent={WIDTH,HEIGHT,1};
    wgpuCommandEncoderCopyTextureToBuffer(encoder,&from,&to,&extent);
    wgpuBindGroupRelease(bindings); wgpuSamplerRelease(regular); wgpuSamplerRelease(comparison);
    wgpuBufferRelease(params);
}
int main(int argc,char **argv)
{
    if (argc>2) { fprintf(stderr,"usage: %s [implicit_sampling.wgsl]\n",argv[0]); return 2; }
    char *source=native_test_shader(argc==2 ? argv[1] : "../compiler/tests/implicit_sampling.wgsl");
    state s={0}; consumer_init(&s);
    WGPURenderPipeline program=render_program(&s,source);
    free(source);
    WGPUTexture color=image(&s,WGPUTextureFormat_RGBA8Unorm,
        WGPUTextureUsage_CopyDst|WGPUTextureUsage_TextureBinding,8,4);
    WGPUTexture depth=image(&s,WGPUTextureFormat_Depth32Float,
        WGPUTextureUsage_RenderAttachment|WGPUTextureUsage_TextureBinding,8,4);
    WGPUTexture target=image(&s,WGPUTextureFormat_RGBA8Unorm,
        WGPUTextureUsage_RenderAttachment|WGPUTextureUsage_CopySrc,WIDTH,1);
    WGPUTextureView color_view=wgpuTextureCreateView(color,NULL),depth_view=wgpuTextureCreateView(depth,NULL),
        target_view=wgpuTextureCreateView(target,NULL);
    assert(color_view && depth_view && target_view);
    upload_colors(&s,color);
    uint32_t guards[TOTAL_BYTES/4];
    for (size_t i=0;i<sizeof(guards)/sizeof(*guards);++i) guards[i]=UINT32_C(0xdeadbeef);
    WGPUBuffer readback=buffer(&s,sizeof(guards),WGPUBufferUsage_CopyDst|WGPUBufferUsage_MapRead);
    wgpuQueueWriteBuffer(s.queue,readback,0,guards,sizeof(guards));
    WGPUCommandEncoder encoder=wgpuDeviceCreateCommandEncoder(s.device,NULL);
    assert(encoder);
    clear_depth(encoder,depth);
    for (size_t i=0;i<CASES;++i)
        record_case(&s,encoder,program,color_view,depth_view,target_view,readback,target,i);
    wgpuRenderPipelineRelease(program);
    wgpuTextureViewRelease(color_view); wgpuTextureViewRelease(depth_view); wgpuTextureViewRelease(target_view);
    wgpuTextureRelease(color); wgpuTextureRelease(depth); wgpuTextureRelease(target);
    submit(&s,encoder);
    const unsigned char *actual=map(&s,readback,sizeof(guards));
    for (size_t i=0;i<sizeof(guards);++i) {
        unsigned char wanted=((const unsigned char *)guards)[i];
        if (i>=PREFIX_BYTES && i<PREFIX_BYTES+CASES*FRAME_BYTES) {
            const size_t relative=i-PREFIX_BYTES,frame=relative/FRAME_BYTES,column=relative%ROW_BYTES;
            if (column<WIDTH*4) wanted=cases[frame].rgba[column%4];
        }
        if (actual[i]!=wanted) {
            size_t frame=i>=PREFIX_BYTES ? (i-PREFIX_BYTES)/FRAME_BYTES : CASES;
            fprintf(stderr,"implicit sampling %s allocation byte%zu: %u != %u\n",
                frame<CASES ? cases[frame].name : "guard",i,(unsigned)actual[i],(unsigned)wanted);
        }
        assert(actual[i]==wanted);
    }
    wgpuBufferUnmap(readback); wgpuBufferRelease(readback);
    consumer_finish(&s);
    printf("PASS public implicit sampling: unchanged fragment WGSL/two-hop helpers, real four-mip color/depth, exact derivative-selected mip0/1/2, Less/Greater/Equal, sampler LOD clamp, 160 literal pixels, full row/prefix/suffix guards and early releases %s\n",adapter_class_scope());
    return 0;
}
