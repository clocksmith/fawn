/* SPDX-License-Identifier: MIT */
#include "consumer.h"

static const WGPUStringView label = {"nested\0diagnostic",17};
static void rejected_finish(state *s, WGPUCommandEncoder encoder)
{
    finish_invalid(encoder);
    expect_validation(s);
    wgpuCommandEncoderRelease(encoder);
}
static void commands(state *s)
{
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device,NULL);
    WGPUProcCommandEncoderPushDebugGroup push = (WGPUProcCommandEncoderPushDebugGroup)
        wgpuGetProcAddress((WGPUStringView){"wgpuCommandEncoderPushDebugGroup",WGPU_STRLEN});
    assert(push);
    push(encoder,label);
    wgpuCommandEncoderInsertDebugMarker(encoder,label);
    WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder,NULL);
    assert(pass);
    wgpuComputePassEncoderPushDebugGroup(pass,label);
    wgpuComputePassEncoderInsertDebugMarker(pass,label);
    wgpuComputePassEncoderPopDebugGroup(pass);
    wgpuComputePassEncoderEnd(pass);
    wgpuComputePassEncoderRelease(pass);
    wgpuCommandEncoderPopDebugGroup(encoder);
    submit(s,encoder);
    encoder = wgpuDeviceCreateCommandEncoder(s->device,NULL);
    wgpuDevicePushErrorScope(s->device,WGPUErrorFilter_Validation);
    wgpuCommandEncoderPopDebugGroup(encoder);
    rejected_finish(s,encoder);
    encoder = wgpuDeviceCreateCommandEncoder(s->device,NULL);
    wgpuCommandEncoderPushDebugGroup(encoder,label);
    wgpuDevicePushErrorScope(s->device,WGPUErrorFilter_Validation);
    rejected_finish(s,encoder); /* Unbalanced end is not a diagnostic no-op. */
    encoder = wgpuDeviceCreateCommandEncoder(s->device,NULL);
    wgpuDevicePushErrorScope(s->device,WGPUErrorFilter_Validation);
    wgpuCommandEncoderInsertDebugMarker(encoder,(WGPUStringView){NULL,1});
    rejected_finish(s,encoder);
    encoder = wgpuDeviceCreateCommandEncoder(s->device,NULL);
    WGPUCommandBuffer finished = wgpuCommandEncoderFinish(encoder,NULL);
    assert(finished);
    wgpuDevicePushErrorScope(s->device,WGPUErrorFilter_Validation);
    wgpuCommandEncoderInsertDebugMarker(encoder,label);
    expect_validation(s);
    wgpuCommandBufferRelease(finished);
    wgpuCommandEncoderRelease(encoder);
}
static void compute(state *s)
{
    for (unsigned which = 0; which < 4; ++which) {
        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device,NULL);
        wgpuCommandEncoderPushDebugGroup(encoder,label);
        WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder,NULL);
        assert(pass);
        wgpuDevicePushErrorScope(s->device,WGPUErrorFilter_Validation);
        switch (which) {
        case 0: wgpuComputePassEncoderPopDebugGroup(pass); break; /* Cannot pop parent. */
        case 1: wgpuComputePassEncoderPushDebugGroup(pass,label); break; /* Unbalanced End. */
        case 2: wgpuCommandEncoderInsertDebugMarker(encoder,label); break; /* Locked parent. */
        default: wgpuComputePassEncoderInsertDebugMarker(pass,(WGPUStringView){NULL,3}); break;
        }
        wgpuComputePassEncoderEnd(pass);
        wgpuComputePassEncoderRelease(pass);
        rejected_finish(s,encoder);
    }
}
static void render_and_bundles(state *s)
{
    WGPUTexture image = texture(s,WGPUTextureFormat_RGBA8Unorm,WGPUTextureUsage_RenderAttachment,1);
    WGPUTextureView view = wgpuTextureCreateView(image,NULL);
    assert(view);
    WGPURenderPassColorAttachment attachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    attachment.view = view;
    attachment.loadOp = WGPULoadOp_Clear;
    attachment.storeOp = WGPUStoreOp_Store;
    WGPURenderPassDescriptor descriptor = {.colorAttachmentCount=1,.colorAttachments=&attachment};
    WGPUTextureFormat format = WGPUTextureFormat_RGBA8Unorm;
    WGPURenderBundleEncoderDescriptor bundle_desc = WGPU_RENDER_BUNDLE_ENCODER_DESCRIPTOR_INIT;
    bundle_desc.colorFormatCount = 1; bundle_desc.colorFormats = &format;
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device,NULL);
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder,&descriptor);
    assert(pass);
    wgpuRenderPassEncoderPushDebugGroup(pass,label);
    wgpuRenderPassEncoderInsertDebugMarker(pass,label);
    WGPURenderBundleEncoder builder = wgpuDeviceCreateRenderBundleEncoder(s->device,&bundle_desc);
    assert(builder);
    wgpuRenderBundleEncoderPushDebugGroup(builder,label);
    wgpuRenderBundleEncoderInsertDebugMarker(builder,label);
    wgpuRenderBundleEncoderPopDebugGroup(builder);
    WGPURenderBundle bundle = wgpuRenderBundleEncoderFinish(builder,NULL);
    assert(bundle);
    wgpuRenderBundleEncoderRelease(builder);
    wgpuRenderPassEncoderExecuteBundles(pass,1,&bundle);
    wgpuRenderBundleRelease(bundle);
    wgpuRenderPassEncoderPopDebugGroup(pass);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
    submit(s,encoder);
    for (unsigned which = 0; which < 3; ++which) {
        encoder = wgpuDeviceCreateCommandEncoder(s->device,NULL);
        pass = wgpuCommandEncoderBeginRenderPass(encoder,&descriptor);
        assert(pass);
        wgpuDevicePushErrorScope(s->device,WGPUErrorFilter_Validation);
        if (!which) wgpuRenderPassEncoderPopDebugGroup(pass);
        else if (which == 1) wgpuRenderPassEncoderPushDebugGroup(pass,label);
        else wgpuRenderPassEncoderInsertDebugMarker(pass,(WGPUStringView){NULL,2});
        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);
        rejected_finish(s,encoder);
        builder = wgpuDeviceCreateRenderBundleEncoder(s->device,&bundle_desc);
        assert(builder);
        wgpuDevicePushErrorScope(s->device,WGPUErrorFilter_Validation);
        if (!which) wgpuRenderBundleEncoderPopDebugGroup(builder);
        else if (which == 1) wgpuRenderBundleEncoderPushDebugGroup(builder,label);
        else wgpuRenderBundleEncoderInsertDebugMarker(builder,(WGPUStringView){NULL,2});
        WGPURenderBundle invalid_bundle = wgpuRenderBundleEncoderFinish(builder,NULL);
        assert(invalid_bundle); /* WebGPU invalid bundle */
        wgpuRenderBundleRelease(invalid_bundle);
        expect_validation(s);
        wgpuRenderBundleEncoderRelease(builder);
    }
    wgpuTextureViewRelease(view);
    wgpuTextureRelease(image);
}
int main(void)
{
    state s = {0};
    consumer_init(&s);
    commands(&s); compute(&s); render_and_bundles(&s);
    assert(s.validation_errors == 14);
    consumer_finish(&s);
    printf("PASS public debug markers: typed procedures, independent nested balance, parent lock, underflow, malformed strings and finished/unbalanced command/pass/bundle rejection %s\n", adapter_class_scope());
    return 0;
}
