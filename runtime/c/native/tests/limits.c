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
    ROW32(maxImmediateSize, 64),
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
    /* WebGPU requires offset alignments of at least 32 (RADV reports 4). */
    assert(adapter->minUniformBufferOffsetAlignment >= 32 && adapter->minStorageBufferOffsetAlignment >= 32);
    assert(adapter->maxStorageBufferBindingSize <= adapter->maxBufferSize);
    assert(adapter->maxUniformBufferBindingSize <= adapter->maxBufferSize);
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

/* Limits Vulkan raises above the WebGPU default, each used above the default
 * on a device that requires the adapter limits, and refused on the default
 * device: 10 storage buffers and 8 storage textures in one stage, the adapter's
 * vertex attributes (above 16, at most 32), and 8 rgba32float targets (128 color
 * bytes per sample). */
static const char *const storage10 =
    "@group(0) @binding(0) var<storage, read_write> b0: array<u32>;\n"
    "@group(0) @binding(1) var<storage, read_write> b1: array<u32>;\n"
    "@group(0) @binding(2) var<storage, read_write> b2: array<u32>;\n"
    "@group(0) @binding(3) var<storage, read_write> b3: array<u32>;\n"
    "@group(0) @binding(4) var<storage, read_write> b4: array<u32>;\n"
    "@group(0) @binding(5) var<storage, read_write> b5: array<u32>;\n"
    "@group(0) @binding(6) var<storage, read_write> b6: array<u32>;\n"
    "@group(0) @binding(7) var<storage, read_write> b7: array<u32>;\n"
    "@group(0) @binding(8) var<storage, read_write> b8: array<u32>;\n"
    "@group(0) @binding(9) var<storage, read_write> b9: array<u32>;\n"
    "@compute @workgroup_size(1) fn main() {\n"
    "  b0[0] = 1u;\n"
    "  b1[0] = 2u;\n"
    "  b2[0] = 3u;\n"
    "  b3[0] = 4u;\n"
    "  b4[0] = 5u;\n"
    "  b5[0] = 6u;\n"
    "  b6[0] = 7u;\n"
    "  b7[0] = 8u;\n"
    "  b8[0] = 9u;\n"
    "  b9[0] = 10u;\n"
    "}\n";
static const char *const storage_textures8 =
    "@group(0) @binding(0) var t0: texture_storage_2d<r32uint, write>;\n"
    "@group(0) @binding(1) var t1: texture_storage_2d<r32uint, write>;\n"
    "@group(0) @binding(2) var t2: texture_storage_2d<r32uint, write>;\n"
    "@group(0) @binding(3) var t3: texture_storage_2d<r32uint, write>;\n"
    "@group(0) @binding(4) var t4: texture_storage_2d<r32uint, write>;\n"
    "@group(0) @binding(5) var t5: texture_storage_2d<r32uint, write>;\n"
    "@group(0) @binding(6) var t6: texture_storage_2d<r32uint, write>;\n"
    "@group(0) @binding(7) var t7: texture_storage_2d<r32uint, write>;\n"
    "@compute @workgroup_size(1) fn main() {\n"
    "  textureStore(t0, vec2<i32>(0), vec4<u32>(1u));\n"
    "  textureStore(t1, vec2<i32>(0), vec4<u32>(2u));\n"
    "  textureStore(t2, vec2<i32>(0), vec4<u32>(3u));\n"
    "  textureStore(t3, vec2<i32>(0), vec4<u32>(4u));\n"
    "  textureStore(t4, vec2<i32>(0), vec4<u32>(5u));\n"
    "  textureStore(t5, vec2<i32>(0), vec4<u32>(6u));\n"
    "  textureStore(t6, vec2<i32>(0), vec4<u32>(7u));\n"
    "  textureStore(t7, vec2<i32>(0), vec4<u32>(8u));\n"
    "}\n";
/* The raised vertex-attribute shader for `count` f32 attributes (count <= 32):
 * the adapter's own maxVertexAttributes, which WebGPU only bounds below by 16
 * (ANV reports 29, llvmpipe 32). */
static void attributes_shader(char *code, size_t capacity, uint32_t count)
{
    size_t used = 0;
    int written = snprintf(code, capacity, "struct In {\n  @builtin(vertex_index) i: u32,\n");
    assert(written > 0 && (size_t)written < capacity);
    used = (size_t)written;
    for (uint32_t i = 0; i < count; ++i) {
        written = snprintf(code + used, capacity - used, "  @location(%u) a%u: f32,\n", i, i);
        assert(written > 0 && (size_t)written < capacity - used);
        used += (size_t)written;
    }
    written = snprintf(code + used, capacity - used, "}\n"
        "struct V { @builtin(position) p: vec4<f32>, @location(0) @interpolate(flat) s: f32 }\n"
        "@vertex fn vs(v: In) -> V {\n  var o: V;\n"
        "  o.p = vec4<f32>(f32(v.i == 1u) * 4.0 - 1.0, f32(v.i == 2u) * 4.0 - 1.0, 0.0, 1.0);\n  o.s = 0.0");
    assert(written > 0 && (size_t)written < capacity - used);
    used += (size_t)written;
    for (uint32_t i = 0; i < count; ++i) {
        written = snprintf(code + used, capacity - used, " + v.a%u", i);
        assert(written > 0 && (size_t)written < capacity - used);
        used += (size_t)written;
    }
    written = snprintf(code + used, capacity - used, ";\n  return o;\n}\n"
        "@fragment fn fs(v: V) -> @location(0) vec4<f32> { return vec4<f32>(v.s, 0.0, 0.0, 1.0); }\n");
    assert(written > 0 && (size_t)written < capacity - used);
}
enum { ATTRIBUTE_SHADER_BYTES = 4096 };
static const char *const color128 =
    "struct O {\n"
    "  @location(0) c0: vec4<f32>,\n"
    "  @location(1) c1: vec4<f32>,\n"
    "  @location(2) c2: vec4<f32>,\n"
    "  @location(3) c3: vec4<f32>,\n"
    "  @location(4) c4: vec4<f32>,\n"
    "  @location(5) c5: vec4<f32>,\n"
    "  @location(6) c6: vec4<f32>,\n"
    "  @location(7) c7: vec4<f32>,\n"
    "}\n"
    "@vertex fn vs(@builtin(vertex_index) i: u32) -> @builtin(position) vec4<f32> {\n"
    "  return vec4<f32>(f32(i == 1u) * 4.0 - 1.0, f32(i == 2u) * 4.0 - 1.0, 0.0, 1.0);\n"
    "}\n"
    "@fragment fn fs() -> O {\n"
    "  var o: O;\n"
    "  o.c0 = vec4<f32>(1.0);\n"
    "  o.c1 = vec4<f32>(2.0);\n"
    "  o.c2 = vec4<f32>(3.0);\n"
    "  o.c3 = vec4<f32>(4.0);\n"
    "  o.c4 = vec4<f32>(5.0);\n"
    "  o.c5 = vec4<f32>(6.0);\n"
    "  o.c6 = vec4<f32>(7.0);\n"
    "  o.c7 = vec4<f32>(8.0);\n"
    "  return o;\n"
    "}\n";

static void expect_words(state *s, WGPUBuffer readback, size_t size, size_t stride, const uint32_t *expected, size_t count)
{
    const unsigned char *bytes = map(s, readback, size);
    for (size_t i = 0; i < count; ++i) {
        uint32_t word;
        memcpy(&word, bytes + i * stride, sizeof(word));
        assert(word == expected[i]);
    }
    wgpuBufferUnmap(readback);
}

static void raised_storage_buffers(state *s)
{
    WGPUComputePipeline compute = try_compute(s, storage10, NULL);
    assert(compute);
    WGPUBuffer outputs[10];
    WGPUBindGroupEntry entries[10];
    for (uint32_t i = 0; i < 10; ++i) {
        outputs[i] = buffer(s, 16, WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc);
        entries[i] = (WGPUBindGroupEntry){.binding = i, .buffer = outputs[i], .size = 16};
    }
    WGPUBindGroup bound = group(s, compute, entries, 10);
    WGPUBuffer readback = buffer(s, 10 * 256, WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead);
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device, NULL);
    dispatch(encoder, compute, bound);
    for (uint32_t i = 0; i < 10; ++i) wgpuCommandEncoderCopyBufferToBuffer(encoder, outputs[i], 0, readback, i * 256, 16);
    submit(s, encoder);
    const uint32_t expected[10] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    expect_words(s, readback, 10 * 256, 256, expected, 10);
    wgpuBufferRelease(readback);
    wgpuBindGroupRelease(bound);
    for (uint32_t i = 0; i < 10; ++i) wgpuBufferRelease(outputs[i]);
    wgpuComputePipelineRelease(compute);
}

static void raised_storage_textures(state *s)
{
    WGPUComputePipeline compute = try_compute(s, storage_textures8, NULL);
    assert(compute);
    WGPUTexture textures[8];
    WGPUTextureView views[8];
    WGPUBindGroupEntry entries[8];
    for (uint32_t i = 0; i < 8; ++i) {
        textures[i] = texture(s, WGPUTextureFormat_R32Uint, WGPUTextureUsage_StorageBinding | WGPUTextureUsage_CopySrc, 1);
        views[i] = wgpuTextureCreateView(textures[i], NULL);
        assert(views[i]);
        entries[i] = (WGPUBindGroupEntry){.binding = i, .textureView = views[i]};
    }
    WGPUBindGroup bound = group(s, compute, entries, 8);
    WGPUBuffer readback = buffer(s, 8 * 512, WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead);
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device, NULL);
    dispatch(encoder, compute, bound);
    for (uint32_t i = 0; i < 8; ++i) {
        WGPUTexelCopyTextureInfo source = {.texture = textures[i]};
        WGPUTexelCopyBufferInfo destination = {.buffer = readback, .layout = {.offset = i * 512, .bytesPerRow = 256, .rowsPerImage = 2}};
        WGPUExtent3D size = {2, 2, 1};
        wgpuCommandEncoderCopyTextureToBuffer(encoder, &source, &destination, &size);
    }
    submit(s, encoder);
    const uint32_t expected[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    expect_words(s, readback, 8 * 512, 512, expected, 8);
    wgpuBufferRelease(readback);
    wgpuBindGroupRelease(bound);
    for (uint32_t i = 0; i < 8; ++i) { wgpuTextureViewRelease(views[i]); wgpuTextureRelease(textures[i]); }
    wgpuComputePipelineRelease(compute);
}

/* NULL (with one validation error, checked by the caller) when refused. */
static WGPURenderPipeline raised_render_pipeline(state *s, const char *code, const WGPUColorTargetState *targets,
    size_t target_count, const WGPUVertexBufferLayout *vertex_buffer)
{
    WGPUShaderModule shader = module(s, code);
    WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
    fragment.module = shader;
    fragment.targetCount = target_count;
    fragment.targets = targets;
    WGPURenderPipelineDescriptor descriptor = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    descriptor.vertex.module = shader;
    descriptor.vertex.bufferCount = vertex_buffer ? 1 : 0;
    descriptor.vertex.buffers = vertex_buffer;
    descriptor.fragment = &fragment;
    WGPURenderPipeline result = wgpuDeviceCreateRenderPipeline(s->device, &descriptor);
    wgpuShaderModuleRelease(shader);
    return result;
}

static WGPUVertexBufferLayout attribute_layout(WGPUVertexAttribute attributes[32], uint32_t count)
{
    assert(count <= 32);
    for (uint32_t i = 0; i < count; ++i)
        attributes[i] = (WGPUVertexAttribute){.format = WGPUVertexFormat_Float32, .offset = 4 * i, .shaderLocation = i};
    return (WGPUVertexBufferLayout){.stepMode = WGPUVertexStepMode_Instance, .arrayStride = 4 * count,
        .attributeCount = count, .attributes = attributes};
}

/* Draws one full-target triangle into `count` 2x2 targets and reads texel 0 of each. */
static void draw_and_read(state *s, WGPURenderPipeline render, WGPUTextureFormat format, uint32_t count,
    WGPUBuffer vertices, WGPUBuffer readback)
{
    WGPUTexture targets[8];
    WGPUTextureView views[8];
    WGPURenderPassColorAttachment colors[8];
    for (uint32_t i = 0; i < count; ++i) {
        targets[i] = texture(s, format, WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc, 1);
        views[i] = wgpuTextureCreateView(targets[i], NULL);
        colors[i] = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
        colors[i].view = views[i];
        colors[i].loadOp = WGPULoadOp_Clear;
        colors[i].storeOp = WGPUStoreOp_Store;
    }
    WGPURenderPassDescriptor pass_descriptor = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
    pass_descriptor.colorAttachmentCount = count;
    pass_descriptor.colorAttachments = colors;
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device, NULL);
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &pass_descriptor);
    assert(pass);
    wgpuRenderPassEncoderSetPipeline(pass, render);
    if (vertices) wgpuRenderPassEncoderSetVertexBuffer(pass, 0, vertices, 0, 128);
    wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
    for (uint32_t i = 0; i < count; ++i) {
        WGPUTexelCopyTextureInfo source = {.texture = targets[i]};
        WGPUTexelCopyBufferInfo destination = {.buffer = readback, .layout = {.offset = i * 512, .bytesPerRow = 256, .rowsPerImage = 2}};
        WGPUExtent3D size = {2, 2, 1};
        wgpuCommandEncoderCopyTextureToBuffer(encoder, &source, &destination, &size);
    }
    submit(s, encoder);
    for (uint32_t i = 0; i < count; ++i) { wgpuTextureViewRelease(views[i]); wgpuTextureRelease(targets[i]); }
}

static void raised_vertex_attributes(state *s, uint32_t count)
{
    WGPUVertexAttribute attributes[32];
    WGPUVertexBufferLayout layout = attribute_layout(attributes, count);
    WGPUColorTargetState target = WGPU_COLOR_TARGET_STATE_INIT;
    target.format = WGPUTextureFormat_R32Float;
    char code[ATTRIBUTE_SHADER_BYTES];
    attributes_shader(code, sizeof(code), count);
    WGPURenderPipeline render = raised_render_pipeline(s, code, &target, 1, &layout);
    assert(render);
    float values[32] = {0};
    for (uint32_t i = 0; i < count; ++i) values[i] = (float)(i + 1);
    WGPUBuffer vertices = buffer(s, 128, WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst);
    wgpuQueueWriteBuffer(s->queue, vertices, 0, values, sizeof(values));
    WGPUBuffer readback = buffer(s, 512, WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead);
    draw_and_read(s, render, WGPUTextureFormat_R32Float, 1, vertices, readback);
    const float *texel = map(s, readback, 512);
    assert(texel[0] == (float)(count * (count + 1) / 2)); /* 1 + 2 + ... + count, exact in f32 */
    wgpuBufferUnmap(readback);
    wgpuBufferRelease(readback);
    wgpuBufferRelease(vertices);
    wgpuRenderPipelineRelease(render);
}

static void color_targets(WGPUColorTargetState targets[8])
{
    for (uint32_t i = 0; i < 8; ++i) {
        targets[i] = WGPU_COLOR_TARGET_STATE_INIT;
        targets[i].format = WGPUTextureFormat_RGBA32Float;
    }
}

static void raised_color_bytes(state *s)
{
    WGPUColorTargetState targets[8];
    color_targets(targets);
    WGPURenderPipeline render = raised_render_pipeline(s, color128, targets, 8, NULL);
    assert(render);
    WGPUBuffer readback = buffer(s, 8 * 512, WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead);
    draw_and_read(s, render, WGPUTextureFormat_RGBA32Float, 8, NULL, readback);
    const unsigned char *bytes = map(s, readback, 8 * 512);
    for (uint32_t i = 0; i < 8; ++i) {
        float texel[4];
        memcpy(texel, bytes + i * 512, sizeof(texel));
        for (int c = 0; c < 4; ++c) assert(texel[c] == (float)(i + 1));
    }
    wgpuBufferUnmap(readback);
    wgpuBufferRelease(readback);
    wgpuRenderPipelineRelease(render);
}

static void check_raised_limits(state *s, const WGPULimits *adapter)
{
    assert(adapter->maxStorageBuffersPerShaderStage >= 10 && adapter->maxStorageTexturesPerShaderStage >= 8);
    /* Raised vertex attributes use the adapter's own value: above the default 16,
     * at most the 32 these fixtures hold. */
    assert(adapter->maxVertexAttributes > 16 && adapter->maxColorAttachmentBytesPerSample >= 128);
    const uint32_t attribute_count = adapter->maxVertexAttributes < 32 ? adapter->maxVertexAttributes : 32;
    /* The default device refuses each one. */
    WGPUVertexAttribute attributes[32];
    WGPUVertexBufferLayout layout = attribute_layout(attributes, attribute_count);
    char attribute_code[ATTRIBUTE_SHADER_BYTES];
    attributes_shader(attribute_code, sizeof(attribute_code), attribute_count);
    WGPUColorTargetState r32 = WGPU_COLOR_TARGET_STATE_INIT, wide[8];
    r32.format = WGPUTextureFormat_R32Float;
    color_targets(wide);
    wgpuDevicePushErrorScope(s->device, WGPUErrorFilter_Validation);
    assert(!try_compute(s, storage10, NULL));
    expect_validation(s);
    wgpuDevicePushErrorScope(s->device, WGPUErrorFilter_Validation);
    assert(!try_compute(s, storage_textures8, NULL));
    expect_validation(s);
    wgpuDevicePushErrorScope(s->device, WGPUErrorFilter_Validation);
    assert(!raised_render_pipeline(s, attribute_code, &r32, 1, &layout));
    expect_validation(s);
    wgpuDevicePushErrorScope(s->device, WGPUErrorFilter_Validation);
    assert(!raised_render_pipeline(s, color128, wide, 8, NULL));
    expect_validation(s);
    /* A device requiring the adapter limits executes each with exact readback. */
    WGPUDevice defaults = s->device, maximal = open_device(s, adapter);
    WGPUQueue default_queue = s->queue;
    assert(maximal);
    s->device = maximal;
    s->queue = wgpuDeviceGetQueue(maximal);
    wgpuDevicePushErrorScope(s->device, WGPUErrorFilter_Validation);
    raised_storage_buffers(s);
    raised_storage_textures(s);
    raised_vertex_attributes(s, attribute_count);
    raised_color_bytes(s);
    expect_no_error(s);
    wgpuQueueRelease(s->queue);
    s->device = defaults;
    s->queue = default_queue;
    wgpuDeviceRelease(maximal);
}

/* The CTS opens every device with all adapter features and all adapter limits. */
static void check_every_feature_and_limit(state *s, const WGPULimits *adapter)
{
    WGPUSupportedFeatures features = WGPU_SUPPORTED_FEATURES_INIT;
    wgpuAdapterGetFeatures(s->adapter, &features);
    assert(features.featureCount);
    WGPUDeviceDescriptor descriptor = WGPU_DEVICE_DESCRIPTOR_INIT;
    descriptor.requiredFeatureCount = features.featureCount;
    descriptor.requiredFeatures = features.features;
    descriptor.requiredLimits = adapter;
    request reply = {0};
    WGPURequestDeviceCallbackInfo callback = {.mode=WGPUCallbackMode_AllowSpontaneous,.callback=requested,.userdata1=&reply};
    assert(wgpuAdapterRequestDevice(s->adapter, &descriptor, callback).id);
    assert(reply.status == WGPURequestDeviceStatus_Success && reply.device);
    WGPUDevice device = reply.device;
    WGPUSupportedFeatures enabled = WGPU_SUPPORTED_FEATURES_INIT;
    wgpuDeviceGetFeatures(device, &enabled);
    assert(enabled.featureCount == features.featureCount);
    for (size_t i = 0; i < features.featureCount; ++i) assert(wgpuDeviceHasFeature(device, features.features[i]));
    WGPULimits resolved = WGPU_LIMITS_INIT;
    assert(wgpuDeviceGetLimits(device, &resolved) == WGPUStatus_Success);
    for (size_t i = 0; i < sizeof(rows) / sizeof(rows[0]); ++i) assert(get(&resolved, &rows[i]) == get(adapter, &rows[i]));
    wgpuSupportedFeaturesFreeMembers(enabled);
    wgpuSupportedFeaturesFreeMembers(features);
    wgpuDeviceRelease(device);
}

int main(void)
{
    setvbuf(stdout, NULL, _IOLBF, 0);
    state s = {0};
    consumer_init(&s);
    WGPULimits adapter;
    check_adapter_and_defaults(&s, &adapter);
    check_required(&s, &adapter);
    check_every_feature_and_limit(&s, &adapter);
    check_default_validation(&s, &adapter);
    check_dispatch_and_binding_bounds(&s);
    check_sparse_bindings(&s);
    check_raised_limits(&s, &adapter);
    consumer_finish(&s);
    printf("PASS public limits: %zu adapter limits meet the WebGPU defaults, default device limits exact, "
        "requiredLimits refusal/default/adapter resolution, a device with every adapter feature and limit, default-limit validation of dynamic storage, "
        "workgroup size/storage, inter-stage location, buffer size, texture size, dispatch count and binding "
        "number, a sparse 0/17/31/500/999 group with binding-ordered dynamic offsets and an automatic layout, "
        "and executions above the default of 10 storage buffers, 8 storage textures, the adapter's vertex attributes and "
        "8 rgba32float targets (128 color bytes per sample), each refused on the default device; "
        "exact 32 words %s\n",
        sizeof(rows) / sizeof(rows[0]), adapter_class_scope());
    return 0;
}
