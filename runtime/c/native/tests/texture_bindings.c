/* SPDX-License-Identifier: MIT
 * Public sampled/storage/read-alias/runtime-sampler compute consumer. */
#include "consumer.h"
#include "sampling_pair_cases.h"

static WGPUTexture image(state *s,WGPUTextureFormat format,uint32_t height,WGPUTextureUsage usage)
{
    WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    desc.size = (WGPUExtent3D){4,height,1};
    desc.format = format;
    desc.usage = usage;
    WGPUTexture result = wgpuDeviceCreateTexture(s->device,&desc);
    assert(result);
    return result;
}
static void write_pixels(state *s,WGPUTexture texture,const void *bytes,size_t size)
{
    WGPUTexelCopyTextureInfo to = {.texture=texture};
    WGPUTexelCopyBufferLayout layout = {.bytesPerRow=16,.rowsPerImage=1};
    WGPUExtent3D extent = {4,1,1};
    wgpuQueueWriteTexture(s->queue,&to,bytes,size,&layout,&extent);
}
static void sampler_pixels(state *s)
{
    WGPUComputePipeline program = pipeline(s,
        "@group(0) @binding(0) var input_image:texture_2d<f32>;"
        "@group(0) @binding(1) var sampling:sampler;"
        "@group(0) @binding(2) var image_output:texture_storage_2d<r32uint,write>;"
        "@group(0) @binding(3) var<uniform> params:vec4u;"
        "@group(0) @binding(4) var<storage,read_write> output_values:array<u32>;"
        "@compute @workgroup_size(1) fn main(@builtin(global_invocation_id) id:vec3u,"
        "@builtin(num_workgroups) count:vec3u){if(id.x<4u){"
        "let color=textureSampleLevel(input_image,sampling,vec2f((f32(id.x)+1.0)/4.0,0.5),0.0);"
        "let value=u32(color.x*255.0+0.5);output_values[params.x+id.x]=value;"
        "output_values[8u+params.x+id.x]=count.x;"
        "textureStore(image_output,vec2i(i32(id.x),i32(params.x/4u)),vec4u(value,0u,0u,1u));}}");
    WGPUTexture sampled = image(s,WGPUTextureFormat_RGBA8Unorm,1,WGPUTextureUsage_TextureBinding|WGPUTextureUsage_CopyDst);
    WGPUTexture stored = image(s,WGPUTextureFormat_R32Uint,2,WGPUTextureUsage_StorageBinding|WGPUTextureUsage_CopySrc);
    WGPUTextureView sampled_view = wgpuTextureCreateView(sampled,NULL), stored_view = wgpuTextureCreateView(stored,NULL);
    assert(sampled_view && stored_view);
    const unsigned char pixels[16] = {10,0,0,255,40,0,0,255,80,0,0,255,160,0,0,255};
    write_pixels(s,sampled,pixels,sizeof(pixels));
    WGPUSamplerDescriptor sampler_desc = WGPU_SAMPLER_DESCRIPTOR_INIT;
    WGPUSampler nearest = wgpuDeviceCreateSampler(s->device,&sampler_desc);
    sampler_desc.minFilter = sampler_desc.magFilter = WGPUFilterMode_Linear;
    WGPUSampler linear = wgpuDeviceCreateSampler(s->device,&sampler_desc);
    assert(nearest && linear);
    WGPUBuffer uniform = buffer(s,512,WGPUBufferUsage_Uniform|WGPUBufferUsage_CopyDst);
    WGPUBuffer output = buffer(s,64,WGPUBufferUsage_Storage|WGPUBufferUsage_CopySrc);
    WGPUBuffer readback = buffer(s,768,WGPUBufferUsage_MapRead|WGPUBufferUsage_CopyDst);
    WGPUBuffer arguments = buffer(s,12,WGPUBufferUsage_Indirect|WGPUBufferUsage_CopyDst);
    const uint32_t first[4] = {0}, second[4] = {4}, groups[3] = {4,1,1};
    wgpuQueueWriteBuffer(s->queue,uniform,0,first,sizeof(first));
    wgpuQueueWriteBuffer(s->queue,uniform,256,second,sizeof(second));
    wgpuQueueWriteBuffer(s->queue,arguments,0,groups,sizeof(groups));
    WGPUBindGroupEntry entries[5] = {{.binding=0,.textureView=sampled_view},{.binding=1,.sampler=nearest},
        {.binding=2,.textureView=stored_view},{.binding=3,.buffer=uniform,.size=16},
        {.binding=4,.buffer=output,.size=64}};
    WGPUBindGroup first_group = group(s,program,entries,5);
    entries[1].sampler = linear;
    entries[3].offset = 256;
    WGPUBindGroup second_group = group(s,program,entries,5);
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device,NULL);
    WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder,NULL);
    wgpuComputePassEncoderSetPipeline(pass,program);
    wgpuComputePassEncoderSetBindGroup(pass,0,first_group,0,NULL);
    wgpuComputePassEncoderDispatchWorkgroups(pass,4,1,1);
    wgpuComputePassEncoderSetBindGroup(pass,0,second_group,0,NULL);
    wgpuComputePassEncoderDispatchWorkgroupsIndirect(pass,arguments,0);
    wgpuComputePassEncoderEnd(pass);
    wgpuComputePassEncoderRelease(pass);
    texture_to_buffer(encoder,stored,readback,0,4,2);
    wgpuCommandEncoderCopyBufferToBuffer(encoder,output,0,readback,512,64);
    wgpuComputePipelineRelease(program);
    wgpuBindGroupRelease(first_group);
    wgpuBindGroupRelease(second_group);
    wgpuTextureViewRelease(sampled_view);
    wgpuTextureViewRelease(stored_view);
    wgpuTextureRelease(sampled);
    wgpuTextureRelease(stored);
    wgpuSamplerRelease(nearest);
    wgpuSamplerRelease(linear);
    wgpuBufferRelease(uniform);
    wgpuBufferRelease(output);
    wgpuBufferRelease(arguments);
    submit(s,encoder);
    const uint32_t *actual = map(s,readback,768);
    const uint32_t expected[8] = {40,80,160,160,25,60,120,160};
    for (size_t i = 0; i < 8; ++i) {
        assert(actual[i < 4 ? i : 64 + i - 4] == expected[i]);
        assert(actual[128+i] == expected[i] && actual[136+i] == 4);
    }
    for (size_t i = 0; i < 192; ++i)
        if (!(i < 4 || (i >= 64 && i < 68) || (i >= 128 && i < 144))) assert(actual[i] == 0);
    wgpuBufferUnmap(readback);
    wgpuBufferRelease(readback);
}
static void read_aliases(state *s)
{
    WGPUComputePipeline program = pipeline(s,
        "@group(0) @binding(0) var sampled:texture_2d<u32>;"
        "@group(0) @binding(1) var input_image:texture_storage_2d<r32uint,read>;"
        "@group(0) @binding(2) var storage_only:texture_storage_2d<r32uint,read>;"
        "@group(0) @binding(3) var<storage,read_write> output_values:array<u32>;"
        "@compute @workgroup_size(1) fn main(@builtin(global_invocation_id) id:vec3u){if(id.x<4u){"
        "let a=textureLoad(sampled,vec2i(i32(id.x),0),0);let b=textureLoad(input_image,vec2i(i32(id.x),0));"
        "let c=textureLoad(storage_only,vec2i(i32(id.x),0));output_values[id.x*4u]=a.x+b.x;"
        "output_values[id.x*4u+1u]=a.y+a.z+a.w;output_values[id.x*4u+2u]=b.y+b.z+b.w;"
        "output_values[id.x*4u+3u]=c.x+textureDimensions(input_image).x;}}");
    WGPUTexture alias = image(s,WGPUTextureFormat_R32Uint,1,
        WGPUTextureUsage_TextureBinding|WGPUTextureUsage_StorageBinding|WGPUTextureUsage_CopyDst);
    WGPUTexture only_storage = image(s,WGPUTextureFormat_R32Uint,1,WGPUTextureUsage_StorageBinding|WGPUTextureUsage_CopyDst);
    assert(!(wgpuTextureGetUsage(only_storage) & WGPUTextureUsage_TextureBinding));
    const uint32_t data[4] = {3,5,7,11}, other[4] = {13,17,19,23};
    write_pixels(s,alias,data,sizeof(data));
    write_pixels(s,only_storage,other,sizeof(other));
    WGPUTextureView alias_view = wgpuTextureCreateView(alias,NULL), other_view = wgpuTextureCreateView(only_storage,NULL);
    assert(alias_view && other_view);
    WGPUBuffer output = buffer(s,64,WGPUBufferUsage_Storage|WGPUBufferUsage_CopySrc);
    WGPUBuffer readback = buffer(s,68,WGPUBufferUsage_MapRead|WGPUBufferUsage_CopyDst);
    WGPUBindGroupEntry entries[4] = {{.binding=0,.textureView=alias_view},{.binding=1,.textureView=alias_view},
        {.binding=2,.textureView=other_view},{.binding=3,.buffer=output,.size=64}};
    WGPUBindGroup bindings = group(s,program,entries,4);
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device,NULL);
    dispatch(encoder,program,bindings);
    wgpuCommandEncoderCopyBufferToBuffer(encoder,output,0,readback,0,64);
    wgpuComputePipelineRelease(program);
    wgpuBindGroupRelease(bindings);
    wgpuTextureViewRelease(alias_view);
    wgpuTextureViewRelease(other_view);
    wgpuTextureRelease(alias);
    wgpuTextureRelease(only_storage);
    wgpuBufferRelease(output);
    submit(s,encoder);
    const uint32_t *actual = map(s,readback,68);
    for (size_t i = 0; i < 4; ++i) {
        assert(actual[i*4] == 2*data[i] && actual[i*4+1] == 1 && actual[i*4+2] == 1);
        assert(actual[i*4+3] == other[i]+4);
    }
    assert(actual[16] == 0);
    wgpuBufferUnmap(readback);
    wgpuBufferRelease(readback);
}
static void reject_write_alias(state *s)
{
    WGPUComputePipeline program = pipeline(s,
        "@group(0) @binding(0) var sampled:texture_2d<u32>;"
        "@group(0) @binding(1) var stored:texture_storage_2d<r32uint,write>;"
        "@compute @workgroup_size(1) fn main(){textureStore(stored,vec2i(0),textureLoad(sampled,vec2i(0),0));}");
    WGPUTexture shared = image(s,WGPUTextureFormat_R32Uint,1,WGPUTextureUsage_TextureBinding|WGPUTextureUsage_StorageBinding);
    WGPUTextureView view = wgpuTextureCreateView(shared,NULL);
    assert(view);
    const WGPUBindGroupEntry entries[2] = {{.binding=0,.textureView=view},{.binding=1,.textureView=view}};
    WGPUBindGroup bindings = group(s,program,entries,2);
    wgpuDevicePushErrorScope(s->device,WGPUErrorFilter_Validation);
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device,NULL);
    dispatch(encoder,program,bindings);
    finish_invalid(encoder);
    wgpuCommandEncoderRelease(encoder);
    expect_validation(s);
    wgpuBindGroupRelease(bindings);
    wgpuTextureViewRelease(view);
    wgpuTextureRelease(shared);
    wgpuComputePipelineRelease(program);
}
static void unrelated_sampler_pair(state *s)
{
    WGPUComputePipeline program = pipeline(s,
        "@group(0) @binding(0) var loaded:texture_2d<f32>;"
        "@group(0) @binding(1) var sampled:texture_2d<f32>;"
        "@group(0) @binding(2) var sampling:sampler;"
        "@group(0) @binding(3) var<storage,read_write> result:array<f32>;"
        "fn leaf(t:texture_2d<f32>,s:sampler)->f32{return textureSampleLevel(t,s,vec2f(0.5),0.0).r;}"
        "fn middle(s:sampler,t:texture_2d<f32>)->f32{return leaf(t,s);}"
        "@compute @workgroup_size(1) fn main(){result[0]=textureLoad(loaded,vec2i(0),0).r+middle(sampling,sampled);}");
    assert(program);
    WGPUTexture loaded = image(s,WGPUTextureFormat_R32Float,1,WGPUTextureUsage_TextureBinding|WGPUTextureUsage_CopyDst);
    WGPUTexture sampled = image(s,WGPUTextureFormat_RGBA8Unorm,1,WGPUTextureUsage_TextureBinding|WGPUTextureUsage_CopyDst);
    const float values[4] = {2,2,2,2};
    const unsigned char white[16] = {255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255};
    write_pixels(s,loaded,values,sizeof(values));
    write_pixels(s,sampled,white,sizeof(white));
    WGPUTextureView loaded_view = wgpuTextureCreateView(loaded,NULL);
    WGPUTextureView sampled_view = wgpuTextureCreateView(sampled,NULL);
    WGPUSamplerDescriptor sampler_desc = WGPU_SAMPLER_DESCRIPTOR_INIT;
    sampler_desc.minFilter = sampler_desc.magFilter = WGPUFilterMode_Linear;
    WGPUSampler sampling = wgpuDeviceCreateSampler(s->device,&sampler_desc);
    WGPUBuffer output = buffer(s,4,WGPUBufferUsage_Storage|WGPUBufferUsage_CopySrc);
    WGPUBuffer readback = buffer(s,4,WGPUBufferUsage_MapRead|WGPUBufferUsage_CopyDst);
    WGPUBindGroupEntry entries[4] = {{.binding=0,.textureView=loaded_view},{.binding=1,.textureView=sampled_view},
        {.binding=2,.sampler=sampling},{.binding=3,.buffer=output,.size=4}};
    WGPUBindGroup bindings = group(s,program,entries,4);
    assert(bindings);
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device,NULL);
    WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder,NULL);
    wgpuComputePassEncoderSetPipeline(pass,program);
    wgpuComputePassEncoderSetBindGroup(pass,0,bindings,0,NULL);
    wgpuComputePassEncoderDispatchWorkgroups(pass,1,1,1);
    wgpuComputePassEncoderEnd(pass);
    wgpuComputePassEncoderRelease(pass);
    wgpuCommandEncoderCopyBufferToBuffer(encoder,output,0,readback,0,4);
    wgpuBindGroupRelease(bindings);
    wgpuTextureViewRelease(loaded_view); wgpuTextureViewRelease(sampled_view);
    wgpuTextureRelease(loaded); wgpuTextureRelease(sampled);
    wgpuSamplerRelease(sampling); wgpuComputePipelineRelease(program); wgpuBufferRelease(output);
    submit(s,encoder);
    const float *actual = map(s,readback,4);
    assert(actual[0] == 3.0f);
    wgpuBufferUnmap(readback); wgpuBufferRelease(readback);
    assert(!s->errors);
}

int main(void)
{
    state s = {0};
    consumer_init(&s);
    unrelated_sampler_pair(&s);
    exact_pair_layouts(&s);
    sampler_pixels(&s);
    read_aliases(&s);
    reject_write_alias(&s);
    consumer_finish(&s);
    printf("PASS public sampled/storage textures: exact helper-call sampler pairs and unrelated load-only r32float, runtime nearest/linear changes, direct+guarded indirect, GPU num_workgroups, sampled/read-storage aliases, storage-only SRV usage, writable-alias rejection, exact pixels/padding and early releases %s\n",adapter_class_scope());
}
