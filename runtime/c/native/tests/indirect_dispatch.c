/* SPDX-License-Identifier: MIT
 * Portable public ABI control: GPU-produced arguments, guarded execution and dispatch metadata. */
#include "consumer.h"

static void rejected_record(state *s, WGPUComputePipeline program, WGPUBindGroup bindings,
    WGPUBuffer arguments, uint64_t offset)
{
    wgpuDevicePushErrorScope(s->device,WGPUErrorFilter_Validation);
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device,NULL);
    WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder,NULL);
    wgpuComputePassEncoderSetPipeline(pass,program);
    wgpuComputePassEncoderSetBindGroup(pass,0,bindings,0,NULL);
    wgpuComputePassEncoderDispatchWorkgroupsIndirect(pass,arguments,offset);
    wgpuComputePassEncoderEnd(pass);
    wgpuComputePassEncoderRelease(pass);
    finish_invalid(encoder);
    wgpuCommandEncoderRelease(encoder);
    expect_validation(s);
}
typedef struct recording {
    WGPUComputePipeline producer, counter, inspect;
    WGPUBindGroup producer_group, counter_group, inspect_group;
    WGPUBuffer arguments, result, readback;
    WGPUProcComputePassEncoderDispatchWorkgroupsIndirect indirect;
} recording;
/* One complete produce/count/inspect submission's command buffer. */
static WGPUCommandBuffer encode(state *s, const recording *r)
{
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device,NULL);
    WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder,NULL);
    wgpuComputePassEncoderSetPipeline(pass,r->producer);
    wgpuComputePassEncoderSetBindGroup(pass,0,r->producer_group,0,NULL);
    wgpuComputePassEncoderDispatchWorkgroups(pass,1,1,1);
    wgpuComputePassEncoderSetPipeline(pass,r->counter);
    wgpuComputePassEncoderSetBindGroup(pass,0,r->counter_group,0,NULL);
    const uint64_t offsets[] = {4,20,36,52,68,4};
    for (size_t i=0;i<sizeof(offsets)/sizeof(*offsets);++i) r->indirect(pass,r->arguments,offsets[i]);
    wgpuComputePassEncoderSetPipeline(pass,r->inspect);
    wgpuComputePassEncoderSetBindGroup(pass,0,r->inspect_group,0,NULL);
    wgpuComputePassEncoderDispatchWorkgroups(pass,1,1,1);
    wgpuComputePassEncoderEnd(pass);
    wgpuComputePassEncoderRelease(pass);
    wgpuCommandEncoderCopyBufferToBuffer(encoder,r->result,0,r->readback,0,112);
    WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder,NULL);
    assert(commands);
    wgpuCommandEncoderRelease(encoder);
    return commands;
}
int main(void)
{
    state s = {0};
    consumer_init(&s);
    WGPULimits limits = WGPU_LIMITS_INIT;
    assert(wgpuDeviceGetLimits(s.device,&limits) == WGPUStatus_Success);
    assert(limits.maxComputeWorkgroupsPerDimension < UINT32_MAX);
    char source[1400];
    int length = snprintf(source,sizeof(source),
        "@group(0) @binding(0) var<storage,read_write> args:array<u32>;"
        "@compute @workgroup_size(1) fn main(){"
        "for(var i=0u;i<20u;i++){args[i]=0xa5a5a5a5u;}"
        "args[1]=2u;args[2]=1u;args[3]=1u;"
        "args[5]=%uu;args[6]=1u;args[7]=1u;"
        "args[9]=1u;args[10]=%uu;args[11]=1u;"
        "args[13]=1u;args[14]=1u;args[15]=%uu;"
        "args[17]=0u;args[18]=1u;args[19]=1u;}",
        limits.maxComputeWorkgroupsPerDimension+1,limits.maxComputeWorkgroupsPerDimension+1,
        limits.maxComputeWorkgroupsPerDimension+1);
    assert(length > 0 && (size_t)length < sizeof(source));
    WGPUComputePipeline producer = pipeline(&s,source);
    WGPUComputePipeline counter = pipeline(&s,
        "@group(0) @binding(0) var<storage,read_write> value:array<atomic<u32>>;"
        "@compute @workgroup_size(1) fn main(@builtin(num_workgroups) n:vec3<u32>,"
        "@builtin(workgroup_id) id:vec3<u32>){atomicAdd(&value[0],1u);"
        "atomicStore(&value[1],n.x);atomicStore(&value[2],n.y);atomicStore(&value[3],n.z);"
        "atomicAdd(&value[4],id.x);}");
    WGPUComputePipeline inspect = pipeline(&s,
        "@group(0) @binding(0) var<storage,read> args:array<u32>;"
        "@group(0) @binding(1) var<storage,read_write> out:array<u32>;"
        "@compute @workgroup_size(1) fn main(){for(var i=0u;i<20u;i++){out[i+8u]=args[i];}}");
    WGPUBuffer arguments = buffer(&s,80,WGPUBufferUsage_Storage|WGPUBufferUsage_Indirect);
    WGPUBuffer result = buffer(&s,112,WGPUBufferUsage_Storage|WGPUBufferUsage_CopySrc);
    WGPUBuffer readback = buffer(&s,116,WGPUBufferUsage_MapRead|WGPUBufferUsage_CopyDst);
    assert(wgpuBufferGetUsage(arguments) == (WGPUBufferUsage_Storage|WGPUBufferUsage_Indirect));
    WGPUBindGroupEntry argument_entry = {.binding=0,.buffer=arguments,.size=80};
    WGPUBindGroupEntry counter_entry = {.binding=0,.buffer=result,.size=112};
    WGPUBindGroupEntry inspect_entries[] = {argument_entry,{.binding=1,.buffer=result,.size=112}};
    WGPUBindGroup producer_group = group(&s,producer,&argument_entry,1);
    WGPUBindGroup counter_group = group(&s,counter,&counter_entry,1);
    WGPUBindGroup inspect_group = group(&s,inspect,inspect_entries,2);
    rejected_record(&s,counter,counter_group,arguments,2);
    rejected_record(&s,counter,counter_group,arguments,72);
    rejected_record(&s,counter,counter_group,result,0); /* Missing public Indirect usage. */
    rejected_record(&s,producer,producer_group,arguments,4); /* Writable/indirect scope conflict. */
    WGPUProcComputePassEncoderDispatchWorkgroupsIndirect indirect =
        (WGPUProcComputePassEncoderDispatchWorkgroupsIndirect)wgpuGetProcAddress(
            (WGPUStringView){"wgpuComputePassEncoderDispatchWorkgroupsIndirect",WGPU_STRLEN});
    assert(indirect);
    const recording r = {producer,counter,inspect,producer_group,counter_group,inspect_group,arguments,result,
        readback,indirect};
    WGPUCommandBuffer commands = encode(&s,&r);
    wgpuComputePipelineRelease(producer);
    wgpuComputePipelineRelease(counter);
    wgpuComputePipelineRelease(inspect);
    wgpuBindGroupRelease(producer_group);
    wgpuBindGroupRelease(counter_group);
    wgpuBindGroupRelease(inspect_group);
    wgpuBufferRelease(arguments);
    wgpuBufferRelease(result);
    wgpuQueueSubmit(s.queue,1,&commands);
    wgpuCommandBufferRelease(commands);
    const uint32_t *words = map(&s,readback,116);
    uint32_t expected[] = {0xa5a5a5a5,2,1,1,0xa5a5a5a5,limits.maxComputeWorkgroupsPerDimension+1,1,1,
        0xa5a5a5a5,1,limits.maxComputeWorkgroupsPerDimension+1,1,
        0xa5a5a5a5,1,1,limits.maxComputeWorkgroupsPerDimension+1,0xa5a5a5a5,0,1,1};
    assert(words[0] == 4 && words[1] == 2 && words[2] == 1 && words[3] == 1 && words[4] == 2);
    assert(words[5] == 0 && words[6] == 0 && words[7] == 0 && words[28] == 0);
    assert(!memcmp(words+8,expected,sizeof(expected)));
    wgpuBufferUnmap(readback);
    wgpuBufferRelease(readback);
    consumer_finish(&s);
    printf("PASS public indirect dispatch: GPU-produced valid/overlimit XYZ/zero/valid records, "
        "four invocations, GPU num_workgroups metadata, original arguments/padding, "
        "logical admission and early releases %s\n", adapter_class_scope());
}
