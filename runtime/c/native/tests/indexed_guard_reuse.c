/* SPDX-License-Identifier: MIT
 * Exact pixels for identical guarded draws and GPU input mutation between
 * passes. Every scissor needs its own real draw; no timing claim. */
#include "consumer.h"

static WGPURenderPipeline graphics(state *s)
{
    WGPUShaderSourceWGSL code = WGPU_SHADER_SOURCE_WGSL_INIT;
    code.code = (WGPUStringView){
        "@vertex fn vs(@location(0) p:vec2f)->@builtin(position)vec4f{return vec4f(p,0,1);}"
        "@fragment fn fs()->@location(0)vec4f{return vec4f(1,0,0,1);}",WGPU_STRLEN};
    WGPUShaderModuleDescriptor shader = {.nextInChain=&code.chain};
    WGPUShaderModule module = wgpuDeviceCreateShaderModule(s->device, &shader);
    assert(module);
    WGPUVertexAttribute attribute = {.format=WGPUVertexFormat_Float32x2};
    WGPUVertexBufferLayout vertices = {.arrayStride=8,.stepMode=WGPUVertexStepMode_Vertex,
        .attributeCount=1,.attributes=&attribute};
    WGPUColorTargetState color = {.format=WGPUTextureFormat_RGBA8Unorm,.writeMask=WGPUColorWriteMask_All};
    WGPUFragmentState fragment = {.module=module,.entryPoint={"fs",2},.targetCount=1,.targets=&color};
    WGPURenderPipelineDescriptor desc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    desc.vertex = (WGPUVertexState){.module=module,.entryPoint={"vs",2},.bufferCount=1,.buffers=&vertices};
    desc.fragment = &fragment;
    WGPURenderPipeline result = wgpuDeviceCreateRenderPipeline(s->device, &desc);
    assert(result);
    wgpuShaderModuleRelease(module);
    return result;
}

static void produce(WGPUCommandEncoder encoder, WGPUComputePipeline compute, WGPUBindGroup bindings)
{
    WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, NULL);
    assert(pass);
    wgpuComputePassEncoderSetPipeline(pass, compute);
    wgpuComputePassEncoderSetBindGroup(pass, 0, bindings, 0, NULL);
    wgpuComputePassEncoderDispatchWorkgroups(pass, 1, 1, 1);
    wgpuComputePassEncoderEnd(pass);
    wgpuComputePassEncoderRelease(pass);
}

static void columns(WGPUCommandEncoder encoder, WGPUTextureView view, WGPURenderPipeline render,
    WGPUBuffer vertices, WGPUBuffer indices, WGPUBuffer arguments)
{
    WGPURenderPassColorAttachment color = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    color.view = view;
    color.loadOp = WGPULoadOp_Clear;
    color.storeOp = WGPUStoreOp_Store;
    color.clearValue = (WGPUColor){0,0,1,1};
    WGPURenderPassDescriptor desc = {.colorAttachmentCount=1,.colorAttachments=&color};
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &desc);
    assert(pass);
    wgpuRenderPassEncoderSetPipeline(pass, render);
    wgpuRenderPassEncoderSetVertexBuffer(pass, 0, vertices, 0, 24);
    wgpuRenderPassEncoderSetIndexBuffer(pass, indices, WGPUIndexFormat_Uint32, 0, 12);
    for (uint32_t x=0; x<8; ++x) {
        wgpuRenderPassEncoderSetScissorRect(pass, x, 0, 1, 1);
        wgpuRenderPassEncoderDrawIndexedIndirect(pass, arguments, 0);
    }
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
}

static void capture(WGPUCommandEncoder encoder, WGPUTexture image, WGPUBuffer readback, uint64_t offset)
{
    WGPUTexelCopyTextureInfo source = {.texture=image,.aspect=WGPUTextureAspect_All};
    WGPUTexelCopyBufferInfo destination = {.buffer=readback,.layout={.offset=offset,.bytesPerRow=256,.rowsPerImage=1}};
    WGPUExtent3D extent = {8,1,1};
    wgpuCommandEncoderCopyTextureToBuffer(encoder, &source, &destination, &extent);
}

int main(void)
{
    state s = {0};
    consumer_init(&s);
    WGPUBuffer vertices = buffer(&s, 24, WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst);
    const float positions[6] = {-1,-1,3,-1,-1,3};
    wgpuQueueWriteBuffer(s.queue, vertices, 0, positions, sizeof(positions));
    WGPUBuffer indices = buffer(&s, 12, WGPUBufferUsage_Index | WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc);
    WGPUBuffer arguments = buffer(&s, 20, WGPUBufferUsage_Indirect | WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc);
    WGPUComputePipeline good = pipeline(&s,
        "@group(0) @binding(0) var<storage,read_write> ix:array<u32>;"
        "@group(0) @binding(1) var<storage,read_write> args:array<u32>;"
        "@compute @workgroup_size(1) fn main(){ix[0]=0;ix[1]=1;ix[2]=2;"
        "args[0]=3;args[1]=1;args[2]=0;args[3]=0;args[4]=0;}");
    WGPUComputePipeline invalid_count = pipeline(&s,
        "@group(0) @binding(0) var<storage,read_write> ix:array<u32>;"
        "@group(0) @binding(1) var<storage,read_write> args:array<u32>;"
        "@compute @workgroup_size(1) fn main(){ix[0]=2;ix[1]=1;ix[2]=0;"
        "args[0]=4;args[1]=1;args[2]=0;args[3]=0;args[4]=0;}");
    /* WebGPU permits several outcomes for out-of-range vertex attributes.
     * Whole-draw cancellation is this Metal scan owner's stronger contract,
     * not a portable pixel expectation: gpuweb.github.io/gpuweb/#vertex-processing */
    bool index_scan = adapter_class_backend()==WGPUBackendType_Metal;
    WGPUComputePipeline bad_index = index_scan ? pipeline(&s,
        "@group(0) @binding(0) var<storage,read_write> ix:array<u32>;"
        "@group(0) @binding(1) var<storage,read_write> args:array<u32>;"
        "@compute @workgroup_size(1) fn main(){ix[0]=99;ix[1]=1;ix[2]=2;"
        "args[0]=3;args[1]=1;args[2]=0;args[3]=0;args[4]=0;}") : NULL;
    WGPUBindGroupEntry entries[] = {{.binding=0,.buffer=indices,.size=12},
        {.binding=1,.buffer=arguments,.size=20}};
    WGPUBindGroup good_group = group(&s, good, entries, 2);
    WGPUBindGroup invalid_count_group = group(&s, invalid_count, entries, 2);
    WGPUBindGroup bad_index_group = index_scan ? group(&s, bad_index, entries, 2) : NULL;
    WGPURenderPipeline render = graphics(&s);
    WGPUTextureDescriptor image_desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    image_desc.size = (WGPUExtent3D){8,1,1};
    image_desc.format = WGPUTextureFormat_RGBA8Unorm;
    image_desc.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc;
    WGPUTexture image = wgpuDeviceCreateTexture(s.device, &image_desc);
    WGPUTextureView view = wgpuTextureCreateView(image, NULL);
    assert(image && view);
    size_t final_offset = index_scan ? 768 : 512;
    WGPUBuffer readback = buffer(&s, final_offset+32, WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst);
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s.device, NULL);
    produce(encoder, good, good_group);
    columns(encoder, view, render, vertices, indices, arguments);
    capture(encoder, image, readback, 0);
    produce(encoder, invalid_count, invalid_count_group);
    columns(encoder, view, render, vertices, indices, arguments);
    capture(encoder, image, readback, 256);
    if (index_scan) {
        produce(encoder, bad_index, bad_index_group);
        columns(encoder, view, render, vertices, indices, arguments);
        capture(encoder, image, readback, 512);
    }
    wgpuCommandEncoderCopyBufferToBuffer(encoder, arguments, 0, readback, final_offset, 20);
    wgpuCommandEncoderCopyBufferToBuffer(encoder, indices, 0, readback, final_offset+20, 12);
    wgpuBindGroupRelease(good_group); wgpuBindGroupRelease(invalid_count_group); wgpuBindGroupRelease(bad_index_group);
    wgpuComputePipelineRelease(good); wgpuComputePipelineRelease(invalid_count); wgpuComputePipelineRelease(bad_index);
    wgpuRenderPipelineRelease(render); wgpuTextureViewRelease(view); wgpuTextureRelease(image);
    wgpuBufferRelease(vertices); wgpuBufferRelease(indices); wgpuBufferRelease(arguments);
    submit(&s, encoder);
    const unsigned char *bytes = map(&s, readback, final_offset+32);
    const unsigned char red[4] = {255,0,0,255}, blue[4] = {0,0,255,255};
    for (size_t x=0; x<8; ++x) {
        assert(!memcmp(bytes+x*4, red, 4));
        assert(!memcmp(bytes+256+x*4, blue, 4));
        if (index_scan) assert(!memcmp(bytes+512+x*4, blue, 4));
    }
    uint32_t expected_arguments[5] = {4,1,0,0,0}, expected_indices[3] = {2,1,0};
    if (index_scan) { expected_arguments[0]=3; expected_indices[0]=99; expected_indices[2]=2; }
    assert(!memcmp(bytes+final_offset, expected_arguments, sizeof(expected_arguments)));
    assert(!memcmp(bytes+final_offset+20, expected_indices, sizeof(expected_indices)));
    wgpuBufferUnmap(readback); wgpuBufferRelease(readback);
    consumer_finish(&s);
    printf("PASS indexed guard reuse:%u exact pixels, every scissored draw preserved, GPU-mutated inputs revalidated between passes, unchanged source arguments and early releases %s\n",
        index_scan ? 24u : 16u, adapter_class_scope());
    return 0;
}
