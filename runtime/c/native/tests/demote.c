/* SPDX-License-Identifier: MIT
 * Actual public WGSL discard, with read-only private capability admission controls. */
#include "../src/internal.h"
#include "consumer.h"
#include <stdlib.h>

static bool disable_feature;
doe_vk_status __real_doe_vk_context_create(const doe_vk_options *, doe_vk_context **, doe_vk_error *);
doe_vk_status __wrap_doe_vk_context_create(const doe_vk_options *options, doe_vk_context **out, doe_vk_error *error)
{
    doe_vk_options selected = *options;
    if (disable_feature) selected.enable_shader_demote_if_supported = false;
    return __real_doe_vk_context_create(&selected,out,error);
}

static void capability_controls(native_device *device, WGPUShaderModule module)
{
    assert(device->backend.value.vulkan.caps.shader_demote_to_helper_invocation);
    native_stage stage = {0};
    native_pipeline_error error = {0};
    assert(native_stage_load(device,module,(WGPUStringView){"f",1},
        DOE_COMPILER_STAGE_FRAGMENT,NULL,0,&stage,&error));
    doe_shader_view view = stage.view;
    assert(native_shader_capabilities(&view,&device->backend.value.vulkan.caps));
    doe_vk_caps disabled = device->backend.value.vulkan.caps;
    disabled.api_version = UINT32_MAX; /* Version is never an enabling proof. */
    disabled.shader_demote_to_helper_invocation = false;
    assert(!native_shader_capabilities(&view,&disabled));
    uint32_t *words = malloc(view.counts.words*sizeof(*words));
    assert(words);
    memcpy(words,view.words,view.counts.words*sizeof(*words));
    view.words = words;
    unsigned extensions = 0, capabilities = 0;
    for (size_t at = 5; at < view.counts.words;) {
        uint32_t length = words[at] >> 16;
        uint32_t opcode = words[at] & 65535u;
        if (opcode == 10) {
            ++extensions;
            words[at+1] ^= 1;
            assert(!native_shader_capabilities(&view,&device->backend.value.vulkan.caps));
            words[at+1] ^= 1;
            words[at+length-1] |= UINT32_C(0xff000000); /* Nonzero string padding. */
            assert(!native_shader_capabilities(&view,&device->backend.value.vulkan.caps));
            words[at+length-1] &= UINT32_C(0x00ffffff);
        }
        if (opcode == 17 && words[at+1] == 5379) {
            ++capabilities;
            words[at+1] = 2;
            assert(!native_shader_capabilities(&view,&device->backend.value.vulkan.caps));
            words[at+1] = 5379;
        }
        at += length;
    }
    assert(extensions == 1 && capabilities == 1 && native_shader_capabilities(&view,&device->backend.value.vulkan.caps));
    free(words);
    native_stage_release(&stage);
}
int main(void)
{
    state s = {0};
    s.instance = wgpuCreateInstance(NULL);
    WGPURequestAdapterOptions options = WGPU_REQUEST_ADAPTER_OPTIONS_INIT;
    adapter_class_options(&options);
    WGPURequestAdapterCallbackInfo adapter = {.mode=WGPUCallbackMode_AllowSpontaneous,.callback=adapter_reply,.userdata1=&s};
    assert(wgpuInstanceRequestAdapter(s.instance,&options,adapter).id && s.adapter);
    adapter_class_require(s.adapter);
    WGPUDeviceDescriptor device = WGPU_DEVICE_DESCRIPTOR_INIT;
    device.uncapturedErrorCallbackInfo = (WGPUUncapturedErrorCallbackInfo){.callback=error_reply,.userdata1=&s};
    WGPURequestDeviceCallbackInfo requested = {.mode=WGPUCallbackMode_AllowSpontaneous,.callback=device_reply,.userdata1=&s};
    assert(wgpuAdapterRequestDevice(s.adapter,&device,requested).id && s.device);
    s.queue = wgpuDeviceGetQueue(s.device);
    WGPUShaderSourceWGSL source = WGPU_SHADER_SOURCE_WGSL_INIT;
    source.code = (WGPUStringView){
        "@vertex fn v(@location(0) p:vec2f)->@builtin(position) vec4f{return vec4f(p,0.0,1.0);}"
        "fn helper(x:f32)->f32{if(x<1.0){discard;}return 1.0;}"
        "@fragment fn f(@builtin(position) p:vec4f)->@location(0) vec4f{"
        "let red=helper(p.x);return vec4f(red,0.0,0.0,1.0);}",WGPU_STRLEN};
    WGPUShaderModuleDescriptor shader_desc = {.nextInChain=&source.chain};
    WGPUShaderModule module = wgpuDeviceCreateShaderModule(s.device,&shader_desc);
    assert(module);
    capability_controls((native_device *)s.device,module);
    WGPUVertexAttribute attribute = {.format=WGPUVertexFormat_Float32x2};
    WGPUVertexBufferLayout vertex = {.arrayStride=8,.attributeCount=1,.attributes=&attribute};
    WGPUColorTargetState color = {.format=WGPUTextureFormat_RGBA8Unorm,.writeMask=WGPUColorWriteMask_All};
    WGPUFragmentState fragment = {.module=module,.targetCount=1,.targets=&color};
    WGPURenderPipelineDescriptor pipeline_desc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    pipeline_desc.vertex = (WGPUVertexState){.module=module,.bufferCount=1,.buffers=&vertex};
    pipeline_desc.fragment = &fragment;
    WGPURenderPipeline pipeline = wgpuDeviceCreateRenderPipeline(s.device,&pipeline_desc);
    assert(pipeline);
    /* A real second Vulkan device leaves the extension disabled. Public pipeline
     * admission must reject it even though the first device enabled the feature. */
    state disabled = {.instance=s.instance,.adapter=s.adapter};
    WGPURequestDeviceCallbackInfo disabled_reply = {.mode=WGPUCallbackMode_AllowSpontaneous,
        .callback=device_reply,.userdata1=&disabled};
    disable_feature = true;
    assert(wgpuAdapterRequestDevice(s.adapter,NULL,disabled_reply).id && disabled.device);
    disable_feature = false;
    assert(!((native_device *)disabled.device)->backend.value.vulkan.caps.shader_demote_to_helper_invocation);
    WGPUShaderModule disabled_module = wgpuDeviceCreateShaderModule(disabled.device,&shader_desc);
    assert(disabled_module);
    WGPURenderPipelineDescriptor rejected = pipeline_desc;
    WGPUFragmentState disabled_fragment = fragment;
    disabled_fragment.module = disabled_module;
    rejected.vertex.module = disabled_module;
    rejected.fragment = &disabled_fragment;
    wgpuDevicePushErrorScope(disabled.device,WGPUErrorFilter_Validation);
    assert(!wgpuDeviceCreateRenderPipeline(disabled.device,&rejected));
    expect_validation(&disabled);
    wgpuShaderModuleRelease(disabled_module);
    wgpuDeviceRelease(disabled.device);
    const float points[6][2] = {{-1,-1},{1,-1},{-1,1},{-1,1},{1,-1},{1,1}};
    WGPUBuffer vertices = buffer(&s,sizeof(points),WGPUBufferUsage_Vertex|WGPUBufferUsage_CopyDst);
    wgpuQueueWriteBuffer(s.queue,vertices,0,points,sizeof(points));
    WGPUTexture image = texture(&s,WGPUTextureFormat_RGBA8Unorm,WGPUTextureUsage_RenderAttachment|WGPUTextureUsage_CopySrc,1);
    WGPUTextureView view = wgpuTextureCreateView(image,NULL);
    WGPUBuffer readback = buffer(&s,512,WGPUBufferUsage_MapRead|WGPUBufferUsage_CopyDst);
    WGPURenderPassColorAttachment attachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    attachment.view = view;
    attachment.loadOp = WGPULoadOp_Clear;
    attachment.storeOp = WGPUStoreOp_Store;
    attachment.clearValue = (WGPUColor){0,0,1,1};
    WGPURenderPassDescriptor descriptor = {.colorAttachmentCount=1,.colorAttachments=&attachment};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s.device,NULL);
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder,&descriptor);
    assert(pass);
    wgpuRenderPassEncoderSetPipeline(pass,pipeline);
    wgpuRenderPassEncoderSetVertexBuffer(pass,0,vertices,0,sizeof(points));
    wgpuRenderPassEncoderDraw(pass,6,1,0,0);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
    texture_to_buffer(encoder,image,readback,0,2,2);
    wgpuRenderPipelineRelease(pipeline);
    wgpuShaderModuleRelease(module);
    wgpuTextureViewRelease(view);
    wgpuTextureRelease(image);
    wgpuBufferRelease(vertices);
    submit(&s,encoder);
    const unsigned char *bytes = map(&s,readback,512);
    const unsigned char expected[8] = {0,0,255,255,255,0,0,255};
    assert(!memcmp(bytes,expected,8) && !memcmp(bytes+256,expected,8) && !s.errors);
    wgpuBufferUnmap(readback);
    wgpuBufferRelease(readback);
    wgpuQueueRelease(s.queue);
    wgpuDeviceRelease(s.device);
    wgpuAdapterRelease(s.adapter);
    wgpuInstanceRelease(s.instance);
    printf("PASS public discard: actual enabled feature, helper continuation, asymmetric pixel suppression, exact extension/capability gating and early releases %s\n", adapter_class_scope());
}
