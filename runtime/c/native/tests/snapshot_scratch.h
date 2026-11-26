/* SPDX-License-Identifier: MIT
 * Workload (b) of snapshot_ledger.c: large read_write scratch that each frame
 * declares writable but barely writes, a storage atlas with a small update and
 * a view target redrawn every frame. Included once by snapshot_ledger.c. */
#ifndef DOE_NATIVE_TEST_SNAPSHOT_SCRATCH_H
#define DOE_NATIVE_TEST_SNAPSHOT_SCRATCH_H
#include "snapshot_fixture.h"

enum { SCRATCH_BUFFERS = 6, ATLAS_W = 1280, ATLAS_H = 1024, VIEW_W = 956, VIEW_H = 655 };
static const uint64_t scratch_mib[SCRATCH_BUFFERS] = {48, 48, 32, 16, 16, 4};

/* Bump-allocated prefixes: 40 KiB of s0, 24 KiB of s1, 100 KiB of s2 and
 * 40 KiB of s3 at 1 MiB; s4 and s5 are bound writable and never written
 * (s5's store is behind a test that never holds). The atlas gets a 64x64
 * update at a frame-dependent position. */
static const char scratch_source[] =
    "@group(0) @binding(0) var<storage, read_write> s0: array<u32>;\n"
    "@group(0) @binding(1) var<storage, read_write> s1: array<u32>;\n"
    "@group(0) @binding(2) var<storage, read_write> s2: array<u32>;\n"
    "@group(0) @binding(3) var<storage, read_write> s3: array<u32>;\n"
    "@group(0) @binding(4) var<storage, read_write> s4: array<u32>;\n"
    "@group(0) @binding(5) var<storage, read_write> s5: array<u32>;\n"
    "@group(0) @binding(6) var atlas: texture_storage_2d<rgba8unorm, write>;\n"
    "@group(0) @binding(7) var<uniform> frame: vec4<u32>;\n"
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

static const char view_source[] =
    "@group(0) @binding(0) var<uniform> frame: vec4<u32>;\n"
    "@vertex fn vs(@builtin(vertex_index) i: u32) -> @builtin(position) vec4<f32> {\n"
    "  return vec4<f32>(f32(i & 1u) * 4.0 - 1.0, f32(i >> 1u) * 4.0 - 1.0, 0.0, 1.0);\n"
    "}\n"
    "@fragment fn fs(@builtin(position) p: vec4<f32>) -> @location(0) vec4<f32> {\n"
    "  return vec4<f32>(p.x / 956.0, p.y / 655.0, f32(frame.x & 255u) / 255.0, 1.0);\n"
    "}\n";

static WGPUTexture scratch_texture(state *s, uint32_t width, uint32_t height, WGPUTextureUsage usage)
{
    WGPUTextureDescriptor descriptor = WGPU_TEXTURE_DESCRIPTOR_INIT;
    descriptor.format = WGPUTextureFormat_RGBA8Unorm;
    descriptor.usage = usage | WGPUTextureUsage_CopySrc | WGPUTextureUsage_CopyDst;
    descriptor.size = (WGPUExtent3D){width, height, 1};
    WGPUTexture result = wgpuDeviceCreateTexture(s->device, &descriptor);
    assert(result);
    return result;
}

static WGPURenderPipeline view_pipeline(state *s)
{
    WGPUShaderModule module = snapshot_module(s, view_source);
    WGPUColorTargetState target = WGPU_COLOR_TARGET_STATE_INIT;
    target.format = WGPUTextureFormat_RGBA8Unorm;
    WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
    fragment.module = module;
    fragment.entryPoint = (WGPUStringView){"fs", WGPU_STRLEN};
    fragment.targetCount = 1;
    fragment.targets = &target;
    WGPURenderPipelineDescriptor descriptor = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    descriptor.vertex.module = module;
    descriptor.vertex.entryPoint = (WGPUStringView){"vs", WGPU_STRLEN};
    descriptor.fragment = &fragment;
    WGPURenderPipeline result = wgpuDeviceCreateRenderPipeline(s->device, &descriptor);
    wgpuShaderModuleRelease(module);
    assert(result);
    return result;
}

/* Base bytes of every tracked resource, then per-branch diff and coverage. */
typedef struct scratch_state {
    WGPUBuffer buffers[SCRATCH_BUFFERS];
    WGPUTexture atlas, view;
    uint8_t *base[SCRATCH_BUFFERS + 2];
} scratch_state;

static void scratch_observe(state *s, scratch_state *x, uint8_t **out)
{
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device, NULL);
    readback r[SCRATCH_BUFFERS + 2];
    for (size_t i = 0; i < SCRATCH_BUFFERS; ++i)
        r[i] = readback_buffer(s, encoder, x->buffers[i], scratch_mib[i] << 20);
    r[SCRATCH_BUFFERS] = readback_texture(s, encoder, x->atlas, ATLAS_W, ATLAS_H);
    r[SCRATCH_BUFFERS + 1] = readback_texture(s, encoder, x->view, VIEW_W, VIEW_H);
    submit(s, encoder);
    work_done(s);
    for (size_t i = 0; i < SCRATCH_BUFFERS + 2; ++i) out[i] = readback_take(s, &r[i]);
    unpitch(out[SCRATCH_BUFFERS], ATLAS_W, ATLAS_H);
    unpitch(out[SCRATCH_BUFFERS + 1], VIEW_W, VIEW_H);
}

static size_t scratch_cover(void *snapshot, const scratch_state *x, uint8_t *const *now, size_t *changed)
{
    static uint8_t diff[48 * 16];
    size_t marked = 0;
    *changed = 0;
    for (size_t i = 0; i < SCRATCH_BUFFERS + 2; ++i) {
        const bool texture = i >= SCRATCH_BUFFERS;
        const uint32_t width = !texture ? 0 : i == SCRATCH_BUFFERS ? ATLAS_W : VIEW_W;
        const uint32_t height = !texture ? 0 : i == SCRATCH_BUFFERS ? ATLAS_H : VIEW_H;
        const uint64_t size = texture ? (uint64_t)width * height * 4 : scratch_mib[i] << 20;
        const size_t units = texture ? (size_t)((width + TILE - 1) / TILE) * ((height + TILE - 1) / TILE) :
            (size_t)(size / PAGE);
        *changed += changed_units(x->base[i], now[i], size, width, height, diff, sizeof(diff));
        marked += require_covered(snapshot, i, diff, units, texture ? "scratch texture" : "scratch buffer");
    }
    return marked;
}

static void workload_scratch(state *s, uint32_t branches)
{
    scratch_state x = {0};
    uint64_t declared = 0;
    for (size_t i = 0; i < SCRATCH_BUFFERS; ++i) {
        const uint64_t size = scratch_mib[i] << 20;
        x.buffers[i] = buffer(s, size, WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst);
        x.base[i] = malloc(size);
        assert(x.base[i]);
        splitmix_words((uint32_t *)x.base[i], size / 4, BASE_SEED + 100 + i);
        /* s4 is the never-taken guard's input: no word is 0xffffffff. */
        if (i == 4) for (uint64_t w = 0; w < size / 4; ++w) ((uint32_t *)x.base[i])[w] &= 0x7fffffffu;
        wgpuQueueWriteBuffer(s->queue, x.buffers[i], 0, x.base[i], size);
        declared += size;
    }
    x.atlas = scratch_texture(s, ATLAS_W, ATLAS_H, WGPUTextureUsage_StorageBinding);
    x.view = scratch_texture(s, VIEW_W, VIEW_H, WGPUTextureUsage_RenderAttachment);
    declared += (uint64_t)ATLAS_W * ATLAS_H * 4 + (uint64_t)VIEW_W * VIEW_H * 4;
    WGPUBuffer frame = buffer(s, 256 * 64, WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst);
    WGPUComputePipeline compute_pipeline = compute(s, scratch_source);
    WGPURenderPipeline render_pipeline = view_pipeline(s);
    WGPUTextureView atlas_view = wgpuTextureCreateView(x.atlas, NULL), view_view = wgpuTextureCreateView(x.view, NULL);
    work_done(s);
    /* The textures' base contents are their zero initialization, read back. */
    uint8_t *initial[SCRATCH_BUFFERS + 2];
    scratch_observe(s, &x, initial);
    x.base[SCRATCH_BUFFERS] = initial[SCRATCH_BUFFERS];
    x.base[SCRATCH_BUFFERS + 1] = initial[SCRATCH_BUFFERS + 1];
    for (size_t i = 0; i < SCRATCH_BUFFERS; ++i) {
        assert(!memcmp(initial[i], x.base[i], scratch_mib[i] << 20));
        free(initial[i]);
    }
    WGPUBuffer tracked_buffers[SCRATCH_BUFFERS];
    for (size_t i = 0; i < SCRATCH_BUFFERS; ++i) tracked_buffers[i] = x.buffers[i];
    WGPUTexture tracked_textures[2] = {x.atlas, x.view};
    void *snapshot = doeNativeSnapshotCreate(s->device, &(DoeNativeSnapshotDesc){.bufferCount=SCRATCH_BUFFERS,
        .buffers=tracked_buffers,.textureCount=2,.textures=tracked_textures});
    assert(snapshot);
    DoeNativeSnapshotLedger ledger;
    assert(doeNativeSnapshotGetLedger(snapshot, &ledger) == 1 && ledger.declaredBytes == declared);
    printf("(b) large-scratch workload: D3 copies %.2f MiB per branch\n", mib(declared));
    printf("    branch  prepared MiB  copies  marked units  changed units  marked MiB\n");
    uint64_t total_prepared = 0, total_marked = 0;
    for (uint32_t b = 0; b < branches; ++b) {
        const uint32_t words[4] = {b * 2654435761u + 1u, (b * 97u) % (ATLAS_W - 64), (b * 61u) % (ATLAS_H - 64), 0};
        wgpuQueueWriteBuffer(s->queue, frame, (uint64_t)b * 256, words, sizeof(words));
        assert(doeNativeSnapshotBranchBegin(snapshot) == 1);
        WGPUBindGroupEntry entries[8];
        for (uint32_t i = 0; i < SCRATCH_BUFFERS; ++i)
            entries[i] = (WGPUBindGroupEntry){.binding=i,.buffer=x.buffers[i],.size=WGPU_WHOLE_SIZE};
        entries[6] = (WGPUBindGroupEntry){.binding=6,.textureView=atlas_view};
        entries[7] = (WGPUBindGroupEntry){.binding=7,.buffer=frame,.offset=(uint64_t)b * 256,.size=16};
        WGPUBindGroup compute_group = group(s, compute_pipeline, entries, 8);
        WGPUBindGroupLayout layout = wgpuRenderPipelineGetBindGroupLayout(render_pipeline, 0);
        WGPUBindGroupEntry frame_entry = {.binding=0,.buffer=frame,.offset=(uint64_t)b * 256,.size=16};
        WGPUBindGroup render_group = wgpuDeviceCreateBindGroup(s->device,
            &(WGPUBindGroupDescriptor){.layout=layout,.entryCount=1,.entries=&frame_entry});
        wgpuBindGroupLayoutRelease(layout);
        assert(render_group);
        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device, NULL);
        WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, NULL);
        wgpuComputePassEncoderSetPipeline(pass, compute_pipeline);
        wgpuComputePassEncoderSetBindGroup(pass, 0, compute_group, 0, NULL);
        wgpuComputePassEncoderDispatchWorkgroups(pass, 400, 1, 1);
        wgpuComputePassEncoderEnd(pass);
        wgpuComputePassEncoderRelease(pass);
        WGPURenderPassColorAttachment color = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
        color.view = view_view;
        color.loadOp = WGPULoadOp_Clear;
        color.storeOp = WGPUStoreOp_Store;
        WGPURenderPassDescriptor descriptor = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
        descriptor.colorAttachmentCount = 1;
        descriptor.colorAttachments = &color;
        WGPURenderPassEncoder render = wgpuCommandEncoderBeginRenderPass(encoder, &descriptor);
        wgpuRenderPassEncoderSetPipeline(render, render_pipeline);
        wgpuRenderPassEncoderSetBindGroup(render, 0, render_group, 0, NULL);
        wgpuRenderPassEncoderDraw(render, 3, 1, 0, 0);
        wgpuRenderPassEncoderEnd(render);
        wgpuRenderPassEncoderRelease(render);
        submit(s, encoder);
        uint8_t *now[SCRATCH_BUFFERS + 2];
        scratch_observe(s, &x, now);
        assert(doeNativeSnapshotBranchEnd(snapshot) == 1);
        size_t changed;
        const size_t marked = scratch_cover(snapshot, &x, now, &changed);
        assert(doeNativeSnapshotGetLedger(snapshot, &ledger) == 1 && ledger.lastMarkedUnits == marked);
        printf("    %6u  %12.2f  %6llu  %12zu  %13zu  %10.2f\n", b, mib(ledger.lastPrepareBytes),
            (unsigned long long)ledger.lastPrepareCopies, marked, changed, mib(ledger.lastMarkedBytes));
        total_prepared += ledger.lastPrepareBytes;
        total_marked += ledger.lastMarkedBytes;
        for (size_t i = 0; i < SCRATCH_BUFFERS + 2; ++i) free(now[i]);
        wgpuBindGroupRelease(compute_group);
        wgpuBindGroupRelease(render_group);
    }
    uint8_t *after[SCRATCH_BUFFERS + 2];
    scratch_observe(s, &x, after);
    for (size_t i = 0; i < SCRATCH_BUFFERS + 2; ++i) {
        const uint64_t size = i < SCRATCH_BUFFERS ? scratch_mib[i] << 20 :
            i == SCRATCH_BUFFERS ? (uint64_t)ATLAS_W * ATLAS_H * 4 : (uint64_t)VIEW_W * VIEW_H * 4;
        if (memcmp(after[i], x.base[i], size)) {
            fprintf(stderr, "FAIL leak canary: scratch resource %zu base changed\n", i);
            abort();
        }
        free(after[i]);
        free(x.base[i]);
    }
    const uint64_t d3 = (uint64_t)branches * declared;
    printf("    %u branches: E prepared %.2f MiB (first slot fill %.2f MiB, then marked units only) vs D3 %.2f MiB;"
        " steady state %.2f MiB per branch = %.2f%% of D3, %.1fx fewer bytes\n",
        branches, mib(total_prepared), mib(declared), mib(d3), mib(total_marked) / branches,
        100.0 * (double)total_marked / (double)d3, (double)d3 / (double)total_marked);
    doeNativeSnapshotRelease(snapshot);
    wgpuTextureViewRelease(atlas_view);
    wgpuTextureViewRelease(view_view);
    wgpuComputePipelineRelease(compute_pipeline);
    wgpuRenderPipelineRelease(render_pipeline);
    wgpuBufferRelease(frame);
    for (size_t i = 0; i < SCRATCH_BUFFERS; ++i) wgpuBufferRelease(x.buffers[i]);
    wgpuTextureRelease(x.atlas);
    wgpuTextureRelease(x.view);
}
#endif
