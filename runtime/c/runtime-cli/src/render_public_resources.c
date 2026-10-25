/* SPDX-License-Identifier: MIT
 * Retained full/render/wgpu_render_assets.zig and wgpu_render_resources.zig. */
#include "render_public.h"
#include <stdlib.h>

#define VERTEX_HANDLE UINT64_C(0x8C9F250000000000)
#define UNIFORM_HANDLE UINT64_C(0x8C9F260000000000)
#define SAMPLED_HANDLE UINT64_C(0x8C9F280000000000)

const char *runtime_public_render_shader(void)
{
    return "@group(0) @binding(0) var<uniform> color: vec3f;\n"
        "@group(0) @binding(1) var sampled_tex: texture_2d<f32>;\n"
        "@group(0) @binding(2) var sampled_sampler: sampler;\n"
        "struct VsOut { @builtin(position) pos: vec4f, @location(0) uv: vec2f, };\n"
        "@vertex fn vs_main(@location(0) pos: vec4f) -> VsOut {\n"
        "var out: VsOut; out.pos = pos; out.uv = vec2f(pos.x * 0.5 + 0.5, (0.5 - pos.y * 0.5)); return out; }\n"
        "@fragment fn fs_main(in: VsOut) -> @location(0) vec4f {\n"
        "let sampled = textureSample(sampled_tex, sampled_sampler, in.uv);\n"
        "return vec4f(sampled.rgb + color * (1.0 / 5000.0), sampled.a); }\n";
}

const char *doe_runtime_public_render_source(const doe_runtime *runtime)
{
    return runtime->public_render && runtime->public_render->shader_compiled ? runtime_public_render_shader() : NULL;
}

void runtime_release_public_render(doe_runtime *runtime)
{
    public_render_state *state = runtime->public_render;
    if (!state) return;
    for (size_t i = 0; i < sizeof(state->pipelines) / sizeof(*state->pipelines); i++)
        if (state->pipelines[i]) wgpuRenderPipelineRelease(state->pipelines[i]);
    if (state->group) wgpuBindGroupRelease(state->group);
    if (state->layout) wgpuBindGroupLayoutRelease(state->layout);
    if (state->sampled_view) wgpuTextureViewRelease(state->sampled_view);
    if (state->sampler) wgpuSamplerRelease(state->sampler);
    if (state->vertex) wgpuBufferRelease(state->vertex);
    if (state->depth) wgpuTextureRelease(state->depth);
    free(state);
    runtime->public_render = NULL;
}

static bool prepare_vertex(doe_runtime *runtime, public_render_state *state, doe_trace_execution *execution)
{
    const float vertices[] = {0, .5f, 0, 1, -.5f, -.5f, 0, 1, .5f, -.5f, 0, 1};
    buffer_entry *old = runtime_find_buffer(runtime, VERTEX_HANDLE);
    bool initialize = !old || old->size < sizeof(vertices) || !(old->usage & WGPUBufferUsage_Vertex);
    buffer_entry *entry = runtime_ensure_buffer_usage(runtime, VERTEX_HANDLE, sizeof(vertices),
        WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst, execution);
    if (!entry) return false;
    if (initialize) wgpuQueueWriteBuffer(runtime->queue, entry->buffer, 0, vertices, sizeof(vertices));
    if (state->vertex != entry->buffer) {
        wgpuBufferAddRef(entry->buffer);
        if (state->vertex) wgpuBufferRelease(state->vertex);
        state->vertex = entry->buffer;
    }
    return true;
}

public_render_state *runtime_public_render_state(doe_runtime *runtime, doe_trace_execution *execution)
{
    if (!runtime->public_render) runtime->public_render = calloc(1, sizeof(*runtime->public_render));
    public_render_state *state = runtime->public_render;
    if (!state) { runtime_fail(execution, "out_of_memory", "public renderer allocation failed"); return NULL; }
    if (!prepare_vertex(runtime, state, execution)) return NULL;
    if (state->group) return state;
    buffer_entry *uniform = runtime_ensure_buffer_usage(runtime, UNIFORM_HANDLE, PUBLIC_RENDER_UNIFORM_BYTES,
        WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst, execution);
    if (!uniform) return NULL;
    /* New WebGPU buffers are zero-initialized; the source repeats color=(0,0,0)
     * at each 256-byte slot. Existing caller data at this reserved handle stays. */
    WGPUBuffer uniform_buffer = uniform->buffer;
    doe_texture_desc sampled_desc = {.handle = SAMPLED_HANDLE, .width = 2, .height = 2, .depth = 1,
        .format = DOE_FORMAT_RGBA8_UNORM, .usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst};
    texture_entry *previous = runtime_find_texture(runtime, SAMPLED_HANDLE);
    bool initialize = !previous || previous->declared.width != 2 || previous->declared.height != 2 ||
        previous->declared.format != DOE_FORMAT_RGBA8_UNORM || !(previous->usage & WGPUTextureUsage_TextureBinding);
    WGPUTexture sampled = runtime_ensure_texture(runtime, &sampled_desc, execution);
    if (!sampled) return NULL;
    if (initialize) {
        const uint8_t pixels[] = {255,0,0,255, 0,255,0,255, 0,0,255,255, 255,255,0,255};
        WGPUTexelCopyTextureInfo copy = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
        copy.texture = sampled;
        WGPUTexelCopyBufferLayout layout = {.offset = 0, .bytesPerRow = 8, .rowsPerImage = 2};
        WGPUExtent3D extent = {2, 2, 1};
        wgpuQueueWriteTexture(runtime->queue, &copy, pixels, sizeof(pixels), &layout, &extent);
    }
    if (!state->sampled_view) state->sampled_view = wgpuTextureCreateView(sampled, NULL);
    if (!state->sampler) state->sampler = wgpuDeviceCreateSampler(runtime->device, NULL);
    WGPUBindGroupLayoutEntry entries[3] = {WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT,
        WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT, WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT};
    for (uint32_t i = 0; i < 3; i++) {
        entries[i].binding = i;
        entries[i].visibility = WGPUShaderStage_Fragment;
    }
    entries[0].buffer.type = WGPUBufferBindingType_Uniform;
    entries[0].buffer.hasDynamicOffset = true;
    entries[0].buffer.minBindingSize = PUBLIC_RENDER_BINDING_BYTES;
    entries[1].texture.sampleType = WGPUTextureSampleType_Float;
    entries[1].texture.viewDimension = WGPUTextureViewDimension_2D;
    entries[2].sampler.type = WGPUSamplerBindingType_Filtering;
    WGPUBindGroupLayoutDescriptor layout = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
    layout.entryCount = 3;
    layout.entries = entries;
    if (!state->layout) state->layout = wgpuDeviceCreateBindGroupLayout(runtime->device, &layout);
    WGPUBindGroupEntry bindings[3] = {WGPU_BIND_GROUP_ENTRY_INIT, WGPU_BIND_GROUP_ENTRY_INIT, WGPU_BIND_GROUP_ENTRY_INIT};
    for (uint32_t i = 0; i < 3; i++) bindings[i].binding = i;
    bindings[0].buffer = uniform_buffer;
    bindings[0].size = PUBLIC_RENDER_BINDING_BYTES;
    bindings[1].textureView = state->sampled_view;
    bindings[2].sampler = state->sampler;
    WGPUBindGroupDescriptor group = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    group.layout = state->layout;
    group.entryCount = 3;
    group.entries = bindings;
    if (state->layout && state->sampled_view && state->sampler) state->group = wgpuDeviceCreateBindGroup(runtime->device, &group);
    if (!state->group) {
        runtime_fail(execution, "render_public_bindings_failed", "public renderer bindings were not created");
        return NULL;
    }
    return state;
}

WGPURenderPipeline runtime_public_render_pipeline(doe_runtime *runtime, doe_texture_format format,
    doe_trace_execution *execution)
{
    if ((unsigned)format > DOE_FORMAT_RGBA32_FLOAT) {
        runtime_fail(execution, "render_public_format", "unsupported public render format");
        return NULL;
    }
    public_render_state *state = runtime_public_render_state(runtime, execution);
    if (!state) return NULL;
    if (state->pipelines[format]) return state->pipelines[format];
    WGPUShaderSourceWGSL source = WGPU_SHADER_SOURCE_WGSL_INIT;
    source.code = (WGPUStringView){runtime_public_render_shader(), WGPU_STRLEN};
    WGPUShaderModuleDescriptor module = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;
    module.nextInChain = &source.chain;
    WGPUShaderModule shader = wgpuDeviceCreateShaderModule(runtime->device, &module);
    state->shader_compiled = true;
    WGPUPipelineLayoutDescriptor layout_descriptor = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
    layout_descriptor.bindGroupLayoutCount = 1;
    layout_descriptor.bindGroupLayouts = &state->layout;
    WGPUPipelineLayout layout = shader ? wgpuDeviceCreatePipelineLayout(runtime->device, &layout_descriptor) : NULL;
    WGPURenderPipeline pipeline = NULL;
    if (shader && layout) {
        WGPUVertexAttribute attribute = WGPU_VERTEX_ATTRIBUTE_INIT;
        attribute.format = WGPUVertexFormat_Float32x4;
        WGPUVertexBufferLayout vertices = WGPU_VERTEX_BUFFER_LAYOUT_INIT;
        vertices.arrayStride = 4 * sizeof(float);
        vertices.stepMode = WGPUVertexStepMode_Vertex;
        vertices.attributeCount = 1;
        vertices.attributes = &attribute;
        WGPUColorTargetState color = WGPU_COLOR_TARGET_STATE_INIT;
        color.format = runtime_webgpu_format(format);
        WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
        fragment.module = shader;
        fragment.entryPoint = (WGPUStringView){"fs_main", WGPU_STRLEN};
        fragment.targetCount = 1;
        fragment.targets = &color;
        WGPUDepthStencilState depth = WGPU_DEPTH_STENCIL_STATE_INIT;
        depth.format = WGPUTextureFormat_Depth24PlusStencil8;
        depth.depthWriteEnabled = WGPUOptionalBool_False;
        depth.depthCompare = WGPUCompareFunction_Always;
        depth.stencilFront.compare = depth.stencilBack.compare = WGPUCompareFunction_Always;
        depth.stencilReadMask = depth.stencilWriteMask = 0xff;
        WGPURenderPipelineDescriptor descriptor = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
        descriptor.layout = layout;
        descriptor.vertex.module = shader;
        descriptor.vertex.entryPoint = (WGPUStringView){"vs_main", WGPU_STRLEN};
        descriptor.vertex.bufferCount = 1;
        descriptor.vertex.buffers = &vertices;
        descriptor.fragment = &fragment;
        descriptor.depthStencil = &depth;
        pipeline = runtime_async_render_pipeline(runtime, shader, &descriptor, execution);
    } else runtime_fail(execution, "render_public_pipeline_failed", "public renderer shader or layout was not created");
    if (layout) wgpuPipelineLayoutRelease(layout);
    if (shader) wgpuShaderModuleRelease(shader);
    state->pipelines[format] = pipeline;
    return pipeline;
}
