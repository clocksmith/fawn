/* SPDX-License-Identifier: MIT
 * Pinned Dawn normalizes non-array entries to bindingArraySize=1. Both zero
 * and one describe one logical binding; larger arrays remain unsupported. */
#include "consumer.h"
#include "doe_native_device_policy.h"

static WGPUBindGroupLayout layout(state *s, unsigned kind, uint32_t count)
{
    WGPUBindGroupLayoutEntry entry = {.binding=0,.visibility=WGPUShaderStage_Compute,
        .bindingArraySize=count};
    if (kind == 0) entry.buffer = (WGPUBufferBindingLayout){
        .type=WGPUBufferBindingType_Storage,.minBindingSize=4};
    else if (kind == 1) entry.sampler.type = WGPUSamplerBindingType_Filtering;
    else if (kind == 2) entry.texture = (WGPUTextureBindingLayout){
        .sampleType=WGPUTextureSampleType_Float,.viewDimension=WGPUTextureViewDimension_2D};
    else entry.storageTexture = (WGPUStorageTextureBindingLayout){
        .access=WGPUStorageTextureAccess_WriteOnly,.format=WGPUTextureFormat_RGBA8Unorm,
        .viewDimension=WGPUTextureViewDimension_2D};
    const WGPUBindGroupLayoutDescriptor descriptor = {.entryCount=1,.entries=&entry};
    return wgpuDeviceCreateBindGroupLayout(s->device,&descriptor);
}
static void scalar_descriptors(state *s)
{
    for (unsigned kind = 0; kind < 4; ++kind) {
        for (uint32_t count = 0; count < 2; ++count) {
            WGPUBindGroupLayout created = layout(s,kind,count);
            assert(created);
            wgpuBindGroupLayoutRelease(created);
        }
        const uint32_t counts[] = {2,UINT32_MAX};
        for (size_t i = 0; i < sizeof(counts)/sizeof(counts[0]); ++i) {
            wgpuDevicePushErrorScope(s->device,WGPUErrorFilter_Validation);
            assert(!layout(s,kind,counts[i]));
            expect_validation(s);
        }
    }
}
static void explicit_compute(state *s)
{
    const char code[] = "@group(0) @binding(0) var<storage,read_write> data:array<u32>;"
        "@compute @workgroup_size(1) fn main(@builtin(global_invocation_id) gid:vec3u){"
        "data[gid.x]=data[gid.x]+1u;}";
    WGPUShaderSourceWGSL source = WGPU_SHADER_SOURCE_WGSL_INIT;
    source.code = (WGPUStringView){code,sizeof(code)-1};
    const WGPUShaderModuleDescriptor module_descriptor = {.nextInChain=&source.chain};
    WGPUShaderModule module = wgpuDeviceCreateShaderModule(s->device,&module_descriptor);
    assert(module);
    WGPUBindGroupLayout layouts[2] = {layout(s,0,0),layout(s,0,1)};
    assert(layouts[0] && layouts[1]);
    WGPUBuffer storage = buffer(s,16,WGPUBufferUsage_Storage|WGPUBufferUsage_CopySrc|WGPUBufferUsage_CopyDst);
    WGPUBuffer readback = buffer(s,16,WGPUBufferUsage_CopyDst|WGPUBufferUsage_MapRead);
    const WGPUBindGroupEntry entry = {.binding=0,.buffer=storage,.size=16};
    /* A group made from the zero spelling must also bind to the independently
     * created explicit-one layout: normalization preserves scalar compatibility. */
    const WGPUBindGroupDescriptor group_descriptor = {.layout=layouts[0],.entryCount=1,.entries=&entry};
    WGPUBindGroup group = wgpuDeviceCreateBindGroup(s->device,&group_descriptor);
    assert(group);
    for (size_t i = 0; i < 2; ++i) {
        const WGPUPipelineLayoutDescriptor layout_descriptor = {.bindGroupLayoutCount=1,
            .bindGroupLayouts=&layouts[i]};
        WGPUPipelineLayout pipeline_layout = wgpuDeviceCreatePipelineLayout(s->device,&layout_descriptor);
        assert(pipeline_layout);
        WGPUComputePipelineDescriptor descriptor = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
        descriptor.layout = pipeline_layout;
        descriptor.compute.module = module;
        descriptor.compute.entryPoint = (WGPUStringView){"main",4};
        WGPUComputePipeline compute = wgpuDeviceCreateComputePipeline(s->device,&descriptor);
        assert(compute);
        const uint32_t input[4] = {1,2,3,4};
        wgpuQueueWriteBuffer(s->queue,storage,0,input,sizeof(input));
        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device,NULL);
        WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder,NULL);
        assert(pass);
        wgpuComputePassEncoderSetPipeline(pass,compute);
        wgpuComputePassEncoderSetBindGroup(pass,0,group,0,NULL);
        wgpuComputePassEncoderDispatchWorkgroups(pass,4,1,1);
        wgpuComputePassEncoderEnd(pass);
        wgpuCommandEncoderCopyBufferToBuffer(encoder,storage,0,readback,0,sizeof(input));
        submit(s,encoder);
        const uint32_t *actual = map(s,readback,sizeof(input));
        for (size_t word = 0; word < 4; ++word) assert(actual[word] == input[word]+1);
        wgpuBufferUnmap(readback);
        work_done(s);
        /* An ended pass may remain retained across Finish, submission and map. */
        wgpuComputePassEncoderRelease(pass);
        wgpuComputePipelineRelease(compute);
        wgpuPipelineLayoutRelease(pipeline_layout);
    }
    wgpuBindGroupRelease(group);
    wgpuBufferRelease(readback);
    wgpuBufferRelease(storage);
    wgpuBindGroupLayoutRelease(layouts[1]);
    wgpuBindGroupLayoutRelease(layouts[0]);
    wgpuShaderModuleRelease(module);
}
int main(void)
{
    state s = {0}; consumer_init(&s);
    assert(doeNativeDeviceDisablePersistentCache(s.device) == WGPUStatus_Success);
    scalar_descriptors(&s);
    explicit_compute(&s);
    consumer_finish(&s);
    printf("PASS scalar bindingArraySize0/1 normalization, four binding kinds, larger-array rejection, explicit-layout structural compatibility and retained-ended-pass compute exact8words; %s\n",adapter_class_scope());
    return 0;
}
