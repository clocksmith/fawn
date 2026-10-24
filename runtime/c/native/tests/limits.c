/* SPDX-License-Identifier: MIT
 * Public WebGPU limits through the real C compiler and Vulkan: adapter limits
 * meet every core default, a device without requiredLimits gets exactly the
 * defaults and is validated against them, requiredLimits resolve per spec, and
 * a group with sparse binding numbers up to maxBindingsPerBindGroup - 1
 * executes with exact readback through explicit (dynamic-offset) and automatic
 * layouts. */
#include "consumer.h"
#include <stddef.h>
#include <stdlib.h>

typedef struct limit_row {
    const char *name;
    size_t offset;
    bool wide, alignment;
    uint64_t fallback;
} limit_row;
#define ROW32(name, value) {#name, offsetof(WGPULimits, name), false, false, value}
#define ROW64(name, value) {#name, offsetof(WGPULimits, name), true, false, value}
#define ALIGN(name, value) {#name, offsetof(WGPULimits, name), false, true, value}
/* https://gpuweb.github.io/gpuweb/#limits default column. */
static const limit_row rows[] = {
    ROW32(maxTextureDimension1D, 8192), ROW32(maxTextureDimension2D, 8192), ROW32(maxTextureDimension3D, 2048),
    ROW32(maxTextureArrayLayers, 256), ROW32(maxBindGroups, 4), ROW32(maxBindGroupsPlusVertexBuffers, 24),
    ROW32(maxBindingsPerBindGroup, 1000), ROW32(maxDynamicUniformBuffersPerPipelineLayout, 8),
    ROW32(maxDynamicStorageBuffersPerPipelineLayout, 4), ROW32(maxSampledTexturesPerShaderStage, 16),
    ROW32(maxSamplersPerShaderStage, 16), ROW32(maxStorageBuffersPerShaderStage, 8),
    ROW32(maxStorageTexturesPerShaderStage, 4), ROW32(maxUniformBuffersPerShaderStage, 12),
    ROW64(maxUniformBufferBindingSize, 65536), ROW64(maxStorageBufferBindingSize, 134217728),
    ALIGN(minUniformBufferOffsetAlignment, 256), ALIGN(minStorageBufferOffsetAlignment, 256),
    ROW32(maxVertexBuffers, 8), ROW64(maxBufferSize, 268435456), ROW32(maxVertexAttributes, 16),
    ROW32(maxVertexBufferArrayStride, 2048), ROW32(maxInterStageShaderVariables, 16),
    ROW32(maxColorAttachments, 8), ROW32(maxColorAttachmentBytesPerSample, 32),
    ROW32(maxComputeWorkgroupStorageSize, 16384), ROW32(maxComputeInvocationsPerWorkgroup, 256),
    ROW32(maxComputeWorkgroupSizeX, 256), ROW32(maxComputeWorkgroupSizeY, 256),
    ROW32(maxComputeWorkgroupSizeZ, 64), ROW32(maxComputeWorkgroupsPerDimension, 65535),
};

static uint64_t get(const WGPULimits *limits, const limit_row *row)
{
    const unsigned char *base = (const unsigned char *)limits + row->offset;
    uint64_t value;
    if (row->wide) memcpy(&value, base, sizeof(value));
    else {
        uint32_t narrow;
        memcpy(&narrow, base, sizeof(narrow));
        value = narrow;
    }
    return value;
}

typedef struct request { WGPURequestDeviceStatus status; WGPUDevice device; } request;
static void requested(WGPURequestDeviceStatus status, WGPUDevice device, WGPUStringView message,
    void *userdata, void *unused)
{
    (void)message; (void)unused;
    *(request *)userdata = (request){status, device};
}

/* NULL when the adapter refuses the required limits. */
static WGPUDevice open_device(state *s, const WGPULimits *required)
{
    WGPUDeviceDescriptor descriptor = WGPU_DEVICE_DESCRIPTOR_INIT;
    descriptor.requiredLimits = required;
    descriptor.uncapturedErrorCallbackInfo = (WGPUUncapturedErrorCallbackInfo){.callback=error_reply,.userdata1=s};
    request reply = {0};
    WGPURequestDeviceCallbackInfo callback = {.mode=WGPUCallbackMode_AllowSpontaneous,.callback=requested,
        .userdata1=&reply};
    assert(wgpuAdapterRequestDevice(s->adapter, &descriptor, callback).id);
    assert((reply.status == WGPURequestDeviceStatus_Success) == (reply.device != NULL));
    return reply.device;
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
    WGPUPopErrorScopeCallbackInfo callback = {.mode=WGPUCallbackMode_AllowSpontaneous,
        .callback=no_error_reply,.userdata1=&calls};
    assert(wgpuDevicePopErrorScope(s->device, callback).id && calls == 1);
}

static WGPUShaderModule module(state *s, const char *code)
{
    WGPUShaderSourceWGSL source = WGPU_SHADER_SOURCE_WGSL_INIT;
    source.code = (WGPUStringView){code, WGPU_STRLEN};
    WGPUShaderModuleDescriptor descriptor = {.nextInChain = &source.chain};
    WGPUShaderModule result = wgpuDeviceCreateShaderModule(s->device, &descriptor);
    assert(result);
    return result;
}

static WGPUComputePipeline try_compute(state *s, const char *code, WGPUPipelineLayout layout)
{
    WGPUShaderModule shader = module(s, code);
    WGPUComputePipelineDescriptor descriptor = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
    descriptor.compute.module = shader;
    descriptor.layout = layout;
    WGPUComputePipeline result = wgpuDeviceCreateComputePipeline(s->device, &descriptor);
    wgpuShaderModuleRelease(shader);
    return result;
}

static void check_adapter_and_defaults(state *s, WGPULimits *adapter)
{
    *adapter = WGPU_LIMITS_INIT;
    assert(wgpuAdapterGetLimits(s->adapter, adapter) == WGPUStatus_Success);
    WGPULimits device = WGPU_LIMITS_INIT;
    assert(wgpuDeviceGetLimits(s->device, &device) == WGPUStatus_Success);
    printf("%-44s %12s %12s %12s\n", "limit", "default", "adapter", "device");
    for (size_t i = 0; i < sizeof(rows) / sizeof(rows[0]); ++i) {
        const limit_row *row = &rows[i];
        uint64_t supported = get(adapter, row), value = get(&device, row);
        printf("%-44s %12llu %12llu %12llu\n", row->name, (unsigned long long)row->fallback,
            (unsigned long long)supported, (unsigned long long)value);
        if (row->alignment) assert(supported && !(supported & (supported - 1)) && supported <= row->fallback);
        else assert(supported >= row->fallback);
        assert(value == row->fallback);
    }
    assert(adapter->maxUniformBufferBindingSize % 4 == 0 && adapter->maxStorageBufferBindingSize % 4 == 0);
    assert(adapter->maxStorageBufferBindingSize <= adapter->maxBufferSize);
    assert(adapter->maxUniformBufferBindingSize <= adapter->maxBufferSize);
    assert(adapter->maxImmediateSize == 0 && device.maxImmediateSize == 0);
}

static void check_required(state *s, const WGPULimits *adapter)
{
    /* Better than the adapter, malformed alignment, or a better-than-adapter
     * alignment: refused. */
    WGPULimits required = WGPU_LIMITS_INIT;
    required.maxBindGroups = adapter->maxBindGroups + 1;
    assert(!open_device(s, &required));
    required = WGPU_LIMITS_INIT;
    required.maxBufferSize = adapter->maxBufferSize + 1;
    assert(!open_device(s, &required));
    required = WGPU_LIMITS_INIT;
    required.minStorageBufferOffsetAlignment = 48;
    assert(!open_device(s, &required));
    if (adapter->minUniformBufferOffsetAlignment > 1) {
        required = WGPU_LIMITS_INIT;
        required.minUniformBufferOffsetAlignment = adapter->minUniformBufferOffsetAlignment / 2;
        assert(!open_device(s, &required));
    }
    /* Worse than the default resolves to the default. */
    required = WGPU_LIMITS_INIT;
    required.maxBindGroups = 1;
    required.maxBufferSize = 4;
    required.minUniformBufferOffsetAlignment = 4096;
    WGPUDevice device = open_device(s, &required);
    assert(device);
    WGPULimits resolved = WGPU_LIMITS_INIT;
    assert(wgpuDeviceGetLimits(device, &resolved) == WGPUStatus_Success);
    assert(resolved.maxBindGroups == 4 && resolved.maxBufferSize == 268435456 &&
        resolved.minUniformBufferOffsetAlignment == 256);
    wgpuDeviceRelease(device);
    /* Requesting every adapter limit yields exactly the adapter limits. */
    device = open_device(s, adapter);
    assert(device);
    resolved = WGPU_LIMITS_INIT;
    assert(wgpuDeviceGetLimits(device, &resolved) == WGPUStatus_Success);
    for (size_t i = 0; i < sizeof(rows) / sizeof(rows[0]); ++i) assert(get(&resolved, &rows[i]) == get(adapter, &rows[i]));
    wgpuDeviceRelease(device);
}

static WGPUBindGroupLayout storage_layout(state *s, uint32_t count, bool dynamic)
{
    WGPUBindGroupLayoutEntry entries[8];
    for (uint32_t i = 0; i < count; ++i) {
        entries[i] = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
        entries[i].binding = i;
        entries[i].visibility = WGPUShaderStage_Compute;
        entries[i].buffer.type = WGPUBufferBindingType_Storage;
        entries[i].buffer.hasDynamicOffset = dynamic;
    }
    WGPUBindGroupLayoutDescriptor descriptor = {.entryCount = count, .entries = entries};
    return wgpuDeviceCreateBindGroupLayout(s->device, &descriptor);
}

static const char *const large_workgroup =
    "@group(0) @binding(0) var<storage, read_write> o: array<u32>;\n"
    "@compute @workgroup_size(512) fn main(@builtin(local_invocation_index) i: u32) { o[i] = i; }\n";
static const char *const large_shared =
    "var<workgroup> w: array<u32, 5000>;\n"
    "@group(0) @binding(0) var<storage, read_write> o: array<u32>;\n"
    "@compute @workgroup_size(1) fn main() { w[4999] = 7u; workgroupBarrier(); o[0] = w[4999]; }\n";
static const char *const interstage16 =
    "struct V { @builtin(position) p: vec4<f32>, @location(16) v: vec4<f32> }\n"
    "@vertex fn vs() -> V { var o: V; o.p = vec4<f32>(0.0, 0.0, 0.0, 1.0); o.v = vec4<f32>(1.0); return o; }\n"
    "@fragment fn fs(i: V) -> @location(0) vec4<f32> { return i.v; }\n";

static WGPURenderPipeline try_render(state *s, const char *code)
{
    WGPUShaderModule shader = module(s, code);
    WGPUColorTargetState target = WGPU_COLOR_TARGET_STATE_INIT;
    target.format = WGPUTextureFormat_RGBA8Unorm;
    WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
    fragment.module = shader;
    fragment.targetCount = 1;
    fragment.targets = &target;
    WGPURenderPipelineDescriptor descriptor = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    descriptor.vertex.module = shader;
    descriptor.fragment = &fragment;
    WGPURenderPipeline result = wgpuDeviceCreateRenderPipeline(s->device, &descriptor);
    wgpuShaderModuleRelease(shader);
    return result;
}

/* Each check runs on the default device (expect a validation error) and on a
 * device requesting every adapter limit (expect success where the adapter
 * exceeds the default). */
static void check_default_validation(state *s, const WGPULimits *adapter)
{
    WGPUDevice defaults = s->device, maximal = open_device(s, adapter);
    WGPUQueue default_queue = s->queue;
    assert(maximal);
    for (int pass = 0; pass < 2; ++pass) {
        bool raised = pass == 1;
        s->device = raised ? maximal : defaults;
        s->queue = raised ? wgpuDeviceGetQueue(maximal) : default_queue;
        /* maxDynamicStorageBuffersPerPipelineLayout: default 4. */
        wgpuDevicePushErrorScope(s->device, WGPUErrorFilter_Validation);
        WGPUBindGroupLayout five = storage_layout(s, 5, true);
        if (raised) { expect_no_error(s); assert(five); wgpuBindGroupLayoutRelease(five); }
        else { expect_validation(s); assert(!five); }
        /* maxComputeInvocationsPerWorkgroup / X: default 256. */
        wgpuDevicePushErrorScope(s->device, WGPUErrorFilter_Validation);
        WGPUComputePipeline wide = try_compute(s, large_workgroup, NULL);
        bool admits = raised && adapter->maxComputeInvocationsPerWorkgroup >= 512 &&
            adapter->maxComputeWorkgroupSizeX >= 512;
        if (admits) {
            assert(wide);
            wgpuComputePipelineRelease(wide);
            expect_no_error(s);
        } else { assert(!wide); expect_validation(s); }
        /* maxComputeWorkgroupStorageSize: default 16384. */
        wgpuDevicePushErrorScope(s->device, WGPUErrorFilter_Validation);
        WGPUComputePipeline shared = try_compute(s, large_shared, NULL);
        admits = raised && adapter->maxComputeWorkgroupStorageSize >= 20000;
        if (admits) {
            assert(shared);
            wgpuComputePipelineRelease(shared);
            expect_no_error(s);
        } else { assert(!shared); expect_validation(s); }
        /* maxInterStageShaderVariables: default 16, so location 16 is out of range. */
        wgpuDevicePushErrorScope(s->device, WGPUErrorFilter_Validation);
        WGPURenderPipeline varying = try_render(s, interstage16);
        admits = raised && adapter->maxInterStageShaderVariables > 16;
        if (admits) {
            assert(varying);
            wgpuRenderPipelineRelease(varying);
            expect_no_error(s);
        } else { assert(!varying); expect_validation(s); }
        /* maxBufferSize and maxTextureDimension2D: validation precedes allocation. */
        if (!raised) {
            wgpuDevicePushErrorScope(s->device, WGPUErrorFilter_Validation);
            WGPUBufferDescriptor big = {.size = 268435456ull + 4, .usage = WGPUBufferUsage_Storage};
            WGPUBuffer refused = wgpuDeviceCreateBuffer(s->device, &big);
            expect_validation(s);
            if (refused) wgpuBufferRelease(refused); /* An error object, if returned. */
        }
        wgpuDevicePushErrorScope(s->device, WGPUErrorFilter_Validation);
        WGPUTextureDescriptor wide_texture = WGPU_TEXTURE_DESCRIPTOR_INIT;
        wide_texture.format = WGPUTextureFormat_RGBA8Unorm;
        wide_texture.usage = WGPUTextureUsage_CopyDst;
        wide_texture.size = (WGPUExtent3D){8193, 1, 1};
        WGPUTexture texture = wgpuDeviceCreateTexture(s->device, &wide_texture);
        admits = raised && adapter->maxTextureDimension2D > 8192;
        if (admits) {
            assert(texture);
            expect_no_error(s);
        } else expect_validation(s);
        if (texture) wgpuTextureRelease(texture);
        if (raised) wgpuQueueRelease(s->queue);
    }
    s->device = defaults;
    s->queue = default_queue;
    wgpuDeviceRelease(maximal);
}

static void check_dispatch_and_binding_bounds(state *s)
{
    /* maxComputeWorkgroupsPerDimension: the encoder becomes invalid. */
    WGPUComputePipeline pipeline = try_compute(s,
        "@group(0) @binding(0) var<storage, read_write> o: array<u32>;\n"
        "@compute @workgroup_size(1) fn main() { o[0] = 1u; }\n", NULL);
    assert(pipeline);
    WGPUBuffer out = buffer(s, 16, WGPUBufferUsage_Storage);
    WGPUBindGroupEntry entry = {.binding = 0, .buffer = out, .size = 16};
    WGPUBindGroup bound = group(s, pipeline, &entry, 1);
    const uint32_t limit = 65535;
    for (int axis = 0; axis < 3; ++axis) {
        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device, NULL);
        WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, NULL);
        wgpuComputePassEncoderSetPipeline(pass, pipeline);
        wgpuComputePassEncoderSetBindGroup(pass, 0, bound, 0, NULL);
        wgpuDevicePushErrorScope(s->device, WGPUErrorFilter_Validation);
        wgpuComputePassEncoderDispatchWorkgroups(pass, axis == 0 ? limit + 1 : 1, axis == 1 ? limit + 1 : 1,
            axis == 2 ? limit + 1 : 1);
        wgpuComputePassEncoderEnd(pass);
        wgpuComputePassEncoderRelease(pass);
        WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, NULL);
        expect_validation(s);
        if (commands) wgpuCommandBufferRelease(commands);
        wgpuCommandEncoderRelease(encoder);
    }
    /* Binding numbers must be below maxBindingsPerBindGroup. */
    WGPUBindGroupLayoutEntry layout_entry = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
    layout_entry.binding = 1000;
    layout_entry.visibility = WGPUShaderStage_Compute;
    layout_entry.buffer.type = WGPUBufferBindingType_Storage;
    WGPUBindGroupLayoutDescriptor descriptor = {.entryCount = 1, .entries = &layout_entry};
    wgpuDevicePushErrorScope(s->device, WGPUErrorFilter_Validation);
    assert(!wgpuDeviceCreateBindGroupLayout(s->device, &descriptor));
    expect_validation(s);
    wgpuBindGroupRelease(bound);
    wgpuBufferRelease(out);
    wgpuComputePipelineRelease(pipeline);
}

/* This fixture combines binding-ordered dynamic offsets at 0/17/31 with unused
 * sparse entries. mixed_resources.c executes shader binding 999 separately. */
static const char *const sparse =
    "@group(0) @binding(0) var<storage, read> a: array<u32>;\n"
    "@group(0) @binding(17) var<uniform> b: vec4<u32>;\n"
    "@group(0) @binding(31) var<storage, read_write> o: array<u32>;\n"
    "@compute @workgroup_size(4) fn main(@builtin(global_invocation_id) id: vec3<u32>) {\n"
    "  o[id.x] = a[id.x] * 10u + b[id.x];\n"
    "}\n";

/* Writes through binding 31 at output byte offset `at`; 0xEE sentinel elsewhere. */
static void check_output(state *s, WGPUBuffer output, uint32_t at)
{
    WGPUBuffer readback = buffer(s, 512, WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead);
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device, NULL);
    wgpuCommandEncoderCopyBufferToBuffer(encoder, output, 0, readback, 0, 512);
    submit(s, encoder);
    const unsigned char *bytes = map(s, readback, 512);
    const uint32_t expected[4] = {110, 220, 330, 440};
    for (uint32_t i = 0; i < 512; ++i) {
        if (i >= at && i < at + 16) continue;
        assert(bytes[i] == 0xEE);
    }
    assert(memcmp(bytes + at, expected, sizeof(expected)) == 0);
    wgpuBufferUnmap(readback);
    wgpuBufferRelease(readback);
}

static void check_sparse_bindings(state *s)
{
    WGPUBuffer a = buffer(s, 16, WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst);
    WGPUBuffer b = buffer(s, 16, WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst);
    const uint32_t a_values[4] = {1, 2, 3, 4}, b_values[4] = {100, 200, 300, 400};
    wgpuQueueWriteBuffer(s->queue, a, 0, a_values, sizeof(a_values));
    wgpuQueueWriteBuffer(s->queue, b, 0, b_values, sizeof(b_values));
    unsigned char sentinel[512];
    memset(sentinel, 0xEE, sizeof(sentinel));

    /* Explicit layout, entries out of binding order: shader bindings 0/17/31
     * plus unused 500 and 999. Dynamic offsets follow binding order: 31, 999. */
    WGPUBindGroupLayoutEntry entries[5];
    const uint32_t numbers[5] = {999, 0, 500, 31, 17};
    for (int i = 0; i < 5; ++i) {
        entries[i] = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
        entries[i].binding = numbers[i];
        entries[i].visibility = WGPUShaderStage_Compute;
    }
    entries[0].buffer.type = WGPUBufferBindingType_Storage;
    entries[0].buffer.hasDynamicOffset = true;
    entries[1].buffer.type = WGPUBufferBindingType_ReadOnlyStorage;
    entries[2].buffer.type = WGPUBufferBindingType_Uniform;
    entries[3].buffer.type = WGPUBufferBindingType_Storage;
    entries[3].buffer.hasDynamicOffset = true;
    entries[4].buffer.type = WGPUBufferBindingType_Uniform;
    WGPUBindGroupLayoutDescriptor layout_descriptor = {.entryCount = 5, .entries = entries};
    WGPUBindGroupLayout layout = wgpuDeviceCreateBindGroupLayout(s->device, &layout_descriptor);
    assert(layout);
    WGPUPipelineLayoutDescriptor pipeline_layout_descriptor = {.bindGroupLayoutCount = 1, .bindGroupLayouts = &layout};
    WGPUPipelineLayout pipeline_layout = wgpuDeviceCreatePipelineLayout(s->device, &pipeline_layout_descriptor);
    assert(pipeline_layout);
    WGPUComputePipeline explicit_pipeline = try_compute(s, sparse, pipeline_layout);
    assert(explicit_pipeline);
    WGPUBuffer output = buffer(s, 512, WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst);
    wgpuQueueWriteBuffer(s->queue, output, 0, sentinel, sizeof(sentinel));
    WGPUBuffer spare = buffer(s, 1024, WGPUBufferUsage_Storage);
    WGPUBindGroupEntry explicit_bindings[5] = {
        {.binding = 500, .buffer = b, .size = 16},
        {.binding = 31, .buffer = output, .size = 16},
        {.binding = 999, .buffer = spare, .size = 16},
        {.binding = 0, .buffer = a, .size = 16},
        {.binding = 17, .buffer = b, .size = 16},
    };
    WGPUBindGroupDescriptor group_descriptor = {.layout = layout, .entryCount = 5, .entries = explicit_bindings};
    WGPUBindGroup explicit_group = wgpuDeviceCreateBindGroup(s->device, &group_descriptor);
    assert(explicit_group);
    const uint32_t offsets[2] = {256, 512};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device, NULL);
    WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, NULL);
    wgpuComputePassEncoderSetPipeline(pass, explicit_pipeline);
    wgpuComputePassEncoderSetBindGroup(pass, 0, explicit_group, 2, offsets);
    wgpuComputePassEncoderDispatchWorkgroups(pass, 1, 1, 1);
    wgpuComputePassEncoderEnd(pass);
    wgpuComputePassEncoderRelease(pass);
    submit(s, encoder);
    check_output(s, output, offsets[0]);

    /* Automatic layout reflects the same sparse numbers. */
    WGPUComputePipeline automatic = try_compute(s, sparse, NULL);
    assert(automatic);
    WGPUBuffer automatic_output = buffer(s, 512,
        WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst);
    wgpuQueueWriteBuffer(s->queue, automatic_output, 0, sentinel, sizeof(sentinel));
    WGPUBindGroupEntry automatic_bindings[3] = {
        {.binding = 31, .buffer = automatic_output, .size = 16},
        {.binding = 0, .buffer = a, .size = 16},
        {.binding = 17, .buffer = b, .size = 16},
    };
    WGPUBindGroup automatic_group = group(s, automatic, automatic_bindings, 3);
    encoder = wgpuDeviceCreateCommandEncoder(s->device, NULL);
    pass = wgpuCommandEncoderBeginComputePass(encoder, NULL);
    wgpuComputePassEncoderSetPipeline(pass, automatic);
    wgpuComputePassEncoderSetBindGroup(pass, 0, automatic_group, 0, NULL);
    wgpuComputePassEncoderDispatchWorkgroups(pass, 1, 1, 1);
    wgpuComputePassEncoderEnd(pass);
    wgpuComputePassEncoderRelease(pass);
    submit(s, encoder);
    check_output(s, automatic_output, 0);

    wgpuBindGroupRelease(automatic_group);
    wgpuComputePipelineRelease(automatic);
    wgpuBufferRelease(automatic_output);
    wgpuBindGroupRelease(explicit_group);
    wgpuComputePipelineRelease(explicit_pipeline);
    wgpuPipelineLayoutRelease(pipeline_layout);
    wgpuBindGroupLayoutRelease(layout);
    wgpuBufferRelease(output);
    wgpuBufferRelease(spare);
    wgpuBufferRelease(b);
    wgpuBufferRelease(a);
}

int main(void)
{
    setvbuf(stdout, NULL, _IOLBF, 0);
    state s = {0};
    consumer_init(&s);
    WGPULimits adapter;
    check_adapter_and_defaults(&s, &adapter);
    check_required(&s, &adapter);
    check_default_validation(&s, &adapter);
    check_dispatch_and_binding_bounds(&s);
    check_sparse_bindings(&s);
    consumer_finish(&s);
    printf("PASS public limits: %zu adapter limits meet the WebGPU defaults, default device limits exact, "
        "requiredLimits refusal/default/adapter resolution, default-limit validation of dynamic storage, "
        "workgroup size/storage, inter-stage location, buffer size, texture size, dispatch count and binding "
        "number, a sparse 0/17/31/500/999 group with binding-ordered dynamic offsets and an automatic layout; "
        "exact 32 words %s\n",
        sizeof(rows) / sizeof(rows[0]), adapter_class_scope());
    return 0;
}
