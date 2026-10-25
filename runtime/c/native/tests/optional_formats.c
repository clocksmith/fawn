/* SPDX-License-Identifier: MIT
 * Public requested-format admission and real core-vocabulary read/write use. */
#include "consumer.h"

static WGPUTexture create_texture(state *s,WGPUTextureFormat format,WGPUTextureUsage usage)
{
    WGPUTextureDescriptor descriptor=WGPU_TEXTURE_DESCRIPTOR_INIT;
    descriptor.dimension=WGPUTextureDimension_2D;
    descriptor.size=(WGPUExtent3D){2,2,1};
    descriptor.format=format;
    descriptor.usage=usage;
    descriptor.mipLevelCount=1;
    descriptor.sampleCount=1;
    return wgpuDeviceCreateTexture(s->device,&descriptor);
}

static WGPUBindGroupLayout read_write_layout(state *s,WGPUTextureFormat format)
{
    WGPUBindGroupLayoutEntry entry=WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
    entry.visibility=WGPUShaderStage_Compute;
    entry.storageTexture=(WGPUStorageTextureBindingLayout){.access=WGPUStorageTextureAccess_ReadWrite,
        .format=format,.viewDimension=WGPUTextureViewDimension_2D};
    WGPUBindGroupLayoutDescriptor descriptor={.entryCount=1,.entries=&entry};
    return wgpuDeviceCreateBindGroupLayout(s->device,&descriptor);
}

static void read_write_execution(state *s)
{
    WGPUTexture image=texture(s,WGPUTextureFormat_RGBA8Unorm,WGPUTextureUsage_StorageBinding,1);
    WGPUTextureView view=wgpuTextureCreateView(image,NULL);
    WGPUComputePipeline compute=pipeline(s,
        "@group(0) @binding(0) var image:texture_storage_2d<rgba8unorm,read_write>;"
        "@group(0) @binding(1) var<storage,read_write> output:vec4f;"
        "@compute @workgroup_size(1) fn main(){"
        "textureStore(image,vec2i(0),vec4f(1,0,1,1));output=textureLoad(image,vec2i(0));}");
    WGPUBuffer output=buffer(s,16,WGPUBufferUsage_Storage|WGPUBufferUsage_CopySrc);
    WGPUBuffer readback=buffer(s,16,WGPUBufferUsage_MapRead|WGPUBufferUsage_CopyDst);
    WGPUBindGroupEntry entries[]={{.binding=0,.textureView=view},{.binding=1,.buffer=output,.size=16}};
    WGPUBindGroup bindings=group(s,compute,entries,2);
    WGPUCommandEncoder encoder=wgpuDeviceCreateCommandEncoder(s->device,NULL);
    WGPUComputePassEncoder pass=wgpuCommandEncoderBeginComputePass(encoder,NULL);
    wgpuComputePassEncoderSetPipeline(pass,compute);
    wgpuComputePassEncoderSetBindGroup(pass,0,bindings,0,NULL);
    wgpuComputePassEncoderDispatchWorkgroups(pass,1,1,1);
    wgpuComputePassEncoderEnd(pass);
    wgpuComputePassEncoderRelease(pass);
    wgpuCommandEncoderCopyBufferToBuffer(encoder,output,0,readback,0,16);
    wgpuBindGroupRelease(bindings);
    wgpuTextureViewRelease(view);
    wgpuTextureRelease(image);
    wgpuComputePipelineRelease(compute);
    wgpuBufferRelease(output);
    submit(s,encoder);
    const float *actual=map(s,readback,16);
    assert(actual[0]==1 && actual[1]==0 && actual[2]==1 && actual[3]==1);
    wgpuBufferUnmap(readback);
    wgpuBufferRelease(readback);
}

int main(void)
{
    state baseline={0};
    consumer_init(&baseline);
    wgpuDevicePushErrorScope(baseline.device,WGPUErrorFilter_Validation);
    assert(!create_texture(&baseline,WGPUTextureFormat_BGRA8Unorm,WGPUTextureUsage_StorageBinding));
    expect_validation(&baseline);
    wgpuDevicePushErrorScope(baseline.device,WGPUErrorFilter_Validation);
    assert(!create_texture(&baseline,WGPUTextureFormat_R16Unorm,WGPUTextureUsage_TextureBinding));
    expect_validation(&baseline);
    wgpuDevicePushErrorScope(baseline.device,WGPUErrorFilter_Validation);
    assert(!read_write_layout(&baseline,WGPUTextureFormat_RGBA8Unorm));
    expect_validation(&baseline);
    WGPUBindGroupLayout ordinary=read_write_layout(&baseline,WGPUTextureFormat_R32Uint);
    assert(ordinary);
    wgpuBindGroupLayoutRelease(ordinary);
    const bool bgra=wgpuAdapterHasFeature(baseline.adapter,WGPUFeatureName_BGRA8UnormStorage);
    const bool tier2=wgpuAdapterHasFeature(baseline.adapter,WGPUFeatureName_TextureFormatsTier2);
    consumer_finish(&baseline);
    if (bgra) {
        state enabled={0};
        WGPUFeatureName feature=WGPUFeatureName_BGRA8UnormStorage;
        consumer_init_features(&enabled,&feature,1);
        WGPUTexture image=texture(&enabled,WGPUTextureFormat_BGRA8Unorm,WGPUTextureUsage_StorageBinding,1);
        assert(image);
        wgpuTextureRelease(image);
        consumer_finish(&enabled);
    }
    if (tier2) {
        state enabled={0};
        WGPUFeatureName feature=WGPUFeatureName_TextureFormatsTier2;
        consumer_init_features(&enabled,&feature,1);
        assert(wgpuDeviceHasFeature(enabled.device,WGPUFeatureName_TextureFormatsTier1));
        assert(wgpuDeviceHasFeature(enabled.device,WGPUFeatureName_RG11B10UfloatRenderable));
        WGPUTexture image=texture(&enabled,WGPUTextureFormat_R16Unorm,
            WGPUTextureUsage_StorageBinding|WGPUTextureUsage_RenderAttachment,1);
        assert(image);
        wgpuTextureRelease(image);
        wgpuDevicePushErrorScope(enabled.device,WGPUErrorFilter_Validation);
        assert(!read_write_layout(&enabled,WGPUTextureFormat_RG32Float));
        expect_validation(&enabled);
        read_write_execution(&enabled);
        consumer_finish(&enabled);
    }
    printf("PASS optional format admission; BGRA creation=%s, Tier2 rgba8 read/write=%s %s\n",
        bgra?"exercised":"unsupported",tier2?"exercised":"unsupported",adapter_class_scope());
    return 0;
}
