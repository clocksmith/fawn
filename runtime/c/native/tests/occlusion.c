/* SPDX-License-Identifier: MIT
 * Query reuse preserves untouched indices and resets only begun queries. */
#include "consumer.h"

static WGPURenderPipeline fullscreen(state *s)
{
    WGPUShaderSourceWGSL source = WGPU_SHADER_SOURCE_WGSL_INIT;
    source.code = (WGPUStringView){
        "@vertex fn vs(@builtin(vertex_index) i:u32)->@builtin(position) vec4f{"
        "if(i==0u){return vec4f(-1.0,-1.0,0.0,1.0);}"
        "if(i==1u){return vec4f(3.0,-1.0,0.0,1.0);}return vec4f(-1.0,3.0,0.0,1.0);}"
        "@fragment fn fs()->@location(0) vec4f{return vec4f(1.0,0.0,0.0,1.0);}",WGPU_STRLEN};
    WGPUShaderModuleDescriptor module_desc = {.nextInChain=&source.chain};
    WGPUShaderModule module = wgpuDeviceCreateShaderModule(s->device, &module_desc);
    WGPUColorTargetState target = {.format=WGPUTextureFormat_RGBA8Unorm,.writeMask=WGPUColorWriteMask_All};
    WGPUFragmentState fragment = {.module=module,.targetCount=1,.targets=&target};
    WGPURenderPipelineDescriptor desc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    desc.vertex.module = module;
    desc.fragment = &fragment;
    WGPURenderPipeline result = wgpuDeviceCreateRenderPipeline(s->device, &desc);
    wgpuShaderModuleRelease(module);
    return result;
}

static void query(WGPURenderPassEncoder pass, uint32_t index, bool draw, bool visible)
{
    wgpuRenderPassEncoderBeginOcclusionQuery(pass, index);
    wgpuRenderPassEncoderSetScissorRect(pass, 0, 0, visible ? 2 : 0, visible ? 2 : 0);
    if (draw) wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
    wgpuRenderPassEncoderEndOcclusionQuery(pass);
}
static void resolve(WGPUCommandEncoder encoder, WGPUQuerySet queries, WGPUBuffer result,
    WGPUBuffer readback, uint64_t offset)
{
    wgpuCommandEncoderResolveQuerySet(encoder, queries, 0, 4, result, 0);
    wgpuCommandEncoderCopyBufferToBuffer(encoder, result, 0, readback, offset, 32);
}

int main(void)
{
    setvbuf(stdout, NULL, _IOLBF, 0);
    state s = {0};
    consumer_init(&s);
    WGPUQuerySetDescriptor desc = {.type=WGPUQueryType_Occlusion,.count=4};
    WGPUQuerySet queries = wgpuDeviceCreateQuerySet(s.device, &desc);
    assert(queries && wgpuQuerySetGetCount(queries) == 4 && wgpuQuerySetGetType(queries) == WGPUQueryType_Occlusion);
    WGPUBuffer result = buffer(&s, 32, WGPUBufferUsage_QueryResolve | WGPUBufferUsage_CopySrc);
    WGPUBuffer readback = buffer(&s, 128, WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead);
    WGPUTexture image = texture(&s, WGPUTextureFormat_RGBA8Unorm, WGPUTextureUsage_RenderAttachment, 1);
    WGPUTextureView view = wgpuTextureCreateView(image, NULL);
    WGPURenderPipeline render = fullscreen(&s);
    assert(view && render);
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s.device, NULL);
    resolve(encoder, queries, result, readback, 0); /* Initially unavailable. */
    WGPURenderPassColorAttachment attachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    attachment.view = view;
    attachment.loadOp = WGPULoadOp_Clear;
    attachment.storeOp = WGPUStoreOp_Store;
    WGPURenderPassDescriptor pass_desc = {.colorAttachmentCount=1,.colorAttachments=&attachment,.occlusionQuerySet=queries};
    for (unsigned iteration = 0; iteration < 3; ++iteration) {
        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &pass_desc);
        wgpuRenderPassEncoderSetPipeline(pass, render);
        if (iteration == 0) {
            query(pass, 0, true, true);
            query(pass, 1, true, true);
            query(pass, 2, true, false);
        } else if (iteration == 1) query(pass, 0, false, true);
        else {
            query(pass, 0, true, true);
            query(pass, 1, false, true);
        }
        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);
        resolve(encoder, queries, result, readback, (uint64_t)(iteration + 1) * 32);
        if (iteration == 1) {
            submit(&s, encoder);
            encoder = wgpuDeviceCreateCommandEncoder(s.device, NULL);
        }
    }
    /* Snapshots, not public handles, keep every query and render input alive. */
    wgpuQuerySetRelease(queries);
    wgpuBufferRelease(result);
    wgpuRenderPipelineRelease(render);
    wgpuTextureViewRelease(view);
    wgpuTextureRelease(image);
    submit(&s, encoder);
    const uint64_t *words = map(&s, readback, 128);
    for (unsigned i = 0; i < 4; ++i) assert(words[i] == 0);
    assert(words[4] && words[5] && !words[6] && !words[7]);
    assert(!words[8] && words[9] && !words[10] && !words[11]);
    assert(words[12] && !words[13] && !words[14] && !words[15]);
    wgpuBufferUnmap(readback);
    wgpuBufferRelease(readback);
    consumer_finish(&s);
    printf("PASS occlusion: visible, empty-scissor, empty and unavailable results, selected-index reset, "
        "cross-pass and cross-submission reuse, early handles released %s\n", adapter_class_scope());
    return 0;
}
