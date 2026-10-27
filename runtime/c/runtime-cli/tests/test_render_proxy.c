/* SPDX-License-Identifier: MIT
 * Actual proxy consumer with a fake WebGPU call ledger; no device is opened. */
#include "runtime_internal.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned sources, pipelines, direct_draws, indexed_draws, indirect_draws, submits;
static double clear_alpha;
static char selected[2][96];
static texture_entry target;
static unsigned token;
#define HANDLE(type) ((type)(void *)&token)

uint64_t doe_runtime_monotonic_ns(void) { return 1; }
bool runtime_fail(doe_trace_execution *result, const char *code, const char *format, ...)
{
    (void)code; (void)format; result->ok = false; return false;
}
void runtime_push_scopes(doe_runtime *runtime) { (void)runtime; }
bool runtime_pop_scopes(doe_runtime *runtime, doe_trace_execution *result)
{
    (void)runtime; (void)result; return true;
}
bool runtime_timestamp_begin(doe_runtime *runtime, runtime_timestamp *stamp, doe_trace_execution *result)
{
    (void)runtime; (void)stamp; (void)result; return true;
}
void runtime_timestamp_resolve(const runtime_timestamp *stamp, WGPUCommandEncoder encoder)
{
    (void)stamp; (void)encoder;
}
bool runtime_timestamp_finish(doe_runtime *runtime, runtime_timestamp *stamp, bool ok, doe_trace_execution *result)
{
    (void)runtime; (void)stamp; (void)result; return ok;
}
bool runtime_submit(doe_runtime *runtime, WGPUCommandBuffer commands, doe_trace_execution *result)
{
    (void)runtime; (void)result; assert(commands); submits++; return true;
}
texture_entry *runtime_find_texture(doe_runtime *runtime, uint64_t handle)
{
    (void)runtime; assert(handle == 700); return &target;
}
WGPUTextureFormat runtime_webgpu_format(doe_texture_format format)
{
    assert(format == DOE_FORMAT_RGBA8_UNORM); return WGPUTextureFormat_RGBA8Unorm;
}
void runtime_release_public_render(doe_runtime *runtime) { (void)runtime; }
bool runtime_execute_public_render(doe_runtime *runtime, const doe_command *command, doe_trace_execution *result)
{
    (void)runtime; (void)command; (void)result; abort();
}
char *runtime_resolve_kernel(const doe_runtime *runtime, const char *name, char **source, size_t *size)
{
    (void)runtime;
    char path[256];
    int length = snprintf(path, sizeof(path), "../../../bench/kernels/%s", name);
    assert(length > 0 && (size_t)length < sizeof(path));
    FILE *file = fopen(path, "rb"); assert(file);
    assert(!fseek(file, 0, SEEK_END));
    long count = ftell(file); assert(count > 0);
    rewind(file);
    *size = (size_t)count; *source = malloc(*size + 1); assert(*source);
    assert(fread(*source, 1, *size, file) == *size && !fclose(file));
    (*source)[*size] = 0;
    char *owned = malloc(strlen(path) + 1); assert(owned);
    strcpy(owned, path); return owned;
}
bool runtime_record_kernel_source(doe_runtime *runtime, const char *module, const char *path,
    const char *entry, const char *source, size_t size, size_t *index)
{
    (void)runtime; (void)entry;
    assert(sources < 2 && source && size && strstr(path, module));
    assert(strlen(module) < sizeof(selected[0]));
    strcpy(selected[sources], module); *index = sources++; return true;
}
WGPUInstance wgpuCreateInstance(const WGPUInstanceDescriptor *descriptor)
{
    (void)descriptor; abort(); /* A missing fake must never open a device. */
}
WGPUShaderModule wgpuDeviceCreateShaderModule(WGPUDevice device, const WGPUShaderModuleDescriptor *descriptor)
{
    (void)device;
    const WGPUShaderSourceWGSL *source = (const WGPUShaderSourceWGSL *)descriptor->nextInChain;
    assert(source && source->code.data && source->code.length); return HANDLE(WGPUShaderModule);
}
WGPURenderPipeline wgpuDeviceCreateRenderPipeline(WGPUDevice device, const WGPURenderPipelineDescriptor *descriptor)
{
    (void)device;
    assert(descriptor->vertex.module && descriptor->fragment && descriptor->fragment->module);
    return HANDLE(WGPURenderPipeline);
}
WGPUTextureView wgpuTextureCreateView(WGPUTexture texture, const WGPUTextureViewDescriptor *descriptor)
{
    (void)descriptor; assert(texture == target.texture); return HANDLE(WGPUTextureView);
}
WGPUCommandEncoder wgpuDeviceCreateCommandEncoder(WGPUDevice device, const WGPUCommandEncoderDescriptor *descriptor)
{
    (void)device; (void)descriptor; return HANDLE(WGPUCommandEncoder);
}
WGPURenderPassEncoder wgpuCommandEncoderBeginRenderPass(WGPUCommandEncoder encoder, const WGPURenderPassDescriptor *descriptor)
{
    assert(encoder && descriptor->colorAttachmentCount == 1);
    assert(descriptor->colorAttachments[0].loadOp == WGPULoadOp_Clear);
    clear_alpha = descriptor->colorAttachments[0].clearValue.a;
    return HANDLE(WGPURenderPassEncoder);
}
WGPUCommandBuffer wgpuCommandEncoderFinish(WGPUCommandEncoder encoder, const WGPUCommandBufferDescriptor *descriptor)
{
    (void)descriptor; assert(encoder); return HANDLE(WGPUCommandBuffer);
}
void wgpuRenderPassEncoderSetViewport(WGPURenderPassEncoder pass, float x, float y, float width, float height,
    float minimum, float maximum)
{
    assert(pass && x == 0 && y == 0 && width == 8 && height == 8 && minimum == 0 && maximum == 1);
}
void wgpuRenderPassEncoderSetScissorRect(WGPURenderPassEncoder pass, uint32_t x, uint32_t y, uint32_t width, uint32_t height)
{
    assert(pass && x == 0 && y == 0 && width == 8 && height == 8);
}
void wgpuRenderPassEncoderSetPipeline(WGPURenderPassEncoder pass, WGPURenderPipeline pipeline)
{
    assert(pass && pipeline); pipelines++;
}
void wgpuRenderPassEncoderDraw(WGPURenderPassEncoder pass, uint32_t vertices, uint32_t instances,
    uint32_t first_vertex, uint32_t first_instance)
{
    assert(pass && vertices == 3 && instances == 1 && first_vertex == 2 && first_instance == 0); direct_draws++;
}
void wgpuRenderPassEncoderDrawIndexed(WGPURenderPassEncoder pass, uint32_t indices, uint32_t instances,
    uint32_t first_index, int32_t base_vertex, uint32_t first_instance)
{
    assert(pass && indices == 3 && instances == 1 && first_index == 0 && base_vertex == -1 && first_instance == 0);
    indexed_draws++;
}
void wgpuRenderPassEncoderDrawIndirect(WGPURenderPassEncoder pass, WGPUBuffer buffer, uint64_t offset)
{
    assert(pass && buffer && offset == (uint64_t)indirect_draws * 16); indirect_draws++;
}
void wgpuRenderPassEncoderDrawIndexedIndirect(WGPURenderPassEncoder pass, WGPUBuffer buffer, uint64_t offset)
{
    assert(pass && buffer && offset == (uint64_t)indirect_draws * 20); indirect_draws++;
}
WGPUBuffer wgpuDeviceCreateBuffer(WGPUDevice device, const WGPUBufferDescriptor *descriptor)
{
    (void)device; assert(descriptor->size); return HANDLE(WGPUBuffer);
}
void wgpuQueueWriteBuffer(WGPUQueue queue, WGPUBuffer buffer, uint64_t offset, const void *data, size_t size)
{
    (void)queue; assert(buffer && offset == 0 && data && size);
}
void wgpuRenderPassEncoderSetIndexBuffer(WGPURenderPassEncoder pass, WGPUBuffer buffer,
    WGPUIndexFormat format, uint64_t offset, uint64_t size)
{
    assert(pass && buffer && format == WGPUIndexFormat_Uint32 && offset == 0 && size == 12);
}
void wgpuRenderPassEncoderEnd(WGPURenderPassEncoder pass) { assert(pass); }
void wgpuRenderPassEncoderRelease(WGPURenderPassEncoder pass) { assert(pass); }
void wgpuCommandEncoderRelease(WGPUCommandEncoder encoder) { assert(encoder); }
void wgpuTextureViewRelease(WGPUTextureView view) { assert(view); }
void wgpuShaderModuleRelease(WGPUShaderModule module) { assert(module); }
void wgpuRenderPipelineRelease(WGPURenderPipeline pipeline) { assert(pipeline); }
void wgpuBufferRelease(WGPUBuffer buffer) { assert(buffer); }
void wgpuBufferDestroy(WGPUBuffer buffer) { assert(buffer); }

static void exercise(const char *backend, bool redundant, doe_command_kind kind, bool indexed)
{
    sources = pipelines = direct_draws = indexed_draws = indirect_draws = submits = 0;
    doe_runtime runtime = {0}; runtime.info.backend = backend;
    target.texture = HANDLE(WGPUTexture);
    uint32_t indices[] = {0, 1, 2};
    doe_command command = {.kind = kind};
    command.as.render = (doe_render_draw){.draw_count=3,.vertex_count=3,.instance_count=1,.first_vertex=2,
        .target_handle=700,.target_width=8,.target_height=8,.target_format=DOE_FORMAT_RGBA8_UNORM,
        .viewport_max_depth=1,.redundant_pipeline=redundant,.indexed=indexed,.index_uint32=true,
        .indices=indices,.index_data_count=3,.index_count=3,.base_vertex=-1};
    doe_trace_execution result = {.ok=true};
    assert(runtime_execute_render(&runtime, &command, &result));
    bool metal = !strcmp(backend, "metal");
    assert(sources == 2 && submits == 1 && result.dispatch_count == 3);
    assert(!strcmp(selected[0], metal ? "draw_call_metal_proxy_vertex.wgsl" : "draw_call_proxy_vertex.wgsl"));
    assert(!strcmp(selected[1], metal ? "draw_call_metal_proxy_fragment.wgsl" : "draw_call_proxy_fragment.wgsl"));
    assert(clear_alpha == (metal ? 0 : 1));
    assert(pipelines == (metal && redundant ? 4u : 1u));
    bool indirect = kind == DOE_COMMAND_DRAW_INDIRECT || kind == DOE_COMMAND_DRAW_INDEXED_INDIRECT;
    assert(direct_draws == (!indexed && !indirect ? 3u : 0u));
    assert(indexed_draws == (indexed && !indirect ? 3u : 0u));
    assert(indirect_draws == (indirect ? 3u : 0u));
    runtime_release_render(&runtime);
}

int main(void)
{
    exercise("metal", false, DOE_COMMAND_RENDER_DRAW, false);
    exercise("metal", true, DOE_COMMAND_RENDER_DRAW, false);
    exercise("vulkan", true, DOE_COMMAND_RENDER_DRAW, false);
    exercise("d3d12", true, DOE_COMMAND_RENDER_DRAW, false);
    exercise("metal", true, DOE_COMMAND_RENDER_DRAW, true);
    exercise("metal", true, DOE_COMMAND_DRAW_INDIRECT, false);
    exercise("metal", true, DOE_COMMAND_DRAW_INDEXED_INDIRECT, true);
    puts("native proxy: actual adapter source/clear/rebind selection; indexed/indirect requests preserved (fake ABI; no GPU)");
    return 0;
}
