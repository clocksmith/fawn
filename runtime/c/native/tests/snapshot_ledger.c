/* SPDX-License-Identifier: MIT
 * Tracked-restore byte ledgers against the declared-writable copy (D3).
 *
 * (a) The synthetic W0/W1 workload: R0..R3 4 x 48 MiB read storage, W0/W1
 *     2 x 32 MiB read_write storage, 8 steps per branch, each rewriting 128 of
 *     the 1,024 64 KiB pages chosen by a seeded Fisher-Yates draw (expected
 *     written fraction 1 - (7/8)^8 = 65.6%). Every branch output is compared
 *     word for word with an independent CPU computation.
 * (b) A large-scratch workload: five read_write scratch buffers of 48, 48,
 *     32, 16 and 16 MiB plus a 4 MiB one, bound writable every frame but
 *     barely written, a 1280x1024 RGBA8 storage atlas and a 956x655 RGBA8
 *     view target redrawn each frame.
 * For both, every page a CPU diff shows changed must be marked (zero misses),
 * the base must stay intact and each recycled slot copies only marked bytes.
 * Usage: snapshot-ledger [branches], default 4. */
#include "snapshot_fixture.h"

enum { W_WORDS = 8u * 1024u * 1024u, R_WORDS = 12u * 1024u * 1024u, STEPS = 8, PAGES = 1024,
    SELECTED = 128, PAGE_WORDS = 16384, PARAM_STRIDE = 768 };
static const uint64_t BASE_SEED = 20261123u;

static uint64_t splitmix64(uint64_t *state)
{
    uint64_t z = (*state += UINT64_C(0x9e3779b97f4a7c15));
    z = (z ^ (z >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    z = (z ^ (z >> 27)) * UINT64_C(0x94d049bb133111eb);
    return z ^ (z >> 31);
}
static void splitmix_words(uint32_t *words, size_t count, uint64_t seed)
{
    for (size_t i = 0; i < count; ++i) words[i] = (uint32_t)splitmix64(&seed);
}
static uint64_t branch_seed(uint32_t branch)
{
    uint64_t state = BASE_SEED + 1000u + branch;
    return splitmix64(&state);
}
/* 128 distinct pages per step: the first draws of a seeded Fisher-Yates. */
static void step_pages(uint32_t branch, uint32_t step, uint32_t *out)
{
    uint32_t order[PAGES];
    for (uint32_t i = 0; i < PAGES; ++i) order[i] = i;
    uint64_t state = branch_seed(branch) + step;
    for (uint32_t k = 0; k < SELECTED; ++k) {
        uint32_t j = k + (uint32_t)(splitmix64(&state) % (PAGES - k));
        uint32_t swap = order[k];
        order[k] = order[j];
        order[j] = swap;
        out[k] = order[k];
    }
}

static const char w_source[] =
    "struct Params { t: u32, seed: u32, pad0: u32, pad1: u32, pages: array<vec4<u32>, 32> }\n"
    "@group(0) @binding(0) var<storage, read> r0: array<u32>;\n"
    "@group(0) @binding(1) var<storage, read> r1: array<u32>;\n"
    "@group(0) @binding(2) var<storage, read> r2: array<u32>;\n"
    "@group(0) @binding(3) var<storage, read> r3: array<u32>;\n"
    "@group(0) @binding(4) var<storage, read_write> w0: array<u32>;\n"
    "@group(0) @binding(5) var<storage, read_write> w1: array<u32>;\n"
    "@group(0) @binding(6) var<uniform> params: Params;\n"
    "fn rotl7(x: u32) -> u32 { return (x << 7u) | (x >> 25u); }\n"
    "@compute @workgroup_size(256)\n"
    "fn main(@builtin(workgroup_id) wg: vec3<u32>, @builtin(local_invocation_index) li: u32) {\n"
    "  let slot = wg.x / 64u;\n"
    "  let page = params.pages[slot / 4u][slot % 4u];\n"
    "  let i = page * 16384u + (wg.x % 64u) * 256u + li;\n"
    "  let j = (i * 2654435761u + params.t) % 12582912u;\n"
    "  let which = (i >> 12u) & 3u;\n"
    "  var r = r3[j];\n"
    "  if (which == 0u) { r = r0[j]; } else if (which == 1u) { r = r1[j]; } else if (which == 2u) { r = r2[j]; }\n"
    "  if (i < 8388608u) { w0[i] = rotl7(w0[i] ^ r) * 2147483659u + params.seed; }\n"
    "  else { let k = i - 8388608u; w1[k] = rotl7(w1[k] ^ r) * 2147483659u + params.seed; }\n"
    "}\n";

static uint32_t rotl7(uint32_t x) { return (x << 7) | (x >> 25); }

/* The CPU reference: no WebGPU, the same arithmetic on host words. */
static void cpu_branch(uint32_t branch, uint32_t *const *r, uint32_t *w)
{
    const uint32_t seed = (uint32_t)branch_seed(branch);
    uint32_t pages[SELECTED];
    for (uint32_t t = 0; t < STEPS; ++t) {
        step_pages(branch, t, pages);
        for (uint32_t s = 0; s < SELECTED; ++s)
            for (uint32_t k = 0; k < PAGE_WORDS; ++k) {
                uint32_t i = pages[s] * PAGE_WORDS + k;
                uint32_t j = (i * 2654435761u + t) % R_WORDS;
                w[i] = rotl7(w[i] ^ r[(i >> 12) & 3][j]) * 2147483659u + seed;
            }
    }
}

typedef struct ledger_row { uint64_t prepared, marked, changed_pages, marked_pages; } ledger_row;

static WGPUComputePipeline compute(state *s, const char *code)
{
    WGPUShaderModule module = snapshot_module(s, code);
    WGPUComputePipelineDescriptor descriptor = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
    descriptor.compute.module = module;
    WGPUComputePipeline result = wgpuDeviceCreateComputePipeline(s->device, &descriptor);
    wgpuShaderModuleRelease(module);
    assert(result);
    return result;
}

static void workload_w(state *s, uint32_t branches)
{
    const WGPUBufferUsage tracked = WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst;
    uint32_t *r_host[4], *w_base = malloc((size_t)2 * W_WORDS * 4), *w_cpu = malloc((size_t)2 * W_WORDS * 4);
    WGPUBuffer r[4], w[2];
    assert(w_base && w_cpu);
    for (uint32_t k = 0; k < 4; ++k) {
        r_host[k] = malloc((size_t)R_WORDS * 4);
        assert(r_host[k]);
        splitmix_words(r_host[k], R_WORDS, BASE_SEED + 1 + k);
        r[k] = buffer(s, (uint64_t)R_WORDS * 4, WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst);
        wgpuQueueWriteBuffer(s->queue, r[k], 0, r_host[k], (size_t)R_WORDS * 4);
    }
    for (uint32_t k = 0; k < 2; ++k) {
        splitmix_words(w_base + (size_t)k * W_WORDS, W_WORDS, BASE_SEED + 5 + k);
        w[k] = buffer(s, (uint64_t)W_WORDS * 4, tracked);
        wgpuQueueWriteBuffer(s->queue, w[k], 0, w_base + (size_t)k * W_WORDS, (size_t)W_WORDS * 4);
    }
    WGPUBuffer params = buffer(s, (uint64_t)STEPS * PARAM_STRIDE, WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst);
    WGPUComputePipeline pipeline = compute(s, w_source);
    work_done(s);
    void *snapshot = doeNativeSnapshotCreate(s->device, &(DoeNativeSnapshotDesc){.bufferCount=2,.buffers=w});
    assert(snapshot);
    WGPUBindGroup groups[STEPS];
    for (uint32_t t = 0; t < STEPS; ++t) {
        WGPUBindGroupEntry entries[7] = {{.binding=0,.buffer=r[0],.size=WGPU_WHOLE_SIZE},
            {.binding=1,.buffer=r[1],.size=WGPU_WHOLE_SIZE},{.binding=2,.buffer=r[2],.size=WGPU_WHOLE_SIZE},
            {.binding=3,.buffer=r[3],.size=WGPU_WHOLE_SIZE},{.binding=4,.buffer=w[0],.size=WGPU_WHOLE_SIZE},
            {.binding=5,.buffer=w[1],.size=WGPU_WHOLE_SIZE},
            {.binding=6,.buffer=params,.offset=(uint64_t)t * PARAM_STRIDE,.size=16 + SELECTED * 4}};
        groups[t] = group(s, pipeline, entries, 7);
    }
    uint64_t total_prepared = 0, total_marked = 0, total_changed = 0;
    printf("(a) W0/W1 workload: D3 copies %.2f MiB per branch\n", mib((uint64_t)2 * W_WORDS * 4));
    printf("    branch  prepared MiB  marked pages  changed pages  marked MiB  density\n");
    for (uint32_t b = 0; b < branches; ++b) {
        /* Parameters are untracked inputs, written before the branch opens. */
        uint32_t words[STEPS][PARAM_STRIDE / 4] = {{0}};
        for (uint32_t t = 0; t < STEPS; ++t) {
            words[t][0] = t;
            words[t][1] = (uint32_t)branch_seed(b);
            step_pages(b, t, &words[t][4]);
        }
        wgpuQueueWriteBuffer(s->queue, params, 0, words, sizeof(words));
        assert(doeNativeSnapshotBranchBegin(snapshot) == 1);
        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device, NULL);
        for (uint32_t t = 0; t < STEPS; ++t) {
            WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, NULL);
            wgpuComputePassEncoderSetPipeline(pass, pipeline);
            wgpuComputePassEncoderSetBindGroup(pass, 0, groups[t], 0, NULL);
            wgpuComputePassEncoderDispatchWorkgroups(pass, SELECTED * 64, 1, 1);
            wgpuComputePassEncoderEnd(pass);
            wgpuComputePassEncoderRelease(pass);
        }
        readback out0 = readback_buffer(s, encoder, w[0], (uint64_t)W_WORDS * 4);
        readback out1 = readback_buffer(s, encoder, w[1], (uint64_t)W_WORDS * 4);
        submit(s, encoder);
        work_done(s);
        assert(doeNativeSnapshotBranchEnd(snapshot) == 1);
        uint8_t *gpu[2] = {readback_take(s, &out0), readback_take(s, &out1)};
        memcpy(w_cpu, w_base, (size_t)2 * W_WORDS * 4);
        uint32_t *const rr[4] = {r_host[0], r_host[1], r_host[2], r_host[3]};
        cpu_branch(b, rr, w_cpu);
        size_t changed = 0, marked = 0;
        for (uint32_t k = 0; k < 2; ++k) {
            if (memcmp(gpu[k], w_cpu + (size_t)k * W_WORDS, (size_t)W_WORDS * 4)) {
                fprintf(stderr, "FAIL branch %u: W%u differs from the CPU reference\n", b, k);
                abort();
            }
            uint8_t diff[PAGES / 2];
            changed += changed_units((const uint8_t *)(w_base + (size_t)k * W_WORDS), gpu[k],
                (uint64_t)W_WORDS * 4, 0, 0, diff, sizeof(diff));
            marked += require_covered(snapshot, k, diff, sizeof(diff), k ? "W1" : "W0");
            free(gpu[k]);
        }
        DoeNativeSnapshotLedger ledger;
        assert(doeNativeSnapshotGetLedger(snapshot, &ledger) == 1);
        assert(ledger.lastMarkedUnits == marked);
        printf("    %6u  %12.2f  %12zu  %13zu  %10.2f  %6.1f%%\n", b, mib(ledger.lastPrepareBytes), marked, changed,
            mib(ledger.lastMarkedBytes), 100.0 * (double)marked / PAGES);
        total_prepared += ledger.lastPrepareBytes;
        total_marked += ledger.lastMarkedBytes;
        total_changed += changed;
    }
    /* The base never changed: read it outside any branch. */
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device, NULL);
    readback base0 = readback_buffer(s, encoder, w[0], (uint64_t)W_WORDS * 4);
    readback base1 = readback_buffer(s, encoder, w[1], (uint64_t)W_WORDS * 4);
    submit(s, encoder);
    work_done(s);
    uint8_t *after[2] = {readback_take(s, &base0), readback_take(s, &base1)};
    for (uint32_t k = 0; k < 2; ++k) {
        if (memcmp(after[k], w_base + (size_t)k * W_WORDS, (size_t)W_WORDS * 4)) {
            fprintf(stderr, "FAIL leak canary: W%u base changed\n", k);
            abort();
        }
        free(after[k]);
    }
    const uint64_t d3 = (uint64_t)branches * 2 * W_WORDS * 4;
    printf("    %u branches: E prepared %.2f MiB (first slot fill %.2f MiB, then marked pages only) vs D3 %.2f MiB;"
        " steady state %.2f MiB per branch = %.1f%% of D3; mean changed pages %.1f\n",
        branches, mib(total_prepared), mib((uint64_t)2 * W_WORDS * 4), mib(d3), mib(total_marked) / branches,
        100.0 * (double)total_marked / (double)d3, (double)total_changed / branches);
    for (uint32_t t = 0; t < STEPS; ++t) wgpuBindGroupRelease(groups[t]);
    doeNativeSnapshotRelease(snapshot);
    wgpuComputePipelineRelease(pipeline);
    wgpuBufferRelease(params);
    for (uint32_t k = 0; k < 4; ++k) { wgpuBufferRelease(r[k]); free(r_host[k]); }
    for (uint32_t k = 0; k < 2; ++k) wgpuBufferRelease(w[k]);
    free(w_base);
    free(w_cpu);
}

#include "snapshot_scratch.h"

int main(int argc, char **argv)
{
    uint32_t branches = argc > 1 ? (uint32_t)strtoul(argv[1], NULL, 10) : 4;
    assert(branches >= 2 && branches <= 64);
    state s = {0};
    snapshot_device(&s);
    workload_w(&s, branches);
    workload_scratch(&s, branches);
    printf("PASS snapshot ledgers: exact outputs, zero missed marks, base intact, restores copy marked bytes only %s\n",
        adapter_class_scope());
    consumer_finish(&s);
    return 0;
}
