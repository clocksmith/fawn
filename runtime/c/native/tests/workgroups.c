/* SPDX-License-Identifier: MIT
 * Adapter workgroup facts reach requiredLimits and pipeline admission. */
#include "consumer.h"

static WGPUComputePipeline group_pipeline(state *s, uint32_t x, uint32_t y, uint32_t z)
{
    char source[512];
    int count = snprintf(source, sizeof(source),
        "@group(0) @binding(0) var<storage,read_write> output:array<u32>;"
        "@compute @workgroup_size(%u,%u,%u) fn main(@builtin(local_invocation_index) lane:u32){output[lane]=lane+1u;}",
        x, y, z);
    assert(count > 0 && (size_t)count < sizeof(source));
    WGPUShaderSourceWGSL wgsl = WGPU_SHADER_SOURCE_WGSL_INIT;
    wgsl.code = (WGPUStringView){source,(size_t)count};
    WGPUShaderModuleDescriptor module_desc = {.nextInChain=&wgsl.chain};
    WGPUShaderModule module = wgpuDeviceCreateShaderModule(s->device, &module_desc);
    WGPUComputePipelineDescriptor desc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
    desc.compute.module = module;
    desc.compute.entryPoint = (WGPUStringView){"main",WGPU_STRLEN};
    WGPUComputePipeline result = wgpuDeviceCreateComputePipeline(s->device, &desc);
    wgpuShaderModuleRelease(module);
    return result;
}

static void rejected(state *s, uint32_t x, uint32_t y, uint32_t z)
{
    wgpuDevicePushErrorScope(s->device, WGPUErrorFilter_Validation);
    WGPUComputePipeline invalid = group_pipeline(s, x, y, z);
    expect_validation(s);
    if (invalid) wgpuComputePipelineRelease(invalid);
}

static void execute(state *s, uint32_t x, uint32_t y, uint32_t z)
{
    uint32_t count = x * y * z;
    uint64_t size = (uint64_t)count * sizeof(uint32_t);
    WGPUComputePipeline compute = group_pipeline(s, x, y, z);
    assert(compute);
    WGPUBuffer output = buffer(s, size, WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc);
    WGPUBuffer readback = buffer(s, size, WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead);
    WGPUBindGroupLayout layout = wgpuComputePipelineGetBindGroupLayout(compute, 0);
    WGPUBindGroupEntry entry = {.binding=0,.buffer=output,.size=size};
    WGPUBindGroupDescriptor group_desc = {.layout=layout,.entryCount=1,.entries=&entry};
    WGPUBindGroup group = wgpuDeviceCreateBindGroup(s->device, &group_desc);
    assert(group);
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device, NULL);
    WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, NULL);
    wgpuComputePassEncoderSetPipeline(pass, compute);
    wgpuComputePassEncoderSetBindGroup(pass, 0, group, 0, NULL);
    wgpuComputePassEncoderDispatchWorkgroups(pass, 1, 1, 1);
    wgpuComputePassEncoderEnd(pass);
    wgpuComputePassEncoderRelease(pass);
    wgpuCommandEncoderCopyBufferToBuffer(encoder, output, 0, readback, 0, size);
    submit(s, encoder);
    const uint32_t *words = map(s, readback, size);
    for (uint32_t i = 0; i < count; ++i) assert(words[i] == i + 1);
    wgpuBufferUnmap(readback);
    wgpuBufferRelease(readback);
    wgpuBindGroupRelease(group);
    wgpuBindGroupLayoutRelease(layout);
    wgpuBufferRelease(output);
    wgpuComputePipelineRelease(compute);
    printf("workgroup %ux%ux%u: %u exact words\n", x, y, z, count);
}

static void no_error_reply(WGPUPopErrorScopeStatus status, WGPUErrorType type, WGPUStringView message,
    void *userdata, void *unused)
{
    (void)unused;
    if (type != WGPUErrorType_NoError) fprintf(stderr, "unexpected: %.*s\n", (int)message.length, message.data);
    assert(status == WGPUPopErrorScopeStatus_Success && type == WGPUErrorType_NoError);
    ++*(unsigned *)userdata;
}

static void expect_no_error(state *s)
{
    unsigned calls = 0;
    WGPUPopErrorScopeCallbackInfo callback = {.mode = WGPUCallbackMode_AllowSpontaneous,
        .callback = no_error_reply, .userdata1 = &calls};
    assert(wgpuDevicePopErrorScope(s->device, callback).id && calls == 1);
}

/* One workgroup of 64 lanes fills `count` u32 elements (4 * count bytes of
 * workgroup storage, a multiple of 16), then each lane reads an element another
 * lane wrote, so the whole array is distinct live storage. The module must
 * always create; the limit is a pipeline creation rule. */
static WGPUComputePipeline storage_pipeline(state *s, uint32_t count)
{
    char source[768];
    int length = snprintf(source, sizeof(source),
        "var<workgroup> shared_words: array<u32, %u>;\n"
        "@group(0) @binding(0) var<storage,read_write> output: array<u32>;\n"
        "@compute @workgroup_size(64) fn main(@builtin(local_invocation_index) lane: u32) {\n"
        "  for (var i = lane; i < %uu; i += 64u) { shared_words[i] = i + 1u; }\n"
        "  workgroupBarrier();\n"
        "  for (var i = lane; i < %uu; i += 64u) { output[i] = shared_words[%uu - 1u - i]; }\n"
        "}\n", count, count, count, count);
    assert(length > 0 && (size_t)length < sizeof(source));
    WGPUShaderSourceWGSL wgsl = WGPU_SHADER_SOURCE_WGSL_INIT;
    wgsl.code = (WGPUStringView){source, (size_t)length};
    WGPUShaderModuleDescriptor module_desc = {.nextInChain = &wgsl.chain};
    wgpuDevicePushErrorScope(s->device, WGPUErrorFilter_Validation);
    WGPUShaderModule module = wgpuDeviceCreateShaderModule(s->device, &module_desc);
    assert(module);
    expect_no_error(s);
    WGPUComputePipelineDescriptor desc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
    desc.compute.module = module;
    WGPUComputePipeline result = wgpuDeviceCreateComputePipeline(s->device, &desc);
    wgpuShaderModuleRelease(module);
    return result;
}

static void storage_rejected(state *s, uint32_t count)
{
    wgpuDevicePushErrorScope(s->device, WGPUErrorFilter_Validation);
    WGPUComputePipeline invalid = storage_pipeline(s, count);
    expect_validation(s);
    assert(!invalid);
}

static void storage_execute(state *s, uint32_t count)
{
    const uint64_t size = (uint64_t)count * sizeof(uint32_t);
    WGPUComputePipeline compute = storage_pipeline(s, count);
    assert(compute);
    WGPUBuffer output = buffer(s, size, WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc);
    WGPUBuffer readback = buffer(s, size, WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead);
    WGPUBindGroupLayout layout = wgpuComputePipelineGetBindGroupLayout(compute, 0);
    WGPUBindGroupEntry entry = {.binding = 0, .buffer = output, .size = size};
    WGPUBindGroupDescriptor group_desc = {.layout = layout, .entryCount = 1, .entries = &entry};
    WGPUBindGroup group = wgpuDeviceCreateBindGroup(s->device, &group_desc);
    assert(group);
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device, NULL);
    WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, NULL);
    wgpuComputePassEncoderSetPipeline(pass, compute);
    wgpuComputePassEncoderSetBindGroup(pass, 0, group, 0, NULL);
    wgpuComputePassEncoderDispatchWorkgroups(pass, 1, 1, 1);
    wgpuComputePassEncoderEnd(pass);
    wgpuComputePassEncoderRelease(pass);
    wgpuCommandEncoderCopyBufferToBuffer(encoder, output, 0, readback, 0, size);
    submit(s, encoder);
    const uint32_t *words = map(s, readback, size);
    for (uint32_t i = 0; i < count; ++i) assert(words[i] == count - i);
    wgpuBufferUnmap(readback);
    wgpuBufferRelease(readback);
    wgpuBindGroupRelease(group);
    wgpuBindGroupLayoutRelease(layout);
    wgpuBufferRelease(output);
    wgpuComputePipelineRelease(compute);
    printf("workgroup storage %u bytes: %u exact words\n", count * 4u, count);
}

/* maxComputeWorkgroupStorageSize: the default device admits exactly 16384
 * bytes; a device requiring the adapter's value admits and executes exactly
 * that many bytes and rejects 16 more. */
static void check_workgroup_storage(state *base, const WGPULimits *adapter)
{
    assert(adapter->maxComputeWorkgroupStorageSize >= 16384 && adapter->maxComputeWorkgroupStorageSize % 16 == 0);
    storage_execute(base, 16384 / 4);
    storage_rejected(base, 16384 / 4 + 4);
    WGPULimits required = WGPU_LIMITS_INIT;
    required.maxComputeWorkgroupStorageSize = adapter->maxComputeWorkgroupStorageSize;
    state raised = {.instance = base->instance, .adapter = base->adapter};
    WGPUDeviceDescriptor descriptor = WGPU_DEVICE_DESCRIPTOR_INIT;
    descriptor.requiredLimits = &required;
    descriptor.uncapturedErrorCallbackInfo = (WGPUUncapturedErrorCallbackInfo){.callback = error_reply, .userdata1 = &raised};
    WGPURequestDeviceCallbackInfo callback = {.mode = WGPUCallbackMode_AllowSpontaneous, .callback = device_reply,
        .userdata1 = &raised};
    assert(wgpuAdapterRequestDevice(base->adapter, &descriptor, callback).id && raised.device);
    raised.queue = wgpuDeviceGetQueue(raised.device);
    WGPULimits resolved = WGPU_LIMITS_INIT;
    assert(wgpuDeviceGetLimits(raised.device, &resolved) == WGPUStatus_Success);
    assert(resolved.maxComputeWorkgroupStorageSize == adapter->maxComputeWorkgroupStorageSize);
    storage_execute(&raised, adapter->maxComputeWorkgroupStorageSize / 4);
    storage_rejected(&raised, adapter->maxComputeWorkgroupStorageSize / 4 + 4);
    assert(!raised.errors);
    wgpuQueueRelease(raised.queue);
    wgpuDeviceRelease(raised.device);
    printf("adapter workgroup storage: %u bytes\n", adapter->maxComputeWorkgroupStorageSize);
}

int main(void)
{
    consumer_line_output();
    state base = {0};
    consumer_init(&base);
    WGPULimits adapter = WGPU_LIMITS_INIT, defaults = WGPU_LIMITS_INIT;
    assert(wgpuAdapterGetLimits(base.adapter, &adapter) == WGPUStatus_Success);
    assert(wgpuDeviceGetLimits(base.device, &defaults) == WGPUStatus_Success);
    assert(defaults.maxComputeInvocationsPerWorkgroup == 256);
    rejected(&base, 256, 2, 1); /* Each axis fits; the product exceeds the device. */
    WGPULimits required = WGPU_LIMITS_INIT;
    required.maxComputeInvocationsPerWorkgroup = adapter.maxComputeInvocationsPerWorkgroup;
    required.maxComputeWorkgroupSizeX = adapter.maxComputeWorkgroupSizeX;
    required.maxComputeWorkgroupSizeY = adapter.maxComputeWorkgroupSizeY;
    required.maxComputeWorkgroupSizeZ = adapter.maxComputeWorkgroupSizeZ;
    state raised = {.instance=base.instance,.adapter=base.adapter};
    WGPUDeviceDescriptor descriptor = WGPU_DEVICE_DESCRIPTOR_INIT;
    descriptor.requiredLimits = &required;
    descriptor.uncapturedErrorCallbackInfo = (WGPUUncapturedErrorCallbackInfo){.callback=error_reply,.userdata1=&raised};
    WGPURequestDeviceCallbackInfo callback = {.mode=WGPUCallbackMode_AllowSpontaneous,.callback=device_reply,.userdata1=&raised};
    assert(wgpuAdapterRequestDevice(base.adapter, &descriptor, callback).id && raised.device);
    raised.queue = wgpuDeviceGetQueue(raised.device);
    WGPULimits resolved = WGPU_LIMITS_INIT;
    assert(wgpuDeviceGetLimits(raised.device, &resolved) == WGPUStatus_Success);
    assert(resolved.maxComputeInvocationsPerWorkgroup == adapter.maxComputeInvocationsPerWorkgroup);
    printf("adapter compute: invocations=%u dimensions=%ux%ux%u\n", adapter.maxComputeInvocationsPerWorkgroup,
        adapter.maxComputeWorkgroupSizeX, adapter.maxComputeWorkgroupSizeY, adapter.maxComputeWorkgroupSizeZ);
    execute(&raised, 256, 1, 1);
    if (adapter.maxComputeInvocationsPerWorkgroup >= 512 && adapter.maxComputeWorkgroupSizeX >= 512)
        execute(&raised, 512, 1, 1);
    if (adapter.maxComputeInvocationsPerWorkgroup >= 1024 && adapter.maxComputeWorkgroupSizeX >= 512 &&
        adapter.maxComputeWorkgroupSizeY >= 2) execute(&raised, 512, 2, 1);
    assert(adapter.maxComputeWorkgroupSizeX < UINT32_MAX);
    rejected(&raised, adapter.maxComputeWorkgroupSizeX + 1, 1, 1);
    assert(!raised.errors);
    wgpuQueueRelease(raised.queue);
    wgpuDeviceRelease(raised.device);
    check_workgroup_storage(&base, &adapter);
    consumer_finish(&base);
    printf("PASS workgroup adapter/device bounds and exact execution, including workgroup storage at the "
        "default and adapter limits %s\n", adapter_class_scope());
    return 0;
}
