/* SPDX-License-Identifier: MIT
 * Phase-attributed application operation over the public webgpu.h ABI.
 *
 * One source, linked once per provider (Doe C, retained Doe Zig, Dawn), runs a
 * fixed compute application loop: one-time setup, shader compilation, pipeline
 * and resource preparation, then repeated upload -> encode -> submit ->
 * completion -> map/readback operations. Every phase boundary is a host
 * CLOCK_MONOTONIC reading, not a GPU timestamp. Each iteration's output is
 * checked against an independent CPU oracle after the host-operation interval
 * closes; a mismatch fails the process. Inputs alternate between two frozen
 * sets so a skipped dispatch, clear or copy cannot pass on stale bytes.
 * The process prints one JSON object on stdout. */
#define _POSIX_C_SOURCE 200809L
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <time.h>

#include "webgpu.h"

#define WAIT_NS 10000000000ull
#define MAX_ITERATIONS 4096
enum { P_UPLOAD, P_ENCODE, P_SUBMIT, P_COMPLETION, P_MAP, P_OPERATION, P_COUNT };
static const char *const PHASE_NAMES[P_COUNT] = {"upload", "encode", "submit", "completion", "map", "operation"};

static uint64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static void die(const char *what)
{
    printf("{\"status\":\"failed\",\"error\":\"%s\"}\n", what);
    fflush(stdout);
    exit(1);
}

static uint8_t *read_file(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    if (!f) die("input_open");
    if (fseek(f, 0, SEEK_END) || (*size = (size_t)ftell(f)) == 0 || fseek(f, 0, SEEK_SET)) die("input_size");
    uint8_t *bytes = malloc(*size + 1);
    if (!bytes || fread(bytes, 1, *size, f) != *size) die("input_read");
    bytes[*size] = 0;
    fclose(f);
    return bytes;
}

static WGPUStringView view(const char *text) { return (WGPUStringView){.data = text, .length = strlen(text)}; }

struct state {
    WGPUInstance instance;
    WGPUAdapter adapter;
    WGPUDevice device;
    bool done;
    int status;
    bool device_error;
};

static void wait_future(struct state *s, WGPUFuture future, const char *what)
{
    WGPUFutureWaitInfo info = {.future = future};
    if (wgpuInstanceWaitAny(s->instance, 1, &info, WAIT_NS) != WGPUWaitStatus_Success || !info.completed) die(what);
}

static void on_adapter(WGPURequestAdapterStatus status, WGPUAdapter adapter, WGPUStringView m, void *u1, void *u2)
{
    (void)m, (void)u2;
    struct state *s = u1;
    s->status = (int)status;
    s->adapter = adapter;
    s->done = true;
}

static void on_device(WGPURequestDeviceStatus status, WGPUDevice device, WGPUStringView m, void *u1, void *u2)
{
    (void)m, (void)u2;
    struct state *s = u1;
    s->status = (int)status;
    s->device = device;
    s->done = true;
}

static void on_error(WGPUDevice const *d, WGPUErrorType type, WGPUStringView m, void *u1, void *u2)
{
    (void)d, (void)u2;
    struct state *s = u1;
    s->device_error = true;
    fprintf(stderr, "uncaptured error %d: %.*s\n", (int)type, (int)m.length, m.data ? m.data : "");
}

static void on_work_done(WGPUQueueWorkDoneStatus status, WGPUStringView m, void *u1, void *u2)
{
    (void)m, (void)u2;
    struct state *s = u1;
    s->status = (int)status;
    s->done = true;
}

static void on_map(WGPUMapAsyncStatus status, WGPUStringView m, void *u1, void *u2)
{
    (void)m, (void)u2;
    struct state *s = u1;
    s->status = (int)status;
    s->done = true;
}

static void print_json_string(const char *key, WGPUStringView v)
{
    printf("\"%s\":\"", key);
    for (size_t i = 0; v.data && i < v.length && v.data[i]; i++) {
        char c = v.data[i];
        if (c == '"' || c == '\\') putchar('\\');
        putchar((unsigned char)c < 0x20 ? '?' : c);
    }
    printf("\",");
}

static double cpu_ms(void)
{
    struct rusage u;
    getrusage(RUSAGE_SELF, &u);
    return (u.ru_utime.tv_sec + u.ru_stime.tv_sec) * 1e3 + (u.ru_utime.tv_usec + u.ru_stime.tv_usec) / 1e3;
}

int main(int argc, char **argv)
{
    uint64_t process_start = now_ns();
    if (argc != 9) {
        fprintf(stderr, "usage: %s <shader.wgsl|-> <input0> <input1> <expectA0> <expectB0> <expectA1> <expectB1> <iterations>\n"
            "shader '-' selects the copy-only workload (expected = input)\n", argv[0]);
        return 2;
    }
    const bool copy_only = strcmp(argv[1], "-") == 0;
    size_t sizes[6];
    uint8_t *input[2], *expect_a[2], *expect_b[2];
    input[0] = read_file(argv[2], &sizes[0]);
    input[1] = read_file(argv[3], &sizes[1]);
    expect_a[0] = read_file(argv[4], &sizes[2]);
    expect_b[0] = read_file(argv[5], &sizes[3]);
    expect_a[1] = read_file(argv[6], &sizes[4]);
    expect_b[1] = read_file(argv[7], &sizes[5]);
    const size_t bytes = sizes[0];
    for (int i = 1; i < 6; i++) if (sizes[i] != bytes) die("input_size_mismatch");
    if (bytes % 256) die("input_alignment");
    const long iterations = strtol(argv[8], NULL, 10);
    if (iterations < 1 || iterations > MAX_ITERATIONS) die("iterations");
    size_t shader_size = 0;
    uint8_t *shader = copy_only ? NULL : read_file(argv[1], &shader_size);
    uint8_t *host = malloc(bytes);
    static uint64_t samples[P_COUNT][MAX_ITERATIONS];
    if (!host) die("oom");
    double cpu_start = cpu_ms();

    /* Setup: instance, physical adapter, device, queue. */
    struct state s = {0};
    uint64_t t0 = now_ns();
    WGPUInstanceFeatureName instance_features[] = {WGPUInstanceFeatureName_TimedWaitAny};
    WGPUInstanceDescriptor instance_desc = WGPU_INSTANCE_DESCRIPTOR_INIT;
    instance_desc.requiredFeatureCount = 1;
    instance_desc.requiredFeatures = instance_features;
    s.instance = wgpuCreateInstance(&instance_desc);
    if (!s.instance) die("instance");
    WGPURequestAdapterOptions adapter_options = WGPU_REQUEST_ADAPTER_OPTIONS_INIT;
    adapter_options.backendType = WGPUBackendType_Vulkan;
    adapter_options.powerPreference = WGPUPowerPreference_HighPerformance;
    WGPURequestAdapterCallbackInfo adapter_cb = WGPU_REQUEST_ADAPTER_CALLBACK_INFO_INIT;
    adapter_cb.mode = WGPUCallbackMode_WaitAnyOnly;
    adapter_cb.callback = on_adapter;
    adapter_cb.userdata1 = &s;
    wait_future(&s, wgpuInstanceRequestAdapter(s.instance, &adapter_options, adapter_cb), "adapter_wait");
    if (!s.done || s.status != (int)WGPURequestAdapterStatus_Success || !s.adapter) die("adapter");
    WGPUAdapterInfo info = WGPU_ADAPTER_INFO_INIT;
    if (wgpuAdapterGetInfo(s.adapter, &info) != WGPUStatus_Success) die("adapter_info");
    if (info.backendType != WGPUBackendType_Vulkan || info.adapterType == WGPUAdapterType_CPU) die("adapter_not_physical_vulkan");
    s.done = false;
    WGPUDeviceDescriptor device_desc = WGPU_DEVICE_DESCRIPTOR_INIT;
    device_desc.uncapturedErrorCallbackInfo.callback = on_error;
    device_desc.uncapturedErrorCallbackInfo.userdata1 = &s;
    WGPURequestDeviceCallbackInfo device_cb = WGPU_REQUEST_DEVICE_CALLBACK_INFO_INIT;
    device_cb.mode = WGPUCallbackMode_WaitAnyOnly;
    device_cb.callback = on_device;
    device_cb.userdata1 = &s;
    wait_future(&s, wgpuAdapterRequestDevice(s.adapter, &device_desc, device_cb), "device_wait");
    if (!s.done || s.status != (int)WGPURequestDeviceStatus_Success || !s.device) die("device");
    WGPUQueue queue = wgpuDeviceGetQueue(s.device);
    uint64_t t_setup = now_ns();

    /* Compile: WGSL shader module creation. */
    WGPUShaderModule module = NULL;
    if (!copy_only) {
        WGPUShaderSourceWGSL wgsl = WGPU_SHADER_SOURCE_WGSL_INIT;
        wgsl.code = (WGPUStringView){.data = (const char *)shader, .length = shader_size};
        WGPUShaderModuleDescriptor module_desc = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;
        module_desc.nextInChain = &wgsl.chain;
        module = wgpuDeviceCreateShaderModule(s.device, &module_desc);
        if (!module) die("shader_module");
    }
    uint64_t t_compile = now_ns();

    /* Pipeline: compute pipeline with the automatic layout. */
    WGPUComputePipeline pipeline = NULL;
    if (!copy_only) {
        WGPUComputePipelineDescriptor pipeline_desc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
        pipeline_desc.compute.module = module;
        pipeline_desc.compute.entryPoint = view("main");
        pipeline = wgpuDeviceCreateComputePipeline(s.device, &pipeline_desc);
        if (!pipeline) die("pipeline");
    }
    uint64_t t_pipeline = now_ns();

    /* Resources: buffers, parameters and the bind group, created once. */
    WGPUBufferDescriptor buffer_desc = WGPU_BUFFER_DESCRIPTOR_INIT;
    buffer_desc.size = bytes;
    buffer_desc.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst | WGPUBufferUsage_CopySrc;
    WGPUBuffer in_buffer = wgpuDeviceCreateBuffer(s.device, &buffer_desc);
    WGPUBuffer out_buffer = copy_only ? NULL : wgpuDeviceCreateBuffer(s.device, &buffer_desc);
    buffer_desc.usage = WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst;
    WGPUBuffer readback = wgpuDeviceCreateBuffer(s.device, &buffer_desc);
    if (!in_buffer || !readback || (!copy_only && !out_buffer)) die("buffers");
    WGPUBindGroup group = NULL;
    WGPUBuffer params = NULL;
    const uint32_t elements = (uint32_t)(bytes / 4);
    if (!copy_only) {
        buffer_desc.size = 16;
        buffer_desc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
        params = wgpuDeviceCreateBuffer(s.device, &buffer_desc);
        if (!params) die("params");
        const uint32_t words[4] = {elements, 0, 0, 0};
        wgpuQueueWriteBuffer(queue, params, 0, words, sizeof(words));
        WGPUBindGroupLayout layout = wgpuComputePipelineGetBindGroupLayout(pipeline, 0);
        WGPUBindGroupEntry entries[3] = {
            {.binding = 0, .buffer = params, .offset = 0, .size = 16},
            {.binding = 1, .buffer = in_buffer, .offset = 0, .size = bytes},
            {.binding = 2, .buffer = out_buffer, .offset = 0, .size = bytes},
        };
        WGPUBindGroupDescriptor group_desc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
        group_desc.layout = layout;
        group_desc.entryCount = 3;
        group_desc.entries = entries;
        group = wgpuDeviceCreateBindGroup(s.device, &group_desc);
        wgpuBindGroupLayoutRelease(layout);
        if (!group) die("bind_group");
    }
    uint64_t t_resources = now_ns();

    for (long it = 0; it < iterations; it++) {
        const int set = (int)(it & 1);
        uint64_t a = now_ns();
        wgpuQueueWriteBuffer(queue, in_buffer, 0, input[set], bytes);
        uint64_t b = now_ns();
        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s.device, NULL);
        WGPUBuffer source = in_buffer;
        if (!copy_only) {
            wgpuCommandEncoderClearBuffer(encoder, out_buffer, 0, bytes);
            WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, NULL);
            wgpuComputePassEncoderSetPipeline(pass, pipeline);
            wgpuComputePassEncoderSetBindGroup(pass, 0, group, 0, NULL);
            wgpuComputePassEncoderDispatchWorkgroups(pass, (elements + 63) / 64, 1, 1);
            wgpuComputePassEncoderEnd(pass);
            wgpuComputePassEncoderRelease(pass);
            source = out_buffer;
        }
        wgpuCommandEncoderCopyBufferToBuffer(encoder, source, 0, readback, 0, bytes);
        WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, NULL);
        wgpuCommandEncoderRelease(encoder);
        if (!commands) die("encode");
        uint64_t c = now_ns();
        wgpuQueueSubmit(queue, 1, &commands);
        wgpuCommandBufferRelease(commands);
        uint64_t d = now_ns();
        s.done = false;
        WGPUQueueWorkDoneCallbackInfo done_cb = WGPU_QUEUE_WORK_DONE_CALLBACK_INFO_INIT;
        done_cb.mode = WGPUCallbackMode_WaitAnyOnly;
        done_cb.callback = on_work_done;
        done_cb.userdata1 = &s;
        wait_future(&s, wgpuQueueOnSubmittedWorkDone(queue, done_cb), "completion_wait");
        if (!s.done || s.status != (int)WGPUQueueWorkDoneStatus_Success) die("completion");
        uint64_t e = now_ns();
        s.done = false;
        WGPUBufferMapCallbackInfo map_cb = WGPU_BUFFER_MAP_CALLBACK_INFO_INIT;
        map_cb.mode = WGPUCallbackMode_WaitAnyOnly;
        map_cb.callback = on_map;
        map_cb.userdata1 = &s;
        wait_future(&s, wgpuBufferMapAsync(readback, WGPUMapMode_Read, 0, bytes, map_cb), "map_wait");
        if (!s.done || s.status != (int)WGPUMapAsyncStatus_Success) die("map");
        const void *mapped = wgpuBufferGetConstMappedRange(readback, 0, bytes);
        if (!mapped) die("mapped_range");
        memcpy(host, mapped, bytes);
        wgpuBufferUnmap(readback);
        uint64_t f = now_ns();
        samples[P_UPLOAD][it] = b - a;
        samples[P_ENCODE][it] = c - b;
        samples[P_SUBMIT][it] = d - c;
        samples[P_COMPLETION][it] = e - d;
        samples[P_MAP][it] = f - e;
        samples[P_OPERATION][it] = f - a;
        /* Independent oracle, outside the host-operation interval: each 32-bit
         * word equals the A or B reference (B == A unless fused rounding). */
        const uint32_t *got = (const uint32_t *)host, *ea = (const uint32_t *)expect_a[set], *eb = (const uint32_t *)expect_b[set];
        for (uint32_t w = 0; w < elements; w++)
            if (got[w] != ea[w] && got[w] != eb[w]) {
                fprintf(stderr, "oracle mismatch iteration %ld word %u: %08x\n", it, w, got[w]);
                die("oracle_mismatch");
            }
        if (s.device_error) die("device_error");
    }
    uint64_t t_loop = now_ns();

    /* Teardown with explicit device destruction. */
    if (group) wgpuBindGroupRelease(group);
    if (params) wgpuBufferRelease(params);
    if (out_buffer) wgpuBufferRelease(out_buffer);
    wgpuBufferRelease(in_buffer);
    wgpuBufferRelease(readback);
    if (pipeline) wgpuComputePipelineRelease(pipeline);
    if (module) wgpuShaderModuleRelease(module);
    wgpuQueueRelease(queue);
    wgpuDeviceDestroy(s.device);
    wgpuDeviceRelease(s.device);
    uint64_t t_teardown = now_ns();
    double cpu_total = cpu_ms() - cpu_start;
    struct rusage usage;
    getrusage(RUSAGE_SELF, &usage);

    printf("{\"status\":\"ok\",");
    print_json_string("adapterVendor", info.vendor);
    print_json_string("adapterArchitecture", info.architecture);
    print_json_string("adapterDevice", info.device);
    print_json_string("adapterDescription", info.description);
    printf("\"adapterType\":%d,\"vendorID\":%u,\"deviceID\":%u,\"bytes\":%zu,\"iterations\":%ld,"
        "\"oracleChecked\":%ld,\"setupNs\":%llu,\"compileNs\":%llu,\"pipelineNs\":%llu,"
        "\"resourcesNs\":%llu,\"loopNs\":%llu,\"teardownNs\":%llu,\"processInMainNs\":%llu,\"cpuMs\":%.3f,"
        "\"maxRssKiB\":%ld,\"phases\":{",
        (int)info.adapterType, info.vendorID, info.deviceID, bytes, iterations, iterations,
        (unsigned long long)(t_setup - t0), (unsigned long long)(t_compile - t_setup),
        (unsigned long long)(t_pipeline - t_compile), (unsigned long long)(t_resources - t_pipeline),
        (unsigned long long)(t_loop - t_resources), (unsigned long long)(t_teardown - t_loop),
        (unsigned long long)(t_teardown - process_start), cpu_total, usage.ru_maxrss);
    for (int p = 0; p < P_COUNT; p++) {
        printf("%s\"%s\":[", p ? "," : "", PHASE_NAMES[p]);
        for (long it = 0; it < iterations; it++) printf("%s%llu", it ? "," : "", (unsigned long long)samples[p][it]);
        printf("]");
    }
    printf("}}\n");
    wgpuAdapterInfoFreeMembers(info);
    wgpuAdapterRelease(s.adapter);
    wgpuInstanceRelease(s.instance);
    return 0;
}
