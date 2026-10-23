/* SPDX-License-Identifier: MIT
 * Fault injection at actual allocator/native call boundaries, no public hooks. */
#include "../src/internal.h"
#include "consumer.h"

static doe_vk_status injected_buffer_status = DOE_VK_OK;
static doe_vk_status injected_pipeline_status = DOE_VK_OK;
doe_vk_status __real_doe_vk_buffer_create(doe_vk_context *, uint64_t, uint32_t,
    doe_vk_memory_kind, doe_vk_buffer **, doe_vk_error *);
doe_vk_status __wrap_doe_vk_buffer_create(doe_vk_context *context, uint64_t size, uint32_t usage,
    doe_vk_memory_kind memory, doe_vk_buffer **out, doe_vk_error *error)
{
    if (injected_buffer_status != DOE_VK_OK) return injected_buffer_status;
    return __real_doe_vk_buffer_create(context,size,usage,memory,out,error);
}
doe_vk_status __real_doe_vk_pipeline_create(doe_vk_context *, const uint32_t *, size_t,
    const char *, const doe_vk_binding_layout *, size_t, doe_vk_pipeline **, doe_vk_error *);
doe_vk_status __wrap_doe_vk_pipeline_create(doe_vk_context *context, const uint32_t *words, size_t count,
    const char *entry, const doe_vk_binding_layout *bindings, size_t binding_count,
    doe_vk_pipeline **out, doe_vk_error *error)
{
    if (injected_pipeline_status != DOE_VK_OK) return injected_pipeline_status;
    return __real_doe_vk_pipeline_create(context,words,count,entry,bindings,binding_count,out,error);
}
doe_vk_status __real_doe_vk_render_pipeline_create(doe_vk_context *,
    const doe_vk_render_pipeline_desc *, doe_vk_pipeline **, doe_vk_error *);
doe_vk_status __wrap_doe_vk_render_pipeline_create(doe_vk_context *context,
    const doe_vk_render_pipeline_desc *descriptor, doe_vk_pipeline **out, doe_vk_error *error)
{
    if (injected_pipeline_status != DOE_VK_OK) return injected_pipeline_status;
    return __real_doe_vk_render_pipeline_create(context,descriptor,out,error);
}
static bool fail_normalizer;
doe_vk_status __real_doe_vk_timestamp_normalizer_create(doe_vk_context *,
    const doe_vk_timestamp_normalizer_desc *, doe_vk_timestamp_normalizer **, doe_vk_error *);
doe_vk_status __wrap_doe_vk_timestamp_normalizer_create(doe_vk_context *context,
    const doe_vk_timestamp_normalizer_desc *descriptor, doe_vk_timestamp_normalizer **out, doe_vk_error *error)
{
    if (fail_normalizer) return DOE_VK_OUT_OF_MEMORY;
    return __real_doe_vk_timestamp_normalizer_create(context,descriptor,out,error);
}
static void feature_failure(WGPURequestDeviceStatus status, WGPUDevice device,
    WGPUStringView message, void *userdata, void *unused)
{
    (void)message; (void)unused;
    assert(status == WGPURequestDeviceStatus_Error && !device);
    ++*(unsigned *)userdata;
}
static unsigned allocations_before_failure;
static void *fail_allocate(void *userdata, const void *vtable, size_t size, size_t alignment)
{
    if (!allocations_before_failure) return NULL;
    --allocations_before_failure;
    return native_allocate(userdata,vtable,size,alignment);
}
static void expect_type(WGPUPopErrorScopeStatus status, WGPUErrorType type,
    WGPUStringView message, void *userdata, void *unused)
{
    (void)message; (void)unused;
    assert(status == WGPUPopErrorScopeStatus_Success && type == *(WGPUErrorType *)userdata);
    *(WGPUErrorType *)userdata = WGPUErrorType_NoError;
}
static void pop_type(state *s, WGPUErrorType expected)
{
    WGPUPopErrorScopeCallbackInfo callback = {.mode=WGPUCallbackMode_AllowSpontaneous,
        .callback=expect_type,.userdata1=&expected};
    assert(wgpuDevicePopErrorScope(s->device,callback).id && expected == WGPUErrorType_NoError);
}
static void render_failures(state *s)
{
    WGPUShaderSourceWGSL source = WGPU_SHADER_SOURCE_WGSL_INIT;
    source.code = (WGPUStringView){
        "@vertex fn v()->@builtin(position) vec4f{return vec4f(0.0,0.0,0.0,1.0);}"
        "@fragment fn f()->@location(0) vec4f{return vec4f(1.0);}",WGPU_STRLEN};
    WGPUShaderModuleDescriptor shader_desc = {.nextInChain=&source.chain};
    WGPUShaderModule shader = wgpuDeviceCreateShaderModule(s->device,&shader_desc);
    assert(shader);
    WGPUColorTargetState color = {.format=WGPUTextureFormat_RGBA8Unorm,.writeMask=WGPUColorWriteMask_All};
    WGPUFragmentState fragment = {.module=shader,.targetCount=1,.targets=&color};
    WGPURenderPipelineDescriptor descriptor = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    descriptor.vertex.module = shader;
    descriptor.fragment = &fragment;
    native_device *device = (native_device *)s->device;
    const doe_vk_status failures[] = {DOE_VK_OUT_OF_MEMORY,DOE_VK_NATIVE_FAILURE,DOE_VK_INVALID_ARGUMENT};
    const WGPUErrorType types[] = {WGPUErrorType_OutOfMemory,WGPUErrorType_Internal,WGPUErrorType_Validation};
    const WGPUErrorFilter filters[] = {WGPUErrorFilter_OutOfMemory,WGPUErrorFilter_Internal,WGPUErrorFilter_Validation};
    for (size_t i = 0; i < 3; ++i) {
        unsigned references = atomic_load(&device->references);
        injected_pipeline_status = failures[i];
        wgpuDevicePushErrorScope(s->device,filters[i]);
        assert(!wgpuDeviceCreateRenderPipeline(s->device,&descriptor));
        pop_type(s,types[i]);
        assert(atomic_load(&device->references) == references);
        injected_pipeline_status = DOE_VK_OK;
    }
    doe_instance_allocator saved = device->resources.allocator;
    device->resources.allocator.allocate = fail_allocate;
    for (unsigned prefix = 0; prefix < 2; ++prefix) {
        allocations_before_failure = prefix;
        unsigned references = atomic_load(&device->references);
        wgpuDevicePushErrorScope(s->device,WGPUErrorFilter_OutOfMemory);
        assert(!wgpuDeviceCreateRenderPipeline(s->device,&descriptor));
        pop_type(s,WGPUErrorType_OutOfMemory);
        assert(atomic_load(&device->references) == references);
    }
    device->resources.allocator = saved;
    WGPURenderPipeline pipeline = wgpuDeviceCreateRenderPipeline(s->device,&descriptor);
    assert(pipeline);
    wgpuRenderPipelineRelease(pipeline);
    wgpuShaderModuleRelease(shader);
}
int main(void)
{
    state s = {0};
    s.instance = wgpuCreateInstance(NULL);
    WGPURequestAdapterOptions options = WGPU_REQUEST_ADAPTER_OPTIONS_INIT;
    adapter_class_options(&options);
    WGPURequestAdapterCallbackInfo adapter = {.mode=WGPUCallbackMode_AllowSpontaneous,.callback=adapter_reply,.userdata1=&s};
    assert(wgpuInstanceRequestAdapter(s.instance,&options,adapter).id && s.adapter);
    adapter_class_require(s.adapter);
    WGPUDeviceDescriptor descriptor = WGPU_DEVICE_DESCRIPTOR_INIT;
    descriptor.uncapturedErrorCallbackInfo = (WGPUUncapturedErrorCallbackInfo){.callback=error_reply,.userdata1=&s};
    WGPURequestDeviceCallbackInfo requested = {.mode=WGPUCallbackMode_AllowSpontaneous,.callback=device_reply,.userdata1=&s};
    assert(wgpuAdapterRequestDevice(s.adapter,&descriptor,requested).id && s.device);
    WGPUFeatureName timestamp = WGPUFeatureName_TimestampQuery;
    descriptor.requiredFeatureCount = 1;
    descriptor.requiredFeatures = &timestamp;
    unsigned failed_feature_devices = 0;
    WGPURequestDeviceCallbackInfo failure = {.mode=WGPUCallbackMode_AllowSpontaneous,
        .callback=feature_failure,.userdata1=&failed_feature_devices};
    fail_normalizer = true;
    assert(wgpuAdapterRequestDevice(s.adapter,&descriptor,failure).id && failed_feature_devices == 1);
    fail_normalizer = false;
    native_device *device = (native_device *)s.device;
    WGPUBufferDescriptor buffer_desc = {.size=16,.usage=WGPUBufferUsage_Storage};
    WGPUShaderSourceWGSL source = WGPU_SHADER_SOURCE_WGSL_INIT;
    source.code = (WGPUStringView){"@group(2) @binding(0) var<storage,read_write> data:array<u32>;"
        "@compute @workgroup_size(1) fn main(){data[0]=3u;}",WGPU_STRLEN};
    WGPUShaderModuleDescriptor shader_desc = {.nextInChain=&source.chain};
    WGPUShaderModule shader = wgpuDeviceCreateShaderModule(s.device,&shader_desc);
    assert(shader);
    WGPUComputePipelineDescriptor pipeline_desc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
    pipeline_desc.compute.module = shader;
    doe_instance_allocator saved = device->resources.allocator;
    device->resources.allocator.allocate = fail_allocate;
    wgpuDevicePushErrorScope(s.device,WGPUErrorFilter_OutOfMemory);
    assert(!wgpuDeviceCreateBuffer(s.device,&buffer_desc));
    pop_type(&s,WGPUErrorType_OutOfMemory);
    wgpuDevicePushErrorScope(s.device,WGPUErrorFilter_OutOfMemory);
    assert(!wgpuDeviceCreateCommandEncoder(s.device,NULL));
    pop_type(&s,WGPUErrorType_OutOfMemory);
    wgpuDevicePushErrorScope(s.device,WGPUErrorFilter_OutOfMemory);
    assert(!wgpuDeviceCreateComputePipeline(s.device,&pipeline_desc));
    pop_type(&s,WGPUErrorType_OutOfMemory);
    for (unsigned prefix = 1; prefix <= 4; ++prefix) {
        allocations_before_failure = prefix;
        unsigned references = atomic_load(&device->references);
        wgpuDevicePushErrorScope(s.device,WGPUErrorFilter_OutOfMemory);
        assert(!wgpuDeviceCreateComputePipeline(s.device,&pipeline_desc));
        pop_type(&s,WGPUErrorType_OutOfMemory);
        assert(atomic_load(&device->references) == references);
    }
    allocations_before_failure = 0;
    WGPUBindGroupLayoutDescriptor empty = {0};
    wgpuDevicePushErrorScope(s.device,WGPUErrorFilter_OutOfMemory);
    assert(!wgpuDeviceCreateBindGroupLayout(s.device,&empty));
    pop_type(&s,WGPUErrorType_OutOfMemory);
    device->resources.allocator = saved;
    const doe_vk_status failures[] = {DOE_VK_OUT_OF_MEMORY,DOE_VK_NATIVE_FAILURE,DOE_VK_INVALID_ARGUMENT};
    const WGPUErrorType types[] = {WGPUErrorType_OutOfMemory,WGPUErrorType_Internal,WGPUErrorType_Validation};
    const WGPUErrorFilter filters[] = {WGPUErrorFilter_OutOfMemory,WGPUErrorFilter_Internal,WGPUErrorFilter_Validation};
    for (size_t i = 0; i < 3; ++i) {
        injected_buffer_status = failures[i];
        wgpuDevicePushErrorScope(s.device,filters[i]);
        assert(!wgpuDeviceCreateBuffer(s.device,&buffer_desc));
        pop_type(&s,types[i]);
        injected_buffer_status = DOE_VK_OK;
        injected_pipeline_status = failures[i];
        wgpuDevicePushErrorScope(s.device,filters[i]);
        assert(!wgpuDeviceCreateComputePipeline(s.device,&pipeline_desc));
        pop_type(&s,types[i]);
        injected_pipeline_status = DOE_VK_OK;
    }
    pipeline_desc.label = (WGPUStringView){NULL,7};
    wgpuDevicePushErrorScope(s.device,WGPUErrorFilter_Validation);
    assert(!wgpuDeviceCreateComputePipeline(s.device,&pipeline_desc));
    pop_type(&s,WGPUErrorType_Validation);
    pipeline_desc.label = (WGPUStringView){0};
    WGPUComputePipeline compute = wgpuDeviceCreateComputePipeline(s.device,&pipeline_desc);
    assert(compute && !s.errors);
    wgpuComputePipelineRelease(compute);
    device->resources.allocator.allocate = fail_allocate;
    allocations_before_failure = 1000;
    compute = wgpuDeviceCreateComputePipeline(s.device,&pipeline_desc);
    assert(compute);
    allocations_before_failure = 0;
    unsigned references = atomic_load(&device->references);
    wgpuDevicePushErrorScope(s.device,WGPUErrorFilter_OutOfMemory);
    assert(!wgpuComputePipelineGetBindGroupLayout(compute,3));
    pop_type(&s,WGPUErrorType_OutOfMemory);
    assert(atomic_load(&device->references) == references);
    allocations_before_failure = 1000;
    WGPUBindGroupLayout trailing = wgpuComputePipelineGetBindGroupLayout(compute,3);
    assert(trailing);
    wgpuBindGroupLayoutRelease(trailing);
    wgpuComputePipelineRelease(compute);
    device->resources.allocator = saved;
    wgpuShaderModuleRelease(shader);
    render_failures(&s);
    wgpuDeviceRelease(s.device);
    wgpuAdapterRelease(s.adapter);
    wgpuInstanceRelease(s.instance);
    printf("PASS public error types: resource allocation, automatic-layout allocation, compute/render backend OOM/native/validation failures, failed requested-feature device and successful recovery %s\n", adapter_class_scope());
}
