/* SPDX-License-Identifier: MIT */
#ifndef DOE_NATIVE_TEST_METAL_STORAGE_CASES_H
#define DOE_NATIVE_TEST_METAL_STORAGE_CASES_H
static void metal_storage_case(state *s, const char *code, bool admitted, bool valid_module)
{
    WGPUShaderSourceWGSL source = WGPU_SHADER_SOURCE_WGSL_INIT;
    source.code = (WGPUStringView){code,WGPU_STRLEN};
    WGPUShaderModuleDescriptor shader_descriptor = {.nextInChain=&source.chain};
    wgpuDevicePushErrorScope(s->device,WGPUErrorFilter_Validation);
    WGPUShaderModule module = wgpuDeviceCreateShaderModule(s->device, &shader_descriptor);
    assert(module);
    pop(s, valid_module ? WGPUErrorType_NoError : WGPUErrorType_Validation);
    WGPUColorTargetState color = {.format=WGPUTextureFormat_RGBA8Unorm, .writeMask=WGPUColorWriteMask_All};
    WGPUFragmentState fragment = {.module=module, .targetCount=1, .targets=&color};
    WGPURenderPipelineDescriptor descriptor = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    descriptor.vertex.module = module;
    descriptor.fragment = &fragment;
    native_device *device = (native_device *)s->device;
    /* Poison only the inactive union representation's trailing storage: no
     * Metal member changes, and no inactive semantic arm is read. A missing
     * backend tag gate would mistake this byte for Vulkan feature support. */
    size_t offset = offsetof(native_backend, value.vulkan.caps.fragment_stores_and_atomics) -
        offsetof(native_backend, value);
    assert(offset >= sizeof(device->backend.value.metal) && offset < sizeof(device->backend.value));
    native_lock(device);
    unsigned char *representation = (unsigned char *)&device->backend.value;
    unsigned char original = representation[offset];
    representation[offset] = 1;
    native_unlock(device);
    unsigned created = mocks[2].created;
    wgpuDevicePushErrorScope(s->device, WGPUErrorFilter_Validation);
    WGPURenderPipeline pipeline = wgpuDeviceCreateRenderPipeline(s->device, &descriptor);
    assert((pipeline != NULL) == admitted);
    wgpuRenderPipelineRelease(pipeline);
    native_lock(device);
    representation[offset] = original;
    native_unlock(device);
    pop(s, admitted ? WGPUErrorType_NoError : WGPUErrorType_Validation);
    assert(admitted ? mocks[2].created > created : mocks[2].created == created);
    wgpuShaderModuleRelease(module);
}

static void metal_storage_cases(state *s)
{
    const unsigned baseline = mocks[2].live;
    metal_storage_case(s,
        "@group(0) @binding(0) var<storage,read_write> data:array<u32>;"
        "@vertex fn vertex()->@builtin(position) vec4f{return vec4f(0,0,0,1);}"
        "@fragment fn fragment()->@location(0) vec4f{data[0]=7u;return vec4f(1);}",true,true);
    metal_storage_case(s,
        "@group(0) @binding(0) var image:texture_storage_2d<rgba8unorm,write>;"
        "@vertex fn vertex()->@builtin(position) vec4f{return vec4f(0,0,0,1);}"
        "@fragment fn fragment()->@location(0) vec4f{textureStore(image,vec2i(0),vec4f(1));return vec4f(1);}",false,true);
    metal_storage_case(s,
        "@group(0) @binding(0) var<storage,read_write> data:array<u32>;"
        "@vertex fn vertex()->@builtin(position) vec4f{data[0]=7u;return vec4f(0,0,0,1);}"
        "@fragment fn fragment()->@location(0) vec4f{return vec4f(1);}",false,false);
    assert(mocks[2].live == baseline && !s->errors);
}
#endif
