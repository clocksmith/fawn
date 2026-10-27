/* SPDX-License-Identifier: MIT
 * Public external lifecycle/view ownership; native import is separately tested
 * at the Metal typed-call boundary. No fabricated platform handles are used. */
#include "consumer.h"
#include "doe_native_external_texture.h"
#include "doe_native_legacy.h"

static WGPUBindGroupLayout external_layout(state *s)
{
    WGPUExternalTextureBindingLayout external = WGPU_EXTERNAL_TEXTURE_BINDING_LAYOUT_INIT;
    WGPUBindGroupLayoutEntry entry = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
    entry.nextInChain = &external.chain;
    entry.binding = 3;
    entry.visibility = WGPUShaderStage_Compute;
    WGPUBindGroupLayoutDescriptor desc = {.entryCount=1,.entries=&entry};
    WGPUBindGroupLayout layout = wgpuDeviceCreateBindGroupLayout(s->device, &desc);
    assert(layout);
    return layout;
}

static WGPUBindGroup bind_external(state *s, WGPUBindGroupLayout layout, WGPUExternalTexture texture)
{
    WGPUExternalTextureBindingEntry external = WGPU_EXTERNAL_TEXTURE_BINDING_ENTRY_INIT;
    external.externalTexture = texture;
    WGPUBindGroupEntry entry = WGPU_BIND_GROUP_ENTRY_INIT;
    entry.binding = 3;
    entry.nextInChain = &external.chain;
    WGPUBindGroupDescriptor desc = {.layout=layout,.entryCount=1,.entries=&entry};
    return wgpuDeviceCreateBindGroup(s->device, &desc);
}

static void browser_errors(state *s)
{
    WGPUTexture input = texture(s, WGPUTextureFormat_RGBA8Unorm, WGPUTextureUsage_TextureBinding, 1);
    WGPUTextureView view = wgpuTextureCreateView(input, NULL);
    WGPUBindGroupLayout layout = external_layout(s);
    static const float transfer[7] = {1,1,0,1,0,0,0};
    static const float gamut[9] = {1,0,0,0,1,0,0,0,1};
    for (unsigned i = 0; i < 8; ++i) {
        WGPUExternalTextureDescriptor desc = {.plane0=view,.cropSize={2,2},.apparentSize={2,2},
            .srcTransferFunctionParameters=transfer,.dstTransferFunctionParameters=transfer,
            .gamutConversionMatrix=gamut,.rotation=WGPUExternalTextureRotation_Rotate0Degrees};
        float changed[9];
        memcpy(changed, gamut, sizeof(changed));
        switch (i) {
        case 0: desc.mirrored = WGPU_TRUE; break;
        case 1: desc.rotation = WGPUExternalTextureRotation_Rotate90Degrees; break;
        case 2: desc.cropSize.width = 1; break;
        case 3: desc.apparentSize.height = 4; break;
        case 4: desc.srcTransferFunctionParameters = NULL; break;
        case 5: changed[0] = 0.5f; desc.gamutConversionMatrix = changed; break;
        case 6: desc.plane1 = view; break;
        case 7: desc.plane0 = NULL; break;
        }
        wgpuDevicePushErrorScope(s->device, WGPUErrorFilter_Validation);
        WGPUExternalTexture error = wgpuDeviceCreateExternalTexture(s->device, &desc);
        assert(error); /* Dawn's browser wrapper requires a real error handle. */
        expect_validation(s);
        wgpuExternalTextureSetLabel(error, (WGPUStringView){"browser error",13});
        wgpuDevicePushErrorScope(s->device, WGPUErrorFilter_Validation);
        assert(!bind_external(s, layout, error));
        expect_validation(s);
        wgpuExternalTextureExpire(error);
        wgpuDevicePushErrorScope(s->device, WGPUErrorFilter_Validation);
        doeNativeExternalTextureRefresh(error);
        expect_validation(s);
        wgpuExternalTextureRelease(error);
    }
    wgpuBindGroupLayoutRelease(layout);
    wgpuTextureViewRelease(view);
    wgpuTextureRelease(input);
}

static void external_sampling(state *s, bool two_planes, bool conversion_bypassed)
{
    WGPUTexture input = texture(s, WGPUTextureFormat_RGBA8Unorm,
        WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst, 1);
    WGPUTextureView input_view = wgpuTextureCreateView(input,NULL);
    WGPUTexture chroma = two_planes ? texture(s,WGPUTextureFormat_RG8Unorm,WGPUTextureUsage_TextureBinding,1) : NULL;
    WGPUTextureView chroma_view = chroma ? wgpuTextureCreateView(chroma,NULL) : NULL;
    DoeExternalTextureDescriptor descriptor = {.plane0=input_view,.plane1=chroma_view};
    static const float transfer[7] = {1,1,0,1,0,0,0};
    static const float gamut[9] = {1,0,0,0,1,0,0,0,1};
    WGPUExternalTextureDescriptor browser = {.plane0=input_view,.cropSize={2,2},.apparentSize={2,2},
        .srcTransferFunctionParameters=transfer,.dstTransferFunctionParameters=transfer,
        .gamutConversionMatrix=gamut,.rotation=WGPUExternalTextureRotation_Rotate0Degrees};
    static const float nonidentity_transfer[7] = {2,1,0,1,0,0,0};
    static const float zero_gamut[9] = {0};
    if (conversion_bypassed) {
        /* Blink supplies real forward/inverse spans even when equal source and
         * destination spaces bypass conversion. These values must be ignored. */
        browser.doYuvToRgbConversionOnly = WGPU_TRUE;
        browser.srcTransferFunctionParameters = nonidentity_transfer;
        browser.dstTransferFunctionParameters = nonidentity_transfer;
        browser.gamutConversionMatrix = zero_gamut;
    }
    WGPUExternalTexture external = two_planes ? doeNativeDeviceCreateExternalTexture(s->device,&descriptor) :
        wgpuDeviceCreateExternalTexture(s->device,&browser);
    assert(external);
    const unsigned char pixels[] = {255,0,0,255,0,255,0,255,0,0,255,255,255,255,255,255};
    WGPUTexelCopyTextureInfo destination = {.texture=input};
    WGPUTexelCopyBufferLayout bytes = {.bytesPerRow=8,.rowsPerImage=2};
    WGPUExtent3D size = {2,2,1};
    wgpuQueueWriteTexture(s->queue,&destination,pixels,sizeof(pixels),&bytes,&size);
    WGPUComputePipeline compute = pipeline(s,
        "@group(0) @binding(3) var frame:texture_external;"
        "@group(0) @binding(0) var<storage,read_write> output:array<vec4f>;"
        "@compute @workgroup_size(1) fn main(){output[0]=textureLoad(frame,vec2i(0));}");
    WGPUBuffer output = buffer(s,16,WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc);
    WGPUBuffer readback = buffer(s,16,WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst);
    WGPUExternalTextureBindingEntry chained = WGPU_EXTERNAL_TEXTURE_BINDING_ENTRY_INIT;
    chained.externalTexture = external;
    WGPUBindGroupEntry entries[] = {{.binding=0,.buffer=output,.size=16},
        {.nextInChain=&chained.chain,.binding=3}};
    WGPUBindGroup bound = group(s,compute,entries,2);
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device,NULL);
    WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder,NULL);
    wgpuComputePassEncoderSetPipeline(pass,compute);
    wgpuComputePassEncoderSetBindGroup(pass,0,bound,0,NULL);
    wgpuComputePassEncoderDispatchWorkgroups(pass,1,1,1);
    wgpuComputePassEncoderEnd(pass);
    wgpuComputePassEncoderRelease(pass);
    wgpuCommandEncoderCopyBufferToBuffer(encoder,output,0,readback,0,16);
    wgpuExternalTextureRelease(external);
    wgpuTextureViewRelease(input_view);
    wgpuTextureViewRelease(chroma_view);
    wgpuTextureRelease(input);
    wgpuTextureRelease(chroma);
    wgpuBindGroupRelease(bound);
    wgpuComputePipelineRelease(compute);
    wgpuBufferRelease(output);
    submit(s,encoder);
    const float *actual = map(s,readback,16);
    assert(actual[0] == 1.0f && actual[1] == 0.0f && actual[2] == 0.0f && actual[3] == 1.0f);
    wgpuBufferUnmap(readback);
    wgpuBufferRelease(readback);
}

int main(void)
{
    state s = {0}, other = {0};
    consumer_init(&s);
    consumer_init(&other);
    WGPUTexture plane = texture(&s, WGPUTextureFormat_RGBA8Unorm, WGPUTextureUsage_TextureBinding, 1);
    WGPUTexture second_plane = texture(&s, WGPUTextureFormat_RG8Unorm, WGPUTextureUsage_TextureBinding, 1);
    WGPUTextureView view = wgpuTextureCreateView(plane, NULL);
    WGPUTextureView second_view = wgpuTextureCreateView(second_plane, NULL);
    assert(view && second_view);
    DoeExternalTextureDescriptor desc = {.label={"external planes",WGPU_STRLEN},.plane0=view};
    WGPUExternalTexture external = doeNativeDeviceCreateExternalTexture(s.device, &desc);
    assert(external);
    doeNativeExternalTextureAddRef(external);
    wgpuExternalTextureRelease(external);
    wgpuExternalTextureAddRef(external);
    doeNativeExternalTextureRelease(external);
    wgpuExternalTextureSetLabel(external, (WGPUStringView){"renamed",7});
    doeNativeExternalTextureSetLabel(external, "renamed again",13);
    WGPUBindGroupLayout layout = external_layout(&s);
    WGPUBindGroup bound = bind_external(&s, layout, external);
    assert(bound);
    doeNativeExternalTextureExpire(external);
    wgpuDevicePushErrorScope(s.device, WGPUErrorFilter_Validation);
    assert(!bind_external(&s, layout, external));
    expect_validation(&s);
    doeNativeExternalTextureRefresh(external);
    WGPUBindGroup refreshed = bind_external(&s, layout, external);
    assert(refreshed);
    wgpuBindGroupRelease(refreshed);
    doeNativeExternalTextureDestroy(external);
    wgpuDevicePushErrorScope(s.device, WGPUErrorFilter_Validation);
    assert(!bind_external(&s, layout, external));
    expect_validation(&s);
    doeNativeExternalTextureRefresh(external);
    wgpuDevicePushErrorScope(other.device, WGPUErrorFilter_Validation);
    assert(!doeNativeDeviceCreateExternalTexture(other.device, &desc));
    expect_validation(&other);
    desc.plane1 = second_view;
    WGPUExternalTexture two_planes = doeNativeDeviceImportExternalTexture(s.device, &desc);
    assert(two_planes);
    WGPUBindGroup two_bound = bind_external(&s, layout, two_planes);
    assert(two_bound);
    wgpuExternalTextureRelease(two_planes);
    wgpuBindGroupRelease(two_bound);
    DoeExternalTextureNativeDescriptor native = {.chain={.sType=(WGPUSType)DOE_NATIVE_STYPE_EXTERNAL_TEXTURE},
        .sourceType=DOE_NATIVE_EXTERNAL_IOSURFACE};
    DoeExternalTextureDescriptor native_desc = {.nextInChain=&native.chain};
    wgpuDevicePushErrorScope(s.device, WGPUErrorFilter_Validation);
    assert(!doeNativeDeviceImportExternalTexture(s.device, &native_desc));
    expect_validation(&s);
    /* A chain must not silently fall back to the otherwise usable plane. */
    desc.nextInChain = &native.chain;
    wgpuDevicePushErrorScope(s.device, WGPUErrorFilter_Validation);
    assert(!doeNativeDeviceCreateExternalTexture(s.device, &desc));
    expect_validation(&s);
    wgpuTextureViewRelease(view);
    wgpuTextureViewRelease(second_view);
    wgpuTextureRelease(second_plane);
    wgpuTextureDestroy(plane);
    wgpuDevicePushErrorScope(s.device, WGPUErrorFilter_Validation);
    doeNativeExternalTextureRefresh(external);
    expect_validation(&s);
    wgpuExternalTextureRelease(external);
    wgpuTextureRelease(plane);
    /* Group holds the external and its plane after all caller releases. */
    wgpuBindGroupRelease(bound);
    wgpuBindGroupLayoutRelease(layout);
    wgpuExternalTextureRelease(NULL);
    doeNativeExternalTextureRefresh(NULL);
    external_sampling(&s,false,false);
    external_sampling(&s,false,true);
    external_sampling(&s,true,false);
    browser_errors(&s);
    consumer_finish(&other);
    consumer_finish(&s);
    printf("PASS external textures: lifecycle, labels, chained binding, two-plane retention, device mismatch, no import fallback and exact plane-zero sample %s\n", adapter_class_scope());
}
