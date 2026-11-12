/* SPDX-License-Identifier: MIT
 * Public-ABI dispatch chains: consecutive dispatches in one compute pass read
 * the previous dispatch's output (storage write -> read-only storage), rewrite
 * the same storage buffer (write -> read_write hazard), read one buffer as a
 * uniform then as read-only storage, and are followed in the same encoder by a
 * copy, a second pass and a second command buffer in the same submit. Every
 * output word is compared with a CPU oracle. Backends that keep buffers in
 * their shader state across dispatches (D3D12 held states) must still order
 * each hazard. A physical run is developer evidence on the reported adapter. */
#include "consumer.h"
#include <stdint.h>
#include <stdlib.h>

enum { WORDS = 1u << 18, BYTES = WORDS * 4, GROUP = 64, ROUNDS = 48 };

static const char *SEED =
    "@group(0) @binding(0) var<storage, read_write> a: array<u32>;"
    "@compute @workgroup_size(64) fn main(@builtin(global_invocation_id) id: vec3u) {"
    "  a[id.x] = id.x * 2654435761u; }";
/* Thread i owns word i on even rounds and word N-1-i on odd rounds, so a
 * dispatch that overlapped its predecessor would read words still being written. */
static const char *STEP =
    "struct P { round: u32, add: u32, pad0: u32, pad1: u32 }"
    "@group(0) @binding(0) var<storage, read_write> a: array<u32>;"
    "@group(0) @binding(1) var<uniform> p: P;"
    "@compute @workgroup_size(64) fn main(@builtin(global_invocation_id) id: vec3u) {"
    "  let n = arrayLength(&a);"
    "  var i = id.x; if ((p.round & 1u) == 1u) { i = n - 1u - id.x; }"
    "  a[i] = a[i] * 3u + p.add + p.round; }";
static const char *DOUBLE =
    "@group(0) @binding(0) var<storage, read> a: array<u32>;"
    "@group(0) @binding(1) var<storage, read_write> b: array<u32>;"
    "@compute @workgroup_size(64) fn main(@builtin(global_invocation_id) id: vec3u) {"
    "  let n = arrayLength(&a); b[id.x] = a[n - 1u - id.x] * 2u + 1u; }";
static const char *ACCUMULATE =
    "struct P { round: u32, add: u32, pad0: u32, pad1: u32 }"
    "@group(0) @binding(0) var<storage, read_write> a: array<u32>;"
    "@group(0) @binding(1) var<storage, read> b: array<u32>;"
    "@group(0) @binding(2) var<storage, read> q: P;"
    "@compute @workgroup_size(64) fn main(@builtin(global_invocation_id) id: vec3u) {"
    "  a[id.x] = a[id.x] ^ b[id.x] ^ q.add; }";

typedef struct params { uint32_t round, add, pad0, pad1; } params;

static void run(WGPUComputePassEncoder pass, WGPUComputePipeline pipeline, WGPUBindGroup group)
{
    wgpuComputePassEncoderSetPipeline(pass, pipeline);
    wgpuComputePassEncoderSetBindGroup(pass, 0, group, 0, NULL);
    wgpuComputePassEncoderDispatchWorkgroups(pass, WORDS / GROUP, 1, 1);
}

static WGPUBindGroup bind(state *s, WGPUComputePipeline pipeline, WGPUBuffer *buffers, const uint64_t *sizes, size_t count)
{
    WGPUBindGroupEntry entries[3];
    for (size_t i = 0; i < count; ++i)
        entries[i] = (WGPUBindGroupEntry){.binding=(uint32_t)i,.buffer=buffers[i],.size=sizes[i]};
    return group(s, pipeline, entries, count);
}

static void expect(const uint32_t *got, const uint32_t *want, const char *label)
{
    for (uint32_t i = 0; i < WORDS; ++i) {
        if (got[i] == want[i]) continue;
        fprintf(stderr, "%s word %u: got %u expected %u\n", label, i, got[i], want[i]);
        abort();
    }
}

int main(void)
{
    state s = {0};
    consumer_init(&s);
    const WGPUBufferUsage storage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst;
    WGPUBuffer a = buffer(&s, BYTES, storage), b = buffer(&s, BYTES, storage), c = buffer(&s, BYTES, storage);
    WGPUBuffer uniforms[ROUNDS];
    params values[ROUNDS];
    for (uint32_t r = 0; r < ROUNDS; ++r) {
        values[r] = (params){r, 7u + r * 13u, 0, 0};
        uniforms[r] = buffer(&s, 256, WGPUBufferUsage_Uniform | WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst);
        wgpuQueueWriteBuffer(s.queue, uniforms[r], 0, &values[r], sizeof(values[r]));
    }
    WGPUBuffer readback = buffer(&s, BYTES, WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst);
    WGPUComputePipeline seed = pipeline(&s, SEED), step = pipeline(&s, STEP);
    WGPUComputePipeline twice = pipeline(&s, DOUBLE), accumulate = pipeline(&s, ACCUMULATE);
    WGPUBindGroup seed_group = bind(&s, seed, &a, (const uint64_t[]){BYTES}, 1);
    WGPUBindGroup step_groups[ROUNDS], accumulate_groups[ROUNDS];
    for (uint32_t r = 0; r < ROUNDS; ++r) {
        step_groups[r] = bind(&s, step, (WGPUBuffer[]){a, uniforms[r]}, (const uint64_t[]){BYTES, 256}, 2);
        accumulate_groups[r] = bind(&s, accumulate, (WGPUBuffer[]){a, b, uniforms[r]}, (const uint64_t[]){BYTES, BYTES, 256}, 3);
    }
    WGPUBindGroup double_ab = bind(&s, twice, (WGPUBuffer[]){a, b}, (const uint64_t[]){BYTES, BYTES}, 2);
    WGPUBindGroup double_bc = bind(&s, twice, (WGPUBuffer[]){b, c}, (const uint64_t[]){BYTES, BYTES}, 2);

    uint32_t *want_a = malloc(BYTES), *want_b = malloc(BYTES), *want_c = malloc(BYTES);
    assert(want_a && want_b && want_c);
    for (uint32_t i = 0; i < WORDS; ++i) want_a[i] = i * 2654435761u;

    /* One pass: seed, ROUNDS read_write rounds (UAV->UAV) where each round's
     * uniform buffer is next read as read-only storage (uniform -> storage read),
     * a write->read hand-off into b and an accumulate back into a. */
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s.device, NULL);
    WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, NULL);
    run(pass, seed, seed_group);
    for (uint32_t r = 0; r < ROUNDS; ++r) {
        run(pass, step, step_groups[r]);
        for (uint32_t i = 0; i < WORDS; ++i) want_a[i] = want_a[i] * 3u + values[r].add + r;
        if (r % 8 != 7) continue;
        run(pass, twice, double_ab);
        for (uint32_t i = 0; i < WORDS; ++i) want_b[i] = want_a[WORDS - 1 - i] * 2u + 1u;
        run(pass, accumulate, accumulate_groups[r]);
        for (uint32_t i = 0; i < WORDS; ++i) want_a[i] ^= want_b[i] ^ values[r].add;
    }
    /* Consecutive identical dispatch (same pipeline and bind group) after a write. */
    run(pass, twice, double_ab);
    run(pass, twice, double_bc);
    run(pass, twice, double_bc);
    for (uint32_t i = 0; i < WORDS; ++i) want_b[i] = want_a[WORDS - 1 - i] * 2u + 1u;
    for (uint32_t i = 0; i < WORDS; ++i) want_c[i] = want_b[WORDS - 1 - i] * 2u + 1u;
    wgpuComputePassEncoderEnd(pass);
    wgpuComputePassEncoderRelease(pass);
    /* Copy in the same encoder, then another pass that reads the copy target. */
    wgpuCommandEncoderCopyBufferToBuffer(encoder, c, 0, b, 0, BYTES);
    memcpy(want_b, want_c, BYTES);
    pass = wgpuCommandEncoderBeginComputePass(encoder, NULL);
    run(pass, twice, double_bc);
    wgpuComputePassEncoderEnd(pass);
    wgpuComputePassEncoderRelease(pass);
    for (uint32_t i = 0; i < WORDS; ++i) want_c[i] = want_b[WORDS - 1 - i] * 2u + 1u;
    WGPUCommandBuffer first = wgpuCommandEncoderFinish(encoder, NULL);
    wgpuCommandEncoderRelease(encoder);
    /* A second command buffer in the same submit rewrites a and reads c. */
    encoder = wgpuDeviceCreateCommandEncoder(s.device, NULL);
    pass = wgpuCommandEncoderBeginComputePass(encoder, NULL);
    run(pass, step, step_groups[1]);
    run(pass, accumulate, accumulate_groups[1]);
    wgpuComputePassEncoderEnd(pass);
    wgpuComputePassEncoderRelease(pass);
    for (uint32_t i = WORDS; i-- > 0;) want_a[i] = want_a[i] * 3u + values[1].add + 1u;
    for (uint32_t i = 0; i < WORDS; ++i) want_a[i] ^= want_b[i] ^ values[1].add;
    wgpuCommandEncoderCopyBufferToBuffer(encoder, a, 0, readback, 0, BYTES);
    WGPUCommandBuffer second = wgpuCommandEncoderFinish(encoder, NULL);
    wgpuCommandEncoderRelease(encoder);
    WGPUCommandBuffer both[2] = {first, second};
    wgpuQueueSubmit(s.queue, 2, both);
    wgpuCommandBufferRelease(first);
    wgpuCommandBufferRelease(second);
    expect(map(&s, readback, BYTES), want_a, "a");
    wgpuBufferUnmap(readback);
    encoder = wgpuDeviceCreateCommandEncoder(s.device, NULL);
    wgpuCommandEncoderCopyBufferToBuffer(encoder, c, 0, readback, 0, BYTES);
    submit(&s, encoder);
    expect(map(&s, readback, BYTES), want_c, "c");
    wgpuBufferUnmap(readback);
    /* A queue write between submissions lands before the next dispatch reads it. */
    params changed = {0, 99, 0, 0};
    wgpuQueueWriteBuffer(s.queue, uniforms[0], 0, &changed, sizeof(changed));
    encoder = wgpuDeviceCreateCommandEncoder(s.device, NULL);
    pass = wgpuCommandEncoderBeginComputePass(encoder, NULL);
    run(pass, step, step_groups[0]);
    run(pass, step, step_groups[0]);
    wgpuComputePassEncoderEnd(pass);
    wgpuComputePassEncoderRelease(pass);
    wgpuCommandEncoderCopyBufferToBuffer(encoder, a, 0, readback, 0, BYTES);
    submit(&s, encoder);
    for (uint32_t i = 0; i < WORDS; ++i) want_a[i] = (want_a[i] * 3u + 99u) * 3u + 99u;
    expect(map(&s, readback, BYTES), want_a, "a after write");
    wgpuBufferUnmap(readback);
    assert(!s.errors);
    free(want_a); free(want_b); free(want_c);
    for (uint32_t r = 0; r < ROUNDS; ++r) {
        wgpuBindGroupRelease(step_groups[r]);
        wgpuBindGroupRelease(accumulate_groups[r]);
        wgpuBufferRelease(uniforms[r]);
    }
    wgpuBindGroupRelease(seed_group); wgpuBindGroupRelease(double_ab); wgpuBindGroupRelease(double_bc);
    wgpuComputePipelineRelease(seed); wgpuComputePipelineRelease(step);
    wgpuComputePipelineRelease(twice); wgpuComputePipelineRelease(accumulate);
    wgpuBufferRelease(a); wgpuBufferRelease(b); wgpuBufferRelease(c); wgpuBufferRelease(readback);
    consumer_finish(&s);
    printf("PASS public dispatch chains: %u read_write rounds over one buffer, storage write->read hand-offs, "
        "uniform then storage reads of one buffer, repeated identical dispatches, copy and second pass in one "
        "encoder, two command buffers in one submit and a queue write between submissions %s\n",
        (unsigned)ROUNDS, adapter_class_scope());
    return 0;
}
