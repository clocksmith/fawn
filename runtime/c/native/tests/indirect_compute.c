/* SPDX-License-Identifier: MIT
 * GPU-produced arguments pass through the actual public indirect entrypoint. */
#include "../src/internal.h"
#include "consumer.h"

static doe_vk_status injected;
doe_vk_status __real_doe_vk_dispatch_validator_create(doe_vk_context *,
    const doe_vk_dispatch_validator_desc *, doe_vk_dispatch_validator **, doe_vk_error *);
doe_vk_status __wrap_doe_vk_dispatch_validator_create(doe_vk_context *context,
    const doe_vk_dispatch_validator_desc *descriptor, doe_vk_dispatch_validator **out, doe_vk_error *error)
{
    if (injected != DOE_VK_OK) return injected;
    return __real_doe_vk_dispatch_validator_create(context,descriptor,out,error);
}
static void error_type(WGPUPopErrorScopeStatus status, WGPUErrorType type,
    WGPUStringView message, void *userdata, void *unused)
{
    (void)message; (void)unused;
    assert(status == WGPUPopErrorScopeStatus_Success && type == *(WGPUErrorType *)userdata);
    *(WGPUErrorType *)userdata = WGPUErrorType_NoError;
}
static void pop_error(state *s, WGPUErrorType expected)
{
    WGPUPopErrorScopeCallbackInfo callback = {.mode=WGPUCallbackMode_AllowSpontaneous,
        .callback=error_type,.userdata1=&expected};
    assert(wgpuDevicePopErrorScope(s->device,callback).id && expected == WGPUErrorType_NoError);
}
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
    assert(!wgpuCommandEncoderFinish(encoder,NULL));
    wgpuCommandEncoderRelease(encoder);
    expect_validation(s);
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
        "@group(0) @binding(0) var<storage,read_write> value:atomic<u32>;"
        "@compute @workgroup_size(1) fn main(){atomicAdd(&value,1u);}");
    WGPUComputePipeline inspect = pipeline(&s,
        "@group(0) @binding(0) var<storage,read> args:array<u32>;"
        "@group(0) @binding(1) var<storage,read_write> out:array<u32>;"
        "@compute @workgroup_size(1) fn main(){for(var i=0u;i<20u;i++){out[i+1u]=args[i];}}");
    WGPUBuffer arguments = buffer(&s,80,WGPUBufferUsage_Storage|WGPUBufferUsage_Indirect);
    WGPUBuffer result = buffer(&s,84,WGPUBufferUsage_Storage|WGPUBufferUsage_CopySrc);
    WGPUBuffer readback = buffer(&s,88,WGPUBufferUsage_MapRead|WGPUBufferUsage_CopyDst);
    assert(wgpuBufferGetUsage(arguments) == (WGPUBufferUsage_Storage|WGPUBufferUsage_Indirect));
    WGPUBindGroupEntry argument_entry = {.binding=0,.buffer=arguments,.size=80};
    WGPUBindGroupEntry counter_entry = {.binding=0,.buffer=result,.size=84};
    WGPUBindGroupEntry inspect_entries[] = {argument_entry,{.binding=1,.buffer=result,.size=84}};
    WGPUBindGroup producer_group = group(&s,producer,&argument_entry,1);
    WGPUBindGroup counter_group = group(&s,counter,&counter_entry,1);
    WGPUBindGroup inspect_group = group(&s,inspect,inspect_entries,2);
    rejected_record(&s,counter,counter_group,arguments,2);
    rejected_record(&s,counter,counter_group,arguments,72);
    rejected_record(&s,counter,counter_group,result,0); /* Missing public Indirect usage. */
    rejected_record(&s,producer,producer_group,arguments,4); /* Writable/indirect scope conflict. */
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s.device,NULL);
    WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder,NULL);
    wgpuComputePassEncoderSetPipeline(pass,producer);
    wgpuComputePassEncoderSetBindGroup(pass,0,producer_group,0,NULL);
    wgpuComputePassEncoderDispatchWorkgroups(pass,1,1,1);
    wgpuComputePassEncoderSetPipeline(pass,counter);
    wgpuComputePassEncoderSetBindGroup(pass,0,counter_group,0,NULL);
    WGPUProcComputePassEncoderDispatchWorkgroupsIndirect indirect =
        (WGPUProcComputePassEncoderDispatchWorkgroupsIndirect)wgpuGetProcAddress(
            (WGPUStringView){"wgpuComputePassEncoderDispatchWorkgroupsIndirect",WGPU_STRLEN});
    assert(indirect);
    const uint64_t offsets[] = {4,20,36,52,68,4};
    for (size_t i=0;i<sizeof(offsets)/sizeof(*offsets);++i) indirect(pass,arguments,offsets[i]);
    wgpuComputePassEncoderSetPipeline(pass,inspect);
    wgpuComputePassEncoderSetBindGroup(pass,0,inspect_group,0,NULL);
    wgpuComputePassEncoderDispatchWorkgroups(pass,1,1,1);
    wgpuComputePassEncoderEnd(pass);
    wgpuComputePassEncoderRelease(pass);
    wgpuCommandEncoderCopyBufferToBuffer(encoder,result,0,readback,0,84);
    WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder,NULL);
    assert(commands);
    wgpuCommandEncoderRelease(encoder);
    wgpuComputePipelineRelease(producer);
    wgpuComputePipelineRelease(counter);
    wgpuComputePipelineRelease(inspect);
    wgpuBindGroupRelease(producer_group);
    wgpuBindGroupRelease(counter_group);
    wgpuBindGroupRelease(inspect_group);
    wgpuBufferRelease(arguments);
    wgpuBufferRelease(result);
    injected = DOE_VK_OUT_OF_MEMORY;
    wgpuDevicePushErrorScope(s.device,WGPUErrorFilter_OutOfMemory);
    wgpuQueueSubmit(s.queue,1,&commands);
    pop_error(&s,WGPUErrorType_OutOfMemory);
    assert(!((native_device *)s.device)->dispatch_validator);
    injected = DOE_VK_NATIVE_FAILURE;
    wgpuDevicePushErrorScope(s.device,WGPUErrorFilter_Internal);
    wgpuQueueSubmit(s.queue,1,&commands);
    pop_error(&s,WGPUErrorType_Internal);
    assert(!((native_device *)s.device)->dispatch_validator);
    injected = DOE_VK_OK;
    wgpuQueueSubmit(s.queue,1,&commands);
    wgpuCommandBufferRelease(commands);
    assert(((native_device *)s.device)->dispatch_validator);
    const uint32_t *words = map(&s,readback,88);
    uint32_t expected[] = {0xa5a5a5a5,2,1,1,0xa5a5a5a5,limits.maxComputeWorkgroupsPerDimension+1,1,1,
        0xa5a5a5a5,1,limits.maxComputeWorkgroupsPerDimension+1,1,
        0xa5a5a5a5,1,1,limits.maxComputeWorkgroupsPerDimension+1,0xa5a5a5a5,0,1,1};
    assert(words[0] == 4 && words[21] == 0);
    assert(!memcmp(words+1,expected,sizeof(expected)));
    wgpuBufferUnmap(readback);
    wgpuBufferRelease(readback);
    consumer_finish(&s);
    printf("PASS public indirect compute: same-command GPU-produced valid/overlimit XYZ/zero/valid counts, four actual atomic invocations, unchanged arguments/padding without public CopySrc, typed guard failures/retry and early releases %s\n", adapter_class_scope());
}
