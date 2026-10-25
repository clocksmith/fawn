/* SPDX-License-Identifier: MIT */
#include "consumer.h"
int main(void)
{
    state s={0}; consumer_init(&s);
    WGPUComputePipeline compute=pipeline(&s,
        "requires immediate_address_space; struct P {index:u32,value:u32} var<immediate> p:P;"
        "@group(0)@binding(0)var<storage,read_write> out:array<u32>;"
        "@compute @workgroup_size(1) fn main(){out[p.index]=p.value;}");
    WGPUBuffer output=buffer(&s,16,WGPUBufferUsage_Storage|WGPUBufferUsage_CopySrc);
    WGPUBindGroupEntry entry={.binding=0,.buffer=output,.size=16};
    WGPUBindGroup bindings=group(&s,compute,&entry,1);
    WGPUBuffer readback=buffer(&s,16,WGPUBufferUsage_CopyDst|WGPUBufferUsage_MapRead);
    WGPUCommandEncoder encoder=wgpuDeviceCreateCommandEncoder(s.device,NULL);
    WGPUComputePassEncoder pass=wgpuCommandEncoderBeginComputePass(encoder,NULL);
    wgpuComputePassEncoderSetPipeline(pass,compute);wgpuComputePassEncoderSetBindGroup(pass,0,bindings,0,NULL);
    uint32_t words[2]={0,123};
    wgpuComputePassEncoderSetImmediates(pass,0,words,sizeof(words));
    wgpuComputePassEncoderDispatchWorkgroups(pass,1,1,1);
    words[0]=1;words[1]=456;
    wgpuComputePassEncoderSetImmediates(pass,0,words,sizeof(words));
    wgpuComputePassEncoderDispatchWorkgroups(pass,1,1,1);
    words[0]=99;words[1]=99; /* Recording must not borrow caller bytes. */
    wgpuComputePassEncoderEnd(pass);wgpuComputePassEncoderRelease(pass);
    wgpuCommandEncoderCopyBufferToBuffer(encoder,output,0,readback,0,16);
    wgpuBindGroupRelease(bindings);wgpuComputePipelineRelease(compute);wgpuBufferRelease(output);
    submit(&s,encoder);work_done(&s);
    const uint32_t *actual=map(&s,readback,16);
    assert(actual[0]==123 && actual[1]==456 && actual[2]==0 && actual[3]==0);
    wgpuBufferUnmap(readback);wgpuBufferRelease(readback);consumer_finish(&s);
    puts("PASS immediate compute two immutable snapshots, auto layout, exact GPU readback and early releases; software Vulkan");
}
