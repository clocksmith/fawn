#define _POSIX_C_SOURCE 200809L
#include "webgpu.h"
#include <dlfcn.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <time.h>
#include <sys/resource.h>

/* Diagnostic native SPIR-V executor. Its bounded plan comes from the frozen manifest. */
enum { MAX_BUFFERS = 32, MAX_STAGES = 16, MAX_BINDINGS = 16, PATH_BYTES = 1024 };
#define PROCS(X) \
 X(CreateInstance) X(InstanceRequestAdapter) X(InstanceProcessEvents) X(InstanceRelease) \
 X(AdapterGetInfo) X(AdapterInfoFreeMembers) X(AdapterRequestDevice) X(AdapterRelease) \
 X(DeviceGetQueue) X(DeviceCreateBuffer) X(DeviceCreateShaderModule) \
 X(DeviceCreateBindGroupLayout) X(DeviceCreatePipelineLayout) X(DeviceCreateComputePipeline) \
 X(DeviceCreateBindGroup) X(DeviceCreateCommandEncoder) X(DeviceDestroy) X(DeviceRelease) \
 X(BufferMapAsync) X(BufferGetConstMappedRange) X(BufferUnmap) X(BufferDestroy) X(BufferRelease) \
 X(ShaderModuleRelease) X(BindGroupLayoutRelease) X(PipelineLayoutRelease) \
 X(ComputePipelineRelease) X(BindGroupRelease) X(QueueWriteBuffer) X(QueueSubmit) X(QueueRelease) \
 X(QueueOnSubmittedWorkDone) X(CommandEncoderBeginComputePass) X(CommandEncoderCopyBufferToBuffer) \
 X(CommandEncoderFinish) X(CommandEncoderRelease) X(ComputePassEncoderSetPipeline) \
 X(ComputePassEncoderSetBindGroup) X(ComputePassEncoderDispatchWorkgroups) \
 X(ComputePassEncoderEnd) X(ComputePassEncoderRelease) X(CommandBufferRelease)
#define DECLARE(name) static __typeof__(wgpu##name)* api##name;
PROCS(DECLARE)
#undef DECLARE

typedef struct { int done, ok; void* handle; } Completion;
typedef struct {
    size_t size; uint64_t usage; char initial[PATH_BYTES], expected[PATH_BYTES];
    uint8_t* bytes; uint8_t* oracle; WGPUBuffer buffer, readback;
} Buffer;
typedef struct {
    char shader[PATH_BYTES], entry[128]; uint32_t x, y, z;
    size_t count; uint32_t binding[MAX_BINDINGS], buffer[MAX_BINDINGS], type[MAX_BINDINGS];
    WGPUShaderModule module; WGPUBindGroupLayout group_layout; WGPUPipelineLayout layout;
    WGPUComputePipeline pipeline; WGPUBindGroup group;
} Stage;

static double now_ms(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000.0 + t.tv_nsec / 1000000.0;
}
static double cpu_ms(void) {
    struct rusage usage; getrusage(RUSAGE_SELF, &usage);
    return (usage.ru_utime.tv_sec + usage.ru_stime.tv_sec) * 1000.0
        + (usage.ru_utime.tv_usec + usage.ru_stime.tv_usec) / 1000.0;
}
static uint8_t* read_bytes(const char* path, size_t* size) {
    FILE* f = fopen(path, "rb"); if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) || (*size = (size_t)ftell(f)) > 64 * 1024 * 1024 || fseek(f, 0, SEEK_SET)) { fclose(f); return NULL; }
    uint8_t* bytes = malloc(*size ? *size : 1);
    if (!bytes || fread(bytes, 1, *size, f) != *size) { free(bytes); bytes = NULL; }
    fclose(f); return bytes;
}
static bool wait_completion(WGPUInstance instance, Completion* c) {
    const double deadline = now_ms() + 30000;
    while (!c->done && now_ms() < deadline) apiInstanceProcessEvents(instance);
    if (!c->done) {
        fprintf(stderr, "Unknown callback/GPU completion; terminating without resource reuse or release\n");
        fflush(NULL); _Exit(124);
    }
    return c->done && c->ok;
}
static void adapter_ready(WGPURequestAdapterStatus status, WGPUAdapter value, WGPUStringView message, void* data, void* unused) {
    (void)unused; Completion* c = data; c->handle = value; c->ok = status == WGPURequestAdapterStatus_Success;
    if (!c->ok) fprintf(stderr, "adapter: %.*s\n", (int)message.length, message.data);
    c->done = 1;
}
static void device_ready(WGPURequestDeviceStatus status, WGPUDevice value, WGPUStringView message, void* data, void* unused) {
    (void)unused; Completion* c = data; c->handle = value; c->ok = status == WGPURequestDeviceStatus_Success;
    if (!c->ok) fprintf(stderr, "device: %.*s\n", (int)message.length, message.data);
    c->done = 1;
}
static void map_ready(WGPUMapAsyncStatus status, WGPUStringView message, void* data, void* unused) {
    (void)unused; Completion* c = data; c->ok = status == WGPUMapAsyncStatus_Success;
    if (!c->ok) fprintf(stderr, "map: %.*s\n", (int)message.length, message.data);
    c->done = 1;
}
static void queue_ready(WGPUQueueWorkDoneStatus status, WGPUStringView message, void* data, void* unused) {
    (void)message; (void)unused; Completion* c = data; c->ok = status == WGPUQueueWorkDoneStatus_Success; c->done = 1;
}
static _Atomic bool had_error = false;
static void uncaptured_error(WGPUDevice const* device, WGPUErrorType type, WGPUStringView message, void* data, void* unused) {
    (void)device; (void)data; (void)unused; atomic_store(&had_error, true);
    fprintf(stderr, "uncaptured error %u: %.*s\n", type, (int)message.length, message.data);
}

int main(int argc, char** argv) {
    if (argc != 5) { fprintf(stderr, "Usage: projection-native <library> <plan> <iterations> <warmup>\n"); return 2; }
    const int iterations = atoi(argv[3]), warmup = atoi(argv[4]);
    if (iterations < 1 || iterations > 10000 || warmup < 0 || warmup > 10000) return 2;
    void* library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL); if (!library) { fprintf(stderr, "%s\n", dlerror()); return 2; }
#define LOAD(name) api##name = (__typeof__(api##name))dlsym(library, "wgpu" #name); if (!api##name) { fprintf(stderr, "missing proc: %s\n", #name); return 2; }
    PROCS(LOAD)
#undef LOAD
    bool loaded = false; FILE* maps = fopen("/proc/self/maps", "r");
    if (maps) {
        char* line = NULL; size_t capacity = 0;
        while (getline(&line, &capacity, maps) >= 0) loaded |= strstr(line, argv[1]) != NULL;
        free(line); fclose(maps);
    }
    printf("{\"kind\":\"loadedLibrary\",\"observed\":%s}\n", loaded ? "true" : "false");
    if (!loaded) return 2;
    Buffer buffers[MAX_BUFFERS] = {0}; Stage stages[MAX_STAGES] = {0};
    size_t buffer_count = 0, stage_count = 0; unsigned version = 0;
    FILE* plan = fopen(argv[2], "r");
    if (!plan || fscanf(plan, "%u %zu", &version, &buffer_count) != 2 || version != 1 || buffer_count > MAX_BUFFERS) return 2;
    for (size_t i = 0; i < buffer_count; i++) {
        Buffer* b = &buffers[i];
        if (fscanf(plan, "%zu %lu %1023s %1023s", &b->size, &b->usage, b->initial, b->expected) != 4 || !b->size || b->size % 4) return 2;
        if (strcmp(b->initial, "zero")) { size_t size = 0; b->bytes = read_bytes(b->initial, &size); if (!b->bytes || size != b->size) return 2; }
        if (strcmp(b->expected, "none")) { size_t size = 0; b->oracle = read_bytes(b->expected, &size); if (!b->oracle || size != b->size) return 2; }
    }
    if (fscanf(plan, "%zu", &stage_count) != 1 || !stage_count || stage_count > MAX_STAGES) return 2;
    for (size_t i = 0; i < stage_count; i++) {
        Stage* s = &stages[i];
        if (fscanf(plan, "%1023s %127s %u %u %u %zu", s->shader, s->entry, &s->x, &s->y, &s->z, &s->count) != 6 || !s->x || !s->y || !s->z || s->count > MAX_BINDINGS) return 2;
        for (size_t j = 0; j < s->count; j++) if (fscanf(plan, "%u %u %u", &s->binding[j], &s->buffer[j], &s->type[j]) != 3 || s->buffer[j] >= buffer_count || s->type[j] > 2) return 2;
    }
    fclose(plan);
    int result = 1; WGPUInstance instance = NULL; WGPUAdapter adapter = NULL;
    WGPUDevice device = NULL; WGPUQueue queue = NULL;
    WGPUInstanceDescriptor instance_desc = WGPU_INSTANCE_DESCRIPTOR_INIT;
    const WGPUInstanceFeatureName features[] = {WGPUInstanceFeatureName_ShaderSourceSPIRV};
    instance_desc.requiredFeatureCount = 1; instance_desc.requiredFeatures = features;
    instance = apiCreateInstance(&instance_desc); if (!instance) goto cleanup;
    WGPURequestAdapterOptions options = WGPU_REQUEST_ADAPTER_OPTIONS_INIT; options.backendType = WGPUBackendType_Vulkan;
    options.powerPreference = WGPUPowerPreference_HighPerformance;
    Completion c = {0}; WGPURequestAdapterCallbackInfo ac = WGPU_REQUEST_ADAPTER_CALLBACK_INFO_INIT;
    ac.mode = WGPUCallbackMode_AllowProcessEvents; ac.callback = adapter_ready; ac.userdata1 = &c;
    apiInstanceRequestAdapter(instance, &options, ac);
    if (!wait_completion(instance, &c)) goto cleanup;
    adapter = c.handle;
    WGPUAdapterInfo info = WGPU_ADAPTER_INFO_INIT;
    if (apiAdapterGetInfo(adapter, &info) != WGPUStatus_Success) goto cleanup;
    const bool physical = info.backendType == WGPUBackendType_Vulkan && info.adapterType != WGPUAdapterType_CPU && info.vendorID == 0x8086;
    printf("{\"kind\":\"adapter\",\"vendorId\":%u,\"deviceId\":%u,\"physicalIntelVulkan\":%s}\n", info.vendorID, info.deviceID, physical ? "true" : "false");
    apiAdapterInfoFreeMembers(info); if (!physical) goto cleanup;
    WGPUDeviceDescriptor device_desc = WGPU_DEVICE_DESCRIPTOR_INIT;
    device_desc.uncapturedErrorCallbackInfo.callback = uncaptured_error;
    c = (Completion){0}; WGPURequestDeviceCallbackInfo dc = WGPU_REQUEST_DEVICE_CALLBACK_INFO_INIT;
    dc.mode = WGPUCallbackMode_AllowProcessEvents; dc.callback = device_ready; dc.userdata1 = &c;
    apiAdapterRequestDevice(adapter, &device_desc, dc);
    if (!wait_completion(instance, &c)) goto cleanup;
    device = c.handle;
    queue = apiDeviceGetQueue(device); if (!queue) goto cleanup;
    const double prepare = now_ms();
    for (size_t i = 0; i < buffer_count; i++) {
        Buffer* b = &buffers[i]; WGPUBufferDescriptor desc = WGPU_BUFFER_DESCRIPTOR_INIT;
        desc.size = b->size; desc.usage = b->usage; b->buffer = apiDeviceCreateBuffer(device, &desc); if (!b->buffer) goto cleanup;
        if (b->bytes) apiQueueWriteBuffer(queue, b->buffer, 0, b->bytes, b->size);
        if (b->oracle) { desc.usage = WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst; b->readback = apiDeviceCreateBuffer(device, &desc); if (!b->readback) goto cleanup; }
    }
    for (size_t i = 0; i < stage_count; i++) {
        Stage* s = &stages[i]; size_t size = 0; uint8_t* bytes = read_bytes(s->shader, &size);
        if (!bytes || size < 20 || size % 4) { free(bytes); goto cleanup; }
        WGPUShaderSourceSPIRV source = WGPU_SHADER_SOURCE_SPIRV_INIT; source.codeSize = size / 4; source.code = (uint32_t*)bytes;
        WGPUShaderModuleDescriptor shader_desc = WGPU_SHADER_MODULE_DESCRIPTOR_INIT; shader_desc.nextInChain = &source.chain;
        s->module = apiDeviceCreateShaderModule(device, &shader_desc); free(bytes); if (!s->module) goto cleanup;
        WGPUBindGroupLayoutEntry entries[MAX_BINDINGS]; WGPUBindGroupEntry resources[MAX_BINDINGS];
        for (size_t j = 0; j < s->count; j++) {
            entries[j] = (WGPUBindGroupLayoutEntry)WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
            entries[j].binding = s->binding[j]; entries[j].visibility = WGPUShaderStage_Compute;
            entries[j].buffer.type = s->type[j] == 0 ? WGPUBufferBindingType_Uniform : s->type[j] == 1 ? WGPUBufferBindingType_ReadOnlyStorage : WGPUBufferBindingType_Storage;
            resources[j] = (WGPUBindGroupEntry)WGPU_BIND_GROUP_ENTRY_INIT;
            resources[j].binding = s->binding[j]; resources[j].buffer = buffers[s->buffer[j]].buffer; resources[j].size = buffers[s->buffer[j]].size;
        }
        WGPUBindGroupLayoutDescriptor gd = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT; gd.entryCount = s->count; gd.entries = entries;
        s->group_layout = apiDeviceCreateBindGroupLayout(device, &gd); if (!s->group_layout) goto cleanup;
        WGPUPipelineLayoutDescriptor ld = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT; ld.bindGroupLayoutCount = 1; ld.bindGroupLayouts = &s->group_layout;
        s->layout = apiDeviceCreatePipelineLayout(device, &ld); if (!s->layout) goto cleanup;
        WGPUComputePipelineDescriptor pd = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT; pd.layout = s->layout;
        pd.compute.module = s->module; pd.compute.entryPoint = (WGPUStringView){s->entry, strlen(s->entry)};
        s->pipeline = apiDeviceCreateComputePipeline(device, &pd); if (!s->pipeline) goto cleanup;
        WGPUBindGroupDescriptor bd = WGPU_BIND_GROUP_DESCRIPTOR_INIT; bd.layout = s->group_layout; bd.entryCount = s->count; bd.entries = resources;
        s->group = apiDeviceCreateBindGroup(device, &bd); if (!s->group) goto cleanup;
    }
    printf("{\"kind\":\"preparation\",\"ms\":%.9f}\n", now_ms() - prepare);
    for (int iteration = -warmup; iteration < iterations; iteration++) {
        const double cpu_start = cpu_ms();
        const double start = now_ms();
        WGPUCommandEncoder encoder = apiDeviceCreateCommandEncoder(device, NULL); if (!encoder) goto cleanup;
        WGPUComputePassEncoder pass = apiCommandEncoderBeginComputePass(encoder, NULL); if (!pass) goto cleanup;
        for (size_t i = 0; i < stage_count; i++) {
            Stage* s = &stages[i]; apiComputePassEncoderSetPipeline(pass, s->pipeline); apiComputePassEncoderSetBindGroup(pass, 0, s->group, 0, NULL);
            apiComputePassEncoderDispatchWorkgroups(pass, s->x, s->y, s->z);
        }
        apiComputePassEncoderEnd(pass); apiComputePassEncoderRelease(pass);
        for (size_t i = 0; i < buffer_count; i++) if (buffers[i].readback) apiCommandEncoderCopyBufferToBuffer(encoder, buffers[i].buffer, 0, buffers[i].readback, 0, buffers[i].size);
        WGPUCommandBuffer commands = apiCommandEncoderFinish(encoder, NULL); apiCommandEncoderRelease(encoder); if (!commands) goto cleanup;
        apiQueueSubmit(queue, 1, &commands); apiCommandBufferRelease(commands);
        Completion done = {0}; WGPUQueueWorkDoneCallbackInfo qc = WGPU_QUEUE_WORK_DONE_CALLBACK_INFO_INIT;
        qc.mode = WGPUCallbackMode_AllowProcessEvents; qc.callback = queue_ready; qc.userdata1 = &done;
        apiQueueOnSubmittedWorkDone(queue, qc); if (!wait_completion(instance, &done)) goto cleanup;
        bool exact = true;
        for (size_t i = 0; i < buffer_count; i++) if (buffers[i].readback) {
            Buffer* b = &buffers[i]; Completion mapped = {0}; WGPUBufferMapCallbackInfo mc = WGPU_BUFFER_MAP_CALLBACK_INFO_INIT;
            mc.mode = WGPUCallbackMode_AllowProcessEvents; mc.callback = map_ready; mc.userdata1 = &mapped;
            apiBufferMapAsync(b->readback, WGPUMapMode_Read, 0, b->size, mc); if (!wait_completion(instance, &mapped)) goto cleanup;
            const void* bytes = apiBufferGetConstMappedRange(b->readback, 0, b->size);
            const bool equal = bytes && memcmp(bytes, b->oracle, b->size) == 0;
            if (!equal && bytes) {
                const uint32_t* actual = bytes; const uint32_t* expected = (const uint32_t*)b->oracle;
                size_t mismatch = 0; while (mismatch < b->size / 4 && actual[mismatch] == expected[mismatch]) mismatch++;
                printf("{\"kind\":\"mismatch\",\"buffer\":%zu,\"word\":%zu,\"actual\":%u,\"expected\":%u}\n", i, mismatch, actual[mismatch], expected[mismatch]);
            }
            exact &= equal; apiBufferUnmap(b->readback);
        }
        const double elapsed = now_ms() - start;
        printf("{\"kind\":\"sample\",\"iteration\":%d,\"ms\":%.9f,\"cpuMs\":%.9f,\"exact\":%s,\"dispatches\":%zu}\n", iteration, elapsed, cpu_ms() - cpu_start, exact ? "true" : "false", stage_count);
        if (!exact || atomic_load(&had_error)) goto cleanup;
    }
    result = 0;
cleanup:
    for (size_t i = 0; i < stage_count; i++) {
        Stage* s = &stages[i]; if (s->group) apiBindGroupRelease(s->group); if (s->pipeline) apiComputePipelineRelease(s->pipeline);
        if (s->layout) apiPipelineLayoutRelease(s->layout);
        if (s->group_layout) apiBindGroupLayoutRelease(s->group_layout);
        if (s->module) apiShaderModuleRelease(s->module);
    }
    for (size_t i = 0; i < buffer_count; i++) {
        Buffer* b = &buffers[i]; if (b->readback) { apiBufferDestroy(b->readback); apiBufferRelease(b->readback); }
        if (b->buffer) { apiBufferDestroy(b->buffer); apiBufferRelease(b->buffer); } free(b->bytes); free(b->oracle);
    }
    if (queue) apiQueueRelease(queue);
    if (device) { apiDeviceDestroy(device); apiDeviceRelease(device); }
    if (adapter) apiAdapterRelease(adapter);
    if (instance) apiInstanceRelease(instance);
    struct rusage usage; getrusage(RUSAGE_SELF, &usage);
    printf("{\"kind\":\"processResources\",\"cpuMs\":%.9f,\"peakRssKiB\":%ld}\n", cpu_ms(), usage.ru_maxrss);
    printf("{\"kind\":\"cleanup\",\"passed\":%s}\n", result == 0 ? "true" : "false");
    dlclose(library); return result;
}
