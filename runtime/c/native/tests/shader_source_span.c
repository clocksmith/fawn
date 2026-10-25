/* SPDX-License-Identifier: MIT
 * Large comment spans retain exact source ownership without larger semantic pools. */
#include "consumer.h"
#include <stdlib.h>
int main(void)
{
    state s={0}; consumer_init(&s);
    const size_t prefix=131072;
    static const char body[]="*/ @compute @workgroup_size(1) fn main(){}";
    char *text=malloc(prefix+sizeof(body)); assert(text);
    memset(text,'x',prefix); text[0]='/'; text[1]='*';
    memcpy(text+prefix,body,sizeof(body));
    WGPUShaderSourceWGSL source=WGPU_SHADER_SOURCE_WGSL_INIT;
    source.code=(WGPUStringView){text,prefix+sizeof(body)-1};
    WGPUShaderModuleDescriptor descriptor={.nextInChain=&source.chain};
    WGPUShaderModule module=wgpuDeviceCreateShaderModule(s.device,&descriptor);
    assert(module && !s.errors);
    memset(text,0,prefix+sizeof(body)); free(text);
    WGPUComputePipelineDescriptor pipeline_desc=WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
    pipeline_desc.compute.module=module;
    WGPUComputePipeline pipeline=wgpuDeviceCreateComputePipeline(s.device,&pipeline_desc);
    assert(pipeline && !s.errors);
    wgpuComputePipelineRelease(pipeline); wgpuShaderModuleRelease(module);
    consumer_finish(&s);
    puts("PASS public WGSL source beyond64KiB, copied ownership after caller overwrite, actual compute pipeline");
    return 0;
}
