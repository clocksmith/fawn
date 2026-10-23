/* SPDX-License-Identifier: MIT
 * Public Doe compute-program extension over the C provider: one reusable GPU
 * recording (gpu-recorded) and host replay (native-recorded), with exact
 * readback across repeated submissions and changing inputs. */
#include "consumer.h"
#include "doe_native_programs.h"
#include <stdint.h>

static const char *const code =
    "@group(0) @binding(0) var<storage, read> input: array<u32>;\n"
    "@group(0) @binding(1) var<storage, read_write> output: array<u32>;\n"
    "@compute @workgroup_size(16) fn main(@builtin(global_invocation_id) id: vec3u) {\n"
    "  output[id.x] = output[id.x] + input[id.x] * 3u + id.x;\n"
    "}\n";

enum { WORDS = 64, BYTES = WORDS * 4 };

typedef struct program_fixture {
    WGPUComputePipeline pipeline;
    WGPUBuffer input, output, readback;
    WGPUBindGroup group;
} program_fixture;

static program_fixture create_fixture(state *s)
{
    program_fixture f;
    f.pipeline = pipeline(s, code);
    f.input = buffer(s, BYTES, WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst);
    f.output = buffer(s, BYTES, WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst);
    f.readback = buffer(s, BYTES, WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst);
    WGPUBindGroupEntry entries[2] = {{.binding=0,.buffer=f.input,.size=BYTES},{.binding=1,.buffer=f.output,.size=BYTES}};
    f.group = group(s, f.pipeline, entries, 2);
    return f;
}

static void release_fixture(program_fixture *f)
{
    wgpuBindGroupRelease(f->group);
    wgpuBufferRelease(f->readback);
    wgpuBufferRelease(f->output);
    wgpuBufferRelease(f->input);
    wgpuComputePipelineRelease(f->pipeline);
}

/* clear output, dispatch, then copy to readback: the program's whole run. */
static WGPUCommandBuffer record(state *s, const program_fixture *f, bool clear)
{
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device, NULL);
    assert(encoder);
    if (clear) wgpuCommandEncoderClearBuffer(encoder, f->output, 0, BYTES);
    dispatch(encoder, f->pipeline, f->group);
    wgpuCommandEncoderCopyBufferToBuffer(encoder, f->output, 0, f->readback, 0, BYTES);
    WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, NULL);
    wgpuCommandEncoderRelease(encoder);
    assert(commands);
    return commands;
}

static void run(state *s, const program_fixture *f, void *program, bool host_replay)
{
    for (uint32_t pass = 1; pass <= 3; ++pass) {
        uint32_t input[WORDS];
        for (uint32_t i = 0; i < WORDS; ++i) input[i] = pass * 1000u + i * (host_replay ? 7u : 5u);
        wgpuQueueWriteBuffer(s->queue, f->input, 0, input, sizeof(input)); /* Between runs. */
        assert(doeNativeComputeProgramSubmit(program) == 1);
        const uint32_t *words = map(s, f->readback, BYTES);
        for (uint32_t i = 0; i < WORDS; ++i) assert(words[i] == input[i] * 3u + i); /* Cleared each run. */
        wgpuBufferUnmap(f->readback); /* Mapping between runs is allowed. */
    }
}

static void expect_null_prepare(state *s, void *(*prepare)(void *, void *), void *queue, WGPUCommandBuffer commands)
{
    wgpuDevicePushErrorScope(s->device, WGPUErrorFilter_Validation);
    assert(prepare(queue, commands) == NULL);
    expect_validation(s);
}

/* Long enough per invocation that begin/end GPU timestamps of one dispatch differ. */
static const char *const busy_code =
    "@group(0) @binding(0) var<storage, read_write> sink: array<u32>;\n"
    "@compute @workgroup_size(64) fn main(@builtin(global_invocation_id) id: vec3u) {\n"
    "  var value = id.x;\n"
    "  for (var i = 0u; i < 4096u; i++) { value = value * 1664525u + 1013904223u; }\n"
    "  sink[id.x] = value;\n"
    "}\n";

static WGPUCommandBuffer timed_commands(state *t, WGPUComputePipeline busy, WGPUBindGroup group,
    WGPUQuerySet queries, WGPUBuffer resolved, WGPUBuffer readback, bool write)
{
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(t->device, NULL);
    if (write) {
        WGPUPassTimestampWrites writes = WGPU_PASS_TIMESTAMP_WRITES_INIT;
        writes.querySet = queries;
        writes.beginningOfPassWriteIndex = 0;
        writes.endOfPassWriteIndex = 1;
        WGPUComputePassDescriptor descriptor = {.timestampWrites=&writes};
        WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, &descriptor);
        wgpuComputePassEncoderSetPipeline(pass, busy);
        wgpuComputePassEncoderSetBindGroup(pass, 0, group, 0, NULL);
        wgpuComputePassEncoderDispatchWorkgroups(pass, 64, 1, 1);
        wgpuComputePassEncoderEnd(pass);
        wgpuComputePassEncoderRelease(pass);
    }
    wgpuCommandEncoderResolveQuerySet(encoder, queries, 0, 2, resolved, 0);
    wgpuCommandEncoderCopyBufferToBuffer(encoder, resolved, 0, readback, 0, 16);
    WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, NULL);
    wgpuCommandEncoderRelease(encoder);
    assert(commands);
    return commands;
}

static void read_times(state *t, WGPUBuffer readback, uint64_t out[2])
{
    memcpy(out, map(t, readback, 16), 16);
    wgpuBufferUnmap(readback);
}

/* gpu-recorded program with compute-pass timestampWrites and a resolve: three
 * runs of strictly increasing, nonzero GPU-ns intervals; ordinary submissions
 * afterwards see the last run's published query state; a recording that
 * resolves queries it did not write is rejected. */
static void timestamp_program(const state *s)
{
    state t = {.instance=s->instance,.adapter=s->adapter};
    WGPUFeatureName required = WGPUFeatureName_TimestampQuery;
    WGPUDeviceDescriptor descriptor = WGPU_DEVICE_DESCRIPTOR_INIT;
    descriptor.requiredFeatureCount = 1;
    descriptor.requiredFeatures = &required;
    descriptor.uncapturedErrorCallbackInfo = (WGPUUncapturedErrorCallbackInfo){.callback=error_reply,.userdata1=&t};
    WGPURequestDeviceCallbackInfo reply = {.mode=WGPUCallbackMode_AllowSpontaneous,.callback=device_reply,.userdata1=&t};
    assert(wgpuAdapterRequestDevice(t.adapter, &descriptor, reply).id && t.device);
    t.queue = wgpuDeviceGetQueue(t.device);
    double period = 0;
    uint32_t bits = 0;
    assert(doeNativeComputeProgramTimestampNanoseconds(t.device, &period, &bits) == 1 && period == 1.0 && bits == 64);
    WGPUComputePipeline busy = pipeline(&t, busy_code);
    WGPUBuffer sink = buffer(&t, 64 * 64 * 4, WGPUBufferUsage_Storage);
    WGPUBindGroupEntry entry = {.binding=0,.buffer=sink,.size=64 * 64 * 4};
    WGPUBindGroup sink_group = group(&t, busy, &entry, 1);
    WGPUQuerySetDescriptor query_descriptor = {.type=WGPUQueryType_Timestamp,.count=2};
    WGPUQuerySet queries = wgpuDeviceCreateQuerySet(t.device, &query_descriptor);
    WGPUBuffer resolved = buffer(&t, 256, WGPUBufferUsage_QueryResolve | WGPUBufferUsage_CopySrc);
    WGPUBuffer readback = buffer(&t, 16, WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst);
    assert(queries);

    /* Not self-contained: resolving queries this recording never wrote. */
    WGPUCommandBuffer commands = timed_commands(&t, busy, sink_group, queries, resolved, readback, false);
    expect_null_prepare(&t, doeNativeComputeProgramPrepare, t.queue, commands);
    wgpuCommandBufferRelease(commands);

    commands = timed_commands(&t, busy, sink_group, queries, resolved, readback, true);
    void *program = doeNativeComputeProgramPrepare(t.queue, commands);
    assert(program);
    wgpuCommandBufferRelease(commands);
    uint64_t times[2] = {0, 0}, previous_end = 0;
    for (int run = 0; run < 3; ++run) {
        assert(doeNativeComputeProgramSubmit(program) == 1);
        read_times(&t, readback, times);
        assert(times[0] != 0 && times[1] > times[0]);  /* Nonzero GPU-ns interval. */
        assert(times[0] > previous_end);               /* Each run measures new GPU time. */
        previous_end = times[1];
    }
    /* Ordinary work sees the last run's published state: resolving the written
     * indices returns those exact values, and new writes reset and advance. */
    commands = timed_commands(&t, busy, sink_group, queries, resolved, readback, false);
    wgpuQueueSubmit(t.queue, 1, &commands);
    wgpuCommandBufferRelease(commands);
    uint64_t again[2];
    read_times(&t, readback, again);
    assert(again[0] == times[0] && again[1] == times[1]);
    commands = timed_commands(&t, busy, sink_group, queries, resolved, readback, true);
    wgpuQueueSubmit(t.queue, 1, &commands);
    wgpuCommandBufferRelease(commands);
    read_times(&t, readback, again);
    assert(again[0] > times[1] && again[1] > again[0]);
    /* The program still runs after ordinary use of its query set. */
    assert(doeNativeComputeProgramSubmit(program) == 1);
    read_times(&t, readback, times);
    assert(times[0] > again[1] && times[1] > times[0]);
    doeNativeComputeProgramRelease(program);
    wgpuBufferRelease(readback);
    wgpuBufferRelease(resolved);
    wgpuQuerySetRelease(queries);
    wgpuBindGroupRelease(sink_group);
    wgpuBufferRelease(sink);
    wgpuComputePipelineRelease(busy);
    assert(!t.errors);
    wgpuQueueRelease(t.queue);
    wgpuDeviceRelease(t.device);
}

int main(void)
{
    state s = {0};
    consumer_init(&s);
    assert(doeNativeComputeProgramContractVersion() == DOE_COMPUTE_PROGRAM_CONTRACT);
    assert(doeNativeComputeProgramSupported() == 1);
    double period = -1.0;
    uint32_t bits = 0;
    assert(doeNativeComputeProgramTimestampNanoseconds(s.device, &period, &bits) == 0 && period == -1.0);
    program_fixture f = create_fixture(&s);

    for (int mode = 0; mode < 2; ++mode) {
        bool host_replay = mode == 1;
        void *(*prepare)(void *, void *) = host_replay ?
            doeNativeComputeProgramPrepareHostReplay : doeNativeComputeProgramPrepare;
        WGPUCommandBuffer commands = record(&s, &f, true);
        void *program = prepare(s.queue, commands);
        assert(program);
        expect_null_prepare(&s, prepare, s.queue, commands); /* Already claimed. */
        wgpuDevicePushErrorScope(s.device, WGPUErrorFilter_Validation);
        wgpuQueueSubmit(s.queue, 1, &commands); /* Single-use path rejects program commands. */
        expect_validation(&s);
        wgpuCommandBufferRelease(commands); /* Program keeps its own reference. */
        run(&s, &f, program, host_replay);
        doeNativeComputeProgramRelease(program);
    }

    /* Dependency lifetime: a destroyed bound buffer stops later submissions. */
    WGPUCommandBuffer commands = record(&s, &f, true);
    void *program = doeNativeComputeProgramPrepare(s.queue, commands);
    wgpuCommandBufferRelease(commands);
    assert(program && doeNativeComputeProgramSubmit(program) == 1);
    program_fixture spare = create_fixture(&s);
    WGPUCommandBuffer spare_commands = record(&s, &spare, false);
    void *spare_program = doeNativeComputeProgramPrepareHostReplay(s.queue, spare_commands);
    wgpuCommandBufferRelease(spare_commands);
    wgpuBufferDestroy(spare.input);
    wgpuDevicePushErrorScope(s.device, WGPUErrorFilter_Validation);
    assert(doeNativeComputeProgramSubmit(spare_program) == 0);
    expect_validation(&s);
    doeNativeComputeProgramRelease(spare_program);
    release_fixture(&spare);
    /* Early public releases: the program still owns everything it recorded. */
    release_fixture(&f);
    assert(doeNativeComputeProgramSubmit(program) == 1);
    doeNativeComputeProgramRelease(program);

    /* GPU recording admits only buffer copies, clears and direct dispatches. */
    WGPUBuffer a = buffer(&s, 16, WGPUBufferUsage_CopySrc), b = buffer(&s, 16, WGPUBufferUsage_CopyDst);
    WGPUTexture image = texture(&s, WGPUTextureFormat_RGBA8Unorm, WGPUTextureUsage_CopySrc, 1);
    WGPUBuffer pixels = buffer(&s, 512, WGPUBufferUsage_CopyDst);
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s.device, NULL);
    wgpuCommandEncoderCopyBufferToBuffer(encoder, a, 0, b, 0, 16);
    texture_to_buffer(encoder, image, pixels, 0, 2, 2);
    commands = wgpuCommandEncoderFinish(encoder, NULL);
    wgpuCommandEncoderRelease(encoder);
    expect_null_prepare(&s, doeNativeComputeProgramPrepare, s.queue, commands);
    program = doeNativeComputeProgramPrepareHostReplay(s.queue, commands); /* Host replay admits it. */
    assert(program && doeNativeComputeProgramSubmit(program) == 1 && doeNativeComputeProgramSubmit(program) == 1);
    doeNativeComputeProgramRelease(program);
    wgpuCommandBufferRelease(commands);
    encoder = wgpuDeviceCreateCommandEncoder(s.device, NULL);
    commands = wgpuCommandEncoderFinish(encoder, NULL); /* Empty: nothing to record. */
    wgpuCommandEncoderRelease(encoder);
    expect_null_prepare(&s, doeNativeComputeProgramPrepare, s.queue, commands);
    wgpuCommandBufferRelease(commands);
    wgpuBufferRelease(pixels);
    wgpuTextureRelease(image);
    wgpuBufferRelease(b);
    wgpuBufferRelease(a);
    timestamp_program(&s);
    assert(doeNativeComputeProgramPrepare(NULL, NULL) == NULL);
    assert(doeNativeComputeProgramSubmit(NULL) == 0);
    doeNativeComputeProgramRelease(NULL);
    consumer_finish(&s);
    printf("PASS Doe compute programs: one reusable GPU recording and host replay, 3 exact runs each with changing inputs and mapped readback between runs, single-use queue rejection, claimed-twice, destroyed dependency, early public releases, GPU-recording admission, gpu-recorded timestamp program (3 runs of increasing nonzero GPU-ns intervals, published state for later ordinary resolves/writes, non-self-contained resolve rejected) and null handles %s\n",
        adapter_class_scope());
    return 0;
}
