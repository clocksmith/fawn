/* SPDX-License-Identifier: MIT */
#include "consumer.h"
#include "doe_native_shader_sources.h"
#include "../../compiler/tests/hlsl_fixture.h"
#include <stdlib.h>

static unsigned compilation_callbacks;
static void compilation(WGPUCompilationInfoRequestStatus status,const WGPUCompilationInfo *info,void *a,void *b)
{
    (void)a; (void)b;
    assert(status==WGPUCompilationInfoRequestStatus_Success && info && !info->messageCount);
    ++compilation_callbacks;
}
static uint32_t *compile_words(size_t *count)
{
    static const char source[]="@group(0)@binding(0)var<storage,read_write>out:array<u32>;"
        "@compute @workgroup_size(1)fn main(){out[0]=305419896u;out[1]=arrayLength(&out);}";
    DoeCompilerStageCapacities caps=hlsl_capacities().stages;
    caps.module.typed.words=65536;
    caps.module.typed.target_declarations=4096;
    DoeCompilerWorkspaceRequirements requirements;
    assert(doe_compiler_stage_requirements(8,&caps,sizeof(caps),&requirements,sizeof(requirements))==DOE_COMPILER_OK);
    void *workspace=malloc(requirements.size_bytes);
    HlslFixture *fixture=calloc(1,sizeof(*fixture));
    uint32_t *words=calloc(65536,sizeof(*words));
    assert(workspace && fixture && words);
    DoeCompilerStageOutputs outputs=hlsl_outputs(fixture).stages;
    outputs.resources.module.words=words; outputs.resources.module.word_capacity=65536;
    DoeCompilerEntrySelectionV5 selection={.mode=DOE_COMPILER_SELECT_AUTO};
    DoeCompilerStageResult result;
    DoeCompilerStatus status=doe_compiler_compile_stages(8,&caps,sizeof(caps),workspace,requirements.size_bytes,
        source,sizeof(source)-1,&selection,sizeof(selection),3,&outputs,sizeof(outputs),&result,sizeof(result));
    if (status!=DOE_COMPILER_OK) fprintf(stderr,"import fixture compiler: %u %s\n",status,result.resources.module.diagnostic.message);
    assert(status==DOE_COMPILER_OK);
    *count=result.resources.module.published.words;
    free(fixture); free(workspace);
    return words;
}
static WGPUShaderModule imported(state *s,const uint32_t *words,size_t count)
{
    assert(count<=UINT32_MAX);
    WGPUShaderSourceSPIRV source=WGPU_SHADER_SOURCE_SPIRV_INIT;
    source.code=words; source.codeSize=(uint32_t)count;
    WGPUShaderModuleDescriptor descriptor={.nextInChain=&source.chain};
    return wgpuDeviceCreateShaderModule(s->device,&descriptor);
}
static WGPUComputePipeline selected(state *s,WGPUShaderModule shader,WGPUPipelineLayout layout,const char *entry)
{
    WGPUComputePipelineDescriptor descriptor=WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
    descriptor.layout=layout; descriptor.compute.module=shader;
    descriptor.compute.entryPoint=(WGPUStringView){entry,WGPU_STRLEN};
    return wgpuDeviceCreateComputePipeline(s->device,&descriptor);
}
int main(void)
{
    _Static_assert(offsetof(DoeNativeShaderSourceMSL,code)==sizeof(WGPUChainedStruct),"retained MSL source prefix");
    _Static_assert(sizeof(DoeNativeShaderSourceMSL)==sizeof(DoeNativeShaderSourceHLSL),"retained native text source ABI");
    state s={0}; consumer_init(&s);
    size_t count; uint32_t *words=compile_words(&count);
    WGPUBindGroupLayoutEntry entry={.binding=0,.visibility=WGPUShaderStage_Compute,
        .buffer={.type=WGPUBufferBindingType_Storage,.minBindingSize=16}};
    WGPUBindGroupLayoutDescriptor group_descriptor={.entryCount=1,.entries=&entry};
    WGPUBindGroupLayout bgl=wgpuDeviceCreateBindGroupLayout(s.device,&group_descriptor); assert(bgl);
    WGPUPipelineLayoutDescriptor layout_descriptor={.bindGroupLayoutCount=1,.bindGroupLayouts=&bgl};
    WGPUPipelineLayout layout=wgpuDeviceCreatePipelineLayout(s.device,&layout_descriptor); assert(layout);
    WGPUShaderModule shader=imported(&s,words,count); assert(shader);
    WGPUCompilationInfoCallbackInfo info={.mode=WGPUCallbackMode_AllowSpontaneous,.callback=compilation};
    assert(wgpuShaderModuleGetCompilationInfo(shader,info).id && compilation_callbacks==1);
    wgpuDevicePushErrorScope(s.device,WGPUErrorFilter_Validation);
    WGPUComputePipeline bad=selected(&s,shader,layout,"absent");
    expect_validation(&s); if (bad) wgpuComputePipelineRelease(bad);
    WGPUConstantEntry constant={.key={"anything",8},.value=1};
    WGPUComputePipelineDescriptor override=WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
    override.layout=layout; override.compute.module=shader; override.compute.entryPoint=(WGPUStringView){"main",4};
    override.compute.constantCount=1; override.compute.constants=&constant;
    wgpuDevicePushErrorScope(s.device,WGPUErrorFilter_Validation);
    bad=wgpuDeviceCreateComputePipeline(s.device,&override);
    expect_validation(&s); if (bad) wgpuComputePipelineRelease(bad);
    size_t mode=0;
    for (size_t at=5;at<count;at+=words[at]>>16)
        if ((words[at]&65535u)==16 && words[at]>>16==6 && words[at+2]==17) mode=at+2;
    assert(mode); words[mode]=18;
    WGPUShaderModule missing=imported(&s,words,count); assert(missing);
    wgpuDevicePushErrorScope(s.device,WGPUErrorFilter_Validation);
    bad=selected(&s,missing,layout,"main");
    expect_validation(&s); if (bad) wgpuComputePipelineRelease(bad);
    wgpuShaderModuleRelease(missing); words[mode]=17;
    uint32_t saved=words[5]; words[5]=0;
    wgpuDevicePushErrorScope(s.device,WGPUErrorFilter_Validation);
    missing=imported(&s,words,count); expect_validation(&s);
    if (missing) wgpuShaderModuleRelease(missing);
    words[5]=saved;
    memset(words,0,count*sizeof(*words)); free(words); /* Import owns the exact copied binary. */
    WGPUComputePipeline compute=selected(&s,shader,layout,"main"); assert(compute);
    wgpuShaderModuleRelease(shader); wgpuPipelineLayoutRelease(layout);
    WGPUBuffer output=buffer(&s,16,WGPUBufferUsage_Storage|WGPUBufferUsage_CopySrc);
    WGPUBuffer readback=buffer(&s,16,WGPUBufferUsage_CopyDst|WGPUBufferUsage_MapRead);
    WGPUBindGroupEntry binding={.binding=0,.buffer=output,.size=16};
    WGPUBindGroupDescriptor binding_descriptor={.layout=bgl,.entryCount=1,.entries=&binding};
    WGPUBindGroup group=wgpuDeviceCreateBindGroup(s.device,&binding_descriptor); assert(group);
    wgpuBindGroupLayoutRelease(bgl);
    WGPUCommandEncoder encoder=wgpuDeviceCreateCommandEncoder(s.device,NULL);
    WGPUComputePassEncoder pass=wgpuCommandEncoderBeginComputePass(encoder,NULL);
    wgpuComputePassEncoderSetPipeline(pass,compute); wgpuComputePassEncoderSetBindGroup(pass,0,group,0,NULL);
    wgpuComputePassEncoderDispatchWorkgroups(pass,1,1,1);
    wgpuComputePassEncoderEnd(pass); wgpuComputePassEncoderRelease(pass);
    wgpuCommandEncoderCopyBufferToBuffer(encoder,output,0,readback,0,16);
    wgpuComputePipelineRelease(compute); wgpuBindGroupRelease(group); wgpuBufferRelease(output);
    submit(&s,encoder); work_done(&s);
    const uint32_t *actual=map(&s,readback,16);
    assert(actual[0]==UINT32_C(0x12345678) && actual[1]==4 && !actual[2] && !actual[3]);
    wgpuBufferUnmap(readback); wgpuBufferRelease(readback); consumer_finish(&s);
    puts("PASS imported SPIR-V explicit-layout dispatch/readback, copied source, early releases and malformed selection controls; software Vulkan");
}
