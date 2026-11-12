/* SPDX-License-Identifier: MIT
 * maxBufferSize and maxStorageBufferBindingSize are usable as reported. On a
 * device requiring the adapter values, one storage buffer just above the WebGPU
 * default 256 MiB (when the adapter reports more) and one of exactly
 * maxStorageBufferBindingSize are each bound whole. A compute pass writes words
 * at the start, around the 256 MiB boundary, the middle and the very end; a copy
 * of those byte offsets reads them back independently of shader addressing. A
 * second pass reads host-written words through a read-only binding and reports
 * arrayLength. With shader-f16 the same buffer is also addressed as array<f16>
 * (D3D12 raw views). Requesting either limit 4 bytes above the adapter fails,
 * and a buffer above the device's maxBufferSize is a validation error. A
 * near-limit allocation the device refuses for memory is printed, never passed
 * silently. Developer evidence on the printed adapter only. */
#include "consumer.h"
#include <stdlib.h>

enum { DEFAULT_MAX_BUFFER = 268435456, PROBES = 8, SHADER_BYTES = 2048 };

typedef struct request { WGPURequestDeviceStatus status; WGPUDevice device; } request;
static void requested(WGPURequestDeviceStatus status, WGPUDevice device, WGPUStringView message,
    void *userdata, void *unused)
{
    (void)message; (void)unused;
    *(request *)userdata = (request){status, device};
}

static WGPUDevice open_device(state *s, uint64_t max_buffer, uint64_t max_binding, bool f16)
{
    WGPULimits required = WGPU_LIMITS_INIT;
    required.maxBufferSize = max_buffer;
    required.maxStorageBufferBindingSize = max_binding;
    const WGPUFeatureName half = WGPUFeatureName_ShaderF16;
    WGPUDeviceDescriptor descriptor = WGPU_DEVICE_DESCRIPTOR_INIT;
    descriptor.requiredLimits = &required;
    descriptor.requiredFeatureCount = f16 ? 1 : 0;
    descriptor.requiredFeatures = f16 ? &half : NULL;
    descriptor.uncapturedErrorCallbackInfo = (WGPUUncapturedErrorCallbackInfo){.callback = error_reply, .userdata1 = s};
    request reply = {0};
    WGPURequestDeviceCallbackInfo callback = {.mode = WGPUCallbackMode_AllowSpontaneous, .callback = requested,
        .userdata1 = &reply};
    assert(wgpuAdapterRequestDevice(s->adapter, &descriptor, callback).id);
    assert((reply.status == WGPURequestDeviceStatus_Success) == (reply.device != NULL));
    return reply.device;
}

static void scope_reply(WGPUPopErrorScopeStatus status, WGPUErrorType type, WGPUStringView message,
    void *userdata, void *unused)
{
    (void)unused;
    assert(status == WGPUPopErrorScopeStatus_Success);
    if (type != WGPUErrorType_NoError) fprintf(stderr, "scope error: %.*s\n", (int)message.length, message.data);
    *(WGPUErrorType *)userdata = type;
}

static WGPUErrorType pop_scope(state *s)
{
    WGPUErrorType type = WGPUErrorType_Unknown;
    WGPUPopErrorScopeCallbackInfo callback = {.mode = WGPUCallbackMode_AllowSpontaneous,
        .callback = scope_reply, .userdata1 = &type};
    assert(wgpuDevicePopErrorScope(s->device, callback).id);
    return type;
}

static uint32_t word_value(uint64_t index) { return (uint32_t)(index * 2654435761u) + 7u; }

/* IEEE binary16 bits of a small positive integer (exact up to 2048). */
static uint16_t half_bits(uint32_t value)
{
    assert(value > 0 && value <= 2048);
    uint32_t exponent = 0;
    while ((value >> exponent) > 1) ++exponent;
    const uint32_t mantissa = (value << (10 - exponent)) & 0x3ffu;
    return (uint16_t)(((exponent + 15u) << 10) | mantissa);
}

static uint32_t half_value(uint64_t index) { return (uint32_t)(index % 2000u) + 1u; }

/* Distinct ascending element indices: start, both sides of the default
 * maxBufferSize boundary, the middle and the last elements. */
static size_t probes(uint64_t elements, uint64_t boundary, uint64_t out[PROBES])
{
    const uint64_t candidates[PROBES] = {0, 1, boundary - 1, boundary, elements / 2, elements - 3,
        elements - 2, elements - 1};
    size_t count = 0;
    for (size_t i = 0; i < PROBES; ++i) {
        const uint64_t value = candidates[i];
        if (value >= elements || (count && value <= out[count - 1])) continue;
        out[count++] = value;
    }
    return count;
}

/* Writes element `index` through the shader and reads element `index` written
 * by the host, for every probe. Returns false only for an out-of-memory refusal. */
static bool exercise(state *s, uint64_t size, bool f16)
{
    const uint64_t element = f16 ? 2 : 4, elements = size / element;
    const uint64_t boundary = DEFAULT_MAX_BUFFER / element;
    uint64_t index[PROBES];
    const size_t count = probes(elements, boundary, index);
    wgpuDevicePushErrorScope(s->device, WGPUErrorFilter_OutOfMemory);
    WGPUBufferDescriptor descriptor = {.size = size,
        .usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst};
    WGPUBuffer big = wgpuDeviceCreateBuffer(s->device, &descriptor);
    const WGPUErrorType allocation = pop_scope(s);
    if (allocation == WGPUErrorType_OutOfMemory) {
        if (big) wgpuBufferRelease(big);
        printf("NOT EXECUTED %s storage buffer of %llu bytes: the device reported out of memory\n",
            f16 ? "f16" : "u32", (unsigned long long)size);
        return false;
    }
    assert(allocation == WGPUErrorType_NoError && big);
    /* Host-written words the shader reads: each probe's 4-byte word gets the
     * read pattern at odd probes; the shader writes even probes. */
    uint32_t probe_words[PROBES] = {0};
    for (size_t i = 0; i < count; ++i) probe_words[i] = (uint32_t)index[i];
    for (size_t i = 1; i < count; i += 2) {
        const uint64_t byte = index[i] * element & ~UINT64_C(3);
        uint32_t word;
        if (f16) {
            const uint16_t bits = half_bits(half_value(index[i]));
            uint16_t pair[2] = {0, 0};
            pair[(index[i] * element - byte) / 2] = bits;
            memcpy(&word, pair, 4);
        } else word = word_value(index[i]) ^ 0x5a5a5a5au;
        wgpuQueueWriteBuffer(s->queue, big, byte, &word, 4);
    }
    WGPUBuffer probe_buffer = buffer(s, sizeof(probe_words), WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst);
    wgpuQueueWriteBuffer(s->queue, probe_buffer, 0, probe_words, sizeof(probe_words));
    WGPUBuffer result = buffer(s, 4 * (PROBES + 1), WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc);
    char write_code[SHADER_BYTES], read_code[SHADER_BYTES];
    const char *type = f16 ? "f16" : "u32";
    const char *prefix = f16 ? "enable f16;\n" : "";
    const char *written = f16 ? "f16(f32(w % 2000u + 1u))" : "w * 2654435761u + 7u";
    const char *loaded = f16 ? "u32(f32(big[probes[i]]))" : "big[probes[i]]";
    int used = snprintf(write_code, sizeof(write_code),
        "%s@group(0) @binding(0) var<storage, read_write> big: array<%s>;\n"
        "@group(0) @binding(1) var<storage, read> probes: array<u32>;\n"
        "@compute @workgroup_size(1) fn main() {\n"
        "  for (var i = 0u; i < %uu; i += 2u) { let w = probes[i]; big[w] = %s; }\n}\n",
        prefix, type, (unsigned)count, written);
    assert(used > 0 && (size_t)used < sizeof(write_code));
    used = snprintf(read_code, sizeof(read_code),
        "%s@group(0) @binding(0) var<storage, read> big: array<%s>;\n"
        "@group(0) @binding(1) var<storage, read> probes: array<u32>;\n"
        "@group(0) @binding(2) var<storage, read_write> result: array<u32>;\n"
        "@compute @workgroup_size(1) fn main() {\n  result[0] = arrayLength(&big);\n"
        "  for (var i = 1u; i < %uu; i += 2u) { result[i + 1u] = %s; }\n}\n",
        prefix, type, (unsigned)count, loaded);
    assert(used > 0 && (size_t)used < sizeof(read_code));
    WGPUComputePipeline writer = pipeline(s, write_code), reader = pipeline(s, read_code);
    const WGPUBindGroupEntry write_entries[2] = {{.binding = 0, .buffer = big, .size = size},
        {.binding = 1, .buffer = probe_buffer, .size = sizeof(probe_words)}};
    const WGPUBindGroupEntry read_entries[3] = {{.binding = 0, .buffer = big, .size = size},
        {.binding = 1, .buffer = probe_buffer, .size = sizeof(probe_words)},
        {.binding = 2, .buffer = result, .size = 4 * (PROBES + 1)}};
    WGPUBindGroup write_group = group(s, writer, write_entries, 2), read_group = group(s, reader, read_entries, 3);
    WGPUBuffer readback = buffer(s, 4 * (2 * PROBES + 1), WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead);
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device, NULL);
    WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, NULL);
    wgpuComputePassEncoderSetPipeline(pass, writer);
    wgpuComputePassEncoderSetBindGroup(pass, 0, write_group, 0, NULL);
    wgpuComputePassEncoderDispatchWorkgroups(pass, 1, 1, 1);
    wgpuComputePassEncoderSetPipeline(pass, reader);
    wgpuComputePassEncoderSetBindGroup(pass, 0, read_group, 0, NULL);
    wgpuComputePassEncoderDispatchWorkgroups(pass, 1, 1, 1);
    wgpuComputePassEncoderEnd(pass);
    wgpuComputePassEncoderRelease(pass);
    /* Words at each probe's byte offset, copied without shader addressing. */
    for (size_t i = 0; i < count; ++i)
        wgpuCommandEncoderCopyBufferToBuffer(encoder, big, index[i] * element & ~UINT64_C(3), readback, 4 * i, 4);
    wgpuCommandEncoderCopyBufferToBuffer(encoder, result, 0, readback, 4 * PROBES, 4 * (PROBES + 1));
    submit(s, encoder);
    const unsigned char *bytes = map(s, readback, 4 * (2 * PROBES + 1));
    uint32_t copied[PROBES], shader[PROBES + 1];
    memcpy(copied, bytes, sizeof(copied));
    memcpy(shader, bytes + 4 * PROBES, sizeof(shader));
    wgpuBufferUnmap(readback);
    assert(shader[0] == elements);
    for (size_t i = 0; i < count; ++i) {
        const uint64_t byte = index[i] * element;
        uint32_t observed = copied[i];
        if (f16) {
            uint16_t pair[2];
            memcpy(pair, &copied[i], 4);
            observed = pair[(byte & 3) / 2];
            /* Even probes were written by the shader, odd ones by the host. */
            if (observed != half_bits(half_value(index[i])))
                fprintf(stderr, "f16 element %llu: 0x%04x\n", (unsigned long long)index[i], observed);
            assert(observed == half_bits(half_value(index[i])));
            if (i % 2) assert(shader[i + 1] == half_value(index[i]));
        } else if (i % 2 == 0) {
            if (observed != word_value(index[i]))
                fprintf(stderr, "u32 word %llu: 0x%08x\n", (unsigned long long)index[i], observed);
            assert(observed == word_value(index[i]));
        } else {
            assert(observed == (word_value(index[i]) ^ 0x5a5a5a5au));
            assert(shader[i + 1] == observed);
        }
    }
    wgpuBufferRelease(readback);
    wgpuBindGroupRelease(write_group);
    wgpuBindGroupRelease(read_group);
    wgpuComputePipelineRelease(writer);
    wgpuComputePipelineRelease(reader);
    wgpuBufferRelease(result);
    wgpuBufferRelease(probe_buffer);
    wgpuBufferRelease(big);
    printf("%s storage binding of %llu bytes: %zu probes through element %llu exact, arrayLength %llu\n",
        type, (unsigned long long)size, count, (unsigned long long)index[count - 1], (unsigned long long)elements);
    return true;
}

int main(void)
{
    consumer_line_output();
    state base = {0};
    consumer_init(&base);
    WGPULimits adapter = WGPU_LIMITS_INIT;
    assert(wgpuAdapterGetLimits(base.adapter, &adapter) == WGPUStatus_Success);
    const uint64_t max_buffer = adapter.maxBufferSize, max_binding = adapter.maxStorageBufferBindingSize;
    const bool f16 = wgpuAdapterHasFeature(base.adapter, WGPUFeatureName_ShaderF16);
    printf("adapter maxBufferSize=%llu maxStorageBufferBindingSize=%llu shader-f16=%d\n",
        (unsigned long long)max_buffer, (unsigned long long)max_binding, (int)f16);
    assert(max_binding <= max_buffer && max_binding % 4 == 0 && max_binding >= 134217728);
    assert(!open_device(&base, max_buffer + 4, max_binding, false));
    assert(!open_device(&base, max_buffer, max_binding + 4, false));
    state raised = {.instance = base.instance, .adapter = base.adapter};
    raised.device = open_device(&raised, max_buffer, max_binding, f16);
    assert(raised.device);
    raised.queue = wgpuDeviceGetQueue(raised.device);
    WGPULimits resolved = WGPU_LIMITS_INIT;
    assert(wgpuDeviceGetLimits(raised.device, &resolved) == WGPUStatus_Success);
    assert(resolved.maxBufferSize == max_buffer && resolved.maxStorageBufferBindingSize == max_binding);
    wgpuDevicePushErrorScope(raised.device, WGPUErrorFilter_Validation);
    WGPUBufferDescriptor too_large = {.size = max_buffer + 4, .usage = WGPUBufferUsage_Storage};
    WGPUBuffer refused = wgpuDeviceCreateBuffer(raised.device, &too_large);
    expect_validation(&raised);
    if (refused) wgpuBufferRelease(refused); /* An error object, if returned. */
    unsigned executed = 0, sizes = 0;
    const uint64_t above_default = (uint64_t)DEFAULT_MAX_BUFFER + 65536;
    if (max_binding >= above_default) {
        ++sizes;
        assert(exercise(&raised, above_default, false));
        ++executed;
        if (f16) assert(exercise(&raised, above_default, true));
    }
    if (max_binding != above_default) {
        ++sizes;
        if (exercise(&raised, max_binding, false)) {
            ++executed;
            if (f16) assert(exercise(&raised, max_binding, true));
        }
    }
    assert(!raised.errors);
    wgpuQueueRelease(raised.queue);
    wgpuDeviceRelease(raised.device);
    consumer_finish(&base);
    printf("%s large storage bindings: %u of %u sizes executed exactly up to %llu bytes%s; limits +4 refused %s\n",
        executed == sizes ? "PASS" : "INCOMPLETE", executed, sizes, (unsigned long long)max_binding,
        f16 ? " (u32 and f16)" : "", adapter_class_scope());
    return executed == sizes ? 0 : 2;
}
