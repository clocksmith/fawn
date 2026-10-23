/* SPDX-License-Identifier: MIT
 * Public queue/resource admission, genuine software compute, no hardware claim. */
#include "consumer.h"

static void sparse_groups(state *s)
{
    WGPUComputePipeline compute = pipeline(s,
        "@group(2) @binding(3) var<storage,read_write> output:array<u32>;"
        "@compute @workgroup_size(1) fn main(){output[0]+=13u;}");
    WGPUBindGroupLayout layout = wgpuComputePipelineGetBindGroupLayout(compute, 2);
    WGPUBindGroupLayout hole = wgpuComputePipelineGetBindGroupLayout(compute, 0);
    assert(layout && hole);
    WGPUBindGroupDescriptor empty = {.layout=hole};
    WGPUBindGroup empty_group = wgpuDeviceCreateBindGroup(s->device, &empty);
    assert(empty_group);
    WGPUBuffer output = buffer(s, 4, WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc);
    WGPUBuffer ignored = buffer(s, 4, WGPUBufferUsage_Storage);
    WGPUBuffer readback = buffer(s, 8, WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead);
    WGPUBindGroupEntry entry = {.binding=3,.buffer=output,.size=4};
    WGPUBindGroupDescriptor descriptor = {.layout=layout,.entryCount=1,.entries=&entry};
    WGPUBindGroup active = wgpuDeviceCreateBindGroup(s->device,&descriptor);
    entry.buffer = ignored;
    WGPUBindGroup unused = wgpuDeviceCreateBindGroup(s->device,&descriptor);
    assert(active && unused);
    wgpuDevicePushErrorScope(s->device,WGPUErrorFilter_Validation);
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device,NULL);
    WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder,NULL);
    wgpuComputePassEncoderSetPipeline(pass,compute);
    wgpuComputePassEncoderDispatchWorkgroups(pass,1,1,1);
    wgpuComputePassEncoderEnd(pass);
    assert(!wgpuCommandEncoderFinish(encoder,NULL));
    wgpuComputePassEncoderRelease(pass);
    wgpuCommandEncoderRelease(encoder);
    expect_validation(s); /* Nonempty group2 is still required. */
    encoder = wgpuDeviceCreateCommandEncoder(s->device,NULL);
    pass = wgpuCommandEncoderBeginComputePass(encoder,NULL);
    wgpuComputePassEncoderSetPipeline(pass,compute);
    wgpuComputePassEncoderSetBindGroup(pass,2,active,0,NULL);
    /* No groups0/1 are required by this auto layout. */
    wgpuComputePassEncoderDispatchWorkgroups(pass,1,1,1);
    wgpuComputePassEncoderSetBindGroup(pass,0,unused,0,NULL);
    wgpuComputePassEncoderSetBindGroup(pass,1,empty_group,0,NULL);
    wgpuComputePassEncoderSetBindGroup(pass,2,NULL,0,NULL);
    wgpuComputePassEncoderSetBindGroup(pass,2,active,0,NULL);
    wgpuComputePassEncoderDispatchWorkgroups(pass,1,1,1);
    wgpuComputePassEncoderEnd(pass);
    wgpuComputePassEncoderRelease(pass);
    wgpuCommandEncoderCopyBufferToBuffer(encoder,output,0,readback,0,4);
    /* Ignored hole groups create no submitted resource obligation. */
    wgpuBufferDestroy(ignored);
    wgpuBindGroupRelease(unused);
    wgpuBufferRelease(ignored);
    wgpuBindGroupRelease(active);
    wgpuBindGroupRelease(empty_group);
    wgpuBindGroupLayoutRelease(layout);
    wgpuBindGroupLayoutRelease(hole);
    wgpuComputePipelineRelease(compute);
    wgpuBufferRelease(output);
    submit(s,encoder);
    const uint32_t *words = map(s,readback,8);
    assert(words[0] == 26 && words[1] == 0);
    wgpuBufferUnmap(readback);
    wgpuBufferRelease(readback);
}

static void dynamic_aliases(state *s)
{
    WGPULimits limits = WGPU_LIMITS_INIT;
    assert(wgpuDeviceGetLimits(s->device,&limits) == WGPUStatus_Success);
    uint32_t stride = limits.minStorageBufferOffsetAlignment;
    if (stride < 8) stride = 8;
    WGPUBindGroupLayoutEntry entries[2] = {
        {.binding=0,.visibility=WGPUShaderStage_Compute,
            .buffer={.type=WGPUBufferBindingType_Storage,.hasDynamicOffset=WGPU_TRUE}},
        {.binding=1,.visibility=WGPUShaderStage_Compute,
            .buffer={.type=WGPUBufferBindingType_Storage,.hasDynamicOffset=WGPU_TRUE}}
    };
    WGPUBindGroupLayoutDescriptor layout_desc = {.entryCount=2,.entries=entries};
    WGPUBindGroupLayout layout = wgpuDeviceCreateBindGroupLayout(s->device,&layout_desc);
    WGPUPipelineLayoutDescriptor pipeline_layout_desc = {.bindGroupLayoutCount=1,.bindGroupLayouts=&layout};
    WGPUPipelineLayout pipeline_layout = wgpuDeviceCreatePipelineLayout(s->device,&pipeline_layout_desc);
    const char *code = "@group(0) @binding(0) var<storage,read_write> a:array<u32>;"
        "@group(0) @binding(1) var<storage,read_write> b:array<u32>;"
        "@compute @workgroup_size(1) fn main(){a[0]=17u;b[0]=29u;}";
    WGPUShaderSourceWGSL source = WGPU_SHADER_SOURCE_WGSL_INIT;
    source.code = (WGPUStringView){code,WGPU_STRLEN};
    WGPUShaderModuleDescriptor shader_desc = {.nextInChain=&source.chain};
    WGPUShaderModule shader = wgpuDeviceCreateShaderModule(s->device,&shader_desc);
    WGPUComputePipelineDescriptor pipeline_desc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
    pipeline_desc.layout = pipeline_layout;
    pipeline_desc.compute.module = shader;
    WGPUComputePipeline compute = wgpuDeviceCreateComputePipeline(s->device,&pipeline_desc);
    assert(layout && pipeline_layout && shader && compute);
    WGPUBuffer data = buffer(s,(uint64_t)stride+8,WGPUBufferUsage_Storage|WGPUBufferUsage_CopySrc);
    WGPUBindGroupEntry bindings[2] = {{.binding=0,.buffer=data,.size=8},{.binding=1,.buffer=data,.size=8}};
    WGPUBindGroupDescriptor group_desc = {.layout=layout,.entryCount=2,.entries=bindings};
    WGPUBindGroup bound = wgpuDeviceCreateBindGroup(s->device,&group_desc);
    assert(bound);
    WGPUBuffer readback = buffer(s,8,WGPUBufferUsage_CopyDst|WGPUBufferUsage_MapRead);
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device,NULL);
    WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder,NULL);
    wgpuComputePassEncoderSetPipeline(pass,compute);
    uint32_t offsets[2] = {0,stride};
    wgpuComputePassEncoderSetBindGroup(pass,0,bound,2,offsets);
    wgpuComputePassEncoderDispatchWorkgroups(pass,1,1,1);
    wgpuComputePassEncoderEnd(pass);
    wgpuComputePassEncoderRelease(pass);
    wgpuCommandEncoderCopyBufferToBuffer(encoder,data,0,readback,0,4);
    wgpuCommandEncoderCopyBufferToBuffer(encoder,data,stride,readback,4,4);
    submit(s,encoder);
    const uint32_t *words = map(s,readback,8);
    assert(words[0] == 17 && words[1] == 29);
    wgpuBufferUnmap(readback);
    /* The same declared bindings overlap only after effective offsets change. */
    wgpuDevicePushErrorScope(s->device,WGPUErrorFilter_Validation);
    encoder = wgpuDeviceCreateCommandEncoder(s->device,NULL);
    pass = wgpuCommandEncoderBeginComputePass(encoder,NULL);
    offsets[1] = 0;
    wgpuComputePassEncoderSetPipeline(pass,compute);
    wgpuComputePassEncoderSetBindGroup(pass,0,bound,2,offsets);
    wgpuComputePassEncoderDispatchWorkgroups(pass,1,1,1);
    wgpuComputePassEncoderEnd(pass);
    assert(!wgpuCommandEncoderFinish(encoder,NULL));
    wgpuComputePassEncoderRelease(pass);
    wgpuCommandEncoderRelease(encoder);
    expect_validation(s);
    wgpuBindGroupRelease(bound);
    wgpuComputePipelineRelease(compute);
    wgpuShaderModuleRelease(shader);
    wgpuPipelineLayoutRelease(pipeline_layout);
    wgpuBindGroupLayoutRelease(layout);
    wgpuBufferRelease(data);
    wgpuBufferRelease(readback);
}

static void readonly_alias(state *s)
{
    WGPULimits limits = WGPU_LIMITS_INIT;
    assert(wgpuDeviceGetLimits(s->device,&limits) == WGPUStatus_Success);
    uint32_t stride = limits.minStorageBufferOffsetAlignment;
    if (stride < 4) stride = 4;
    WGPUBuffer data = buffer(s,(uint64_t)stride+4,WGPUBufferUsage_Storage);
    WGPUComputePipeline compute = pipeline(s,
        "@group(0) @binding(0) var<storage,read_write> a:array<u32>;"
        "@group(0) @binding(1) var<storage,read> b:array<u32>;"
        "@compute @workgroup_size(1) fn main(){a[0]=b[0];}");
    WGPUBindGroupEntry bindings[2] = {{.binding=0,.buffer=data,.size=4},
        {.binding=1,.buffer=data,.offset=stride,.size=4}};
    WGPUBindGroup bound = group(s,compute,bindings,2);
    wgpuDevicePushErrorScope(s->device,WGPUErrorFilter_Validation);
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device,NULL);
    dispatch(encoder,compute,bound);
    assert(!wgpuCommandEncoderFinish(encoder,NULL));
    wgpuCommandEncoderRelease(encoder);
    expect_validation(s); /* Read/write + readonly is whole-buffer usage conflict. */
    wgpuBindGroupRelease(bound);
    wgpuComputePipelineRelease(compute);
    wgpuBufferRelease(data);
}

static void image_aliases(state *s)
{
    WGPUTexture image = texture(s,WGPUTextureFormat_RGBA8Unorm,
        WGPUTextureUsage_TextureBinding|WGPUTextureUsage_StorageBinding|WGPUTextureUsage_CopySrc,2);
    WGPUTextureViewDescriptor view_desc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    view_desc.mipLevelCount = 1;
    WGPUTextureView sampled = wgpuTextureCreateView(image,&view_desc);
    WGPUTextureView storage = wgpuTextureCreateView(image,&view_desc);
    WGPUComputePipeline compute = pipeline(s,
        "@group(0) @binding(0) var src:texture_2d<f32>;"
        "@group(0) @binding(1) var dst:texture_storage_2d<rgba8unorm,write>;"
        "@compute @workgroup_size(1) fn main(@builtin(global_invocation_id) id:vec3<u32>){"
        "if(id.x==0u){textureStore(dst,vec2<i32>(0),textureLoad(src,vec2<i32>(0),0));}}");
    WGPUBindGroupEntry bindings[2] = {{.binding=0,.textureView=sampled},{.binding=1,.textureView=storage}};
    WGPUBindGroup bound = group(s,compute,bindings,2);
    wgpuDevicePushErrorScope(s->device,WGPUErrorFilter_Validation);
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device,NULL);
    dispatch(encoder,compute,bound);
    assert(!wgpuCommandEncoderFinish(encoder,NULL));
    wgpuCommandEncoderRelease(encoder);
    expect_validation(s);
    wgpuBindGroupRelease(bound);
    wgpuTextureViewRelease(storage);
    view_desc.baseMipLevel = 1;
    storage = wgpuTextureCreateView(image,&view_desc);
    bindings[1].textureView = storage;
    bound = group(s,compute,bindings,2);
    encoder = wgpuDeviceCreateCommandEncoder(s->device,NULL);
    /* Disjoint mip subresources of the same texture are valid. */
    dispatch(encoder,compute,bound);
    WGPUBuffer readback = buffer(s,256,WGPUBufferUsage_CopyDst|WGPUBufferUsage_MapRead);
    texture_to_buffer(encoder,image,readback,1,1,1);
    wgpuBindGroupRelease(bound);
    wgpuTextureViewRelease(storage);
    wgpuTextureViewRelease(sampled);
    wgpuTextureRelease(image);
    wgpuComputePipelineRelease(compute);
    submit(s,encoder);
    const unsigned char *bytes = map(s,readback,256);
    for (size_t i = 0; i < 256; ++i) assert(bytes[i] == 0);
    wgpuBufferUnmap(readback);
    wgpuBufferRelease(readback);
}

int main(void)
{
    state s = {0};
    s.instance = wgpuCreateInstance(NULL);
    WGPURequestAdapterOptions options = WGPU_REQUEST_ADAPTER_OPTIONS_INIT;
    options.forceFallbackAdapter = WGPU_TRUE;
    WGPURequestAdapterCallbackInfo adapter = {.mode=WGPUCallbackMode_AllowSpontaneous,.callback=adapter_reply,.userdata1=&s};
    assert(wgpuInstanceRequestAdapter(s.instance,&options,adapter).id && s.adapter);
    WGPUAdapterInfo info = WGPU_ADAPTER_INFO_INIT;
    assert(wgpuAdapterGetInfo(s.adapter,&info) == WGPUStatus_Success && info.adapterType == WGPUAdapterType_CPU);
    wgpuAdapterInfoFreeMembers(info);
    WGPUDeviceDescriptor descriptor = WGPU_DEVICE_DESCRIPTOR_INIT;
    descriptor.uncapturedErrorCallbackInfo = (WGPUUncapturedErrorCallbackInfo){.callback=error_reply,.userdata1=&s};
    WGPURequestDeviceCallbackInfo requested = {.mode=WGPUCallbackMode_AllowSpontaneous,.callback=device_reply,.userdata1=&s};
    assert(wgpuAdapterRequestDevice(s.adapter,&descriptor,requested).id && s.device);
    s.queue = wgpuDeviceGetQueue(s.device);
    sparse_groups(&s);
    dynamic_aliases(&s);
    readonly_alias(&s);
    image_aliases(&s);
    assert(!s.errors && s.validation_errors == 4);
    wgpuQueueRelease(s.queue);
    wgpuDeviceRelease(s.device);
    wgpuAdapterRelease(s.adapter);
    wgpuInstanceRelease(s.instance);
    puts("PASS public admission: sparse auto-layout holes, null unbind, ignored destroyed resources, dynamic writable ranges, whole-buffer read/write conflicts and texture subresource aliases (CPU Vulkan only)");
}
