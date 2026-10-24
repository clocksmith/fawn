/* SPDX-License-Identifier: MIT
 * Actual public C image compute on an explicit CPU Vulkan adapter only. */
#include "consumer.h"

static void view_defaults(state *s)
{
    WGPUTextureDescriptor descriptor = WGPU_TEXTURE_DESCRIPTOR_INIT;
    descriptor.format = WGPUTextureFormat_RGBA8Unorm;
    descriptor.size = (WGPUExtent3D){2,2,12};
    descriptor.mipLevelCount = 2;
    descriptor.usage = WGPUTextureUsage_TextureBinding;
    WGPUTexture image = wgpuDeviceCreateTexture(s->device,&descriptor);
    assert(image);
    WGPUTextureViewDescriptor view = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    view.mipLevelCount = 0;
    wgpuDevicePushErrorScope(s->device,WGPUErrorFilter_Validation);
    assert(!wgpuTextureCreateView(image,&view));
    expect_validation(s);
    view = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    view.arrayLayerCount = 0;
    wgpuDevicePushErrorScope(s->device,WGPUErrorFilter_Validation);
    assert(!wgpuTextureCreateView(image,&view));
    expect_validation(s);
    view = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    view.dimension = WGPUTextureViewDimension_2D;
    WGPUTextureView one_layer = wgpuTextureCreateView(image,&view);
    assert(one_layer); /* Explicit 2D defaults to one layer, not all twelve. */
    wgpuTextureViewRelease(one_layer);
    view.dimension = WGPUTextureViewDimension_Cube;
    WGPUTextureView six_layers = wgpuTextureCreateView(image,&view);
    assert(six_layers); /* Cube defaults to six, not all twelve. */
    wgpuTextureViewRelease(six_layers);
    view.dimension = WGPUTextureViewDimension_2DArray;
    WGPUTextureView remaining = wgpuTextureCreateView(image,&view);
    assert(remaining);
    wgpuTextureViewRelease(remaining);
    wgpuTextureRelease(image);
}

int main(void)
{
    state s = {0};
    s.instance = wgpuCreateInstance(NULL);
    assert(s.instance);
    WGPURequestAdapterOptions options = WGPU_REQUEST_ADAPTER_OPTIONS_INIT;
    adapter_class_options(&options);
    WGPURequestAdapterCallbackInfo adapter = {.mode=WGPUCallbackMode_AllowSpontaneous,.callback=adapter_reply,.userdata1=&s};
    assert(wgpuInstanceRequestAdapter(s.instance,&options,adapter).id && s.adapter);
    adapter_class_require(s.adapter);
    WGPUDeviceDescriptor device = WGPU_DEVICE_DESCRIPTOR_INIT;
    device.uncapturedErrorCallbackInfo = (WGPUUncapturedErrorCallbackInfo){.callback=error_reply,.userdata1=&s};
    WGPURequestDeviceCallbackInfo requested = {.mode=WGPUCallbackMode_AllowSpontaneous,.callback=device_reply,.userdata1=&s};
    assert(wgpuAdapterRequestDevice(s.adapter,&device,requested).id && s.device);
    s.queue = wgpuDeviceGetQueue(s.device);
    WGPUTexture image = texture(&s,WGPUTextureFormat_RGBA8Unorm,
        WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopySrc | WGPUTextureUsage_CopyDst,2);
    WGPUBuffer readback = buffer(&s,512,WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead);
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s.device,NULL);
    texture_to_buffer(encoder,image,readback,1,1,1);
    submit(&s,encoder);
    const unsigned char *zero = map(&s,readback,512);
    for (size_t i = 0; i < 512; ++i) assert(zero[i] == 0);
    wgpuBufferUnmap(readback);
    const unsigned char pixels[16] = {255,0,0,255, 0,255,0,255, 0,0,255,255, 255,255,255,255};
    WGPUTexelCopyTextureInfo destination = {.texture=image};
    WGPUTexelCopyBufferLayout data_layout = {.bytesPerRow=8,.rowsPerImage=2};
    WGPUExtent3D extent = {2,2,1};
    wgpuQueueWriteTexture(s.queue,&destination,pixels,sizeof(pixels),&data_layout,&extent);
    WGPUTextureView view = wgpuTextureCreateView(image,NULL);
    WGPUSamplerDescriptor sampler_desc = WGPU_SAMPLER_DESCRIPTOR_INIT;
    sampler_desc.minFilter = WGPUFilterMode_Linear;
    sampler_desc.magFilter = WGPUFilterMode_Linear;
    WGPUSampler sampler = wgpuDeviceCreateSampler(s.device,&sampler_desc);
    assert(view && sampler);
    const char *sample_code =
        "@group(0) @binding(0) var t:texture_2d<f32>;"
        "@group(0) @binding(1) var s:sampler;"
        "@group(0) @binding(2) var<storage,read_write> result:array<vec4<f32>>;"
        "@compute @workgroup_size(1) fn main(@builtin(global_invocation_id) id:vec3<u32>){"
        "var v=textureLoad(t,vec2<i32>(i32(id.x),0),0);"
        "if(id.x<2u){v+=textureSampleLevel(t,s,vec2<f32>((f32(id.x)+0.5)/2.0,0.25),0.0);}"
        "result[id.x]=v+textureLoad(t,vec2<i32>(0),-1);}";
    WGPUComputePipeline sampled_pipeline = pipeline(&s,sample_code);
    WGPUBuffer output = buffer(&s,64,WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc);
    WGPUBindGroupEntry entries[3] = {{.binding=0,.textureView=view},{.binding=1,.sampler=sampler},
        {.binding=2,.buffer=output,.size=64}};
    WGPUBindGroup sampled_group = group(&s,sampled_pipeline,entries,3);
    encoder = wgpuDeviceCreateCommandEncoder(s.device,NULL);
    dispatch(encoder,sampled_pipeline,sampled_group);
    wgpuCommandEncoderCopyBufferToBuffer(encoder,output,0,readback,0,64);
    wgpuTextureRelease(image);
    wgpuTextureViewRelease(view);
    wgpuSamplerRelease(sampler);
    wgpuBindGroupRelease(sampled_group);
    wgpuComputePipelineRelease(sampled_pipeline);
    wgpuBufferRelease(output);
    submit(&s,encoder);
    const float *values = map(&s,readback,512);
    const float expected[16] = {2,0,0,2, 0,2,0,2, 0,0,0,0, 0,0,0,0};
    assert(!memcmp(values,expected,sizeof(expected)));
    wgpuBufferUnmap(readback);

    image = texture(&s,WGPUTextureFormat_RGBA32Uint,
        WGPUTextureUsage_StorageBinding | WGPUTextureUsage_CopySrc,1);
    view = wgpuTextureCreateView(image,NULL);
    assert(view);
    WGPUComputePipeline storage_pipeline = pipeline(&s,
        "@group(0) @binding(0) var t:texture_storage_2d<rgba32uint,write>;"
        "@compute @workgroup_size(1) fn main(@builtin(global_invocation_id) id:vec3<u32>){"
        "textureStore(t,vec2<i32>(i32(id.x),0),vec4<u32>(id.x+9u,2u,3u,4u));}");
    WGPUBindGroupEntry storage_entry = {.binding=0,.textureView=view};
    WGPUBindGroup storage_group = group(&s,storage_pipeline,&storage_entry,1);
    encoder = wgpuDeviceCreateCommandEncoder(s.device,NULL);
    dispatch(encoder,storage_pipeline,storage_group);
    texture_to_buffer(encoder,image,readback,0,2,2);
    wgpuTextureRelease(image);
    wgpuTextureViewRelease(view);
    wgpuBindGroupRelease(storage_group);
    wgpuComputePipelineRelease(storage_pipeline);
    submit(&s,encoder);
    const uint32_t *words = map(&s,readback,512);
    const uint32_t expected_words[8] = {9,2,3,4,10,2,3,4};
    assert(!memcmp(words,expected_words,sizeof(expected_words)));
    for (size_t i = 64; i < 72; ++i) assert(words[i] == 0);
    wgpuBufferUnmap(readback);
    /* Float32 is unfilterable without an enabled feature, even if the physical
     * format advertises linear filtering. The resource owner admits this rule. */
    image = texture(&s,WGPUTextureFormat_R32Float,WGPUTextureUsage_TextureBinding,1);
    view = wgpuTextureCreateView(image,NULL);
    assert(view);
    WGPUBindGroupLayoutEntry layout_entries[3] = {
        {.binding=0,.visibility=WGPUShaderStage_Compute,.texture={.sampleType=WGPUTextureSampleType_Float}},
        {.binding=1,.visibility=WGPUShaderStage_Compute,.sampler={.type=WGPUSamplerBindingType_Filtering}},
        {.binding=2,.visibility=WGPUShaderStage_Compute,.buffer={.type=WGPUBufferBindingType_Storage}}};
    WGPUBindGroupLayoutDescriptor layout_desc = {.entryCount=1,.entries=layout_entries};
    WGPUBindGroupLayout layout = wgpuDeviceCreateBindGroupLayout(s.device,&layout_desc);
    assert(layout);
    WGPUBindGroupEntry float_entry = {.binding=0,.textureView=view};
    WGPUBindGroupDescriptor group_desc = {.layout=layout,.entryCount=1,.entries=&float_entry};
    wgpuDevicePushErrorScope(s.device,WGPUErrorFilter_Validation);
    assert(!wgpuDeviceCreateBindGroup(s.device,&group_desc));
    expect_validation(&s);
    wgpuBindGroupLayoutRelease(layout);
    layout_entries[0].texture.sampleType = WGPUTextureSampleType_UnfilterableFloat;
    layout = wgpuDeviceCreateBindGroupLayout(s.device,&layout_desc);
    group_desc.layout = layout;
    WGPUBindGroup unfilterable = wgpuDeviceCreateBindGroup(s.device,&group_desc);
    assert(unfilterable);
    wgpuBindGroupRelease(unfilterable);
    wgpuBindGroupLayoutRelease(layout);

    /* Selected closure has ambiguous unfilterable/Filtering pairing; fail until
     * compiler-owned exact pair metadata exists, never infer WGSL text here. */
    layout_desc.entryCount = 3;
    layout = wgpuDeviceCreateBindGroupLayout(s.device,&layout_desc);
    WGPUPipelineLayoutDescriptor pipeline_layout_desc = {.bindGroupLayoutCount=1,.bindGroupLayouts=&layout};
    WGPUPipelineLayout pipeline_layout = wgpuDeviceCreatePipelineLayout(s.device,&pipeline_layout_desc);
    assert(pipeline_layout);
    WGPUShaderSourceWGSL source = WGPU_SHADER_SOURCE_WGSL_INIT;
    source.code = (WGPUStringView){sample_code,WGPU_STRLEN};
    WGPUShaderModuleDescriptor shader_desc = {.nextInChain=&source.chain};
    WGPUShaderModule shader = wgpuDeviceCreateShaderModule(s.device,&shader_desc);
    WGPUComputePipelineDescriptor pipeline_desc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
    pipeline_desc.compute.module = shader;
    pipeline_desc.layout = pipeline_layout;
    wgpuDevicePushErrorScope(s.device,WGPUErrorFilter_Validation);
    assert(!wgpuDeviceCreateComputePipeline(s.device,&pipeline_desc));
    expect_validation(&s);
    wgpuShaderModuleRelease(shader);
    wgpuPipelineLayoutRelease(pipeline_layout);
    wgpuBindGroupLayoutRelease(layout);
    wgpuTextureDestroy(image);
    wgpuDevicePushErrorScope(s.device,WGPUErrorFilter_Validation);
    assert(!wgpuTextureCreateView(image,NULL));
    expect_validation(&s);
    wgpuTextureViewRelease(view);
    wgpuTextureRelease(image);

    /* The public command copy must keep 256-byte row alignment strict, unlike
     * QueueWriteTexture. A bad call poisons its canonical encoder. */
    image = texture(&s,WGPUTextureFormat_RGBA8Unorm,WGPUTextureUsage_CopySrc,1);
    encoder = wgpuDeviceCreateCommandEncoder(s.device,NULL);
    WGPUTexelCopyTextureInfo bad_source = {.texture=image};
    WGPUTexelCopyBufferInfo bad_destination = {.buffer=readback,.layout={.bytesPerRow=8,.rowsPerImage=2}};
    wgpuDevicePushErrorScope(s.device,WGPUErrorFilter_Validation);
    wgpuCommandEncoderCopyTextureToBuffer(encoder,&bad_source,&bad_destination,&extent);
    finish_invalid(encoder);
    expect_validation(&s);
    wgpuCommandEncoderRelease(encoder);
    wgpuTextureRelease(image);
    wgpuBufferRelease(readback);
    assert(!s.errors && s.validation_errors == 4);
    view_defaults(&s);
    assert(!s.errors && s.validation_errors == 6);
    wgpuQueueRelease(s.queue);
    wgpuDeviceRelease(s.device);
    wgpuAdapterRelease(s.adapter);
    wgpuInstanceRelease(s.instance);
    printf("PASS public textures: actual zero mips, packed upload, linear sampling, image OOB guards, storage readback, early releases, exact view defaults and filter/pair/copy negatives %s\n", adapter_class_scope());
}
