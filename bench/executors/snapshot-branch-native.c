/* SPDX-License-Identifier: MIT
 * Snapshot-branch timing executor: 32 branches from one base state, three arms.
 *
 *   D3  any provider: each branch copies every tracked resource from the base
 *       into one private slot, then runs against the slot.
 *   D5  any provider: the same WGSL with explicit stores into a mark buffer,
 *       written as Doe's DOE_COMPILER_WRITE_MARKS lowering writes them (64 KiB
 *       page per storage store, 256x256 tile per textureStore, index clamped to
 *       a sink word, the two words per slot read from a buffer instead of
 *       immediates); the application maps the marks after each branch and
 *       copies back only marked pages and tiles, coalesced the way Doe's
 *       restore coalesces them. Render attachments are marked on the host.
 *   E   Doe only (built with -DSNAPSHOT_BRANCH_DOE): the unmodified WGSL under
 *       doeNativeSnapshotEnableWriteMarks and the snapshot extension.
 *
 * Workloads are snapshot_ledger.c's: "w" (W0/W1 over R0..R3, 66% of pages
 * written per branch) and "scratch" (large barely written scratch, a storage
 * atlas and a redrawn target). Branch b's inputs are written before it opens.
 *
 * Clock: setup = slot or snapshot creation, bind groups and branch 0; a branch
 * is its input write, slot preparation, submission, completion wait and the
 * arm's mark handling (D5 map, E BranchEnd). The output readback (16 MiB
 * chunked map-read copies) and SHA-256 sit outside the clock in every arm and
 * run the same code; for E they run inside the open branch. Expected hashes
 * come from bench/oracles/snapshot_branch_reference_v1.c, never from here.
 *
 * Memory: a 20 ms in-process sampler of the physical footprint (Darwin) or
 * RssAnon (Linux); baseline is taken after device and pipeline creation,
 * before workload buffers. On a CPU adapter device memory is process memory.
 *
 * Usage: snapshot-branch-native <D3|D5|E> <w|scratch> [branches] [size divisor]
 * prints one JSON receipt. The divisor (1, 2, 4 or 8) shrinks the buffers for
 * software dry runs and is recorded in the receipt; 1 is the ledger's sizes.
 * Software adapters only unless SNAPSHOT_BRANCH_ADAPTER=physical.
 *
 * Build, from the repository root (C11, -pthread, -Ibench/oracles):
 *   Doe:  cc -DSNAPSHOT_BRANCH_DOE -Iruntime/c/native/include -Iruntime/vendor/webgpu-headers
 *         bench/executors/snapshot-branch-native.c -L<doe build> -lwebgpu_doe_c
 *   Dawn: cc -I<dawn out>/gen/include/dawn bench/executors/snapshot-branch-native.c
 *         -L<dawn out> -lwebgpu_dawn
 * Dawn's Vulkan backend on macOS opens only SwiftShader (gn: dawn_enable_vulkan
 * and dawn_use_swiftshader), by library name; put the directory of the
 * SwiftShader library behind Doe's Vulkan ICD on the loader search path so both
 * providers run one driver binary. Run with the same Vulkan environment for
 * every arm; bench/runners/snapshot_branch_timing.py interleaves the arms. */
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE
#else
#define _GNU_SOURCE
#endif
#include "webgpu.h"
#if defined(SNAPSHOT_BRANCH_DOE)
#include "doe_native_snapshot.h"
#define PROVIDER "doe"
#else
#define PROVIDER "dawn"
#endif
#include "sha256_small.h"
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#if defined(__APPLE__)
#include <mach/mach.h>
#endif

#define CHECK(cond, ...) do { if (!(cond)) { fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); \
    fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); exit(2); } } while (0)

enum { PAGE = 65536, TILE = 256, CHUNK = 16u << 20, MAX_TRACKED = 8, MAX_BRANCHES = 64 };
enum { STEPS = 8, MAX_PAGES = 1024, PAGE_WORDS = 16384, PARAM_STRIDE = 768, MAX_DIVISOR = 8 };
enum { SCRATCH_BUFFERS = 6, ATLAS_W = 1280, ATLAS_H = 1024, VIEW_W = 956, VIEW_H = 655 };
static const uint64_t BASE_SEED = 20261123u;
static const uint64_t scratch_mib[SCRATCH_BUFFERS] = {48, 48, 32, 16, 16, 4};
/* Size divisor d (1 = the ledger sizes; 8 for fast software dry runs): W0/W1
 * hold 1024/d pages, each step rewrites 1/8 of them (same 65.6% expected
 * density), R0..R3 are 1.5x a W buffer; scratch buffers are 1/d of their
 * sizes (s3's write window at 1 MiB still fits at d = 8). Textures keep their
 * sizes. */
static uint32_t divisor = 1, PAGES, SELECTED, W_WORDS, R_WORDS;
static uint64_t scratch_bytes(size_t i) { return (scratch_mib[i] << 20) / divisor; }
typedef enum { ARM_D3, ARM_D5, ARM_E } arm_kind;

/* ---- workload inputs (same generators as snapshot_ledger.c) ---- */
static uint64_t splitmix64(uint64_t *state)
{
    uint64_t z = (*state += UINT64_C(0x9e3779b97f4a7c15));
    z = (z ^ (z >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    z = (z ^ (z >> 27)) * UINT64_C(0x94d049bb133111eb);
    return z ^ (z >> 31);
}
static uint64_t branch_seed(uint32_t branch)
{
    uint64_t state = BASE_SEED + 1000u + branch;
    return splitmix64(&state);
}
static void step_pages(uint32_t branch, uint32_t step, uint32_t *out)
{
    uint32_t order[MAX_PAGES];
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

/* ---- WGSL: the ledger workloads, and the D5 hand-marked copies ---- */
#define W_HEAD \
    "struct Params { t: u32, seed: u32, pad0: u32, pad1: u32, pages: array<vec4<u32>, 32> }\n" \
    "@group(0) @binding(0) var<storage, read> r0: array<u32>;\n" \
    "@group(0) @binding(1) var<storage, read> r1: array<u32>;\n" \
    "@group(0) @binding(2) var<storage, read> r2: array<u32>;\n" \
    "@group(0) @binding(3) var<storage, read> r3: array<u32>;\n" \
    "@group(0) @binding(4) var<storage, read_write> w0: array<u32>;\n" \
    "@group(0) @binding(5) var<storage, read_write> w1: array<u32>;\n" \
    "@group(0) @binding(6) var<uniform> params: Params;\n" \
    "fn rotl7(x: u32) -> u32 { return (x << 7u) | (x >> 25u); }\n"
#define W_BODY_HEAD \
    "@compute @workgroup_size(256)\n" \
    "fn main(@builtin(workgroup_id) wg: vec3<u32>, @builtin(local_invocation_index) li: u32) {\n" \
    "  let slot = wg.x / 64u;\n" \
    "  let page = params.pages[slot / 4u][slot % 4u];\n" \
    "  let i = page * 16384u + (wg.x % 64u) * 256u + li;\n" \
    "  let j = (i * 2654435761u + params.t) % 12582912u;\n" \
    "  let which = (i >> 12u) & 3u;\n" \
    "  var r = r3[j];\n" \
    "  if (which == 0u) { r = r0[j]; } else if (which == 1u) { r = r1[j]; } else if (which == 2u) { r = r2[j]; }\n"
static const char w_source[] = W_HEAD W_BODY_HEAD
    "  if (i < 8388608u) { w0[i] = rotl7(w0[i] ^ r) * 2147483659u + params.seed; }\n"
    "  else { let k = i - 8388608u; w1[k] = rotl7(w1[k] ^ r) * 2147483659u + params.seed; }\n"
    "}\n";
/* Doe's lowering after a store: index = first + ((offset + byte) >> 16),
 * clamped to arrayLength - 1 (the sink), one plain store of 1. The two words
 * of slot k are mark_bases word 2k (first) and 2k + 1 (offset or tiles/row). */
#define MARK_FUNCTIONS(BINDING_MARKS, BINDING_BASES, VEC4S) \
    "@group(0) @binding(" BINDING_MARKS ") var<storage, read_write> marks: array<u32>;\n" \
    "@group(0) @binding(" BINDING_BASES ") var<uniform> mark_bases: array<vec4<u32>, " VEC4S ">;\n" \
    "fn mark_index(index: u32) {\n" \
    "  let last = arrayLength(&marks) - 1u;\n" \
    "  marks[select(last, index, index < last)] = 1u;\n" \
    "}\n" \
    "fn mark_page(first: u32, start: u32) { mark_index(first + (start >> 16u)); }\n" \
    "fn mark_tile(first: u32, per_row: u32, at: vec2<u32>) { mark_index(first + (at.y >> 8u) * per_row + (at.x >> 8u)); }\n"
static const char w_marked_source[] = W_HEAD MARK_FUNCTIONS("7", "8", "1") W_BODY_HEAD
    "  if (i < 8388608u) { w0[i] = rotl7(w0[i] ^ r) * 2147483659u + params.seed;"
    " mark_page(mark_bases[0].x, mark_bases[0].y + i * 4u); }\n"
    "  else { let k = i - 8388608u; w1[k] = rotl7(w1[k] ^ r) * 2147483659u + params.seed;"
    " mark_page(mark_bases[0].z, mark_bases[0].w + k * 4u); }\n"
    "}\n";

#define SCRATCH_HEAD \
    "@group(0) @binding(0) var<storage, read_write> s0: array<u32>;\n" \
    "@group(0) @binding(1) var<storage, read_write> s1: array<u32>;\n" \
    "@group(0) @binding(2) var<storage, read_write> s2: array<u32>;\n" \
    "@group(0) @binding(3) var<storage, read_write> s3: array<u32>;\n" \
    "@group(0) @binding(4) var<storage, read_write> s4: array<u32>;\n" \
    "@group(0) @binding(5) var<storage, read_write> s5: array<u32>;\n" \
    "@group(0) @binding(6) var atlas: texture_storage_2d<rgba8unorm, write>;\n" \
    "@group(0) @binding(7) var<uniform> frame: vec4<u32>;\n"
static const char scratch_source[] = SCRATCH_HEAD
    "@compute @workgroup_size(64) fn main(@builtin(global_invocation_id) g: vec3<u32>) {\n"
    "  let i = g.x;\n"
    "  if (i < 10240u) { s0[i] = i ^ frame.x; }\n"
    "  if (i < 6144u) { s1[i] = i + frame.x; }\n"
    "  if (i < 25600u) { s2[i] = i * frame.x + 1u; }\n"
    "  if (i < 10240u) { s3[262144u + i] = frame.x; }\n"
    "  if (s4[i] == 0xffffffffu) { s5[i] = s4[i]; }\n"
    "  if (i < 4096u) {\n"
    "    let at = vec2<u32>(frame.y + i % 64u, frame.z + i / 64u);\n"
    "    textureStore(atlas, at, vec4<f32>(f32(frame.x & 255u) / 255.0, 0.5, 0.25, 1.0));\n"
    "  }\n"
    "}\n";
/* Writable slots in source order: s0..s5 = 0..5, atlas = 6. */
static const char scratch_marked_source[] = SCRATCH_HEAD MARK_FUNCTIONS("8", "9", "4")
    "@compute @workgroup_size(64) fn main(@builtin(global_invocation_id) g: vec3<u32>) {\n"
    "  let i = g.x;\n"
    "  if (i < 10240u) { s0[i] = i ^ frame.x; mark_page(mark_bases[0].x, mark_bases[0].y + i * 4u); }\n"
    "  if (i < 6144u) { s1[i] = i + frame.x; mark_page(mark_bases[0].z, mark_bases[0].w + i * 4u); }\n"
    "  if (i < 25600u) { s2[i] = i * frame.x + 1u; mark_page(mark_bases[1].x, mark_bases[1].y + i * 4u); }\n"
    "  if (i < 10240u) { s3[262144u + i] = frame.x;"
    " mark_page(mark_bases[1].z, mark_bases[1].w + (262144u + i) * 4u); }\n"
    "  if (s4[i] == 0xffffffffu) { s5[i] = s4[i]; mark_page(mark_bases[2].z, mark_bases[2].w + i * 4u); }\n"
    "  if (i < 4096u) {\n"
    "    let at = vec2<u32>(frame.y + i % 64u, frame.z + i / 64u);\n"
    "    textureStore(atlas, at, vec4<f32>(f32(frame.x & 255u) / 255.0, 0.5, 0.25, 1.0));\n"
    "    mark_tile(mark_bases[3].x, mark_bases[3].y, at);\n"
    "  }\n"
    "}\n";
static const char view_source[] =
    "@group(0) @binding(0) var<uniform> frame: vec4<u32>;\n"
    "@vertex fn vs(@builtin(vertex_index) i: u32) -> @builtin(position) vec4<f32> {\n"
    "  return vec4<f32>(f32(i & 1u) * 4.0 - 1.0, f32(i >> 1u) * 4.0 - 1.0, 0.0, 1.0);\n"
    "}\n"
    "@fragment fn fs(@builtin(position) p: vec4<f32>) -> @location(0) vec4<f32> {\n"
    "  return vec4<f32>(p.x / 956.0, p.y / 655.0, f32(frame.x & 255u) / 255.0, 1.0);\n"
    "}\n";

/* ---- device, counters and waits ---- */
typedef struct tracked {
    bool texture, host_marked;          /* host_marked: a render attachment, marked whole. */
    uint64_t size;
    uint32_t width, height, tiles_x, units, first;
    WGPUBufferUsage buffer_usage;
    WGPUTextureUsage texture_usage;
    WGPUBuffer base_buffer, slot_buffer;
    WGPUTexture base_texture, slot_texture;
} tracked;

static struct {
    arm_kind arm;
    WGPUInstance instance;
    WGPUAdapter adapter;
    WGPUDevice device;
    WGPUQueue queue;
    unsigned errors;
    bool readback_phase;
    uint64_t submits, waits, readback_submits, readback_waits;
    uint64_t copy_bytes, copy_commands, logical_bytes;
    WGPUBuffer readback;
    size_t count;
    tracked t[MAX_TRACKED];
    uint32_t mark_words;
    uint8_t *last_marks;
    bool filled;
    WGPUBuffer marks, marks_staging, mark_bases;
} G;

static uint64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000u + (uint64_t)ts.tv_nsec;
}

static void on_error(WGPUDevice const *device, WGPUErrorType type, WGPUStringView message, void *u1, void *u2)
{
    (void)device; (void)u1; (void)u2;
    ++G.errors;
    fprintf(stderr, "device error %u: %.*s\n", (unsigned)type, (int)message.length, message.data);
}
static void on_adapter(WGPURequestAdapterStatus status, WGPUAdapter adapter, WGPUStringView message, void *u1, void *u2)
{
    (void)u1; (void)u2;
    CHECK(status == WGPURequestAdapterStatus_Success, "adapter: %.*s", (int)message.length, message.data);
    G.adapter = adapter;
}
static void on_device(WGPURequestDeviceStatus status, WGPUDevice device, WGPUStringView message, void *u1, void *u2)
{
    (void)u1; (void)u2;
    CHECK(status == WGPURequestDeviceStatus_Success, "device: %.*s", (int)message.length, message.data);
    G.device = device;
}
static void on_done(WGPUQueueWorkDoneStatus status, WGPUStringView message, void *u1, void *u2)
{
    (void)message; (void)u2;
    CHECK(status == WGPUQueueWorkDoneStatus_Success, "work done status %u", (unsigned)status);
    *(int *)u1 = 1;
}
static void on_map(WGPUMapAsyncStatus status, WGPUStringView message, void *u1, void *u2)
{
    (void)u2;
    CHECK(status == WGPUMapAsyncStatus_Success, "map: %.*s", (int)message.length, message.data);
    *(int *)u1 = 1;
}

static void wait_future(WGPUFuture future, const int *flag)
{
    CHECK(future.id, "zero future");
    WGPUFutureWaitInfo info = {.future = future};
    CHECK(wgpuInstanceWaitAny(G.instance, 1, &info, UINT64_C(600000000000)) == WGPUWaitStatus_Success && info.completed,
        "wait failed");
    CHECK(*flag, "callback did not run");
    if (G.readback_phase) ++G.readback_waits; else ++G.waits;
}
static void work_done(void)
{
    int done = 0;
    WGPUQueueWorkDoneCallbackInfo info = {.mode = WGPUCallbackMode_WaitAnyOnly, .callback = on_done, .userdata1 = &done};
    wait_future(wgpuQueueOnSubmittedWorkDone(G.queue, info), &done);
}
static const void *map_read(WGPUBuffer buffer, uint64_t size)
{
    int done = 0;
    WGPUBufferMapCallbackInfo info = {.mode = WGPUCallbackMode_WaitAnyOnly, .callback = on_map, .userdata1 = &done};
    wait_future(wgpuBufferMapAsync(buffer, WGPUMapMode_Read, 0, (size_t)size, info), &done);
    const void *bytes = wgpuBufferGetConstMappedRange(buffer, 0, (size_t)size);
    CHECK(bytes, "mapped range");
    return bytes;
}
static void submit(WGPUCommandEncoder encoder)
{
    WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, NULL);
    CHECK(commands, "finish");
    wgpuCommandEncoderRelease(encoder);
    wgpuQueueSubmit(G.queue, 1, &commands);
    wgpuCommandBufferRelease(commands);
    if (G.readback_phase) ++G.readback_submits; else ++G.submits;
}
static WGPUCommandEncoder encoder(void)
{
    WGPUCommandEncoder result = wgpuDeviceCreateCommandEncoder(G.device, NULL);
    CHECK(result, "encoder");
    return result;
}
static WGPUBuffer make_buffer(uint64_t size, WGPUBufferUsage usage)
{
    WGPUBufferDescriptor descriptor = WGPU_BUFFER_DESCRIPTOR_INIT;
    descriptor.size = size;
    descriptor.usage = usage;
    WGPUBuffer result = wgpuDeviceCreateBuffer(G.device, &descriptor);
    CHECK(result, "buffer of %llu bytes", (unsigned long long)size);
    G.logical_bytes += size;
    return result;
}
static WGPUTexture make_texture(uint32_t width, uint32_t height, WGPUTextureUsage usage)
{
    WGPUTextureDescriptor descriptor = WGPU_TEXTURE_DESCRIPTOR_INIT;
    descriptor.format = WGPUTextureFormat_RGBA8Unorm;
    descriptor.usage = usage;
    descriptor.size = (WGPUExtent3D){width, height, 1};
    WGPUTexture result = wgpuDeviceCreateTexture(G.device, &descriptor);
    CHECK(result, "texture");
    G.logical_bytes += (uint64_t)width * height * 4;
    return result;
}
static WGPUShaderModule module(const char *code)
{
    WGPUShaderSourceWGSL source = WGPU_SHADER_SOURCE_WGSL_INIT;
    source.code = (WGPUStringView){code, WGPU_STRLEN};
    WGPUShaderModuleDescriptor descriptor = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;
    descriptor.nextInChain = &source.chain;
    WGPUShaderModule result = wgpuDeviceCreateShaderModule(G.device, &descriptor);
    CHECK(result, "shader module");
    return result;
}
static WGPUComputePipeline compute_pipeline(const char *code)
{
    WGPUComputePipelineDescriptor descriptor = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
    descriptor.compute.module = module(code);
    descriptor.compute.entryPoint = (WGPUStringView){"main", WGPU_STRLEN};
    WGPUComputePipeline result = wgpuDeviceCreateComputePipeline(G.device, &descriptor);
    wgpuShaderModuleRelease(descriptor.compute.module);
    CHECK(result, "compute pipeline");
    return result;
}
static WGPURenderPipeline view_pipeline(void)
{
    WGPUShaderModule shader = module(view_source);
    WGPUColorTargetState target = WGPU_COLOR_TARGET_STATE_INIT;
    target.format = WGPUTextureFormat_RGBA8Unorm;
    WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
    fragment.module = shader;
    fragment.entryPoint = (WGPUStringView){"fs", WGPU_STRLEN};
    fragment.targetCount = 1;
    fragment.targets = &target;
    WGPURenderPipelineDescriptor descriptor = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    descriptor.vertex.module = shader;
    descriptor.vertex.entryPoint = (WGPUStringView){"vs", WGPU_STRLEN};
    descriptor.fragment = &fragment;
    WGPURenderPipeline result = wgpuDeviceCreateRenderPipeline(G.device, &descriptor);
    wgpuShaderModuleRelease(shader);
    CHECK(result, "render pipeline");
    return result;
}
static WGPUBindGroup bind_group(WGPUBindGroupLayout layout, const WGPUBindGroupEntry *entries, size_t count)
{
    WGPUBindGroupDescriptor descriptor = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    descriptor.layout = layout;
    descriptor.entryCount = count;
    descriptor.entries = entries;
    WGPUBindGroup result = wgpuDeviceCreateBindGroup(G.device, &descriptor);
    wgpuBindGroupLayoutRelease(layout);
    CHECK(result, "bind group");
    return result;
}

/* ---- memory sampler ---- */
static atomic_bool sampler_running;
static _Atomic uint64_t sampled_peak;
static uint64_t footprint(void)
{
#if defined(__APPLE__)
    task_vm_info_data_t info;
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_VM_INFO, (task_info_t)&info, &count) != KERN_SUCCESS) return 0;
    return info.phys_footprint;
#else
    FILE *f = fopen("/proc/self/status", "r");
    char line[256];
    uint64_t kib = 0;
    while (f && fgets(line, sizeof(line), f))
        if (!strncmp(line, "RssAnon:", 8)) kib = strtoull(line + 8, NULL, 10);
    if (f) fclose(f);
    return kib * 1024;
#endif
}
static uint64_t kernel_peak(void)
{
#if defined(__APPLE__)
    task_vm_info_data_t info;
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_VM_INFO, (task_info_t)&info, &count) != KERN_SUCCESS) return 0;
    return info.ledger_phys_footprint_peak;
#else
    return 0;
#endif
}
static void sample(void)
{
    uint64_t value = footprint(), seen = atomic_load(&sampled_peak);
    while (value > seen && !atomic_compare_exchange_weak(&sampled_peak, &seen, value)) {}
}
static void *sampler(void *unused)
{
    (void)unused;
    const struct timespec period = {0, 20000000};
    while (atomic_load(&sampler_running)) {
        sample();
        nanosleep(&period, NULL);
    }
    return NULL;
}

/* ---- tracked state, uploads and the shared readback ---- */
static void track_buffer(uint64_t size, WGPUBufferUsage usage)
{
    tracked *t = &G.t[G.count++];
    t->size = size;
    t->buffer_usage = usage;
    t->units = (uint32_t)((size + PAGE - 1) / PAGE);
    t->base_buffer = make_buffer(size, usage);
}
static void track_texture(uint32_t width, uint32_t height, WGPUTextureUsage usage, bool host_marked)
{
    tracked *t = &G.t[G.count++];
    t->texture = true;
    t->host_marked = host_marked;
    t->width = width;
    t->height = height;
    t->size = (uint64_t)width * height * 4;
    t->tiles_x = (width + TILE - 1) / TILE;
    t->units = t->tiles_x * ((height + TILE - 1) / TILE);
    t->texture_usage = usage;
    t->base_texture = make_texture(width, height, usage);
}
static void finish_tracking(void)
{
    uint32_t first = 0;
    for (size_t i = 0; i < G.count; ++i) {
        G.t[i].first = first;
        first += G.t[i].units;
    }
    G.mark_words = first + 1;
}
/* SplitMix64 words through a 16 MiB host chunk; mask clears bit 31 when set. */
static void upload_words(WGPUBuffer buffer, uint64_t bytes, uint64_t seed, uint32_t mask)
{
    uint32_t *chunk = malloc(CHUNK);
    CHECK(chunk, "upload chunk");
    for (uint64_t offset = 0; offset < bytes; offset += CHUNK) {
        const uint64_t n = bytes - offset < CHUNK ? bytes - offset : CHUNK;
        for (uint64_t w = 0; w < n / 4; ++w) chunk[w] = (uint32_t)splitmix64(&seed) & mask;
        wgpuQueueWriteBuffer(G.queue, buffer, offset, chunk, (size_t)n);
    }
    work_done();
    free(chunk);
}
static WGPUBuffer resource_buffer(size_t i, bool slot) { return slot ? G.t[i].slot_buffer : G.t[i].base_buffer; }
static WGPUTexture resource_texture(size_t i, bool slot) { return slot ? G.t[i].slot_texture : G.t[i].base_texture; }

/* Identical in every arm: chunked copy to one map-read buffer, then SHA-256 of
 * buffers in order and each texture's tight rows. */
static void hash_state(bool slot, char hex[65])
{
    G.readback_phase = true;
    sha256_ctx c;
    sha256_init(&c);
    for (size_t i = 0; i < G.count; ++i) {
        const tracked *t = &G.t[i];
        if (!t->texture) {
            for (uint64_t offset = 0; offset < t->size; offset += CHUNK) {
                const uint64_t n = t->size - offset < CHUNK ? t->size - offset : CHUNK;
                WGPUCommandEncoder e = encoder();
                wgpuCommandEncoderCopyBufferToBuffer(e, resource_buffer(i, slot), offset, G.readback, 0, n);
                submit(e);
                sha256_update(&c, map_read(G.readback, n), (size_t)n);
                wgpuBufferUnmap(G.readback);
            }
            continue;
        }
        const uint32_t pitch = (t->width * 4 + 255) / 256 * 256, rows = CHUNK / pitch;
        for (uint32_t y = 0; y < t->height; y += rows) {
            const uint32_t h = t->height - y < rows ? t->height - y : rows;
            WGPUCommandEncoder e = encoder();
            WGPUTexelCopyTextureInfo from = {.texture = resource_texture(i, slot), .origin = {0, y, 0},
                .aspect = WGPUTextureAspect_All};
            WGPUTexelCopyBufferInfo to = {.layout = {.bytesPerRow = pitch, .rowsPerImage = h}, .buffer = G.readback};
            WGPUExtent3D extent = {t->width, h, 1};
            wgpuCommandEncoderCopyTextureToBuffer(e, &from, &to, &extent);
            submit(e);
            const uint8_t *bytes = map_read(G.readback, (uint64_t)pitch * h);
            for (uint32_t r = 0; r < h; ++r) sha256_update(&c, bytes + (size_t)r * pitch, (size_t)t->width * 4);
            wgpuBufferUnmap(G.readback);
        }
    }
    uint8_t digest[32];
    sha256_final(&c, digest);
    sha256_hex(digest, hex);
    G.readback_phase = false;
}

/* ---- slot preparation: D3 whole copies, D5 Doe's marked-run restore ---- */
static void copy_run(WGPUCommandEncoder e, size_t i, uint32_t first, uint32_t end)
{
    const tracked *t = &G.t[i];
    if (!t->texture) {
        const uint64_t offset = (uint64_t)first * PAGE, limit = (uint64_t)end * PAGE;
        const uint64_t size = (limit < t->size ? limit : t->size) - offset;
        wgpuCommandEncoderCopyBufferToBuffer(e, t->base_buffer, offset, t->slot_buffer, offset, size);
        G.copy_bytes += size;
    } else {
        const uint32_t row = first / t->tiles_x, column = first % t->tiles_x;
        const uint32_t x = column * TILE, y = row * TILE;
        uint32_t right = (column + (end - first)) * TILE, bottom = y + TILE;
        if (right > t->width) right = t->width;
        if (bottom > t->height) bottom = t->height;
        WGPUTexelCopyTextureInfo from = {.texture = t->base_texture, .origin = {x, y, 0}, .aspect = WGPUTextureAspect_All};
        WGPUTexelCopyTextureInfo to = from;
        to.texture = t->slot_texture;
        WGPUExtent3D extent = {right - x, bottom - y, 1};
        wgpuCommandEncoderCopyTextureToTexture(e, &from, &to, &extent);
        G.copy_bytes += (uint64_t)extent.width * extent.height * 4;
    }
    ++G.copy_commands;
}
static void copy_whole(WGPUCommandEncoder e)
{
    for (size_t i = 0; i < G.count; ++i) {
        const tracked *t = &G.t[i];
        if (!t->texture) {
            wgpuCommandEncoderCopyBufferToBuffer(e, t->base_buffer, 0, t->slot_buffer, 0, t->size);
        } else {
            WGPUTexelCopyTextureInfo from = {.texture = t->base_texture, .aspect = WGPUTextureAspect_All};
            WGPUTexelCopyTextureInfo to = {.texture = t->slot_texture, .aspect = WGPUTextureAspect_All};
            WGPUExtent3D extent = {t->width, t->height, 1};
            wgpuCommandEncoderCopyTextureToTexture(e, &from, &to, &extent);
        }
        G.copy_bytes += t->size;
        ++G.copy_commands;
    }
}
static void copy_marked(WGPUCommandEncoder e)
{
    for (size_t i = 0; i < G.count; ++i) {
        const tracked *t = &G.t[i];
        const uint32_t row = t->texture ? t->tiles_x : t->units;
        for (uint32_t start = 0; start < t->units; start += row) {
            uint32_t unit = start;
            while (unit < start + row) {
                if (G.filled && !G.last_marks[t->first + unit]) { ++unit; continue; }
                uint32_t end = unit + 1;
                while (end < start + row && (!G.filled || G.last_marks[t->first + end])) ++end;
                copy_run(e, i, unit, end);
                unit = end;
            }
        }
    }
    G.filled = true;
    wgpuCommandEncoderClearBuffer(e, G.marks, 0, (uint64_t)G.mark_words * 4);
}
/* After the staging map completed: GPU marks plus the host-marked attachments. */
static void take_marks(void)
{
    const uint32_t *words = map_read(G.marks_staging, (uint64_t)G.mark_words * 4);
    for (uint32_t u = 0; u + 1 < G.mark_words; ++u) G.last_marks[u] = words[u] != 0;
    wgpuBufferUnmap(G.marks_staging);
    for (size_t i = 0; i < G.count; ++i)
        if (G.t[i].host_marked) memset(G.last_marks + G.t[i].first, 1, G.t[i].units);
}
static void create_slots(void)
{
    for (size_t i = 0; i < G.count; ++i) {
        tracked *t = &G.t[i];
        if (t->texture) t->slot_texture = make_texture(t->width, t->height, t->texture_usage);
        else t->slot_buffer = make_buffer(t->size, t->buffer_usage);
    }
}
/* D5: the mark buffer Doe allocates (one word per unit plus the sink), a
 * map-read staging copy and the per-slot words Doe puts in immediates. */
static void create_marks(size_t shader_slots)
{
    G.marks = make_buffer((uint64_t)G.mark_words * 4, WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc |
        WGPUBufferUsage_CopyDst);
    G.marks_staging = make_buffer((uint64_t)G.mark_words * 4, WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst);
    uint32_t words[16] = {0};
    for (size_t k = 0; k < shader_slots; ++k) {
        words[2 * k] = G.t[k].first;
        words[2 * k + 1] = G.t[k].texture ? G.t[k].tiles_x : 0;
    }
    const uint64_t bytes = (shader_slots * 2 + 3) / 4 * 16;
    G.mark_bases = make_buffer(bytes, WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst);
    wgpuQueueWriteBuffer(G.queue, G.mark_bases, 0, words, (size_t)bytes);
    G.last_marks = calloc(G.mark_words, 1);
    CHECK(G.last_marks, "marks");
}

/* ---- workloads: inputs, bind groups and one branch's commands ---- */
typedef struct workload {
    const char *name;
    WGPUComputePipeline compute;
    WGPURenderPipeline render;
    WGPUBuffer r[4], params, frame;
    WGPUBindGroup groups[STEPS], render_group;
    WGPUTextureView atlas_view, target_view;
} workload;

/* The W WGSL with this divisor's sizes; at d = 1 it is the ledger's text. */
static char *sized(const char *source)
{
    char r[16], wsplit[16];
    snprintf(r, sizeof(r), "%uu", R_WORDS);
    snprintf(wsplit, sizeof(wsplit), "%uu", W_WORDS);
    char *out = malloc(strlen(source) * 2 + 64), *o = out;
    CHECK(out, "source");
    for (const char *p = source; *p;) {
        const char *word = !strncmp(p, "12582912u", 9) ? r : !strncmp(p, "8388608u", 8) ? wsplit : NULL;
        if (word) {
            o += sprintf(o, "%s", word);
            p += !strncmp(p, "12582912u", 9) ? 9 : 8;
        } else *o++ = *p++;
    }
    *o = 0;
    return out;
}

static void w_inputs(workload *w)
{
    for (uint32_t k = 0; k < 4; ++k) {
        w->r[k] = make_buffer((uint64_t)R_WORDS * 4, WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst);
        upload_words(w->r[k], (uint64_t)R_WORDS * 4, BASE_SEED + 1 + k, 0xffffffffu);
    }
    const WGPUBufferUsage usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst;
    for (uint32_t k = 0; k < 2; ++k) {
        track_buffer((uint64_t)W_WORDS * 4, usage);
        upload_words(G.t[k].base_buffer, (uint64_t)W_WORDS * 4, BASE_SEED + 5 + k, 0xffffffffu);
    }
    w->params = make_buffer((uint64_t)STEPS * PARAM_STRIDE, WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst);
    finish_tracking();
}
static void w_bind(workload *w, bool slot)
{
    for (uint32_t t = 0; t < STEPS; ++t) {
        WGPUBindGroupEntry entries[9] = {
            {.binding = 0, .buffer = w->r[0], .size = WGPU_WHOLE_SIZE},
            {.binding = 1, .buffer = w->r[1], .size = WGPU_WHOLE_SIZE},
            {.binding = 2, .buffer = w->r[2], .size = WGPU_WHOLE_SIZE},
            {.binding = 3, .buffer = w->r[3], .size = WGPU_WHOLE_SIZE},
            {.binding = 4, .buffer = resource_buffer(0, slot), .size = WGPU_WHOLE_SIZE},
            {.binding = 5, .buffer = resource_buffer(1, slot), .size = WGPU_WHOLE_SIZE},
            {.binding = 6, .buffer = w->params, .offset = (uint64_t)t * PARAM_STRIDE, .size = 16 + 128 * 4} /* sizeof(Params) */,
            {.binding = 7, .buffer = G.marks, .size = WGPU_WHOLE_SIZE},
            {.binding = 8, .buffer = G.mark_bases, .size = WGPU_WHOLE_SIZE}};
        w->groups[t] = bind_group(wgpuComputePipelineGetBindGroupLayout(w->compute, 0), entries,
            G.arm == ARM_D5 ? 9 : 7);
    }
}
static void w_branch_inputs(workload *w, uint32_t b)
{
    static uint32_t words[STEPS][PARAM_STRIDE / 4];
    memset(words, 0, sizeof(words));
    for (uint32_t t = 0; t < STEPS; ++t) {
        words[t][0] = t;
        words[t][1] = (uint32_t)branch_seed(b);
        step_pages(b, t, &words[t][4]);
    }
    wgpuQueueWriteBuffer(G.queue, w->params, 0, words, sizeof(words));
}
static void w_record(workload *w, WGPUCommandEncoder e)
{
    for (uint32_t t = 0; t < STEPS; ++t) {
        WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(e, NULL);
        wgpuComputePassEncoderSetPipeline(pass, w->compute);
        wgpuComputePassEncoderSetBindGroup(pass, 0, w->groups[t], 0, NULL);
        wgpuComputePassEncoderDispatchWorkgroups(pass, SELECTED * 64, 1, 1);
        wgpuComputePassEncoderEnd(pass);
        wgpuComputePassEncoderRelease(pass);
    }
}

static void scratch_inputs(workload *w)
{
    const WGPUBufferUsage usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst;
    for (size_t i = 0; i < SCRATCH_BUFFERS; ++i) {
        track_buffer(scratch_bytes(i), usage);
        /* s4 feeds the never-taken guard: no word is 0xffffffff. */
        upload_words(G.t[i].base_buffer, scratch_bytes(i), BASE_SEED + 100 + i, i == 4 ? 0x7fffffffu : 0xffffffffu);
    }
    track_texture(ATLAS_W, ATLAS_H, WGPUTextureUsage_StorageBinding | WGPUTextureUsage_CopySrc |
        WGPUTextureUsage_CopyDst, false);
    track_texture(VIEW_W, VIEW_H, WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc |
        WGPUTextureUsage_CopyDst, true);
    w->frame = make_buffer(256, WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst);
    finish_tracking();
}
static void scratch_bind(workload *w, bool slot)
{
    w->atlas_view = wgpuTextureCreateView(resource_texture(SCRATCH_BUFFERS, slot), NULL);
    w->target_view = wgpuTextureCreateView(resource_texture(SCRATCH_BUFFERS + 1, slot), NULL);
    CHECK(w->atlas_view && w->target_view, "views");
    WGPUBindGroupEntry entries[10];
    for (uint32_t i = 0; i < SCRATCH_BUFFERS; ++i)
        entries[i] = (WGPUBindGroupEntry){.binding = i, .buffer = resource_buffer(i, slot), .size = WGPU_WHOLE_SIZE};
    entries[6] = (WGPUBindGroupEntry){.binding = 6, .textureView = w->atlas_view};
    entries[7] = (WGPUBindGroupEntry){.binding = 7, .buffer = w->frame, .size = 16};
    entries[8] = (WGPUBindGroupEntry){.binding = 8, .buffer = G.marks, .size = WGPU_WHOLE_SIZE};
    entries[9] = (WGPUBindGroupEntry){.binding = 9, .buffer = G.mark_bases, .size = WGPU_WHOLE_SIZE};
    w->groups[0] = bind_group(wgpuComputePipelineGetBindGroupLayout(w->compute, 0), entries, G.arm == ARM_D5 ? 10 : 8);
    WGPUBindGroupEntry frame = {.binding = 0, .buffer = w->frame, .size = 16};
    w->render_group = bind_group(wgpuRenderPipelineGetBindGroupLayout(w->render, 0), &frame, 1);
}
static void scratch_branch_inputs(workload *w, uint32_t b)
{
    const uint32_t words[4] = {b * 2654435761u + 1u, (b * 97u) % (ATLAS_W - 64), (b * 61u) % (ATLAS_H - 64), 0};
    wgpuQueueWriteBuffer(G.queue, w->frame, 0, words, sizeof(words));
}
static void scratch_record(workload *w, WGPUCommandEncoder e)
{
    WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(e, NULL);
    wgpuComputePassEncoderSetPipeline(pass, w->compute);
    wgpuComputePassEncoderSetBindGroup(pass, 0, w->groups[0], 0, NULL);
    wgpuComputePassEncoderDispatchWorkgroups(pass, 400, 1, 1);
    wgpuComputePassEncoderEnd(pass);
    wgpuComputePassEncoderRelease(pass);
    WGPURenderPassColorAttachment color = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    color.view = w->target_view;
    color.loadOp = WGPULoadOp_Clear;
    color.storeOp = WGPUStoreOp_Store;
    WGPURenderPassDescriptor descriptor = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
    descriptor.colorAttachmentCount = 1;
    descriptor.colorAttachments = &color;
    WGPURenderPassEncoder render = wgpuCommandEncoderBeginRenderPass(e, &descriptor);
    wgpuRenderPassEncoderSetPipeline(render, w->render);
    wgpuRenderPassEncoderSetBindGroup(render, 0, w->render_group, 0, NULL);
    wgpuRenderPassEncoderDraw(render, 3, 1, 0, 0);
    wgpuRenderPassEncoderEnd(render);
    wgpuRenderPassEncoderRelease(render);
}

/* ---- device ---- */
static void json_view(const char *key, WGPUStringView v, const char *tail)
{
    printf("\"%s\":\"", key);
    for (size_t i = 0; v.data && i < v.length; ++i) {
        const char ch = v.data[i];
        if (ch == '"' || ch == '\\') putchar('\\');
        if ((unsigned char)ch >= 0x20) putchar(ch);
    }
    printf("\"%s", tail);
}
static void open_device(void)
{
    const WGPUInstanceFeatureName timed = WGPUInstanceFeatureName_TimedWaitAny;
    WGPUInstanceDescriptor instance = WGPU_INSTANCE_DESCRIPTOR_INIT;
    instance.requiredFeatureCount = 1;
    instance.requiredFeatures = &timed;
    G.instance = wgpuCreateInstance(&instance);
    CHECK(G.instance, "instance");
    const char *adapter_class = getenv("SNAPSHOT_BRANCH_ADAPTER");
    const bool physical = adapter_class && !strcmp(adapter_class, "physical");
    WGPURequestAdapterOptions options = WGPU_REQUEST_ADAPTER_OPTIONS_INIT;
    options.backendType = WGPUBackendType_Vulkan;
    options.forceFallbackAdapter = physical ? WGPU_FALSE : WGPU_TRUE;
    int unused = 1;
    WGPURequestAdapterCallbackInfo adapter = {.mode = WGPUCallbackMode_WaitAnyOnly, .callback = on_adapter};
    wait_future(wgpuInstanceRequestAdapter(G.instance, &options, adapter), &unused);
    CHECK(G.adapter, "no adapter");
    WGPUAdapterInfo info = WGPU_ADAPTER_INFO_INIT;
    CHECK(wgpuAdapterGetInfo(G.adapter, &info) == WGPUStatus_Success, "adapter info");
    CHECK(info.backendType == WGPUBackendType_Vulkan, "adapter backend %u is not Vulkan", (unsigned)info.backendType);
    CHECK(physical || info.adapterType == WGPUAdapterType_CPU, "adapter type %u is not CPU", (unsigned)info.adapterType);
    printf("\"adapter\":{\"backend\":\"vulkan\",\"type\":%u,", (unsigned)info.adapterType);
    json_view("vendor", info.vendor, ",");
    json_view("architecture", info.architecture, ",");
    json_view("device", info.device, ",");
    json_view("description", info.description, "},");
    wgpuAdapterInfoFreeMembers(info);
    WGPUDeviceDescriptor descriptor = WGPU_DEVICE_DESCRIPTOR_INIT;
    descriptor.uncapturedErrorCallbackInfo = (WGPUUncapturedErrorCallbackInfo){.callback = on_error};
    WGPURequestDeviceCallbackInfo device = {.mode = WGPUCallbackMode_WaitAnyOnly, .callback = on_device};
    wait_future(wgpuAdapterRequestDevice(G.adapter, &descriptor, device), &unused);
    CHECK(G.device, "no device");
    G.queue = wgpuDeviceGetQueue(G.device);
    G.waits = 0;
}

static void print_u64s(const char *key, const uint64_t *values, uint32_t n, const char *tail)
{
    printf("\"%s\":[", key);
    for (uint32_t i = 0; i < n; ++i) printf("%llu%s", (unsigned long long)values[i], i + 1 < n ? "," : "");
    printf("]%s", tail);
}

int main(int argc, char **argv)
{
    CHECK(argc >= 3, "usage: %s <D3|D5|E> <w|scratch> [branches] [size divisor]", argv[0]);
    const char *arm = argv[1], *name = argv[2];
    const uint32_t branches = argc > 3 ? (uint32_t)strtoul(argv[3], NULL, 10) : 32;
    divisor = argc > 4 ? (uint32_t)strtoul(argv[4], NULL, 10) : 1;
    CHECK(branches >= 1 && branches <= MAX_BRANCHES, "branches");
    CHECK(divisor == 1 || divisor == 2 || divisor == 4 || divisor == MAX_DIVISOR, "size divisor must be 1, 2, 4 or 8");
    PAGES = MAX_PAGES / divisor;
    SELECTED = PAGES / 8;
    W_WORDS = PAGES / 2 * PAGE_WORDS;
    R_WORDS = W_WORDS / 2 * 3;
    G.arm = !strcmp(arm, "D3") ? ARM_D3 : !strcmp(arm, "D5") ? ARM_D5 : !strcmp(arm, "E") ? ARM_E : (arm_kind)-1;
    CHECK(G.arm <= ARM_E, "arm must be D3, D5 or E");
    const bool is_w = !strcmp(name, "w");
    CHECK(is_w || !strcmp(name, "scratch"), "workload must be w or scratch");
#if !defined(SNAPSHOT_BRANCH_DOE)
    CHECK(G.arm != ARM_E, "arm E needs the Doe build (-DSNAPSHOT_BRANCH_DOE)");
#endif
    setvbuf(stdout, NULL, _IOFBF, 1 << 20);
    printf("{\"executor\":\"snapshot-branch-native\",\"provider\":\"%s\",\"arm\":\"%s\",\"workload\":\"%s\","
        "\"branches\":%u,\"size_divisor\":%u,\"readback_in_clock\":false,", PROVIDER, arm, name, branches, divisor);
    open_device();
#if defined(SNAPSHOT_BRANCH_DOE)
    if (G.arm == ARM_E) CHECK(doeNativeSnapshotEnableWriteMarks(G.device) == 1, "write marks");
#endif
    workload w = {.name = name};
    const bool marked = G.arm == ARM_D5;
    if (is_w) {
        char *source = sized(marked ? w_marked_source : w_source);
        w.compute = compute_pipeline(source);
        free(source);
    }
    else {
        w.compute = compute_pipeline(marked ? scratch_marked_source : scratch_source);
        w.render = view_pipeline();
    }
    work_done();
    const uint64_t baseline = footprint();
    atomic_store(&sampled_peak, baseline);
    atomic_store(&sampler_running, true);
    pthread_t thread;
    CHECK(pthread_create(&thread, NULL, sampler, NULL) == 0, "sampler");

    G.logical_bytes = 0;
    G.readback = make_buffer(CHUNK, WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst);
    if (is_w) w_inputs(&w); else scratch_inputs(&w);
    char base_before[65], base_after[65], outputs[MAX_BRANCHES][65];
    hash_state(false, base_before);
    G.submits = G.waits = 0;

    uint64_t branch_ns[MAX_BRANCHES], prepare_bytes[MAX_BRANCHES], prepare_copies[MAX_BRANCHES];
    const uint64_t setup_start = now_ns();
#if defined(SNAPSHOT_BRANCH_DOE)
    void *snapshot = NULL;
    if (G.arm == ARM_E) {
        WGPUBuffer buffers[MAX_TRACKED];
        WGPUTexture textures[MAX_TRACKED];
        size_t nb = 0, nt = 0;
        for (size_t i = 0; i < G.count; ++i) {
            if (G.t[i].texture) textures[nt++] = G.t[i].base_texture;
            else buffers[nb++] = G.t[i].base_buffer;
        }
        DoeNativeSnapshotDesc desc = {.bufferCount = nb, .buffers = buffers, .textureCount = nt, .textures = textures};
        snapshot = doeNativeSnapshotCreate(G.device, &desc);
        CHECK(snapshot, "snapshot create");
        for (size_t i = 0; i < G.count; ++i) G.logical_bytes += G.t[i].size; /* the slot */
        G.logical_bytes += (uint64_t)G.mark_words * 4;
    }
#endif
    if (G.arm != ARM_E) create_slots();
    if (G.arm == ARM_D5) create_marks(is_w ? 2 : SCRATCH_BUFFERS + 1);
    if (is_w) w_bind(&w, G.arm != ARM_E); else scratch_bind(&w, G.arm != ARM_E);
    const uint64_t setup_create_ns = now_ns() - setup_start;

    for (uint32_t b = 0; b < branches; ++b) {
        const uint64_t bytes_before = G.copy_bytes, copies_before = G.copy_commands;
        const uint64_t start = now_ns();
        if (is_w) w_branch_inputs(&w, b); else scratch_branch_inputs(&w, b);
#if defined(SNAPSHOT_BRANCH_DOE)
        if (G.arm == ARM_E) CHECK(doeNativeSnapshotBranchBegin(snapshot) == 1, "branch begin");
#endif
        WGPUCommandEncoder e = encoder();
        if (G.arm == ARM_D3) copy_whole(e);
        if (G.arm == ARM_D5) copy_marked(e);
        if (is_w) w_record(&w, e); else scratch_record(&w, e);
        if (G.arm == ARM_D5)
            wgpuCommandEncoderCopyBufferToBuffer(e, G.marks, 0, G.marks_staging, 0, (uint64_t)G.mark_words * 4);
        submit(e);
        if (G.arm == ARM_D5) take_marks(); /* the staging map waits for the branch */
        else work_done();
        uint64_t elapsed = now_ns() - start;
        hash_state(G.arm != ARM_E, outputs[b]);
#if defined(SNAPSHOT_BRANCH_DOE)
        if (G.arm == ARM_E) {
            const uint64_t end_start = now_ns();
            CHECK(doeNativeSnapshotBranchEnd(snapshot) == 1, "branch end");
            elapsed += now_ns() - end_start;
            DoeNativeSnapshotLedger ledger;
            CHECK(doeNativeSnapshotGetLedger(snapshot, &ledger) == 1, "ledger");
            G.copy_bytes += ledger.lastPrepareBytes;
            G.copy_commands += ledger.lastPrepareCopies;
        }
#endif
        branch_ns[b] = elapsed;
        prepare_bytes[b] = G.copy_bytes - bytes_before;
        prepare_copies[b] = G.copy_commands - copies_before;
        sample();
    }
    uint64_t internal_submits = 0;
#if defined(SNAPSHOT_BRANCH_DOE)
    if (G.arm == ARM_E) {
        DoeNativeSnapshotLedger ledger;
        CHECK(doeNativeSnapshotGetLedger(snapshot, &ledger) == 1, "ledger");
        internal_submits = ledger.prepareSubmits;
        CHECK(ledger.totalPrepareBytes == G.copy_bytes, "ledger bytes");
    }
#endif
    const uint64_t submits = G.submits + internal_submits, waits = G.waits;
    hash_state(false, base_after);
    sample();
    atomic_store(&sampler_running, false);
    pthread_join(thread, NULL);
    const uint64_t peak = atomic_load(&sampled_peak), peak_kernel = kernel_peak();

    uint64_t total = setup_create_ns;
    for (uint32_t b = 0; b < branches; ++b) total += branch_ns[b];
    printf("\"timing_ns\":{\"setup_create\":%llu,\"setup\":%llu,\"total\":%llu,", (unsigned long long)setup_create_ns,
        (unsigned long long)(setup_create_ns + branch_ns[0]), (unsigned long long)total);
    print_u64s("branch", branch_ns, branches, "},");
    print_u64s("prepare_bytes", prepare_bytes, branches, ",");
    print_u64s("prepare_copies", prepare_copies, branches, ",");
    printf("\"copy_bytes\":%llu,\"copy_commands\":%llu,\"submits\":%llu,\"waits\":%llu,\"internal_submits\":%llu,"
        "\"readback_submits\":%llu,\"readback_waits\":%llu,\"logical_bytes\":%llu,",
        (unsigned long long)G.copy_bytes, (unsigned long long)G.copy_commands, (unsigned long long)submits,
        (unsigned long long)waits, (unsigned long long)internal_submits, (unsigned long long)G.readback_submits,
        (unsigned long long)G.readback_waits, (unsigned long long)G.logical_bytes);
    printf("\"memory\":{\"source\":\"%s\",\"baseline\":%llu,\"peak_sampled\":%llu,\"peak_kernel\":%llu,"
        "\"incremental\":%llu},",
#if defined(__APPLE__)
        "phys_footprint",
#else
        "RssAnon",
#endif
        (unsigned long long)baseline, (unsigned long long)peak, (unsigned long long)peak_kernel,
        (unsigned long long)((peak_kernel > peak ? peak_kernel : peak) - baseline));
    printf("\"base_before\":\"%s\",\"base_after\":\"%s\",\"outputs\":[", base_before, base_after);
    for (uint32_t b = 0; b < branches; ++b) printf("\"%s\"%s", outputs[b], b + 1 < branches ? "," : "");
    printf("],\"device_errors\":%u}\n", G.errors);
    fflush(stdout);
#if defined(SNAPSHOT_BRANCH_DOE)
    if (snapshot) doeNativeSnapshotRelease(snapshot);
#endif
    return G.errors ? 1 : 0;
}
