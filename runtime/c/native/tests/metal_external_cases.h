/* SPDX-License-Identifier: MIT */
#ifndef DOE_NATIVE_TEST_METAL_EXTERNAL_CASES_H
#define DOE_NATIVE_TEST_METAL_EXTERNAL_CASES_H
#include "doe_native_external_texture.h"

static doe_mtl_status external_import_ledger(void *userdata, void *native_device,
    doe_mtl_external_source source, void *handle, doe_mtl_native_external *out, doe_mtl_error *error)
{
    unsigned index = 0;
    while (index < context_count && userdata != &mocks[index]) ++index;
    assert(index < context_count && handle == userdata && source == DOE_MTL_EXTERNAL_IOSURFACE);
    doe_mtl_native_external result = {.count=2};
    for (uint32_t i = 0; i < 2; ++i) {
        result.desc[i] = (doe_mtl_texture_desc){.type=DOE_MTL_TEXTURE_2D,
            .format=i ? DOE_MTL_RG8_UNORM : DOE_MTL_R8_UNORM,.width=2,.height=2,
            .depth_or_layers=1,.mip_levels=1,.sample_count=1,.usage=DOE_MTL_TEXTURE_SAMPLED | DOE_MTL_TEXTURE_COPY_SRC};
        doe_mtl_status status = external_drivers[index].texture(userdata, native_device,
            &result.desc[i], &result.planes[i], error);
        if (status != DOE_MTL_OK) {
            for (uint32_t j = 0; j < i; ++j) external_drivers[index].release(userdata, result.planes[j]);
            return status;
        }
    }
    *out = result;
    return DOE_MTL_OK;
}

static void metal_external_import_cases(state *s)
{
    mock_native *mock = &mocks[2];
    unsigned baseline = mock->live;
    DoeExternalTextureNativeDescriptor native = {.chain={.sType=(WGPUSType)DOE_NATIVE_STYPE_EXTERNAL_TEXTURE},
        .sourceType=DOE_NATIVE_EXTERNAL_IOSURFACE,.handle=mock};
    DoeExternalTextureDescriptor desc = {.nextInChain=&native.chain};
    /* Failure after importing both planes and adopting one rolls everything
     * back through ordinary resource/Metal owners, preserving the first error. */
    mock->fail_view_at = mock->views + 2;
    wgpuDevicePushErrorScope(s->device, WGPUErrorFilter_Internal);
    assert(!doeNativeDeviceImportExternalTexture(s->device, &desc));
    pop(s,WGPUErrorType_Internal);
    assert(mock->live == baseline);
    mock->fail_view_at = 0;
    WGPUExternalTexture external = doeNativeDeviceImportExternalTexture(s->device, &desc);
    assert(external && mock->live == baseline + 4);
    doe_resource_external_texture *owner = (doe_resource_external_texture *)external;
    doe_resource_view *p0 = doe_resource_external_texture_plane(owner,0);
    doe_resource_view *p1 = doe_resource_external_texture_plane(owner,1);
    assert(p0 && p1 && doe_resource_view_device(p0) == (void *)s->device && doe_resource_view_device(p1) == (void *)s->device);
    native_view *first = doe_resource_view_native(p0);
    void *identity = doe_mtl_texture_view_native(first->metal);
    WGPUComputePipeline compute = pipeline(s,
        "@group(0) @binding(3) var frame:texture_external;"
        "@group(0) @binding(0) var<storage,read_write> output:array<vec4f>;"
        "@compute @workgroup_size(1) fn main(){output[0]=textureLoad(frame,vec2i(0));}");
    native_pipeline *pipeline_owner = doe_resource_pipeline_native((doe_resource_pipeline *)compute);
    uint32_t slot = UINT32_MAX;
    for (size_t i = 0; i < pipeline_owner->binding_count; ++i)
        if (pipeline_owner->bindings[i].binding == 3) {
            assert(pipeline_owner->bindings[i].kind == DOE_BIND_EXTERNAL_TEXTURE);
            slot = pipeline_owner->metal_compute->slots[i];
        }
    assert(slot != UINT32_MAX);
    WGPUBuffer output = buffer(s,16,WGPUBufferUsage_Storage);
    WGPUExternalTextureBindingEntry chained = WGPU_EXTERNAL_TEXTURE_BINDING_ENTRY_INIT;
    chained.externalTexture = external;
    WGPUBindGroupEntry entries[] = {{.binding=0,.buffer=output,.size=16},
        {.nextInChain=&chained.chain,.binding=3}};
    WGPUBindGroup bound = group(s,compute,entries,2);
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device,NULL);
    dispatch(encoder,compute,bound);
    wgpuBindGroupRelease(bound);
    wgpuComputePipelineRelease(compute);
    wgpuBufferRelease(output);
    wgpuExternalTextureRelease(external);
    native_device *device = (native_device *)s->device;
    native_lock(device);
    hold = true;
    native_unlock(device);
    wgpuDevicePushErrorScope(s->device,WGPUErrorFilter_Internal);
    submit(s,encoder);
    pop(s,WGPUErrorType_Internal);
    native_lock(device);
    assert(device->pending && mock->resources.textures[slot] == identity && mock->live > baseline);
    unsigned released = mock->releases_by_kind[MOCK_KIND_TEXTURE];
    hold = false;
    native_queue_settlement settled = native_settle_pending(device);
    assert(settled.complete && settled.failure.status == NATIVE_OK && !device->pending);
    native_unlock(device);
    assert(mock->releases_by_kind[MOCK_KIND_TEXTURE] == released + 2 && mock->live == baseline);
    puts("PASS native Metal external import: transactional resource adoption, canonical plane-zero binding and both-plane pending retention; host ledger only");
}
#endif
