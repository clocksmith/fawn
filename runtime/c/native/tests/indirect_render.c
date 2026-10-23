/* SPDX-License-Identifier: MIT
 * Real C WGSL producer, guard and graphics through public pass/bundle entrypoints. */
#include "consumer.h"

static WGPURenderPipeline graphics(state *s, bool constant_instance)
{
    const char *code =
        "@vertex fn vertex(@location(0) point:vec2f,@location(1) shift:vec2f)"
        "->@builtin(position) vec4f{return vec4f(point+shift,0.0,1.0);}"
        "@fragment fn fragment()->@location(0) vec4f{return vec4f(1.0,0.0,0.0,1.0);}";
    WGPUShaderSourceWGSL source = WGPU_SHADER_SOURCE_WGSL_INIT;
    source.code = (WGPUStringView){code,WGPU_STRLEN};
    WGPUShaderModuleDescriptor shader = {.nextInChain=&source.chain};
    WGPUShaderModule module = wgpuDeviceCreateShaderModule(s->device,&shader);
    assert(module);
    WGPUVertexAttribute attributes[] = {{.format=WGPUVertexFormat_Float32x2,.shaderLocation=0},
        {.format=WGPUVertexFormat_Float32x2,.shaderLocation=1}};
    WGPUVertexBufferLayout buffers[] = {{0},{.arrayStride=8,.attributeCount=1,.attributes=attributes},
        {.arrayStride=constant_instance?0:8,.stepMode=WGPUVertexStepMode_Instance,
            .attributeCount=1,.attributes=attributes+1}};
    WGPUColorTargetState color = {.format=WGPUTextureFormat_RGBA8Unorm,.writeMask=WGPUColorWriteMask_All};
    WGPUFragmentState fragment = {.module=module,.targetCount=1,.targets=&color};
    WGPURenderPipelineDescriptor descriptor = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    descriptor.vertex = (WGPUVertexState){.module=module,.bufferCount=3,.buffers=buffers};
    descriptor.fragment = &fragment;
    WGPURenderPipeline result = wgpuDeviceCreateRenderPipeline(s->device,&descriptor);
    wgpuShaderModuleRelease(module);
    assert(result);
    return result;
}
static void bind(WGPURenderPassEncoder pass, WGPURenderPipeline pipeline,
    WGPUBuffer vertices, WGPUBuffer instances)
{
    wgpuRenderPassEncoderSetPipeline(pass,pipeline);
    wgpuRenderPassEncoderSetVertexBuffer(pass,1,vertices,8,24);
    wgpuRenderPassEncoderSetVertexBuffer(pass,2,instances,8,8);
}
int main(void)
{
    state s = {0};
    consumer_init(&s);
    WGPURenderPipeline program = graphics(&s,false), constant = graphics(&s,true);
    const uint32_t expected[] = {0xa5a5a5a5,3,1,0,0, 0xa5a5a5a5,3,1,UINT32_MAX,0,
        0xa5a5a5a5,3,2,0,0, 0xa5a5a5a5,3,1,0,1, 0xa5a5a5a5,0,1,0,0,
        0xa5a5a5a5,6,1,0,0};
    char source[1600];
    size_t length = (size_t)snprintf(source,sizeof(source),
        "@group(0) @binding(0) var<storage,read_write> args:array<u32>;"
        "@compute @workgroup_size(1) fn main(){");
    for (size_t i=0;i<sizeof(expected)/sizeof(*expected);++i) {
        int written = snprintf(source+length,sizeof(source)-length,"args[%zu]=%uu;",i,expected[i]);
        assert(written>0 && (size_t)written<sizeof(source)-length);
        length += (size_t)written;
    }
    assert(length+2<sizeof(source));
    source[length++]='}';
    source[length]=0;
    WGPUComputePipeline producer = pipeline(&s,source);
    WGPUComputePipeline inspector = pipeline(&s,
        "@group(0) @binding(0) var<storage,read> args:array<u32>;"
        "@group(0) @binding(1) var<storage,read_write> out:array<u32>;"
        "@compute @workgroup_size(1) fn main(){for(var i=0u;i<30u;i++){out[i]=args[i];}}");
    WGPUBuffer arguments = buffer(&s,sizeof(expected),WGPUBufferUsage_Storage|WGPUBufferUsage_Indirect);
    assert(!(wgpuBufferGetUsage(arguments)&WGPUBufferUsage_CopySrc));
    WGPUBuffer copied = buffer(&s,sizeof(expected),WGPUBufferUsage_Storage|WGPUBufferUsage_CopySrc);
    WGPUBindGroupEntry args_entry = {.binding=0,.buffer=arguments,.size=sizeof(expected)};
    WGPUBindGroup produce_group = group(&s,producer,&args_entry,1);
    WGPUBindGroupEntry inspect_entries[] = {args_entry,{.binding=1,.buffer=copied,.size=sizeof(expected)}};
    WGPUBindGroup inspect_group = group(&s,inspector,inspect_entries,2);
    const float points[] = {17,19,-1,-1,3,-1,-1,3};
    const float shifts[] = {23,29,0,0};
    WGPUBuffer vertices = buffer(&s,sizeof(points),WGPUBufferUsage_Vertex|WGPUBufferUsage_CopyDst);
    WGPUBuffer instances = buffer(&s,sizeof(shifts),WGPUBufferUsage_Vertex|WGPUBufferUsage_CopyDst);
    wgpuQueueWriteBuffer(s.queue,vertices,0,points,sizeof(points));
    wgpuQueueWriteBuffer(s.queue,instances,0,shifts,sizeof(shifts));
    WGPUTextureDescriptor texture_desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    texture_desc.format=WGPUTextureFormat_RGBA8Unorm;
    texture_desc.size=(WGPUExtent3D){8,8,1};
    texture_desc.usage=WGPUTextureUsage_RenderAttachment|WGPUTextureUsage_CopySrc;
    WGPUTexture image=wgpuDeviceCreateTexture(s.device,&texture_desc);
    WGPUTextureView view=wgpuTextureCreateView(image,NULL);
    WGPUQuerySetDescriptor query_desc={.type=WGPUQueryType_Occlusion,.count=12};
    WGPUQuerySet queries=wgpuDeviceCreateQuerySet(s.device,&query_desc);
    WGPUBuffer query_results=buffer(&s,96,WGPUBufferUsage_QueryResolve|WGPUBufferUsage_CopySrc);
    WGPUBuffer readback=buffer(&s,2264,WGPUBufferUsage_MapRead|WGPUBufferUsage_CopyDst);
    assert(view && queries);
    WGPURenderPassColorAttachment attachment=WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    attachment.view=view;
    attachment.loadOp=WGPULoadOp_Clear;
    attachment.storeOp=WGPUStoreOp_Store;
    attachment.clearValue=(WGPUColor){0,0,1,1};
    WGPURenderPassDescriptor pass_desc={.colorAttachmentCount=1,.colorAttachments=&attachment,.occlusionQuerySet=queries};
    WGPURenderBundleEncoderDescriptor bundle_desc=WGPU_RENDER_BUNDLE_ENCODER_DESCRIPTOR_INIT;
    bundle_desc.colorFormatCount=1;
    bundle_desc.colorFormats=&texture_desc.format;
    WGPURenderBundleEncoder builder=wgpuDeviceCreateRenderBundleEncoder(s.device,&bundle_desc);
    wgpuRenderBundleEncoderSetPipeline(builder,program);
    wgpuRenderBundleEncoderSetVertexBuffer(builder,1,vertices,8,24);
    wgpuRenderBundleEncoderSetVertexBuffer(builder,2,instances,8,8);
    wgpuRenderBundleEncoderDrawIndirect(builder,arguments,4);
    WGPURenderBundle bundle=wgpuRenderBundleEncoderFinish(builder,NULL);
    wgpuRenderBundleEncoderRelease(builder);
    assert(bundle);
    WGPUCommandEncoder encoder=wgpuDeviceCreateCommandEncoder(s.device,NULL);
    WGPUComputePassEncoder compute=wgpuCommandEncoderBeginComputePass(encoder,NULL);
    wgpuComputePassEncoderSetPipeline(compute,producer);
    wgpuComputePassEncoderSetBindGroup(compute,0,produce_group,0,NULL);
    wgpuComputePassEncoderDispatchWorkgroups(compute,1,1,1);
    wgpuComputePassEncoderEnd(compute);
    wgpuComputePassEncoderRelease(compute);
    WGPURenderPassEncoder pass=wgpuCommandEncoderBeginRenderPass(encoder,&pass_desc);
    assert(pass);
    bind(pass,program,vertices,instances);
    wgpuRenderPassEncoderSetScissorRect(pass,0,0,4,8);
    for(uint32_t i=0;i<6;++i){
        wgpuRenderPassEncoderBeginOcclusionQuery(pass,i);
        wgpuRenderPassEncoderDrawIndirect(pass,arguments,4+i*20);
        wgpuRenderPassEncoderEndOcclusionQuery(pass);
    }
    wgpuRenderPassEncoderSetScissorRect(pass,4,0,4,8);
    wgpuRenderPassEncoderBeginOcclusionQuery(pass,6);
    wgpuRenderPassEncoderExecuteBundles(pass,1,&bundle);
    wgpuRenderPassEncoderEndOcclusionQuery(pass);
    bind(pass,constant,vertices,instances);
    wgpuRenderPassEncoderBeginOcclusionQuery(pass,7);
    wgpuRenderPassEncoderDrawIndirect(pass,arguments,44);
    wgpuRenderPassEncoderEndOcclusionQuery(pass);
    const float viewports[4][4]={{-4,-4,16,16},{32,32,8,8},{1,1,0,8},{1,1,8,0}};
    wgpuRenderPassEncoderSetScissorRect(pass,0,0,8,8);
    for(uint32_t i=0;i<4;++i){
        wgpuRenderPassEncoderSetViewport(pass,viewports[i][0],viewports[i][1],viewports[i][2],viewports[i][3],0,1);
        wgpuRenderPassEncoderBeginOcclusionQuery(pass,8+i);
        wgpuRenderPassEncoderDraw(pass,3,1,0,0);
        wgpuRenderPassEncoderEndOcclusionQuery(pass);
    }
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
    compute=wgpuCommandEncoderBeginComputePass(encoder,NULL);
    wgpuComputePassEncoderSetPipeline(compute,inspector);
    wgpuComputePassEncoderSetBindGroup(compute,0,inspect_group,0,NULL);
    wgpuComputePassEncoderDispatchWorkgroups(compute,1,1,1);
    wgpuComputePassEncoderEnd(compute);
    wgpuComputePassEncoderRelease(compute);
    texture_to_buffer(encoder,image,readback,0,8,8);
    wgpuCommandEncoderResolveQuerySet(encoder,queries,0,12,query_results,0);
    wgpuCommandEncoderCopyBufferToBuffer(encoder,query_results,0,readback,2048,96);
    wgpuCommandEncoderCopyBufferToBuffer(encoder,copied,0,readback,2144,sizeof(expected));
    wgpuRenderPipelineRelease(program);
    wgpuRenderPipelineRelease(constant);
    wgpuRenderBundleRelease(bundle);
    wgpuComputePipelineRelease(producer);
    wgpuComputePipelineRelease(inspector);
    wgpuBindGroupRelease(produce_group);
    wgpuBindGroupRelease(inspect_group);
    wgpuBufferRelease(arguments);
    wgpuBufferRelease(copied);
    wgpuBufferRelease(vertices);
    wgpuBufferRelease(instances);
    wgpuQuerySetRelease(queries);
    wgpuBufferRelease(query_results);
    wgpuTextureViewRelease(view);
    wgpuTextureRelease(image);
    submit(&s,encoder);
    const unsigned char *bytes=map(&s,readback,2264);
    for(size_t y=0;y<8;++y)for(size_t x=0;x<8;++x){
        const unsigned char *pixel=bytes+y*256+x*4;
        assert(pixel[0]==255 && pixel[1]==0 && pixel[2]==0 && pixel[3]==255);
    }
    uint64_t query[12];
    memcpy(query,bytes+2048,sizeof(query));
    for(size_t i=0;i<12;++i)assert((query[i]!=0)==(i==0||i==6||i==7||i==8));
    assert(!memcmp(bytes+2144,expected,sizeof(expected)));
    wgpuBufferUnmap(readback);
    wgpuBufferRelease(readback);
    consumer_finish(&s);
    printf("PASS public indirect draw/bundle: GPU-produced arguments, actual 64 pixels and 12 query results, guarded first/count/instance ranges, disabled firstInstance, distinct draw scratch, stride0 instances, negative/outside/zero viewports, unchanged arguments and early releases %s\n", adapter_class_scope());
}
