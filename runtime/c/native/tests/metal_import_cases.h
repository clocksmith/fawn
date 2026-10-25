/* SPDX-License-Identifier: MIT */
#ifndef DOE_NATIVE_TEST_METAL_IMPORT_CASES_H
#define DOE_NATIVE_TEST_METAL_IMPORT_CASES_H
#include "doe_native_shader_sources.h"
static void metal_shader_import_cases(state *s)
{
    mock_native *mock=&mocks[2];
    char source[]="#include <metal_stdlib>\nusing namespace metal; kernel void main(device uint* out [[buffer(3)]], uint i [[thread_position_in_grid]]) { if(i==0) out[0]=7; }";
    DoeNativeShaderSourceMSL chain={.chain={.sType=DOE_NATIVE_STYPE_SHADER_SOURCE_MSL},
        .code={source,sizeof(source)-1},.workgroup_size_x=1};
    WGPUShaderModuleDescriptor sd={.nextInChain=&chain.chain};
    unsigned calls=mock->shader_source_calls;
    WGPUShaderModule shader=wgpuDeviceCreateShaderModule(s->device,&sd);
    assert(shader && mock->shader_source_calls==calls+1);
    native_shader *payload=doe_resource_shader_native((doe_resource_shader *)shader);
    assert(payload && !payload->module && payload->imported.data && payload->imported.data!=source);
    source[0]='!';
    assert(((char *)payload->imported.data)[0]=='#');
    WGPUBindGroupLayoutEntry row={.binding=3,.visibility=WGPUShaderStage_Compute,
        .buffer={.type=WGPUBufferBindingType_Storage,.minBindingSize=16}};
    WGPUBindGroupLayoutDescriptor ld={.entryCount=1,.entries=&row};
    WGPUBindGroupLayout layout=wgpuDeviceCreateBindGroupLayout(s->device,&ld);
    WGPUPipelineLayoutDescriptor pd={.bindGroupLayoutCount=1,.bindGroupLayouts=&layout};
    WGPUPipelineLayout pl=wgpuDeviceCreatePipelineLayout(s->device,&pd);
    WGPUComputePipelineDescriptor cd={.layout=pl,.compute={.module=shader,.entryPoint={"main",4}}};
    WGPUComputePipeline compute=wgpuDeviceCreateComputePipeline(s->device,&cd);
    assert(compute);
    native_pipeline *pipeline_owner=doe_resource_pipeline_native((doe_resource_pipeline *)compute);
    assert(pipeline_owner && pipeline_owner->binding_count==1 && pipeline_owner->metal_compute->slots[0]==3);
    wgpuShaderModuleRelease(shader); wgpuPipelineLayoutRelease(pl); wgpuBindGroupLayoutRelease(layout);
    WGPUBuffer output=buffer(s,16,WGPUBufferUsage_Storage);
    WGPUBindGroupEntry entry={.binding=3,.buffer=output,.size=16};
    WGPUBindGroup bound=group(s,compute,&entry,1);
    WGPUCommandEncoder encoder=wgpuDeviceCreateCommandEncoder(s->device,NULL);
    dispatch(encoder,compute,bound);
    wgpuBindGroupRelease(bound); wgpuBufferRelease(output); wgpuComputePipelineRelease(compute);
    unsigned dispatches=mock->dispatches;
    submit(s,encoder);
    assert(mock->dispatches==dispatches+1);
    source[0]='#'; mock->fail=MOCK_SHADER_SOURCE;
    wgpuDevicePushErrorScope(s->device,WGPUErrorFilter_Validation);
    shader=wgpuDeviceCreateShaderModule(s->device,&sd);
    assert(shader);
    payload=doe_resource_shader_native((doe_resource_shader *)shader);
    assert(payload && !payload->imported.data);
    assert(strstr(payload->error.diagnostic.message,"native=-71") &&
        strstr(payload->error.diagnostic.message,"owned fault boundary"));
    pop(s,WGPUErrorType_Validation);
    wgpuShaderModuleRelease(shader); mock->fail=MOCK_NONE;
    puts("PASS native MSL copied source, constructor validation, explicit slots and early-release dispatch; host ledger only");
}
#endif
