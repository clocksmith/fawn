/* SPDX-License-Identifier: MIT
 * A depth/stencil attachment the pass only reads is sampled in that pass
 * (WebGPU read-only depth/stencil): depth through a DepthOnly view as
 * texture_depth_2d and stencil through a StencilOnly view as texture_2d<u32>,
 * with exact readback of both cleared values at every pixel; a writable depth
 * attachment sampled in its own pass remains a validation error; depthReadOnly
 * on a stencil8 attachment (no depth aspect) draws and submits. */
#include "consumer.h"

enum { SIZE = 4, ROW = 256 };

static WGPUShaderModule module_of(state *s, const char *code)
{
    WGPUShaderSourceWGSL source = WGPU_SHADER_SOURCE_WGSL_INIT;
    source.code = (WGPUStringView){code, WGPU_STRLEN};
    WGPUShaderModuleDescriptor descriptor = {.nextInChain = &source.chain};
    WGPUShaderModule module = wgpuDeviceCreateShaderModule(s->device, &descriptor);
    assert(module);
    return module;
}

static WGPUTextureView aspect_view(WGPUTexture texture, WGPUTextureAspect aspect)
{
    WGPUTextureViewDescriptor descriptor = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    descriptor.aspect = aspect;
    WGPUTextureView view = wgpuTextureCreateView(texture, &descriptor);
    assert(view);
    return view;
}

/* One pass sampling `depth` while it is the depth/stencil attachment;
 * read_only selects the WebGPU read-only aspects. */
static WGPUCommandEncoder sampling_pass(state *s, WGPURenderPipeline pipeline, WGPUBindGroup group,
    WGPUTextureView attachment_view, WGPUTextureView color, bool read_only)
{
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device, NULL);
    WGPURenderPassColorAttachment target = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    target.view = color;
    target.loadOp = WGPULoadOp_Clear;
    target.storeOp = WGPUStoreOp_Store;
    WGPURenderPassDepthStencilAttachment depth = WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
    depth.view = attachment_view;
    depth.stencilReadOnly = true;
    if (read_only) {
        depth.depthReadOnly = true;
    } else {
        depth.depthLoadOp = WGPULoadOp_Load;
        depth.depthStoreOp = WGPUStoreOp_Store;
    }
    WGPURenderPassDescriptor descriptor = {.colorAttachmentCount = 1, .colorAttachments = &target,
        .depthStencilAttachment = &depth};
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &descriptor);
    assert(pass);
    wgpuRenderPassEncoderSetPipeline(pass, pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, group, 0, NULL);
    wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
    return encoder;
}

int main(void)
{
    state s = {0};
    consumer_init(&s);
    WGPUTextureDescriptor ds_desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    ds_desc.format = WGPUTextureFormat_Depth24PlusStencil8;
    ds_desc.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding;
    ds_desc.size = (WGPUExtent3D){SIZE, SIZE, 1};
    WGPUTexture ds = wgpuDeviceCreateTexture(s.device, &ds_desc);
    assert(ds);
    WGPUTextureView attachment_view = wgpuTextureCreateView(ds, NULL);
    assert(attachment_view);
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s.device, NULL);
    WGPURenderPassDepthStencilAttachment clear = WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
    clear.view = attachment_view;
    clear.depthLoadOp = WGPULoadOp_Clear;
    clear.depthStoreOp = WGPUStoreOp_Store;
    clear.depthClearValue = 0.25f;
    clear.stencilLoadOp = WGPULoadOp_Clear;
    clear.stencilStoreOp = WGPUStoreOp_Store;
    clear.stencilClearValue = 7;
    WGPURenderPassDescriptor clear_desc = {.depthStencilAttachment = &clear};
    WGPURenderPassEncoder clear_pass = wgpuCommandEncoderBeginRenderPass(encoder, &clear_desc);
    wgpuRenderPassEncoderEnd(clear_pass);
    wgpuRenderPassEncoderRelease(clear_pass);
    submit(&s, encoder);

    WGPUShaderModule module = module_of(&s,
        "@group(0) @binding(0) var depth: texture_depth_2d;\n"
        "@group(0) @binding(1) var stencil: texture_2d<u32>;\n"
        "@vertex fn vs(@builtin(vertex_index) i: u32) -> @builtin(position) vec4f {\n"
        "  var p = array(vec2f(-1.0, -1.0), vec2f(3.0, -1.0), vec2f(-1.0, 3.0));\n"
        "  return vec4f(p[i], 0.5, 1.0);\n}\n"
        "@fragment fn fs(@builtin(position) at: vec4f) -> @location(0) vec4f {\n"
        "  let texel = vec2i(at.xy);\n"
        "  return vec4f(textureLoad(depth, texel, 0), f32(textureLoad(stencil, texel, 0).r), 0.0, 1.0);\n}\n");
    WGPUColorTargetState color_target = {.format = WGPUTextureFormat_RGBA32Float, .writeMask = WGPUColorWriteMask_All};
    WGPUFragmentState fragment = {.module = module, .entryPoint = {"fs", WGPU_STRLEN}, .targetCount = 1,
        .targets = &color_target};
    WGPUDepthStencilState depth_state = WGPU_DEPTH_STENCIL_STATE_INIT;
    depth_state.format = WGPUTextureFormat_Depth24PlusStencil8;
    depth_state.depthWriteEnabled = WGPUOptionalBool_False;
    depth_state.depthCompare = WGPUCompareFunction_Always;
    WGPURenderPipelineDescriptor pipeline_desc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    pipeline_desc.vertex = (WGPUVertexState){.module = module, .entryPoint = {"vs", WGPU_STRLEN}};
    pipeline_desc.fragment = &fragment;
    pipeline_desc.depthStencil = &depth_state;
    WGPURenderPipeline pipeline = wgpuDeviceCreateRenderPipeline(s.device, &pipeline_desc);
    assert(pipeline);
    wgpuShaderModuleRelease(module);
    WGPUTextureView depth_view = aspect_view(ds, WGPUTextureAspect_DepthOnly);
    WGPUTextureView stencil_view = aspect_view(ds, WGPUTextureAspect_StencilOnly);
    WGPUBindGroupLayout layout = wgpuRenderPipelineGetBindGroupLayout(pipeline, 0);
    WGPUBindGroupEntry entries[2] = {{.binding = 0, .textureView = depth_view}, {.binding = 1, .textureView = stencil_view}};
    WGPUBindGroupDescriptor group_desc = {.layout = layout, .entryCount = 2, .entries = entries};
    WGPUBindGroup group = wgpuDeviceCreateBindGroup(s.device, &group_desc);
    assert(group);
    WGPUTextureDescriptor color_desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    color_desc.format = WGPUTextureFormat_RGBA32Float;
    color_desc.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc;
    color_desc.size = (WGPUExtent3D){SIZE, SIZE, 1};
    WGPUTexture color = wgpuDeviceCreateTexture(s.device, &color_desc);
    assert(color);
    WGPUTextureView color_view = wgpuTextureCreateView(color, NULL);
    WGPUBuffer readback = buffer(&s, ROW * SIZE, WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead);

    encoder = sampling_pass(&s, pipeline, group, attachment_view, color_view, true);
    texture_to_buffer(encoder, color, readback, 0, SIZE, SIZE);
    submit(&s, encoder);
    const float *texels = map(&s, readback, ROW * SIZE);
    for (unsigned y = 0; y < SIZE; ++y) {
        for (unsigned x = 0; x < SIZE; ++x) {
            const float *texel = texels + (y * ROW) / sizeof(float) + x * 4;
            assert(texel[0] == 0.25f && texel[1] == 7.0f && texel[3] == 1.0f);
        }
    }
    wgpuBufferUnmap(readback);

    /* Writable depth sampled in its own pass: a usage conflict. */
    wgpuDevicePushErrorScope(s.device, WGPUErrorFilter_Validation);
    encoder = sampling_pass(&s, pipeline, group, attachment_view, color_view, false);
    finish_invalid(encoder);
    wgpuCommandEncoderRelease(encoder);
    expect_validation(&s);

    /* depthReadOnly on a stencil-only attachment names an absent aspect,
     * which WebGPU allows: the pass draws and submits without an error. */
    WGPUTextureDescriptor stencil_desc = ds_desc;
    stencil_desc.format = WGPUTextureFormat_Stencil8;
    stencil_desc.usage = WGPUTextureUsage_RenderAttachment;
    WGPUTexture stencil_only = wgpuDeviceCreateTexture(s.device, &stencil_desc);
    assert(stencil_only);
    WGPUTextureView stencil_attachment = wgpuTextureCreateView(stencil_only, NULL);
    WGPUShaderModule plain = module_of(&s,
        "@vertex fn vs(@builtin(vertex_index) i: u32) -> @builtin(position) vec4f {\n"
        "  var p = array(vec2f(-1.0, -1.0), vec2f(3.0, -1.0), vec2f(-1.0, 3.0));\n"
        "  return vec4f(p[i], 0.5, 1.0);\n}\n"
        "@fragment fn fs() -> @location(0) vec4f { return vec4f(1.0); }\n");
    WGPUFragmentState plain_fragment = {.module = plain, .entryPoint = {"fs", WGPU_STRLEN}, .targetCount = 1,
        .targets = &color_target};
    WGPUDepthStencilState stencil_state = WGPU_DEPTH_STENCIL_STATE_INIT;
    stencil_state.format = WGPUTextureFormat_Stencil8;
    WGPURenderPipelineDescriptor stencil_pipeline_desc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    stencil_pipeline_desc.vertex = (WGPUVertexState){.module = plain, .entryPoint = {"vs", WGPU_STRLEN}};
    stencil_pipeline_desc.fragment = &plain_fragment;
    stencil_pipeline_desc.depthStencil = &stencil_state;
    WGPURenderPipeline stencil_pipeline = wgpuDeviceCreateRenderPipeline(s.device, &stencil_pipeline_desc);
    assert(stencil_pipeline);
    wgpuShaderModuleRelease(plain);
    encoder = wgpuDeviceCreateCommandEncoder(s.device, NULL);
    WGPURenderPassColorAttachment stencil_color = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    stencil_color.view = color_view;
    stencil_color.loadOp = WGPULoadOp_Clear;
    stencil_color.storeOp = WGPUStoreOp_Store;
    WGPURenderPassDepthStencilAttachment stencil_target = WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
    stencil_target.view = stencil_attachment;
    stencil_target.depthReadOnly = true;
    stencil_target.stencilLoadOp = WGPULoadOp_Clear;
    stencil_target.stencilStoreOp = WGPUStoreOp_Store;
    WGPURenderPassDescriptor stencil_pass_desc = {.colorAttachmentCount = 1, .colorAttachments = &stencil_color,
        .depthStencilAttachment = &stencil_target};
    WGPURenderPassEncoder stencil_pass = wgpuCommandEncoderBeginRenderPass(encoder, &stencil_pass_desc);
    wgpuRenderPassEncoderSetPipeline(stencil_pass, stencil_pipeline);
    wgpuRenderPassEncoderDraw(stencil_pass, 3, 1, 0, 0);
    wgpuRenderPassEncoderEnd(stencil_pass);
    wgpuRenderPassEncoderRelease(stencil_pass);
    submit(&s, encoder);
    work_done(&s);
    assert(!s.errors);
    wgpuRenderPipelineRelease(stencil_pipeline);
    wgpuTextureViewRelease(stencil_attachment);
    wgpuTextureRelease(stencil_only);

    wgpuBufferRelease(readback);
    wgpuTextureViewRelease(color_view);
    wgpuTextureRelease(color);
    wgpuBindGroupRelease(group);
    wgpuBindGroupLayoutRelease(layout);
    wgpuTextureViewRelease(stencil_view);
    wgpuTextureViewRelease(depth_view);
    wgpuRenderPipelineRelease(pipeline);
    wgpuTextureViewRelease(attachment_view);
    wgpuTextureRelease(ds);
    consumer_finish(&s);
    printf("PASS read-only depth/stencil sampling: a DepthOnly and a StencilOnly view of the read-only "
        "depth24plus-stencil8 attachment sampled in its own pass read 0.25 and 7 at all %u texels; a writable "
        "depth attachment sampled in its pass is a validation error; a depthReadOnly stencil8 pass submits %s\n", SIZE * SIZE, adapter_class_scope());
    return 0;
}
