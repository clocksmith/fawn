/* SPDX-License-Identifier: MIT
 * Observe the inner call already made by Chromium's Dawn WebGPU wrapper.
 * This Linux software test measures public calls on one queue, outside Chromium. */
#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L
#include "consumer.h"
#include "doe_native_queue.h"
#include <dlfcn.h>
#include <stdatomic.h>
#include <sys/stat.h>
#include <time.h>

enum { words = 4096, steps = 2048 };

static void require_library(const char *expected) {
    struct stat pinned;
    assert(expected && stat(expected, &pinned) == 0);
    const char *names[] = {"wgpuQueueSubmit", "wgpuQueueOnSubmittedWorkDone", "wgpuBufferMapAsync"};
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        Dl_info found = {0};
        struct stat actual;
        void *proc = dlsym(RTLD_DEFAULT, names[i]);
        assert(proc && dladdr(proc, &found) && found.dli_fname);
        assert(stat(found.dli_fname, &actual) == 0);
        if (pinned.st_dev != actual.st_dev || pinned.st_ino != actual.st_ino) {
            fprintf(stderr, "The loaded library differs from the expected library for %s\n", names[i]);
            exit(2);
        }
    }
}

static double now_ms(void) {
    struct timespec t;
    assert(clock_gettime(CLOCK_MONOTONIC, &t) == 0);
    return t.tv_sec * 1000.0 + t.tv_nsec / 1e6;
}

static void completed(WGPUQueueWorkDoneStatus status, WGPUStringView message,
                      void *target, void *unused) {
    (void)message;
    (void)unused;
    atomic_store((atomic_int *)target, (int)status);
}

static void observed_map(WGPUMapAsyncStatus status, WGPUStringView message, void *target, void *unused) {
    (void)message;
    (void)unused;
    atomic_store((atomic_int *)target, (int)status);
}

static int await_status(state *s, atomic_int *status) {
    double deadline = now_ms() + 15000;
    while (!atomic_load(status)) {
        assert(now_ms() < deadline);
        wgpuInstanceProcessEvents(s->instance);
        nanosleep(&(struct timespec){.tv_nsec = 100000}, NULL);
    }
    return atomic_load(status);
}

static unsigned expected(unsigned x) {
    for (unsigned j = 0; j < steps; ++j) {
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
    }
    return x;
}

static void observe(state *s, const char *kind, unsigned trial, unsigned errors) {
    uint32_t before_bits = doeNativeQueueSyncInfo(s->queue);
    atomic_int callback_status = 0;
    double start = now_ms();
    WGPUFuture f = wgpuQueueOnSubmittedWorkDone(s->queue, (WGPUQueueWorkDoneCallbackInfo){
        .mode = WGPUCallbackMode_AllowSpontaneous, .callback = completed, .userdata1 = &callback_status});
    double returned = now_ms();
    int on_return = atomic_load(&callback_status);
    assert(f.id);
    uint32_t after_bits = doeNativeQueueSyncInfo(s->queue);
    int terminal = await_status(s, &callback_status);
    if (!errors) assert(terminal == WGPUQueueWorkDoneStatus_Success);
    printf("{\"case\":\"%s\",\"trial\":%u,\"queueRegistrationMs\":%.6f,"
           "\"beforePending\":%s,\"afterPending\":%s,\"callbackSeenOnReturn\":%s,"
           "\"success\":%s,\"validationErrors\":%u}\n", kind, trial, returned - start,
           before_bits & DOE_NATIVE_QUEUE_SYNC_DEFERRED_SUBMISSIONS ? "true" : "false",
           after_bits & DOE_NATIVE_QUEUE_SYNC_DEFERRED_SUBMISSIONS ? "true" : "false",
           on_return ? "true" : "false", terminal == WGPUQueueWorkDoneStatus_Success ? "true" : "false", errors);
}

int main(void) {
    require_library(getenv("RESIDENT_EXPECTED_DOE_LIBRARY"));
    assert(!adapter_class_physical());
    state s = {0};
    consumer_init(&s);
    assert(doeNativeQueueSyncInfo(s.queue) & DOE_NATIVE_QUEUE_SYNC_BACKEND_VULKAN);
    const size_t size = words * sizeof(uint32_t);
    WGPUBuffer data = buffer(&s, size, WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst);
    WGPUBuffer output = buffer(&s, size, WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead);
    WGPUComputePipeline pipe = pipeline(&s,
        "@group(0) @binding(0) var<storage,read_write> output:array<u32>;"
        "@compute @workgroup_size(32) fn main(@builtin(global_invocation_id) i:vec3<u32>){"
        "var x=output[i.x];for(var j=0u;j<2048u;j=j+1u){x=x^(x<<13u);x=x^(x>>17u);x=x^(x<<5u);}output[i.x]=x;}");
    WGPUBindGroupEntry entry = {.binding = 0, .buffer = data, .size = size};
    WGPUBindGroup bindings = group(&s, pipe, &entry, 1);
    for (unsigned i = 0; i < 4; ++i) {
        observe(&s, "idle", i, 0);
        uint32_t input[words];
        for (unsigned j = 0; j < words; ++j) input[j] = 1u + i * words + j;
        wgpuQueueWriteBuffer(s.queue, data, 0, input, size);
        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s.device, NULL);
        WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, NULL);
        wgpuComputePassEncoderSetPipeline(pass, pipe);
        wgpuComputePassEncoderSetBindGroup(pass, 0, bindings, 0, NULL);
        wgpuComputePassEncoderDispatchWorkgroups(pass, words / 32, 1, 1);
        wgpuComputePassEncoderEnd(pass);
        wgpuComputePassEncoderRelease(pass);
        wgpuCommandEncoderCopyBufferToBuffer(encoder, data, 0, output, 0, size);
        WGPUCommandBuffer command = wgpuCommandEncoderFinish(encoder, NULL);
        wgpuCommandEncoderRelease(encoder);
        assert(command);
        wgpuQueueSubmit(s.queue, 1, &command);
        observe(&s, "valid", i, 0);
        atomic_int mapped_status = 0;
        WGPUFuture f = wgpuBufferMapAsync(output, WGPUMapMode_Read, 0, size, (WGPUBufferMapCallbackInfo){
            .mode = WGPUCallbackMode_AllowSpontaneous, .callback = observed_map, .userdata1 = &mapped_status});
        assert(f.id && await_status(&s, &mapped_status) == WGPUMapAsyncStatus_Success);
        const uint32_t *result = wgpuBufferGetConstMappedRange(output, 0, size);
        assert(result);
        for (unsigned j = 0; j < words; ++j) assert(result[j] == expected(input[j]));
        wgpuBufferUnmap(output);
        /* A command buffer is single-use by the public WebGPU contract. */
        wgpuDevicePushErrorScope(s.device, WGPUErrorFilter_Validation);
        wgpuQueueSubmit(s.queue, 1, &command);
        expect_validation(&s);
        observe(&s, "already_consumed", i, s.validation_errors);
        wgpuCommandBufferRelease(command);
    }
    wgpuBindGroupRelease(bindings);
    wgpuComputePipelineRelease(pipe);
    wgpuBufferRelease(output);
    wgpuBufferRelease(data);
    consumer_finish(&s);
    return 0;
}
