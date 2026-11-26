/* SPDX-License-Identifier: MIT
 * Snapshot leak canary, one branch per write path: storage store, textureStore,
 * buffer copy, buffer-to-texture and texture-to-texture copy, clear and render
 * attachment. Each branch must (1) change its slot, (2) mark every unit the
 * CPU diff shows changed, (3) leave the base resources byte-identical, and
 * (4) hand the next branch a slot equal to the base: a missed mark survives
 * the restore and fails there. Negative controls prove the base check can
 * fail and that unmarked write paths are refused, not run. */
#include "snapshot_fixture.h"

enum { BUFFER_BYTES = 16 * PAGE, SIDE = 512, TEXTURE_BYTES = SIDE * SIDE * 4 };

typedef struct canary {
    state s;
    WGPUBuffer tracked, source;
    WGPUTexture image, source_image;
    WGPUTextureView image_view;
    uint8_t *base_buffer, *base_image;
    void *snapshot;
    WGPUComputePipeline store, texel;
    WGPURenderPipeline draw;
} canary;

/* Three disjoint bindings of the tracked buffer (pages 0-3, 8-11, 12-15):
 * a plain store, a vector component through a struct member and an atomic
 * write pages 3, 9 and 12, so each binding's offset word must be right. */
static const char store_source[] =
    "struct Cell { a: u32, b: vec4<u32> }\n"
    "@group(0) @binding(0) var<storage, read_write> data: array<u32>;\n"
    "@group(0) @binding(1) var<storage, read_write> cells: array<Cell>;\n"
    "@group(0) @binding(2) var<storage, read_write> counters: array<atomic<u32>>;\n"
    "@compute @workgroup_size(4) fn main(@builtin(local_invocation_index) i: u32) {\n"
    "  data[3u * 16384u + i * 7u] = 0xa5a5a5a5u + i;\n"
    "  cells[65536u / 32u + i].b.z = 0x5a5a0000u + i;\n"
    "  atomicAdd(&counters[i], 1u);\n"
    "}\n";

/* An override constant takes the specialization compile path: its pipeline
 * must still carry marks, or the branch submission is refused. */
static const char texel_source[] =
    "override column: u32 = 0u;\n"
    "@group(0) @binding(0) var image: texture_storage_2d<rgba8unorm, write>;\n"
    "@compute @workgroup_size(8, 8) fn main(@builtin(global_invocation_id) g: vec3<u32>) {\n"
    "  textureStore(image, vec2<u32>(column + g.x, 20u + g.y), vec4<f32>(1.0, 0.25, 0.5, 1.0));\n"
    "}\n";

static const char draw_source[] =
    "@vertex fn vs(@builtin(vertex_index) i: u32) -> @builtin(position) vec4<f32> {\n"
    "  let x = f32(i & 1u) * 4.0 - 1.0; let y = f32(i >> 1u) * 4.0 - 1.0;\n"
    "  return vec4<f32>(x, y, 0.0, 1.0);\n"
    "}\n"
    "@fragment fn fs() -> @location(0) vec4<f32> { return vec4<f32>(0.0, 1.0, 0.0, 1.0); }\n";

static WGPUComputePipeline compute(canary *c, const char *code, const WGPUConstantEntry *constant)
{
    WGPUShaderModule module = snapshot_module(&c->s, code);
    WGPUComputePipelineDescriptor descriptor = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
    descriptor.compute.module = module;
    descriptor.compute.constantCount = constant ? 1 : 0;
    descriptor.compute.constants = constant;
    WGPUComputePipeline result = wgpuDeviceCreateComputePipeline(c->s.device, &descriptor);
    wgpuShaderModuleRelease(module);
    assert(result);
    return result;
}

static WGPURenderPipeline render(canary *c)
{
    WGPUShaderModule module = snapshot_module(&c->s, draw_source);
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
    WGPURenderPipeline result = wgpuDeviceCreateRenderPipeline(c->s.device, &descriptor);
    wgpuShaderModuleRelease(module);
    assert(result);
    return result;
}

static WGPUTexture image(canary *c, WGPUTextureUsage usage)
{
    WGPUTextureDescriptor descriptor = WGPU_TEXTURE_DESCRIPTOR_INIT;
    descriptor.format = WGPUTextureFormat_RGBA8Unorm;
    descriptor.usage = usage;
    descriptor.size = (WGPUExtent3D){SIDE, SIDE, 1};
    WGPUTexture result = wgpuDeviceCreateTexture(c->s.device, &descriptor);
    assert(result);
    return result;
}

static void fill(uint8_t *bytes, size_t size, uint32_t seed)
{
    for (size_t i = 0; i < size; ++i) {
        seed = seed * 1664525u + 1013904223u;
        bytes[i] = (uint8_t)(seed >> 24);
    }
}

static void upload_image(canary *c, WGPUTexture texture, const uint8_t *bytes)
{
    WGPUTexelCopyTextureInfo destination = {.texture=texture};
    WGPUTexelCopyBufferLayout layout = {.bytesPerRow=SIDE * 4,.rowsPerImage=SIDE};
    WGPUExtent3D extent = {SIDE, SIDE, 1};
    wgpuQueueWriteTexture(c->s.queue, &destination, bytes, TEXTURE_BYTES, &layout, &extent);
}

/* Current bytes of the tracked buffer and image, as the submission sees them:
 * the slot inside a branch, the base outside. */
static void observe(canary *c, uint8_t **buffer_bytes, uint8_t **image_bytes)
{
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(c->s.device, NULL);
    readback b = readback_buffer(&c->s, encoder, c->tracked, BUFFER_BYTES);
    readback t = readback_texture(&c->s, encoder, c->image, SIDE, SIDE);
    submit(&c->s, encoder);
    work_done(&c->s);
    *buffer_bytes = readback_take(&c->s, &b);
    *image_bytes = readback_take(&c->s, &t);
    unpitch(*image_bytes, SIDE, SIDE);
}

static bool equals_base(canary *c, const uint8_t *buffer_bytes, const uint8_t *image_bytes)
{
    return !memcmp(buffer_bytes, c->base_buffer, BUFFER_BYTES) && !memcmp(image_bytes, c->base_image, TEXTURE_BYTES);
}

static WGPUBindGroup bind(canary *c, WGPUComputePipeline pipeline, const WGPUBindGroupEntry *entries, size_t count)
{
    return group(&c->s, pipeline, entries, count);
}

typedef enum path { STORE, TEXEL, COPY, BUFFER_TEXTURE, TEXTURE_TEXTURE, CLEAR, ATTACHMENT, PATHS } path;
static const char *const path_names[PATHS] = {"storage store and atomic", "textureStore", "buffer copy",
    "buffer-to-texture copy", "texture-to-texture copy", "buffer clear", "render attachment"};

static void record_path(canary *c, WGPUCommandEncoder encoder, path p)
{
    switch (p) {
    case STORE: {
        WGPUBindGroupEntry entries[3] = {{.binding=0,.buffer=c->tracked,.size=4 * PAGE},
            {.binding=1,.buffer=c->tracked,.offset=8 * PAGE,.size=4 * PAGE},
            {.binding=2,.buffer=c->tracked,.offset=12 * PAGE,.size=4 * PAGE}};
        WGPUBindGroup g = bind(c, c->store, entries, 3);
        WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, NULL);
        wgpuComputePassEncoderSetPipeline(pass, c->store);
        wgpuComputePassEncoderSetBindGroup(pass, 0, g, 0, NULL);
        wgpuComputePassEncoderDispatchWorkgroups(pass, 1, 1, 1);
        wgpuComputePassEncoderEnd(pass);
        wgpuComputePassEncoderRelease(pass);
        wgpuBindGroupRelease(g);
        break;
    }
    case TEXEL: {
        WGPUBindGroupEntry entry = {.binding=0,.textureView=c->image_view};
        WGPUBindGroup g = bind(c, c->texel, &entry, 1);
        WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, NULL);
        wgpuComputePassEncoderSetPipeline(pass, c->texel);
        wgpuComputePassEncoderSetBindGroup(pass, 0, g, 0, NULL);
        wgpuComputePassEncoderDispatchWorkgroups(pass, 1, 1, 1);
        wgpuComputePassEncoderEnd(pass);
        wgpuComputePassEncoderRelease(pass);
        wgpuBindGroupRelease(g);
        break;
    }
    case COPY:
        wgpuCommandEncoderCopyBufferToBuffer(encoder, c->source, 0, c->tracked, 5 * PAGE + 100 * 4, 200 * 4);
        break;
    case BUFFER_TEXTURE: {
        WGPUTexelCopyBufferInfo from = {.buffer=c->source,.layout={.bytesPerRow=256,.rowsPerImage=8}};
        WGPUTexelCopyTextureInfo to = {.texture=c->image,.origin={10, 400, 0}};
        WGPUExtent3D extent = {8, 8, 1};
        wgpuCommandEncoderCopyBufferToTexture(encoder, &from, &to, &extent);
        break;
    }
    case TEXTURE_TEXTURE: {
        WGPUTexelCopyTextureInfo from = {.texture=c->source_image};
        WGPUTexelCopyTextureInfo to = {.texture=c->image,.origin={400, 400, 0}};
        WGPUExtent3D extent = {16, 16, 1};
        wgpuCommandEncoderCopyTextureToTexture(encoder, &from, &to, &extent);
        break;
    }
    case CLEAR:
        wgpuCommandEncoderClearBuffer(encoder, c->tracked, 7 * PAGE + 256, 512);
        break;
    case ATTACHMENT: {
        WGPURenderPassColorAttachment color = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
        color.view = c->image_view;
        color.loadOp = WGPULoadOp_Load;
        color.storeOp = WGPUStoreOp_Store;
        WGPURenderPassDescriptor descriptor = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
        descriptor.colorAttachmentCount = 1;
        descriptor.colorAttachments = &color;
        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &descriptor);
        wgpuRenderPassEncoderSetPipeline(pass, c->draw);
        wgpuRenderPassEncoderSetViewport(pass, 0, 0, 64, 64, 0, 1);
        wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);
        break;
    }
    case PATHS: abort();
    }
}

static void run_path(canary *c, path p)
{
    assert(doeNativeSnapshotBranchBegin(c->snapshot) == 1);
    DoeNativeSnapshotLedger ledger;
    assert(doeNativeSnapshotGetLedger(c->snapshot, &ledger) == 1);
    const uint64_t prepared = ledger.lastPrepareBytes;
    uint8_t *start_buffer, *start_image;
    observe(c, &start_buffer, &start_image);
    /* (4) of the previous branch: the restored slot starts equal to the base. */
    if (!equals_base(c, start_buffer, start_image)) {
        fprintf(stderr, "FAIL leak canary: a write before \"%s\" survived the restore\n", path_names[p]);
        abort();
    }
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(c->s.device, NULL);
    record_path(c, encoder, p);
    submit(&c->s, encoder);
    uint8_t *after_buffer, *after_image;
    observe(c, &after_buffer, &after_image);
    assert(doeNativeSnapshotBranchEnd(c->snapshot) == 1);
    uint8_t buffer_changed[BUFFER_BYTES / PAGE], image_changed[(SIDE / TILE) * (SIDE / TILE)];
    size_t changed = changed_units(c->base_buffer, after_buffer, BUFFER_BYTES, 0, 0, buffer_changed, sizeof(buffer_changed)) +
        changed_units(c->base_image, after_image, TEXTURE_BYTES, SIDE, SIDE, image_changed, sizeof(image_changed));
    assert(changed != 0); /* (1) the path wrote its slot. */
    size_t marked = require_covered(c->snapshot, 0, buffer_changed, sizeof(buffer_changed), path_names[p]) +
        require_covered(c->snapshot, 1, image_changed, sizeof(image_changed), path_names[p]); /* (2) */
    uint8_t *base_buffer, *base_image;
    observe(c, &base_buffer, &base_image);
    if (!equals_base(c, base_buffer, base_image)) { /* (3) */
        fprintf(stderr, "FAIL leak canary: %s reached the base\n", path_names[p]);
        abort();
    }
    assert(doeNativeSnapshotGetLedger(c->snapshot, &ledger) == 1);
    printf("canary %-26s changed %zu units, marked %zu (%.2f MiB), base intact; slot prepared with %.2f MiB\n",
        path_names[p], changed, marked, mib(ledger.lastMarkedBytes), mib(prepared));
    free(start_buffer); free(start_image); free(after_buffer); free(after_image); free(base_buffer); free(base_image);
}

static void pop_validation(canary *c, const char *what)
{
    unsigned before = c->s.validation_errors;
    WGPUPopErrorScopeCallbackInfo callback = {.mode=WGPUCallbackMode_AllowSpontaneous,
        .callback=validation_reply,.userdata1=&c->s};
    assert(wgpuDevicePopErrorScope(c->s.device, callback).id);
    if (c->s.validation_errors != before + 1) {
        fprintf(stderr, "FAIL negative control: %s was not refused\n", what);
        abort();
    }
}

static void negative_controls(canary *c)
{
    /* An unmarked write path inside a branch is refused, never run. */
    assert(doeNativeSnapshotBranchBegin(c->snapshot) == 1);
    const uint32_t word = 7;
    wgpuDevicePushErrorScope(c->s.device, WGPUErrorFilter_Validation);
    wgpuQueueWriteBuffer(c->s.queue, c->tracked, 0, &word, sizeof(word));
    pop_validation(c, "queue write to a tracked buffer during a branch");
    work_done(&c->s);
    assert(doeNativeSnapshotBranchEnd(c->snapshot) == 1);
    /* Write marks refuse what they cannot represent. */
    wgpuDevicePushErrorScope(c->s.device, WGPUErrorFilter_Validation);
    WGPUShaderModule module = snapshot_module(&c->s,
        "requires immediate_address_space; struct P { v: u32 } var<immediate> p: P;\n"
        "@group(0) @binding(0) var<storage, read_write> d: array<u32>;\n"
        "@compute @workgroup_size(1) fn main() { d[0] = p.v; }\n");
    WGPUComputePipelineDescriptor descriptor = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
    descriptor.compute.module = module;
    WGPUComputePipeline refused = wgpuDeviceCreateComputePipeline(c->s.device, &descriptor);
    wgpuShaderModuleRelease(module);
    if (refused) wgpuComputePipelineRelease(refused);
    pop_validation(c, "var<immediate> under write marks");
    /* The base check itself can fail: a deliberate base write is caught. */
    uint8_t *base_buffer, *base_image;
    wgpuQueueWriteBuffer(c->s.queue, c->tracked, 2 * PAGE, &word, sizeof(word));
    observe(c, &base_buffer, &base_image);
    assert(!equals_base(c, base_buffer, base_image));
    free(base_buffer);
    free(base_image);
    puts("negative controls: queue write in a branch refused, var<immediate> refused, deliberate base write detected");
}

int main(void)
{
    canary c = {0};
    snapshot_device(&c.s);
    c.tracked = buffer(&c.s, BUFFER_BYTES, WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst);
    c.source = buffer(&c.s, BUFFER_BYTES, WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst);
    c.image = image(&c, WGPUTextureUsage_StorageBinding | WGPUTextureUsage_RenderAttachment |
        WGPUTextureUsage_CopySrc | WGPUTextureUsage_CopyDst);
    c.source_image = image(&c, WGPUTextureUsage_CopySrc | WGPUTextureUsage_CopyDst);
    c.image_view = wgpuTextureCreateView(c.image, NULL);
    c.base_buffer = malloc(BUFFER_BYTES);
    c.base_image = malloc(TEXTURE_BYTES);
    uint8_t *scratch = malloc(TEXTURE_BYTES);
    assert(c.image_view && c.base_buffer && c.base_image && scratch);
    fill(c.base_buffer, BUFFER_BYTES, 1);
    fill(c.base_image, TEXTURE_BYTES, 2);
    fill(scratch, TEXTURE_BYTES, 3);
    wgpuQueueWriteBuffer(c.s.queue, c.tracked, 0, c.base_buffer, BUFFER_BYTES);
    wgpuQueueWriteBuffer(c.s.queue, c.source, 0, scratch, BUFFER_BYTES);
    upload_image(&c, c.image, c.base_image);
    upload_image(&c, c.source_image, scratch);
    free(scratch);
    c.store = compute(&c, store_source, NULL);
    const WGPUConstantEntry column = {.key={"column", WGPU_STRLEN},.value=300.0};
    c.texel = compute(&c, texel_source, &column);
    c.draw = render(&c);
    work_done(&c.s);
    DoeNativeSnapshotDesc desc = {.bufferCount=1,.buffers=&c.tracked,.textureCount=1,.textures=&c.image};
    c.snapshot = doeNativeSnapshotCreate(c.s.device, &desc);
    assert(c.snapshot);
    for (path p = STORE; p < PATHS; ++p) run_path(&c, p);
    /* The last path's writes are restored too. */
    assert(doeNativeSnapshotBranchBegin(c.snapshot) == 1);
    uint8_t *buffer_bytes, *image_bytes;
    observe(&c, &buffer_bytes, &image_bytes);
    if (!equals_base(&c, buffer_bytes, image_bytes)) {
        fputs("FAIL leak canary: the render attachment survived the restore\n", stderr);
        abort();
    }
    free(buffer_bytes);
    free(image_bytes);
    assert(doeNativeSnapshotBranchEnd(c.snapshot) == 1);
    negative_controls(&c);
    doeNativeSnapshotRelease(c.snapshot);
    wgpuComputePipelineRelease(c.store);
    wgpuComputePipelineRelease(c.texel);
    wgpuRenderPipelineRelease(c.draw);
    wgpuTextureViewRelease(c.image_view);
    wgpuTextureRelease(c.image);
    wgpuTextureRelease(c.source_image);
    wgpuBufferRelease(c.tracked);
    wgpuBufferRelease(c.source);
    free(c.base_buffer);
    free(c.base_image);
    printf("PASS snapshot leak canary: %u write paths marked with zero misses, base intact, every restore exact %s\n",
        (unsigned)PATHS, adapter_class_scope());
    consumer_finish(&c.s);
    return 0;
}
