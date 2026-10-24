/* SPDX-License-Identifier: MIT
 * Public C direct vertex/instance inputs, actual selected provider execution. */
#include "consumer.h"
#include "d3d12_vertex_cases.h"

enum { WIDTH=4, HEIGHT=4, PITCH=256, FRAME_BYTES=PITCH*HEIGHT, PREFIX=8,
    CASES=sizeof(cases)/sizeof(*cases), FRAMES=2*(CASES+2), BYTES=PREFIX+FRAMES*FRAME_BYTES+8 };
static const char *const vertex_sources[]={
    "struct Expected{f:vec4f,u:vec4u,i:vec4i,checks:vec4u}"
    "@group(0) @binding(0) var<uniform> expected:Expected;"
    "struct Out{@builtin(position) position:vec4f,@location(0) @interpolate(flat) color:vec4f}"
    "@vertex fn vs(@builtin(vertex_index) index:u32,@builtin(instance_index) instance:u32,@location(0) value:vec4f)->Out{"
    "let points=array<vec2f,3>(vec2f(-1,-1),vec2f(3,-1),vec2f(-1,3));var color=vec4f(1,0,0,1);"
    "if((value.x==expected.f.x&&value.y==expected.f.y&&value.z==expected.f.z&&value.w==expected.f.w)&&instance==expected.checks.x){color.y=1.0;}return Out(vec4f(points[index%3u],0,1),color);}",
    "struct Expected{f:vec4f,u:vec4u,i:vec4i,checks:vec4u}"
    "@group(0) @binding(0) var<uniform> expected:Expected;"
    "struct Out{@builtin(position) position:vec4f,@location(0) @interpolate(flat) color:vec4f}"
    "@vertex fn vs(@builtin(vertex_index) index:u32,@builtin(instance_index) instance:u32,@location(0) value:vec4u)->Out{"
    "let points=array<vec2f,3>(vec2f(-1,-1),vec2f(3,-1),vec2f(-1,3));var color=vec4f(1,0,0,1);"
    "if((value.x==expected.u.x&&value.y==expected.u.y&&value.z==expected.u.z&&value.w==expected.u.w)&&instance==expected.checks.x){color.y=1.0;}return Out(vec4f(points[index%3u],0,1),color);}",
    "struct Expected{f:vec4f,u:vec4u,i:vec4i,checks:vec4u}"
    "@group(0) @binding(0) var<uniform> expected:Expected;"
    "struct Out{@builtin(position) position:vec4f,@location(0) @interpolate(flat) color:vec4f}"
    "@vertex fn vs(@builtin(vertex_index) index:u32,@builtin(instance_index) instance:u32,@location(0) value:vec4i)->Out{"
    "let points=array<vec2f,3>(vec2f(-1,-1),vec2f(3,-1),vec2f(-1,3));var color=vec4f(1,0,0,1);"
    "if((value.x==expected.i.x&&value.y==expected.i.y&&value.z==expected.i.z&&value.w==expected.i.w)&&instance==expected.checks.x){color.y=1.0;}return Out(vec4f(points[index%3u],0,1),color);}"
};
static const char fragment_source[]=
    "@fragment fn fs(@location(0) @interpolate(flat) color:vec4f)->@location(0) vec4f{return color;}";
static WGPUShaderModule module(state *s,const char *code)
{
    WGPUShaderSourceWGSL source=WGPU_SHADER_SOURCE_WGSL_INIT;
    source.code=(WGPUStringView){code,WGPU_STRLEN};
    WGPUShaderModule result=wgpuDeviceCreateShaderModule(s->device,&(WGPUShaderModuleDescriptor){.nextInChain=&source.chain});
    assert(result);
    return result;
}
static WGPURenderPipeline pipeline_for(state *s,WGPUShaderModule vertex,WGPUShaderModule fragment,
    WGPUVertexFormat format,unsigned mode)
{
    WGPUVertexAttribute attribute={.format=format,.offset=8};
    WGPUVertexBufferLayout layouts[3]={{0},{.stepMode=WGPUVertexStepMode_Vertex},
        {.stepMode=mode==1 ? WGPUVertexStepMode_Vertex : WGPUVertexStepMode_Instance,
            .arrayStride=mode==2 ? 0 : 32,.attributes=&attribute,.attributeCount=1}};
    WGPUColorTargetState color=WGPU_COLOR_TARGET_STATE_INIT;
    color.format=WGPUTextureFormat_RGBA8Unorm;
    WGPUFragmentState pixel={.module=fragment,.targets=&color,.targetCount=1};
    WGPURenderPipelineDescriptor desc=WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    desc.vertex=(WGPUVertexState){.module=vertex,.buffers=layouts,.bufferCount=3}; desc.fragment=&pixel;
    WGPURenderPipeline result=wgpuDeviceCreateRenderPipeline(s->device,&desc);
    assert(result);
    return result;
}
static WGPUBindGroup binding(state *s,WGPURenderPipeline pipeline,WGPUBuffer value)
{
    WGPUBindGroupLayout layout=wgpuRenderPipelineGetBindGroupLayout(pipeline,0);
    WGPUBindGroupEntry entry={.buffer=value,.size=64};
    WGPUBindGroup result=wgpuDeviceCreateBindGroup(s->device,&(WGPUBindGroupDescriptor){
        .layout=layout,.entryCount=1,.entries=&entry});
    wgpuBindGroupLayoutRelease(layout);
    assert(result);
    return result;
}
static void record(state *s,WGPUCommandEncoder encoder,WGPUTexture image,WGPUTextureView view,WGPUBuffer readback,
    const vertex_case *c,WGPUShaderModule vertex,WGPUShaderModule fragment,unsigned mode,uint32_t *frame)
{
    WGPURenderPipeline pipeline=pipeline_for(s,vertex,fragment,c->format,mode);
    const uint32_t first_instance=mode==0 ? 1u : mode==1 ? 2u : 7u, first_vertex=mode==2 ? 0u : 3u;
    unsigned char source[256]={0};
    if (mode==1) for (size_t i=3;i<6;++i) memcpy(source+16+i*32+8,c->data,sizeof(c->data));
    else memcpy(source+(mode==0 ? 56 : 24),c->data,sizeof(c->data));
    WGPUBuffer vertices=buffer(s,sizeof(source),WGPUBufferUsage_Vertex|WGPUBufferUsage_CopyDst);
    wgpuQueueWriteBuffer(s->queue,vertices,0,source,sizeof(source));
    WGPUBuffer values[2]; WGPUBindGroup groups[2];
    for (size_t i=0;i<2;++i) {
        uint32_t expected[16]={0};
        if (!i) memcpy(expected+(size_t)c->kind*4,c->expected.u,16);
        expected[12]=first_instance; /* Both controls preserve the same instance ID. */
        values[i]=buffer(s,sizeof(expected),WGPUBufferUsage_Uniform|WGPUBufferUsage_CopyDst);
        wgpuQueueWriteBuffer(s->queue,values[i],0,expected,sizeof(expected));
        groups[i]=binding(s,pipeline,values[i]);
        WGPURenderPassColorAttachment color=WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
        color.view=view; color.loadOp=WGPULoadOp_Clear; color.storeOp=WGPUStoreOp_Store;
        color.clearValue=(WGPUColor){0,0,1,1};
        if (!i && c==cases && !mode) for (unsigned invalid=0;invalid<2;++invalid) {
            WGPUCommandEncoder rejected=wgpuDeviceCreateCommandEncoder(s->device,NULL);
            wgpuDevicePushErrorScope(s->device,WGPUErrorFilter_Validation);
            WGPURenderPassEncoder missing=wgpuCommandEncoderBeginRenderPass(rejected,&(WGPURenderPassDescriptor){
                .colorAttachmentCount=1,.colorAttachments=&color});
            wgpuRenderPassEncoderSetPipeline(missing,pipeline);
            wgpuRenderPassEncoderSetBindGroup(missing,0,groups[i],0,NULL);
            if (invalid) wgpuRenderPassEncoderSetVertexBuffer(missing,1,vertices,0,4);
            wgpuRenderPassEncoderSetVertexBuffer(missing,2,vertices,16,invalid ? 9 : 240);
            /* Missing explicit-empty slot, then instance fetch beyond bound view. */
            wgpuRenderPassEncoderDraw(missing,3,1,first_vertex,first_instance);
            wgpuRenderPassEncoderEnd(missing); wgpuRenderPassEncoderRelease(missing);
            finish_invalid(rejected); wgpuCommandEncoderRelease(rejected);
            expect_validation(s);
        }
        WGPURenderPassEncoder pass=wgpuCommandEncoderBeginRenderPass(encoder,&(WGPURenderPassDescriptor){
            .colorAttachmentCount=1,.colorAttachments=&color});
        wgpuRenderPassEncoderSetPipeline(pass,pipeline);
        wgpuRenderPassEncoderSetBindGroup(pass,0,groups[i],0,NULL);
        /* Slot0 is a hole; slot1 is explicitly empty and remains required. */
        wgpuRenderPassEncoderSetVertexBuffer(pass,1,vertices,0,4);
        wgpuRenderPassEncoderSetVertexBuffer(pass,2,vertices,16,240);
        wgpuRenderPassEncoderDraw(pass,3,1,first_vertex,first_instance);
        wgpuRenderPassEncoderEnd(pass); wgpuRenderPassEncoderRelease(pass);
        WGPUTexelCopyTextureInfo from={.texture=image};
        WGPUTexelCopyBufferInfo to={.buffer=readback,.layout={.offset=PREFIX+(uint64_t)(*frame)*FRAME_BYTES,
            .bytesPerRow=PITCH,.rowsPerImage=HEIGHT}};
        wgpuCommandEncoderCopyTextureToBuffer(encoder,&from,&to,&(WGPUExtent3D){WIDTH,HEIGHT,1});
        ++*frame;
    }
    wgpuRenderPipelineRelease(pipeline);
    for (size_t i=0;i<2;++i) { wgpuBindGroupRelease(groups[i]); wgpuBufferRelease(values[i]); }
    wgpuBufferRelease(vertices); /* Every record owns its dependencies before submit. */
}
int main(void)
{
    state s={0}; consumer_init(&s);
    _Static_assert(CASES==41,"complete canonical vertex format inventory");
    WGPUShaderModule vertices[3];
    for (size_t i=0;i<3;++i) vertices[i]=module(&s,vertex_sources[i]);
    WGPUShaderModule fragment=module(&s,fragment_source);
    WGPUTextureDescriptor desc=WGPU_TEXTURE_DESCRIPTOR_INIT;
    desc.size=(WGPUExtent3D){WIDTH,HEIGHT,1}; desc.format=WGPUTextureFormat_RGBA8Unorm;
    desc.usage=WGPUTextureUsage_RenderAttachment|WGPUTextureUsage_CopySrc;
    WGPUTexture image=wgpuDeviceCreateTexture(s.device,&desc);
    WGPUTextureView view=wgpuTextureCreateView(image,NULL);
    assert(image && view);
    WGPUBuffer readback=buffer(&s,BYTES,WGPUBufferUsage_CopyDst|WGPUBufferUsage_MapRead);
    uint32_t canaries[BYTES/4];
    for (size_t i=0;i<sizeof(canaries)/sizeof(*canaries);++i) canaries[i]=UINT32_C(0xdeadbeef);
    wgpuQueueWriteBuffer(s.queue,readback,0,canaries,sizeof(canaries));
    WGPUCommandEncoder encoder=wgpuDeviceCreateCommandEncoder(s.device,NULL);
    uint32_t frame=0;
    for (size_t i=0;i<CASES;++i) record(&s,encoder,image,view,readback,&cases[i],vertices[cases[i].kind],fragment,0,&frame);
    record(&s,encoder,image,view,readback,&cases[0],vertices[cases[0].kind],fragment,1,&frame);
    record(&s,encoder,image,view,readback,&cases[0],vertices[cases[0].kind],fragment,2,&frame);
    assert(frame==FRAMES);
    for (size_t i=0;i<3;++i) wgpuShaderModuleRelease(vertices[i]);
    wgpuShaderModuleRelease(fragment); wgpuTextureViewRelease(view); wgpuTextureRelease(image);
    submit(&s,encoder);
    const unsigned char *actual=map(&s,readback,BYTES);
    for (size_t i=0;i<BYTES/4;++i) {
        size_t byte=i*4;
        uint32_t expected=UINT32_C(0xdeadbeef),value;
        if (byte>=PREFIX && byte<PREFIX+FRAMES*FRAME_BYTES) {
            size_t local=byte-PREFIX,frame_index=local/FRAME_BYTES;
            if (local%PITCH<WIDTH*4) {
                const unsigned char rgba[4]={255,frame_index%2 ? 0 : 255,0,255};
                memcpy(&expected,rgba,sizeof(expected));
            }
        }
        memcpy(&value,actual+byte,sizeof(value));
        if (value!=expected) fprintf(stderr,"vertex input word%zu: %08x != %08x\n",i,value,expected);
        assert(value==expected);
    }
    wgpuBufferUnmap(readback); wgpuBufferRelease(readback);
    consumer_finish(&s);
    printf("PASS public direct vertex inputs: 41 formats, literal GPU fetch/default lanes versus wrong-expected controls, per-vertex/per-instance, nonzero first/ranges, holes/required empty slot, stride0, exact 86 RGBA8 frames/padding/canaries and early release %s\n",adapter_class_scope());
    return 0;
}
