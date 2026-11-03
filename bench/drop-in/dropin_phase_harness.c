/* SPDX-License-Identifier: MIT
 * Phase-attributed application operation over the public webgpu.h ABI.
 *
 * One source, linked once per provider (Doe C, retained Doe Zig, Dawn), runs a
 * fixed compute application loop: one-time setup, shader compilation, pipeline
 * and resource preparation, then repeated upload -> encode -> submit ->
 * completion -> map/readback operations. Every phase boundary is a host
 * monotonic host-clock reading, not a GPU timestamp (CLOCK_MONOTONIC; on Apple,
 * CLOCK_UPTIME_RAW, because Darwin quantizes CLOCK_MONOTONIC to 1 us; on
 * Windows, QueryPerformanceCounter). The
 * backend is fixed at compile time: Vulkan by default, or the explicitly
 * selected Metal/D3D12 backend; the adapter must report it. Each iteration's output is
 * checked against an independent CPU oracle after its host-operation interval
 * closes; a mismatch fails the process. Optional depth two permits overlap,
 * with separate readback slots and per-slot alternating inputs. The optional
 * independent mode also rotates input/output buffers and their bind groups. Its wall time
 * includes oracle work; overlapping latency intervals must never be summed.
 * Inputs alternate between two frozen
 * sets so a skipped dispatch, clear or copy cannot pass on stale bytes.
 * The process prints one JSON object on stdout. */
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE /* ru_maxrss */
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#else
#include <sys/resource.h>
#include <time.h>
#endif

/* Dynamic provider loading is opt-in: neither the provider nor its linked
 * frameworks may load before setup starts. Legacy linked builds are unchanged. */
#if defined(DOE_PHASE_LOAD_PROVIDER)
#if !defined(__APPLE__) || !defined(DOE_PHASE_BACKEND_METAL)
#error DOE_PHASE_LOAD_PROVIDER requires Apple Metal
#endif
#define WGPU_SKIP_DECLARATIONS
#include <dlfcn.h>
#include <mach-o/dyld.h>
#endif
#include "webgpu.h"

#if defined(DOE_PHASE_LOAD_PROVIDER)
#define PHASE_PROCS(X) \
    X(AdapterGetInfo) \
    X(AdapterInfoFreeMembers) \
    X(AdapterRelease) \
    X(AdapterRequestDevice) \
    X(BindGroupLayoutRelease) \
    X(BindGroupRelease) \
    X(BufferGetConstMappedRange) \
    X(BufferMapAsync) \
    X(BufferRelease) \
    X(BufferUnmap) \
    X(CommandBufferRelease) \
    X(CommandEncoderBeginComputePass) \
    X(CommandEncoderClearBuffer) \
    X(CommandEncoderCopyBufferToBuffer) \
    X(CommandEncoderFinish) \
    X(CommandEncoderRelease) \
    X(ComputePassEncoderDispatchWorkgroups) \
    X(ComputePassEncoderEnd) \
    X(ComputePassEncoderRelease) \
    X(ComputePassEncoderSetBindGroup) \
    X(ComputePassEncoderSetPipeline) \
    X(ComputePipelineGetBindGroupLayout) \
    X(ComputePipelineRelease) \
    X(CreateInstance) \
    X(DeviceCreateBindGroup) \
    X(DeviceCreateBuffer) \
    X(DeviceCreateCommandEncoder) \
    X(DeviceCreateComputePipeline) \
    X(DeviceCreateShaderModule) \
    X(DeviceDestroy) \
    X(DeviceGetQueue) \
    X(DeviceRelease) \
    X(InstanceRelease) \
    X(InstanceRequestAdapter) \
    X(InstanceWaitAny) \
    X(QueueOnSubmittedWorkDone) \
    X(QueueRelease) \
    X(QueueSubmit) \
    X(QueueWriteBuffer) \
    X(ShaderModuleRelease)
#define DECLARE_PROC(name) static WGPUProc##name wgpu##name;
PHASE_PROCS(DECLARE_PROC)
#undef DECLARE_PROC
#endif

#if defined(DOE_PHASE_BACKEND_METAL) && defined(DOE_PHASE_BACKEND_D3D12)
#error Select exactly one phase backend
#elif defined(DOE_PHASE_BACKEND_METAL)
#define PHASE_BACKEND WGPUBackendType_Metal
#define PHASE_BACKEND_ERROR "adapter_not_physical_metal"
#elif defined(DOE_PHASE_BACKEND_D3D12)
#define PHASE_BACKEND WGPUBackendType_D3D12
#define PHASE_BACKEND_ERROR "adapter_not_physical_d3d12"
#else
#define PHASE_BACKEND WGPUBackendType_Vulkan
#define PHASE_BACKEND_ERROR "adapter_not_physical_vulkan"
#endif
#if defined(_WIN32)
/* Peak working set and process CPU come from Win32; see now_ns and cpu_ms. */
#elif defined(__APPLE__)
#define PHASE_CLOCK CLOCK_UPTIME_RAW
#define RSS_UNITS_PER_KIB 1024 /* Darwin ru_maxrss is bytes */
#else
#define PHASE_CLOCK CLOCK_MONOTONIC
#define RSS_UNITS_PER_KIB 1 /* Linux ru_maxrss is KiB */
#endif
#define WAIT_NS 10000000000ull
#define MAX_ITERATIONS 4096
/* DOE_PHASE_UPLOAD_SPLIT (build --upload-split): before wgpuQueueWriteBuffer the
 * harness reads its own source array once, timed as uploadSource, so uploadDoe
 * is the provider's call with a warm source; upload stays their sum. Off by
 * default, which keeps the original phases and timing. */
#if defined(DOE_PHASE_UPLOAD_SPLIT)
enum { P_UPLOAD, P_ENCODE, P_SUBMIT, P_COMPLETION, P_MAP, P_OPERATION, P_UPLOAD_SOURCE, P_UPLOAD_DOE, P_COUNT };
static const char *const PHASE_NAMES[P_COUNT] = {"upload", "encode", "submit", "completion", "map", "operation",
    "uploadSource", "uploadDoe"};
static volatile uint64_t source_sink;
#else
enum { P_UPLOAD, P_ENCODE, P_SUBMIT, P_COMPLETION, P_MAP, P_OPERATION, P_COUNT };
static const char *const PHASE_NAMES[P_COUNT] = {"upload", "encode", "submit", "completion", "map", "operation"};
#endif

static uint64_t now_ns(void)
{
#ifdef _WIN32
    static LARGE_INTEGER frequency;
    LARGE_INTEGER counter;
    if (!frequency.QuadPart) QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&counter);
    const uint64_t ticks = (uint64_t)counter.QuadPart, hz = (uint64_t)frequency.QuadPart;
    return ticks / hz * 1000000000ull + ticks % hz * 1000000000ull / hz;
#else
    struct timespec ts;
    clock_gettime(PHASE_CLOCK, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
#endif
}

static void die(const char *what)
{
    printf("{\"status\":\"failed\",\"error\":\"%s\"}\n", what);
    fflush(stdout);
    exit(1);
}

#if defined(DOE_PHASE_LOAD_PROVIDER)
/* This executable links only libSystem. Refuse accidental provider/framework
 * preload (including DYLD_INSERT_LIBRARIES) instead of silently understating
 * setup. Image enumeration does not load any framework. */
static unsigned frameworks_before_setup(void)
{
    unsigned count = 0;
    for (uint32_t i = 0; i < _dyld_image_count(); ++i) {
        const char *path = _dyld_get_image_name(i);
        if (path && (strstr(path, "/Foundation.framework/") ||
                strstr(path, "/Metal.framework/") || strstr(path, "/IOSurface.framework/") ||
                strstr(path, "/CoreVideo.framework/"))) ++count;
        if (path && (strstr(path, "libwebgpu") || strstr(path, "libdoe_compiler")))
            die("provider_loaded_before_setup");
    }
    return count;
}

static void load_provider(void)
{
    /* Retained until process exit: pending callbacks can retain provider code. */
    void *library = dlopen(DOE_PHASE_LOAD_PROVIDER, RTLD_NOW | RTLD_LOCAL);
    if (!library) {
        fprintf(stderr, "provider dlopen: %s\n", dlerror());
        die("provider_load");
    }
#define LOAD_PROC(name) do { \
    void *symbol = dlsym(library, "wgpu" #name); \
    if (!symbol) { fprintf(stderr, "missing provider symbol: wgpu%s\n", #name); die("provider_symbol"); } \
    _Static_assert(sizeof(wgpu##name) == sizeof(symbol), "function pointer ABI"); \
    memcpy(&wgpu##name, &symbol, sizeof(symbol)); \
} while (0);
    PHASE_PROCS(LOAD_PROC)
#undef LOAD_PROC
}
#endif

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
#ifdef _WIN32
    FILETIME created, exited, kernel, user;
    if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) die("process_times");
    const uint64_t k = ((uint64_t)kernel.dwHighDateTime << 32) | kernel.dwLowDateTime;
    const uint64_t u = ((uint64_t)user.dwHighDateTime << 32) | user.dwLowDateTime;
    return (double)(k + u) / 1e4; /* 100 ns units */
#else
    struct rusage u;
    getrusage(RUSAGE_SELF, &u);
    return (u.ru_utime.tv_sec + u.ru_stime.tv_sec) * 1e3 + (u.ru_utime.tv_usec + u.ru_stime.tv_usec) / 1e3;
#endif
}

static long max_rss_kib(void)
{
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS counters = {.cb = sizeof(counters)};
    if (!GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters))) die("process_memory");
    return (long)(counters.PeakWorkingSetSize / 1024);
#else
    struct rusage usage;
    getrusage(RUSAGE_SELF, &usage);
    return usage.ru_maxrss / RSS_UNITS_PER_KIB;
#endif
}

int main(int argc, char **argv)
{
    uint64_t process_start = now_ns();
    if (argc < 9 || argc > 11) {
        fprintf(stderr, "usage: %s <shader.wgsl|-> <input0> <input1> <expectA0> <expectB0> <expectA1> <expectB1> <iterations> [inflight-depth:1|2] [shared|independent]\n"
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
    if (argc >= 10 && strcmp(argv[9], "1") && strcmp(argv[9], "2")) die("inflight_depth");
    const int depth = argc >= 10 && !strcmp(argv[9], "2") ? 2 : 1;
    if (argc == 11 && strcmp(argv[10], "shared") && strcmp(argv[10], "independent")) die("buffer_reuse");
    const bool independent = argc == 11 && !strcmp(argv[10], "independent");
    if ((depth != 1 && depth != 2) || iterations < depth) die("inflight_depth");
    size_t shader_size = 0;
    uint8_t *shader = copy_only ? NULL : read_file(argv[1], &shader_size);
    uint8_t *host = malloc(bytes);
    static uint64_t samples[P_COUNT][MAX_ITERATIONS];
    if (!host) die("oom");
    double cpu_start = cpu_ms();

#if defined(DOE_PHASE_LOAD_PROVIDER)
    const unsigned framework_images_before = frameworks_before_setup();
    if (framework_images_before != 0) die("framework_loaded_before_setup");
    const uint32_t images_before = _dyld_image_count();
#endif
    /* Setup includes provider and framework loading in dynamic builds, followed
     * by instance, physical adapter, device and queue on both providers. */
    struct state s = {0};
    uint64_t t0 = now_ns();
#if defined(DOE_PHASE_LOAD_PROVIDER)
    load_provider();
    const uint64_t t_provider = now_ns();
    const uint32_t provider_images = _dyld_image_count() - images_before;
#else
    const uint64_t t_provider = t0;
#endif
    WGPUInstanceFeatureName instance_features[] = {WGPUInstanceFeatureName_TimedWaitAny};
    WGPUInstanceDescriptor instance_desc = WGPU_INSTANCE_DESCRIPTOR_INIT;
    instance_desc.requiredFeatureCount = 1;
    instance_desc.requiredFeatures = instance_features;
    s.instance = wgpuCreateInstance(&instance_desc);
    if (!s.instance) die("instance");
    uint64_t t_instance = now_ns();
    WGPURequestAdapterOptions adapter_options = WGPU_REQUEST_ADAPTER_OPTIONS_INIT;
    adapter_options.backendType = PHASE_BACKEND;
    adapter_options.powerPreference = WGPUPowerPreference_HighPerformance;
    WGPURequestAdapterCallbackInfo adapter_cb = WGPU_REQUEST_ADAPTER_CALLBACK_INFO_INIT;
    adapter_cb.mode = WGPUCallbackMode_WaitAnyOnly;
    adapter_cb.callback = on_adapter;
    adapter_cb.userdata1 = &s;
    wait_future(&s, wgpuInstanceRequestAdapter(s.instance, &adapter_options, adapter_cb), "adapter_wait");
    if (!s.done || s.status != (int)WGPURequestAdapterStatus_Success || !s.adapter) die("adapter");
    WGPUAdapterInfo info = WGPU_ADAPTER_INFO_INIT;
    if (wgpuAdapterGetInfo(s.adapter, &info) != WGPUStatus_Success) die("adapter_info");
    if (info.backendType != PHASE_BACKEND || info.adapterType == WGPUAdapterType_CPU) die(PHASE_BACKEND_ERROR);
    uint64_t t_adapter = now_ns();
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
#if defined(DOE_PHASE_LOAD_PROVIDER)
    const uint32_t setup_images = _dyld_image_count() - images_before;
#endif

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
    WGPUBuffer in_buffers[2] = {0}, out_buffers[2] = {0};
    WGPUBindGroup groups[2] = {0};
    const int resource_slots = independent ? depth : 1;
    for (int i = 0; i < resource_slots; ++i) {
        in_buffers[i] = wgpuDeviceCreateBuffer(s.device, &buffer_desc);
        out_buffers[i] = copy_only ? NULL : wgpuDeviceCreateBuffer(s.device, &buffer_desc);
        if (!in_buffers[i] || (!copy_only && !out_buffers[i])) die("buffers");
    }
    buffer_desc.usage = WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst;
    struct flight {
        WGPUBuffer readback;
        struct state callback;
        WGPUFuture completion;
        uint64_t start, submitted;
    } flights[2] = {0};
    for (int i = 0; i < depth; ++i) {
        flights[i].readback = wgpuDeviceCreateBuffer(s.device, &buffer_desc);
        flights[i].callback.instance = s.instance;
        if (!flights[i].readback) die("readback_buffer");
    }
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
        for (int i = 0; i < resource_slots; ++i) {
            WGPUBindGroupEntry entries[3] = {
                {.binding = 0, .buffer = params, .offset = 0, .size = 16},
                {.binding = 1, .buffer = in_buffers[i], .offset = 0, .size = bytes},
                {.binding = 2, .buffer = out_buffers[i], .offset = 0, .size = bytes},
            };
            WGPUBindGroupDescriptor group_desc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
            group_desc.layout = layout;
            group_desc.entryCount = 3;
            group_desc.entries = entries;
            groups[i] = wgpuDeviceCreateBindGroup(s.device, &group_desc);
            if (!groups[i]) die("bind_group");
        }
        wgpuBindGroupLayoutRelease(layout);
    }
    uint64_t t_resources = now_ns();

    for (long step = 0; step < iterations + depth - 1; ++step) {
        if (step < iterations) {
            const long it = step;
            struct flight *flight = &flights[it % depth];
            const int slot = independent ? (int)(it % depth) : 0;
            WGPUBuffer in_buffer = in_buffers[slot], out_buffer = out_buffers[slot];
            WGPUBindGroup group = groups[slot];
            /* Alternate each slot's input, so stale readback from that slot fails.
             * At depth two the sequence is A,A,B,B, rather than permanently A/B. */
            const int set = (int)((it / depth) & 1);
            uint64_t a = now_ns();
#if defined(DOE_PHASE_UPLOAD_SPLIT)
            {
                uint64_t sum = 0;
                for (size_t offset = 0; offset + sizeof(uint64_t) <= bytes; offset += 64) {
                    uint64_t word;
                    memcpy(&word, input[set] + offset, sizeof(word));
                    sum += word;
                }
                source_sink = sum;
            }
            const uint64_t a2 = now_ns();
#endif
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
            wgpuCommandEncoderCopyBufferToBuffer(encoder, source, 0, flight->readback, 0, bytes);
            WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, NULL);
            wgpuCommandEncoderRelease(encoder);
            if (!commands) die("encode");
            uint64_t c = now_ns();
            wgpuQueueSubmit(queue, 1, &commands);
            wgpuCommandBufferRelease(commands);
            uint64_t d = now_ns();
            flight->callback.done = false;
            WGPUQueueWorkDoneCallbackInfo done_cb = WGPU_QUEUE_WORK_DONE_CALLBACK_INFO_INIT;
            done_cb.mode = WGPUCallbackMode_WaitAnyOnly;
            done_cb.callback = on_work_done;
            done_cb.userdata1 = &flight->callback;
            /* Capture this submission's future before issuing the next operation. */
            flight->completion = wgpuQueueOnSubmittedWorkDone(queue, done_cb);
            flight->start = a;
            flight->submitted = d;
            samples[P_UPLOAD][it] = b - a;
#if defined(DOE_PHASE_UPLOAD_SPLIT)
            samples[P_UPLOAD_SOURCE][it] = a2 - a;
            samples[P_UPLOAD_DOE][it] = b - a2;
#endif
            samples[P_ENCODE][it] = c - b;
            samples[P_SUBMIT][it] = d - c;
        }
        if (step < depth - 1) continue;
        const long it = step - depth + 1;
        struct flight *flight = &flights[it % depth];
        const int set = (int)((it / depth) & 1);
        wait_future(&flight->callback, flight->completion, "completion_wait");
        if (!flight->callback.done || flight->callback.status != (int)WGPUQueueWorkDoneStatus_Success) die("completion");
        uint64_t e = now_ns();
        flight->callback.done = false;
        WGPUBufferMapCallbackInfo map_cb = WGPU_BUFFER_MAP_CALLBACK_INFO_INIT;
        map_cb.mode = WGPUCallbackMode_WaitAnyOnly;
        map_cb.callback = on_map;
        map_cb.userdata1 = &flight->callback;
        wait_future(&flight->callback, wgpuBufferMapAsync(flight->readback, WGPUMapMode_Read, 0, bytes, map_cb), "map_wait");
        if (!flight->callback.done || flight->callback.status != (int)WGPUMapAsyncStatus_Success) die("map");
        const void *mapped = wgpuBufferGetConstMappedRange(flight->readback, 0, bytes);
        if (!mapped) die("mapped_range");
        memcpy(host, mapped, bytes);
        wgpuBufferUnmap(flight->readback);
        uint64_t f = now_ns();
        samples[P_COMPLETION][it] = e - flight->submitted;
        samples[P_MAP][it] = f - e;
        samples[P_OPERATION][it] = f - flight->start;
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
    for (int i = 0; i < resource_slots; ++i) {
        if (groups[i]) wgpuBindGroupRelease(groups[i]);
        if (out_buffers[i]) wgpuBufferRelease(out_buffers[i]);
        wgpuBufferRelease(in_buffers[i]);
    }
    if (params) wgpuBufferRelease(params);
    for (int i = 0; i < depth; ++i) wgpuBufferRelease(flights[i].readback);
    if (pipeline) wgpuComputePipelineRelease(pipeline);
    if (module) wgpuShaderModuleRelease(module);
    wgpuQueueRelease(queue);
    uint64_t t_released = now_ns();
    wgpuDeviceDestroy(s.device);
    wgpuDeviceRelease(s.device);
    uint64_t t_teardown = now_ns();
    double cpu_total = cpu_ms() - cpu_start;
    const long rss_kib = max_rss_kib();

    printf("{\"status\":\"ok\",");
    printf("\"inflightDepth\":%d,\"bufferReuse\":\"%s\",", depth, independent ? "independent" : "shared");
    print_json_string("adapterVendor", info.vendor);
    print_json_string("adapterArchitecture", info.architecture);
    print_json_string("adapterDevice", info.device);
    print_json_string("adapterDescription", info.description);
    printf("\"adapterType\":%d,\"vendorID\":%u,\"deviceID\":%u,\"bytes\":%zu,\"iterations\":%ld,"
        "\"oracleChecked\":%ld,\"setupNs\":%llu,\"setupInstanceNs\":%llu,\"setupAdapterNs\":%llu,\"setupDeviceNs\":%llu,\"compileNs\":%llu,\"pipelineNs\":%llu,"
        "\"resourcesNs\":%llu,\"loopNs\":%llu,\"teardownNs\":%llu,\"teardownReleaseNs\":%llu,\"teardownDeviceNs\":%llu,\"processInMainNs\":%llu,\"cpuMs\":%.3f,"
        "\"maxRssKiB\":%ld,",
        (int)info.adapterType, info.vendorID, info.deviceID, bytes, iterations, iterations,
        (unsigned long long)(t_setup - t0), (unsigned long long)(t_instance - t_provider),
        (unsigned long long)(t_adapter - t_instance), (unsigned long long)(t_setup - t_adapter),
        (unsigned long long)(t_compile - t_setup),
        (unsigned long long)(t_pipeline - t_compile), (unsigned long long)(t_resources - t_pipeline),
        (unsigned long long)(t_loop - t_resources), (unsigned long long)(t_teardown - t_loop),
        (unsigned long long)(t_released - t_loop), (unsigned long long)(t_teardown - t_released),
        (unsigned long long)(t_teardown - process_start), cpu_total, rss_kib);
#if defined(DOE_PHASE_LOAD_PROVIDER)
    printf("\"providerLoadNs\":%llu,\"frameworkImagesBeforeSetup\":%u,"
        "\"imagesBeforeSetup\":%u,\"providerImagesLoaded\":%u,\"setupImagesLoaded\":%u,",
        (unsigned long long)(t_provider - t0), framework_images_before,
        images_before, provider_images, setup_images);
#endif
    printf("\"phases\":{");
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
