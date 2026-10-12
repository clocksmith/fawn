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

/* Separate projection-cost diagnostic. The historical correctness executor is unchanged.
 * Plan v2 adds the mandatory status-buffer index. GPU instrumentation splits passes;
 * its perturbation is measured against host-only and uninstrumented runs. */
enum { MAX_BUFFERS = 32, MAX_STAGES = 16, MAX_BINDINGS = 16, PATH_BYTES = 1024 };
#define PROCS(X) \
 X(AdapterHasFeature) X(DeviceCreateQuerySet) X(QuerySetDestroy) X(QuerySetRelease) \
 X(CommandEncoderResolveQuerySet) X(CommandEncoderClearBuffer) X(CreateInstance) X(InstanceRequestAdapter) X(InstanceProcessEvents) X(InstanceRelease) \
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
static uint64_t event_polls = 0;
static bool count_polls = false;
static bool wait_completion(WGPUInstance instance, Completion* c) {
    const double deadline = now_ms() + 30000;
    while (!c->done && now_ms() < deadline) {
        if (count_polls) event_polls++;
        apiInstanceProcessEvents(instance);
    }
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
    if (argc != 9) {
        fprintf(stderr, "Usage: projection-cost-native <library> <v2-plan> <iterations> <warmup> <vendor-id> <device-id> <none|host|gpu> <resident|verification>\n");
        return 2;
    }
    char* end = NULL;
    const unsigned long vendor = strtoul(argv[5], &end, 0);
    if (!*argv[5] || *end || vendor > UINT32_MAX) return 2;
    const unsigned long device_id = strtoul(argv[6], &end, 0);
    if (!*argv[6] || *end || device_id > UINT32_MAX) return 2;
    const bool instrument = strcmp(argv[7], "none") != 0;
    const bool gpu_timing = strcmp(argv[7], "gpu") == 0;
    if (instrument && !gpu_timing && strcmp(argv[7], "host")) return 2;
    const bool all_readbacks = strcmp(argv[8], "verification") == 0;
    if (!all_readbacks && strcmp(argv[8], "resident")) return 2;
    count_polls = instrument;
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
    size_t buffer_count = 0, stage_count = 0, status_index = 0; unsigned version = 0;
    FILE* plan = fopen(argv[2], "r");
    if (!plan || fscanf(plan, "%u %zu %zu", &version, &buffer_count, &status_index) != 3 || version != 2 || !buffer_count || buffer_count > MAX_BUFFERS || status_index >= buffer_count) return 2;
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
    if (buffers[status_index].size != 4 || !buffers[status_index].oracle
        || *(uint32_t*)buffers[status_index].oracle != 0
        || !(buffers[status_index].usage & WGPUBufferUsage_CopyDst)) return 2;
    int result = 1; WGPUInstance instance = NULL; WGPUAdapter adapter = NULL;
    WGPUDevice device = NULL; WGPUQueue queue = NULL;
    WGPUQuerySet queries = NULL; WGPUBuffer query_resolve = NULL, query_readback = NULL;
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
    const bool physical = info.backendType == WGPUBackendType_Vulkan && info.adapterType != WGPUAdapterType_CPU && info.vendorID == vendor && info.deviceID == device_id;
    printf("{\"kind\":\"adapter\",\"vendorId\":%u,\"deviceId\":%u,\"physicalSelectedVulkan\":%s}\n", info.vendorID, info.deviceID, physical ? "true" : "false");
    apiAdapterInfoFreeMembers(info); if (!physical) goto cleanup;
    WGPUDeviceDescriptor device_desc = WGPU_DEVICE_DESCRIPTOR_INIT;
    const bool timestamps_available = apiAdapterHasFeature(adapter, WGPUFeatureName_TimestampQuery);
    printf("{\"kind\":\"instrumentation\",\"mode\":\"%s\",\"transfer\":\"%s\",\"timestampsAvailable\":%s}\n",
        argv[7], argv[8], timestamps_available ? "true" : "false");
    const WGPUFeatureName timestamp_feature = WGPUFeatureName_TimestampQuery;
    if (gpu_timing) {
        if (!timestamps_available) { fprintf(stderr, "Timestamp queries unsupported; no fallback\n"); goto cleanup; }
        device_desc.requiredFeatureCount = 1; device_desc.requiredFeatures = &timestamp_feature;
    }
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
    if (gpu_timing) {
        WGPUQuerySetDescriptor qd = WGPU_QUERY_SET_DESCRIPTOR_INIT;
        qd.type = WGPUQueryType_Timestamp; qd.count = stage_count * 2;
        queries = apiDeviceCreateQuerySet(device, &qd); if (!queries) goto cleanup;
        WGPUBufferDescriptor bd = WGPU_BUFFER_DESCRIPTOR_INIT;
        bd.size = stage_count * 16; bd.usage = WGPUBufferUsage_QueryResolve | WGPUBufferUsage_CopySrc;
        query_resolve = apiDeviceCreateBuffer(device, &bd); if (!query_resolve) goto cleanup;
        bd.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead;
        query_readback = apiDeviceCreateBuffer(device, &bd); if (!query_readback) goto cleanup;
    }
    Completion prepared = {0}; WGPUQueueWorkDoneCallbackInfo pc = WGPU_QUEUE_WORK_DONE_CALLBACK_INFO_INIT;
    pc.mode = WGPUCallbackMode_AllowProcessEvents; pc.callback = queue_ready; pc.userdata1 = &prepared;
    apiQueueOnSubmittedWorkDone(queue, pc); if (!wait_completion(instance, &prepared)) goto cleanup;
    printf("{\"kind\":\"preparation\",\"ms\":%.9f}\n", now_ms() - prepare);
    /* Separate complete-output checks bracket every process, including none mode. */
    for (int iteration = -warmup - 1; iteration <= iterations; iteration++) {
        const bool verification = iteration == -warmup - 1 || iteration == iterations;
        const bool copy_all = all_readbacks || verification;
        const uint64_t poll_start = event_polls;
        const double cpu_start = cpu_ms(), start = now_ms();
        WGPUCommandEncoder encoder = apiDeviceCreateCommandEncoder(device, NULL); if (!encoder) goto cleanup;
        apiCommandEncoderClearBuffer(encoder, buffers[status_index].buffer, 0, 4);
        WGPUComputePassEncoder pass = NULL;
        if (!gpu_timing) { pass = apiCommandEncoderBeginComputePass(encoder, NULL); if (!pass) goto cleanup; }
        for (size_t i = 0; i < stage_count; i++) {
            if (gpu_timing) {
                WGPUPassTimestampWrites writes = WGPU_PASS_TIMESTAMP_WRITES_INIT;
                writes.querySet = queries; writes.beginningOfPassWriteIndex = i * 2; writes.endOfPassWriteIndex = i * 2 + 1;
                WGPUComputePassDescriptor desc = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT; desc.timestampWrites = &writes;
                pass = apiCommandEncoderBeginComputePass(encoder, &desc); if (!pass) goto cleanup;
            }
            Stage* s = &stages[i]; apiComputePassEncoderSetPipeline(pass, s->pipeline);
            apiComputePassEncoderSetBindGroup(pass, 0, s->group, 0, NULL);
            apiComputePassEncoderDispatchWorkgroups(pass, s->x, s->y, s->z);
            if (gpu_timing) { apiComputePassEncoderEnd(pass); apiComputePassEncoderRelease(pass); }
        }
        if (!gpu_timing) { apiComputePassEncoderEnd(pass); apiComputePassEncoderRelease(pass); }
        size_t readback_bytes = 0, readback_count = 0;
        for (size_t i = 0; i < buffer_count; i++) if (buffers[i].readback && (copy_all || i == status_index)) {
            if (all_readbacks) apiCommandEncoderCopyBufferToBuffer(encoder, buffers[i].buffer, 0, buffers[i].readback, 0, buffers[i].size);
            readback_bytes += buffers[i].size; readback_count++;
        }
        if (gpu_timing) {
            apiCommandEncoderResolveQuerySet(encoder, queries, 0, stage_count * 2, query_resolve, 0);
            apiCommandEncoderCopyBufferToBuffer(encoder, query_resolve, 0, query_readback, 0, stage_count * 16);
        }
        WGPUCommandBuffer commands = apiCommandEncoderFinish(encoder, NULL); apiCommandEncoderRelease(encoder); if (!commands) goto cleanup;
        const double recorded = instrument ? now_ms() : 0;
        apiQueueSubmit(queue, 1, &commands); apiCommandBufferRelease(commands);
        const double submitted = instrument ? now_ms() : 0;
        Completion done = {0}; WGPUQueueWorkDoneCallbackInfo qc = WGPU_QUEUE_WORK_DONE_CALLBACK_INFO_INIT;
        qc.mode = WGPUCallbackMode_AllowProcessEvents; qc.callback = queue_ready; qc.userdata1 = &done;
        apiQueueOnSubmittedWorkDone(queue, qc); if (!wait_completion(instance, &done)) goto cleanup;
        const double completed = instrument ? now_ms() : 0;
        bool exact = true;
        double map_ms = 0, compare_ms = 0;
        for (size_t i = 0; i < buffer_count; i++) if (buffers[i].readback && (copy_all || i == status_index)) {
            const double map_start = instrument ? now_ms() : 0;
            Buffer* b = &buffers[i];
            if (!all_readbacks) {
                WGPUCommandEncoder copy = apiDeviceCreateCommandEncoder(device, NULL); if (!copy) goto cleanup;
                apiCommandEncoderCopyBufferToBuffer(copy, b->buffer, 0, b->readback, 0, b->size);
                WGPUCommandBuffer copy_commands = apiCommandEncoderFinish(copy, NULL); apiCommandEncoderRelease(copy);
                if (!copy_commands) goto cleanup;
                apiQueueSubmit(queue, 1, &copy_commands); apiCommandBufferRelease(copy_commands);
            }
            Completion mapped = {0}; WGPUBufferMapCallbackInfo mc = WGPU_BUFFER_MAP_CALLBACK_INFO_INIT;
            mc.mode = WGPUCallbackMode_AllowProcessEvents; mc.callback = map_ready; mc.userdata1 = &mapped;
            apiBufferMapAsync(b->readback, WGPUMapMode_Read, 0, b->size, mc); if (!wait_completion(instance, &mapped)) goto cleanup;
            const void* bytes = apiBufferGetConstMappedRange(b->readback, 0, b->size);
            const double compare_start = instrument ? now_ms() : 0;
            exact &= bytes && memcmp(bytes, b->oracle, b->size) == 0;
            const double compare_end = instrument ? now_ms() : 0;
            apiBufferUnmap(b->readback);
            if (instrument) {
                map_ms += compare_start - map_start + now_ms() - compare_end;
                compare_ms += compare_end - compare_start;
            }
        }
        /* Timestamp collection is diagnostic overhead, kept inside its wall boundary. */
        uint64_t timestamps[MAX_STAGES * 2] = {0};
        const double timestamp_start = instrument ? now_ms() : 0;
        if (gpu_timing) {
            Completion mapped = {0}; WGPUBufferMapCallbackInfo mc = WGPU_BUFFER_MAP_CALLBACK_INFO_INIT;
            mc.mode = WGPUCallbackMode_AllowProcessEvents; mc.callback = map_ready; mc.userdata1 = &mapped;
            apiBufferMapAsync(query_readback, WGPUMapMode_Read, 0, stage_count * 16, mc);
            if (!wait_completion(instance, &mapped)) goto cleanup;
            const void* data = apiBufferGetConstMappedRange(query_readback, 0, stage_count * 16);
            if (!data) goto cleanup;
            memcpy(timestamps, data, stage_count * 16); apiBufferUnmap(query_readback);
            for (size_t i = 0; i < stage_count; i++) if (timestamps[2*i+1] <= timestamps[2*i]) {
                fprintf(stderr, "Nonpositive GPU interval; reject instrumentation\n"); goto cleanup;
            }
        }
        const double end = now_ms(), elapsed = end - start, cpu_elapsed = cpu_ms() - cpu_start;
        printf("{\"kind\":\"%s\",\"iteration\":%d,\"ms\":%.9f,\"cpuMs\":%.9f,\"exact\":%s,\"dispatches\":%zu,\"readbackBytes\":%zu,\"readbackCount\":%zu,\"phases\":",
            verification ? "verification" : "sample", iteration, elapsed, cpu_elapsed, exact ? "true" : "false", stage_count, readback_bytes, readback_count);
        if (instrument) printf("{\"recordMs\":%.9f,\"submitMs\":%.9f,\"waitMs\":%.9f,\"mapMs\":%.9f,\"compareMs\":%.9f,\"timestampReadMs\":%.9f,\"polls\":%llu}",
            recorded-start, submitted-recorded, completed-submitted, map_ms, compare_ms,
            gpu_timing ? end-timestamp_start : 0, (unsigned long long)(event_polls-poll_start));
        else printf("null");
        printf(",\"gpuStageNs\":[");
        if (gpu_timing) for (size_t i = 0; i < stage_count; i++) printf("%s%llu", i ? "," : "", (unsigned long long)(timestamps[2*i+1]-timestamps[2*i]));
        printf("]}\n");
        if (!exact || atomic_load(&had_error)) goto cleanup;
    }
    result = 0;
cleanup:
    if (query_readback) { apiBufferDestroy(query_readback); apiBufferRelease(query_readback); }
    if (query_resolve) { apiBufferDestroy(query_resolve); apiBufferRelease(query_resolve); }
    if (queries) { apiQuerySetDestroy(queries); apiQuerySetRelease(queries); }
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
