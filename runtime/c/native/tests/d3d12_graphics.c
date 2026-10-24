/* SPDX-License-Identifier: MIT
 * Portable public C graphics control; selected adapter/backend owns execution. */
#include "consumer.h"
#include <stdlib.h>

enum { WIDTH=4, HEIGHT=4, ROW_BYTES=256, FRAME_BYTES=ROW_BYTES*HEIGHT, FRAMES=7, PREFIX_BYTES=8,
    READBACK_BYTES=PREFIX_BYTES+FRAMES*FRAME_BYTES+8 };
static const char vertex_source[]=
    "@group(0) @binding(0) var<uniform> v:vec4f;@group(1) @binding(0) var<uniform> common_params:vec4f;"
    "struct Out{@builtin(position) position:vec4f,@location(0) @interpolate(flat) red:f32}"
    "@vertex fn vertex(@builtin(vertex_index) i:u32)->Out{var out:Out;"
    "let points=array<vec2f,3>(vec2f(-1,-1),vec2f(3,-1),vec2f(-1,3));"
    "out.position=vec4f(points[i],0,1);out.red=v.x+common_params.x;return out;}";
static const char yellow_source[]=
    "@group(0) @binding(1) var<uniform> p:vec4f;@group(1) @binding(0) var<uniform> common_params:vec4f;"
    "@fragment fn fragment(@location(0) @interpolate(flat) red:f32,@builtin(front_facing) front:bool)->@location(0) vec4f{"
    "var green=p.y+common_params.y;if(!front){green=0.0;}return vec4f(red,green,0.0,common_params.z);}";
static const char sampled_source[]=
    "@group(0) @binding(1) var<uniform> p:vec4f;@group(1) @binding(0) var<uniform> common_params:vec4f;"
    "@group(2) @binding(0) var image:texture_2d<f32>;@group(2) @binding(1) var sampling:sampler;"
    "@fragment fn fragment(@location(0) @interpolate(flat) red:f32)->@location(0) vec4f{"
    "let color=textureSampleLevel(image,sampling,vec2f(0.375,0.5),0.0);"
    "return vec4f(red,p.y+common_params.y,color.z,common_params.z);}";

static WGPUShaderModule shader(state *s,const char *code)
{
    WGPUShaderSourceWGSL source=WGPU_SHADER_SOURCE_WGSL_INIT;
    source.code=(WGPUStringView){code,WGPU_STRLEN};
    WGPUShaderModuleDescriptor desc={.nextInChain=&source.chain};
    WGPUShaderModule result=wgpuDeviceCreateShaderModule(s->device,&desc);
    assert(result);
    return result;
}
static WGPURenderPipeline program(state *s,WGPUShaderModule vertex,WGPUShaderModule fragment,WGPUFrontFace face)
{
    WGPUColorTargetState color=WGPU_COLOR_TARGET_STATE_INIT;
    color.format=WGPUTextureFormat_RGBA8Unorm;
    WGPUFragmentState pixel=WGPU_FRAGMENT_STATE_INIT;
    pixel.module=fragment; pixel.targetCount=1; pixel.targets=&color;
    WGPURenderPipelineDescriptor desc=WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    desc.vertex.module=vertex; desc.fragment=&pixel; desc.primitive.frontFace=face;
    WGPURenderPipeline result=wgpuDeviceCreateRenderPipeline(s->device,&desc);
    assert(result);
    return result;
}
static WGPUBindGroup render_group(state *s,WGPURenderPipeline program,uint32_t index,
    const WGPUBindGroupEntry *entries,size_t count)
{
    WGPUBindGroupLayout layout=wgpuRenderPipelineGetBindGroupLayout(program,index);
    assert(layout);
    WGPUBindGroupDescriptor desc={.layout=layout,.entries=entries,.entryCount=count};
    WGPUBindGroup result=wgpuDeviceCreateBindGroup(s->device,&desc);
    wgpuBindGroupLayoutRelease(layout);
    assert(result);
    return result;
}
static WGPURenderPassEncoder pass(WGPUCommandEncoder encoder,WGPUTextureView target,WGPULoadOp load,
    WGPUStoreOp store,WGPUColor clear)
{
    WGPURenderPassColorAttachment color=WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    color.view=target; color.loadOp=load; color.storeOp=store; color.clearValue=clear;
    WGPURenderPassDescriptor desc={.colorAttachmentCount=1,.colorAttachments=&color};
    WGPURenderPassEncoder result=wgpuCommandEncoderBeginRenderPass(encoder,&desc);
    assert(result);
    return result;
}
static void end_pass(WGPURenderPassEncoder pass)
{
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
}
static void bind_draw(WGPURenderPassEncoder pass,WGPURenderPipeline pipeline,WGPUBindGroup a,WGPUBindGroup b,uint32_t count)
{
    wgpuRenderPassEncoderSetPipeline(pass,pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass,0,a,0,NULL);
    wgpuRenderPassEncoderSetBindGroup(pass,1,b,0,NULL);
    wgpuRenderPassEncoderSetViewport(pass,0,0,WIDTH,HEIGHT,0,1);
    wgpuRenderPassEncoderDraw(pass,count,1,0,0);
}
static void copy_frame(WGPUCommandEncoder encoder,WGPUTexture target,WGPUBuffer readback,uint32_t frame)
{
    WGPUTexelCopyTextureInfo from={.texture=target};
    WGPUTexelCopyBufferInfo to={.buffer=readback,.layout={.offset=PREFIX_BYTES+(uint64_t)frame*FRAME_BYTES,
        .bytesPerRow=ROW_BYTES,.rowsPerImage=HEIGHT}};
    WGPUExtent3D extent={WIDTH,HEIGHT,1};
    wgpuCommandEncoderCopyTextureToBuffer(encoder,&from,&to,&extent);
}
static uint32_t pixel(size_t frame,size_t x)
{
    /* Literal image oracle, separate from every shader's arithmetic. */
    unsigned char rgba[4];
    static const unsigned char colors[FRAMES][4]={{0,0,255,255},{255,255,0,255},
        {255,255,0,255},{255,255,0,255},{255,0,0,255},{0,0,0,0},{0,0,0,0}};
    memcpy(rgba,colors[frame],sizeof(rgba));
    if ((frame==2 || frame==3) && x>=2) rgba[2]=64;
    uint32_t result;
    memcpy(&result,rgba,sizeof(result));
    return result;
}
int main(void)
{
    state s={0};
    consumer_init(&s);
    WGPUShaderModule vs=shader(&s,vertex_source),ps=shader(&s,yellow_source),sample_ps=shader(&s,sampled_source);
    WGPURenderPipeline yellow=program(&s,vs,ps,WGPUFrontFace_CCW),clockwise=program(&s,vs,ps,WGPUFrontFace_CW),
        sampled=program(&s,vs,sample_ps,WGPUFrontFace_CCW);
    wgpuShaderModuleRelease(vs); wgpuShaderModuleRelease(ps); wgpuShaderModuleRelease(sample_ps);
    WGPUBuffer uniforms[3];
    const float values[3][4]={{0},{0},{1,1,1,0}};
    for (size_t i=0;i<3;++i) {
        uniforms[i]=buffer(&s,16,WGPUBufferUsage_Uniform|WGPUBufferUsage_CopyDst);
        wgpuQueueWriteBuffer(s.queue,uniforms[i],0,values[i],sizeof(values[i]));
    }
    WGPUBindGroupEntry first[2]={{.binding=0,.buffer=uniforms[0],.size=16},{.binding=1,.buffer=uniforms[1],.size=16}};
    WGPUBindGroupEntry common={.binding=0,.buffer=uniforms[2],.size=16};
    WGPURenderPipeline programs[3]={yellow,clockwise,sampled};
    WGPUBindGroup groups[3][2];
    for (size_t i=0;i<3;++i) {
        groups[i][0]=render_group(&s,programs[i],0,first,2);
        groups[i][1]=render_group(&s,programs[i],1,&common,1);
    }
    WGPUTextureDescriptor target_desc=WGPU_TEXTURE_DESCRIPTOR_INIT;
    target_desc.size=(WGPUExtent3D){WIDTH,HEIGHT,1}; target_desc.format=WGPUTextureFormat_RGBA8Unorm;
    target_desc.usage=WGPUTextureUsage_RenderAttachment|WGPUTextureUsage_CopySrc;
    WGPUTexture target=wgpuDeviceCreateTexture(s.device,&target_desc);
    WGPUTextureView view=wgpuTextureCreateView(target,NULL);
    assert(target && view);
    WGPUTextureDescriptor image_desc=WGPU_TEXTURE_DESCRIPTOR_INIT;
    image_desc.size=(WGPUExtent3D){2,1,1}; image_desc.format=WGPUTextureFormat_RGBA8Unorm;
    image_desc.usage=WGPUTextureUsage_TextureBinding|WGPUTextureUsage_CopyDst;
    WGPUTexture image=wgpuDeviceCreateTexture(s.device,&image_desc);
    WGPUTextureView image_view=wgpuTextureCreateView(image,NULL);
    const unsigned char texels[8]={0,0,0,255,0,0,255,255};
    WGPUTexelCopyTextureInfo to={.texture=image};
    WGPUTexelCopyBufferLayout upload_layout={.bytesPerRow=8,.rowsPerImage=1};
    WGPUExtent3D upload_size={2,1,1};
    wgpuQueueWriteTexture(s.queue,&to,texels,sizeof(texels),&upload_layout,&upload_size);
    WGPUSamplerDescriptor sampler_desc=WGPU_SAMPLER_DESCRIPTOR_INIT;
    WGPUSampler nearest=wgpuDeviceCreateSampler(s.device,&sampler_desc);
    sampler_desc.minFilter=sampler_desc.magFilter=WGPUFilterMode_Linear;
    WGPUSampler linear=wgpuDeviceCreateSampler(s.device,&sampler_desc);
    WGPUBindGroupEntry image_entries[2]={{.binding=0,.textureView=image_view},{.binding=1,.sampler=nearest}};
    WGPUBindGroup nearest_group=render_group(&s,sampled,2,image_entries,2);
    image_entries[1].sampler=linear;
    WGPUBindGroup linear_group=render_group(&s,sampled,2,image_entries,2);
    WGPUBuffer readback=buffer(&s,READBACK_BYTES,WGPUBufferUsage_MapRead|WGPUBufferUsage_CopyDst);
    uint32_t canaries[READBACK_BYTES/4];
    for (size_t i=0;i<sizeof(canaries)/sizeof(*canaries);++i) canaries[i]=UINT32_C(0xdeadbeef);
    wgpuQueueWriteBuffer(s.queue,readback,0,canaries,sizeof(canaries));
    WGPUCommandEncoder encoder=wgpuDeviceCreateCommandEncoder(s.device,NULL);
    WGPURenderPassEncoder active=pass(encoder,view,WGPULoadOp_Clear,WGPUStoreOp_Store,(WGPUColor){0,0,1,1});
    end_pass(active); copy_frame(encoder,target,readback,0);
    active=pass(encoder,view,WGPULoadOp_Load,WGPUStoreOp_Store,(WGPUColor){0});
    bind_draw(active,yellow,groups[0][0],groups[0][1],3);
    end_pass(active); copy_frame(encoder,target,readback,1);
    active=pass(encoder,view,WGPULoadOp_Clear,WGPUStoreOp_Store,(WGPUColor){0});
    wgpuRenderPassEncoderSetPipeline(active,sampled);
    wgpuRenderPassEncoderSetBindGroup(active,0,groups[2][0],0,NULL);
    wgpuRenderPassEncoderSetBindGroup(active,1,groups[2][1],0,NULL);
    wgpuRenderPassEncoderSetBindGroup(active,2,nearest_group,0,NULL);
    wgpuRenderPassEncoderSetScissorRect(active,0,0,2,HEIGHT);
    wgpuRenderPassEncoderDraw(active,3,1,0,0);
    wgpuRenderPassEncoderSetBindGroup(active,2,linear_group,0,NULL);
    wgpuRenderPassEncoderSetScissorRect(active,2,0,2,HEIGHT);
    wgpuRenderPassEncoderDraw(active,3,1,0,0);
    end_pass(active); copy_frame(encoder,target,readback,2);
    active=pass(encoder,view,WGPULoadOp_Load,WGPUStoreOp_Store,(WGPUColor){0});
    bind_draw(active,yellow,groups[0][0],groups[0][1],0);
    end_pass(active); copy_frame(encoder,target,readback,3);
    active=pass(encoder,view,WGPULoadOp_Load,WGPUStoreOp_Store,(WGPUColor){0});
    bind_draw(active,clockwise,groups[1][0],groups[1][1],3);
    end_pass(active); copy_frame(encoder,target,readback,4);
    active=pass(encoder,view,WGPULoadOp_Load,WGPUStoreOp_Discard,(WGPUColor){0});
    bind_draw(active,yellow,groups[0][0],groups[0][1],3);
    end_pass(active); copy_frame(encoder,target,readback,5);
    active=pass(encoder,view,WGPULoadOp_Load,WGPUStoreOp_Store,(WGPUColor){0});
    end_pass(active); copy_frame(encoder,target,readback,6);
    for (size_t i=0;i<3;++i) {
        wgpuRenderPipelineRelease(programs[i]);
        wgpuBindGroupRelease(groups[i][0]); wgpuBindGroupRelease(groups[i][1]); wgpuBufferRelease(uniforms[i]);
    }
    wgpuBindGroupRelease(nearest_group); wgpuBindGroupRelease(linear_group);
    wgpuSamplerRelease(nearest); wgpuSamplerRelease(linear);
    wgpuTextureViewRelease(image_view); wgpuTextureRelease(image);
    wgpuTextureViewRelease(view); wgpuTextureRelease(target);
    submit(&s,encoder);
    const uint32_t *actual=map(&s,readback,READBACK_BYTES);
    for (size_t i=0;i<READBACK_BYTES/4;++i) {
        size_t byte=i*4;
        uint32_t expected=UINT32_C(0xdeadbeef);
        if (byte>=PREFIX_BYTES && byte<PREFIX_BYTES+FRAMES*FRAME_BYTES) {
            size_t within=byte-PREFIX_BYTES,frame=within/FRAME_BYTES,row=within%FRAME_BYTES;
            if (row%ROW_BYTES<WIDTH*4) expected=pixel(frame,row%ROW_BYTES/4);
        }
        if (actual[i]!=expected) fprintf(stderr,"graphics word%zu: %08x != %08x\n",i,actual[i],expected);
        assert(actual[i]==expected);
    }
    wgpuBufferUnmap(readback); wgpuBufferRelease(readback);
    consumer_finish(&s);
    printf("PASS public procedural graphics: independent VS/PS modules, shared binding, yellow pixels, frontFace, runtime sampler replacement, scissor/viewport, empty/zero draw, LOAD/STORE/discard zeros, padding/canaries and early releases %s\n",adapter_class_scope());
    return 0;
}
