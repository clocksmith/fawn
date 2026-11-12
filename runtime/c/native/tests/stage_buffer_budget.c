/* SPDX-License-Identifier: MIT
 * maxStorageBuffersPerShaderStage is usable exactly as reported. A device can
 * require the adapter value but not one more. On that device a compute stage
 * with every storage buffer, every uniform buffer and immediate data executes
 * with exact readback, directly and indirectly, and one more storage buffer is
 * refused. A vertex stage
 * holding every per-stage buffer at once also draws exactly: the storage
 * buffers (with runtime sizes), the uniform buffers, immediate data and
 * maxVertexBuffers vertex streams. On Metal that is the whole 31-entry buffer
 * argument table (immediates share the size table's slot), so a reported value
 * one higher would not fit. */
#include "consumer.h"
#include <stdlib.h>

enum { MAX_STORAGE = 17, UNIFORMS = 12, VERTEX_BUFFERS = 8, SHADER_BYTES = 8192, IMMEDIATE = 7 };

typedef struct text { char bytes[SHADER_BYTES]; size_t used; } text;

static void append(text *t, const char *format, unsigned a, unsigned b)
{
    int written = snprintf(t->bytes + t->used, sizeof(t->bytes) - t->used, format, a, b);
    assert(written > 0 && (size_t)written < sizeof(t->bytes) - t->used);
    t->used += (size_t)written;
}

/* Group 0 holds `storage` runtime-sized u32 arrays, group 1 twelve vec4<u32>
 * uniforms. The total adds immediate data, every uniform's x and, for each
 * storage array, its element 1 and its arrayLength (which needs the
 * runtime-size table). Compute writes the total into each array's element 0. */
static void declarations(text *t, unsigned storage, const char *access)
{
    append(t, "requires immediate_address_space;\nvar<immediate> p: u32;\n", 0, 0);
    for (unsigned i = 0; i < storage; ++i) {
        append(t, "@group(0) @binding(%u) var<storage, ", i, 0);
        append(t, access, 0, 0);
        append(t, "> b%u: array<u32>;\n", i, 0);
    }
    for (unsigned j = 0; j < UNIFORMS; ++j) append(t, "@group(1) @binding(%u) var<uniform> u%u: vec4<u32>;\n", j, j);
    append(t, "fn total() -> u32 {\n  var sum = p;\n", 0, 0);
    for (unsigned j = 0; j < UNIFORMS; ++j) append(t, "  sum += u%u.x;\n", j, 0);
    for (unsigned i = 0; i < storage; ++i) append(t, "  sum += b%u[1] + arrayLength(&b%u);\n", i, i);
    append(t, "  return sum;\n}\n", 0, 0);
}

static void compute_shader(text *t, unsigned storage)
{
    declarations(t, storage, "read_write");
    append(t, "@compute @workgroup_size(1) fn main() {\n  let sum = total();\n  workgroupBarrier();\n", 0, 0);
    for (unsigned i = 0; i < storage; ++i) append(t, "  b%u[0] = sum + %uu;\n", i, i);
    append(t, "}\n", 0, 0);
}

static void render_shader(text *t, unsigned storage, unsigned vertex_buffers)
{
    declarations(t, storage, "read");
    append(t, "struct In {\n  @builtin(vertex_index) index: u32,\n", 0, 0);
    for (unsigned k = 0; k < vertex_buffers; ++k) append(t, "  @location(%u) a%u: u32,\n", k, k);
    append(t, "}\nstruct V { @builtin(position) position: vec4<f32>, @location(0) @interpolate(flat) value: u32 }\n"
        "@vertex fn vs(v: In) -> V {\n  var o: V;\n"
        "  o.position = vec4<f32>(f32(v.index == 1u) * 4.0 - 1.0, f32(v.index == 2u) * 4.0 - 1.0, 0.0, 1.0);\n"
        "  o.value = total()", 0, 0);
    for (unsigned k = 0; k < vertex_buffers; ++k) append(t, " + v.a%u", k, 0);
    append(t, ";\n  return o;\n}\n@fragment fn fs(v: V) -> @location(0) vec4<u32> { return vec4<u32>(v.value, 0u, 0u, 1u); }\n", 0, 0);
}

static WGPUShaderModule module(state *s, const text *t)
{
    WGPUShaderSourceWGSL wgsl = WGPU_SHADER_SOURCE_WGSL_INIT;
    wgsl.code = (WGPUStringView){t->bytes, t->used};
    WGPUShaderModuleDescriptor descriptor = {.nextInChain = &wgsl.chain};
    WGPUShaderModule result = wgpuDeviceCreateShaderModule(s->device, &descriptor);
    assert(result);
    return result;
}

typedef struct request { WGPURequestDeviceStatus status; WGPUDevice device; } request;
static void requested(WGPURequestDeviceStatus status, WGPUDevice device, WGPUStringView message,
    void *userdata, void *unused)
{
    (void)message; (void)unused;
    *(request *)userdata = (request){status, device};
}

static WGPUDevice open_device(state *s, uint32_t storage)
{
    WGPULimits required = WGPU_LIMITS_INIT;
    required.maxStorageBuffersPerShaderStage = storage;
    WGPUDeviceDescriptor descriptor = WGPU_DEVICE_DESCRIPTOR_INIT;
    descriptor.requiredLimits = &required;
    descriptor.uncapturedErrorCallbackInfo = (WGPUUncapturedErrorCallbackInfo){.callback = error_reply, .userdata1 = s};
    request reply = {0};
    WGPURequestDeviceCallbackInfo callback = {.mode = WGPUCallbackMode_AllowSpontaneous, .callback = requested,
        .userdata1 = &reply};
    assert(wgpuAdapterRequestDevice(s->adapter, &descriptor, callback).id);
    assert((reply.status == WGPURequestDeviceStatus_Success) == (reply.device != NULL));
    return reply.device;
}

/* Storage array i holds 2 + i words with element 1 = 100 * (i + 1); uniform j
 * holds x = j + 1. Returns the shader total for `storage` arrays. */
typedef struct resources {
    WGPUBuffer storage[MAX_STORAGE], uniforms[UNIFORMS];
    WGPUBindGroup groups[2];
    uint32_t total;
} resources;

static void make_resources(state *s, resources *r, WGPUBindGroupLayout layouts[2], unsigned storage,
    WGPUBufferUsage storage_usage)
{
    WGPUBindGroupEntry storage_entries[MAX_STORAGE], uniform_entries[UNIFORMS];
    r->total = IMMEDIATE;
    for (unsigned i = 0; i < storage; ++i) {
        const uint32_t words = 2 + i, size = words * 4;
        r->storage[i] = buffer(s, size, storage_usage | WGPUBufferUsage_CopyDst | WGPUBufferUsage_CopySrc);
        const uint32_t initial[2] = {0, 100 * (i + 1)};
        wgpuQueueWriteBuffer(s->queue, r->storage[i], 0, initial, sizeof(initial));
        storage_entries[i] = (WGPUBindGroupEntry){.binding = i, .buffer = r->storage[i], .size = size};
        r->total += 100 * (i + 1) + words;
    }
    for (unsigned j = 0; j < UNIFORMS; ++j) {
        r->uniforms[j] = buffer(s, 16, WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst);
        const uint32_t value[4] = {j + 1, 0, 0, 0};
        wgpuQueueWriteBuffer(s->queue, r->uniforms[j], 0, value, sizeof(value));
        uniform_entries[j] = (WGPUBindGroupEntry){.binding = j, .buffer = r->uniforms[j], .size = 16};
        r->total += j + 1;
    }
    WGPUBindGroupDescriptor storage_group = {.layout = layouts[0], .entryCount = storage, .entries = storage_entries};
    WGPUBindGroupDescriptor uniform_group = {.layout = layouts[1], .entryCount = UNIFORMS, .entries = uniform_entries};
    r->groups[0] = wgpuDeviceCreateBindGroup(s->device, &storage_group);
    r->groups[1] = wgpuDeviceCreateBindGroup(s->device, &uniform_group);
    assert(r->groups[0] && r->groups[1]);
}

static void release_resources(resources *r, unsigned storage)
{
    wgpuBindGroupRelease(r->groups[0]);
    wgpuBindGroupRelease(r->groups[1]);
    for (unsigned i = 0; i < storage; ++i) wgpuBufferRelease(r->storage[i]);
    for (unsigned j = 0; j < UNIFORMS; ++j) wgpuBufferRelease(r->uniforms[j]);
}

static WGPUComputePipeline compute_pipeline(state *s, unsigned storage)
{
    text *t = calloc(1, sizeof(*t));
    assert(t);
    compute_shader(t, storage);
    WGPUShaderModule shader = module(s, t);
    free(t);
    WGPUComputePipelineDescriptor descriptor = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
    descriptor.compute.module = shader;
    WGPUComputePipeline result = wgpuDeviceCreateComputePipeline(s->device, &descriptor);
    wgpuShaderModuleRelease(shader);
    return result;
}

static void compute_refused(state *s, unsigned storage)
{
    wgpuDevicePushErrorScope(s->device, WGPUErrorFilter_Validation);
    WGPUComputePipeline refused = compute_pipeline(s, storage);
    expect_validation(s);
    assert(!refused);
}

static void compute_executes(state *s, unsigned storage, bool indirect)
{
    WGPUComputePipeline compute = compute_pipeline(s, storage);
    assert(compute);
    WGPUBindGroupLayout layouts[2] = {wgpuComputePipelineGetBindGroupLayout(compute, 0),
        wgpuComputePipelineGetBindGroupLayout(compute, 1)};
    resources r;
    make_resources(s, &r, layouts, storage, WGPUBufferUsage_Storage);
    WGPUBuffer readback = buffer(s, (uint64_t)storage * 256, WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead);
    WGPUBuffer arguments = buffer(s, 12, WGPUBufferUsage_Indirect | WGPUBufferUsage_CopyDst);
    const uint32_t groups[3] = {1, 1, 1};
    wgpuQueueWriteBuffer(s->queue, arguments, 0, groups, sizeof(groups));
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device, NULL);
    WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, NULL);
    wgpuComputePassEncoderSetPipeline(pass, compute);
    wgpuComputePassEncoderSetBindGroup(pass, 0, r.groups[0], 0, NULL);
    wgpuComputePassEncoderSetBindGroup(pass, 1, r.groups[1], 0, NULL);
    const uint32_t immediate = IMMEDIATE;
    wgpuComputePassEncoderSetImmediates(pass, 0, &immediate, sizeof(immediate));
    if (indirect) wgpuComputePassEncoderDispatchWorkgroupsIndirect(pass, arguments, 0);
    else wgpuComputePassEncoderDispatchWorkgroups(pass, 1, 1, 1);
    wgpuComputePassEncoderEnd(pass);
    wgpuComputePassEncoderRelease(pass);
    for (unsigned i = 0; i < storage; ++i)
        wgpuCommandEncoderCopyBufferToBuffer(encoder, r.storage[i], 0, readback, (uint64_t)i * 256, 8);
    submit(s, encoder);
    const unsigned char *bytes = map(s, readback, (size_t)storage * 256);
    for (unsigned i = 0; i < storage; ++i) {
        uint32_t words[2];
        memcpy(words, bytes + (size_t)i * 256, sizeof(words));
        assert(words[0] == r.total + i && words[1] == 100 * (i + 1));
    }
    wgpuBufferUnmap(readback);
    wgpuBufferRelease(readback);
    wgpuBufferRelease(arguments);
    release_resources(&r, storage);
    wgpuBindGroupLayoutRelease(layouts[0]);
    wgpuBindGroupLayoutRelease(layouts[1]);
    wgpuComputePipelineRelease(compute);
    printf("compute stage (%s): %u storage + %u uniform buffers + immediates, exact total %u\n",
        indirect ? "indirect" : "direct", storage, UNIFORMS, r.total);
}

static void vertex_stage_executes(state *s, unsigned storage, unsigned vertex_buffers)
{
    text *t = calloc(1, sizeof(*t));
    assert(t);
    render_shader(t, storage, vertex_buffers);
    WGPUShaderModule shader = module(s, t);
    free(t);
    WGPUVertexAttribute attributes[VERTEX_BUFFERS];
    WGPUVertexBufferLayout streams[VERTEX_BUFFERS];
    for (unsigned k = 0; k < vertex_buffers; ++k) {
        attributes[k] = (WGPUVertexAttribute){.format = WGPUVertexFormat_Uint32, .offset = 0, .shaderLocation = k};
        streams[k] = (WGPUVertexBufferLayout){.stepMode = WGPUVertexStepMode_Instance, .arrayStride = 4,
            .attributeCount = 1, .attributes = &attributes[k]};
    }
    WGPUColorTargetState target = WGPU_COLOR_TARGET_STATE_INIT;
    target.format = WGPUTextureFormat_R32Uint;
    WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
    fragment.module = shader;
    fragment.targetCount = 1;
    fragment.targets = &target;
    WGPURenderPipelineDescriptor descriptor = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    descriptor.vertex.module = shader;
    descriptor.vertex.bufferCount = vertex_buffers;
    descriptor.vertex.buffers = streams;
    descriptor.fragment = &fragment;
    WGPURenderPipeline render = wgpuDeviceCreateRenderPipeline(s->device, &descriptor);
    wgpuShaderModuleRelease(shader);
    assert(render);
    WGPUBindGroupLayout layouts[2] = {wgpuRenderPipelineGetBindGroupLayout(render, 0),
        wgpuRenderPipelineGetBindGroupLayout(render, 1)};
    resources r;
    make_resources(s, &r, layouts, storage, WGPUBufferUsage_Storage);
    WGPUBuffer vertices[VERTEX_BUFFERS];
    uint32_t expected = r.total;
    for (unsigned k = 0; k < vertex_buffers; ++k) {
        const uint32_t value = 10000 * (k + 1);
        vertices[k] = buffer(s, 4, WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst);
        wgpuQueueWriteBuffer(s->queue, vertices[k], 0, &value, sizeof(value));
        expected += value;
    }
    WGPUTexture color = texture(s, WGPUTextureFormat_R32Uint, WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc, 1);
    WGPUTextureView view = wgpuTextureCreateView(color, NULL);
    WGPURenderPassColorAttachment attachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    attachment.view = view;
    attachment.loadOp = WGPULoadOp_Clear;
    attachment.storeOp = WGPUStoreOp_Store;
    WGPURenderPassDescriptor pass_descriptor = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
    pass_descriptor.colorAttachmentCount = 1;
    pass_descriptor.colorAttachments = &attachment;
    WGPUBuffer readback = buffer(s, 512, WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead);
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device, NULL);
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &pass_descriptor);
    wgpuRenderPassEncoderSetPipeline(pass, render);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, r.groups[0], 0, NULL);
    wgpuRenderPassEncoderSetBindGroup(pass, 1, r.groups[1], 0, NULL);
    const uint32_t immediate = IMMEDIATE;
    wgpuRenderPassEncoderSetImmediates(pass, 0, &immediate, sizeof(immediate));
    for (unsigned k = 0; k < vertex_buffers; ++k) wgpuRenderPassEncoderSetVertexBuffer(pass, k, vertices[k], 0, 4);
    wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
    WGPUTexelCopyTextureInfo source = {.texture = color};
    WGPUTexelCopyBufferInfo destination = {.buffer = readback, .layout = {.bytesPerRow = 256, .rowsPerImage = 2}};
    WGPUExtent3D size = {2, 2, 1};
    wgpuCommandEncoderCopyTextureToBuffer(encoder, &source, &destination, &size);
    submit(s, encoder);
    const unsigned char *bytes = map(s, readback, 512);
    for (unsigned row = 0; row < 2; ++row)
        for (unsigned column = 0; column < 2; ++column) {
            uint32_t texel;
            memcpy(&texel, bytes + row * 256 + column * 4, sizeof(texel));
            assert(texel == expected);
        }
    wgpuBufferUnmap(readback);
    wgpuBufferRelease(readback);
    wgpuTextureViewRelease(view);
    wgpuTextureRelease(color);
    for (unsigned k = 0; k < vertex_buffers; ++k) wgpuBufferRelease(vertices[k]);
    release_resources(&r, storage);
    wgpuBindGroupLayoutRelease(layouts[0]);
    wgpuBindGroupLayoutRelease(layouts[1]);
    wgpuRenderPipelineRelease(render);
    printf("vertex stage: %u storage + %u uniform buffers + immediates + %u vertex streams, exact texels %u\n",
        storage, UNIFORMS, vertex_buffers, expected);
}

int main(void)
{
    consumer_line_output();
    state base = {0};
    consumer_init(&base);
    WGPULimits adapter = WGPU_LIMITS_INIT, defaults = WGPU_LIMITS_INIT;
    assert(wgpuAdapterGetLimits(base.adapter, &adapter) == WGPUStatus_Success);
    assert(wgpuDeviceGetLimits(base.device, &defaults) == WGPUStatus_Success);
    const unsigned storage = adapter.maxStorageBuffersPerShaderStage;
    assert(storage >= 8 && storage < MAX_STORAGE && defaults.maxStorageBuffersPerShaderStage == 8);
    assert(adapter.maxUniformBuffersPerShaderStage >= UNIFORMS && adapter.maxImmediateSize >= 4);
    assert(adapter.maxBindGroups >= 2 && adapter.maxVertexBuffers >= 8);
    /* Eight streams: every WebGPU device admits them, and Metal reports eight. */
    const unsigned vertex_buffers = VERTEX_BUFFERS;
    assert(vertex_buffers <= adapter.maxVertexAttributes);
    printf("adapter maxStorageBuffersPerShaderStage=%u maxUniformBuffersPerShaderStage=%u maxVertexBuffers=%u\n",
        storage, adapter.maxUniformBuffersPerShaderStage, adapter.maxVertexBuffers);
    /* The default device admits exactly the WebGPU default, 8. */
    compute_executes(&base, 8, false);
    compute_refused(&base, 9);
    /* One above the adapter is refused; the adapter value opens a device. */
    assert(!open_device(&base, storage + 1));
    state raised = {.instance = base.instance, .adapter = base.adapter};
    raised.device = open_device(&raised, storage);
    assert(raised.device);
    raised.queue = wgpuDeviceGetQueue(raised.device);
    WGPULimits resolved = WGPU_LIMITS_INIT;
    assert(wgpuDeviceGetLimits(raised.device, &resolved) == WGPUStatus_Success);
    assert(resolved.maxStorageBuffersPerShaderStage == storage);
    compute_executes(&raised, storage, false);
    compute_executes(&raised, storage, true);
    compute_refused(&raised, storage + 1);
    vertex_stage_executes(&raised, storage, vertex_buffers);
    assert(!raised.errors);
    wgpuQueueRelease(raised.queue);
    wgpuDeviceRelease(raised.device);
    consumer_finish(&base);
    printf("PASS per-stage storage buffers: %u (adapter) usable in compute and in a full vertex stage, %u refused "
        "on that device and for requiredLimits, default device exactly 8 %s\n", storage, storage + 1, adapter_class_scope());
    return 0;
}
