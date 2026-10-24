/* SPDX-License-Identifier: MIT
 * The WebGPU encoding error rule, counted through the uncaptured-error callback:
 * a rejected pass descriptor, pass command or copy only invalidates its encoder
 * and Finish reports exactly one validation error; later valid passes on that
 * encoder add nothing; commands on an ended encoder or pass report at the call.
 * Also: core-features-and-limits is listed and accepted in requiredFeatures. */
#include "consumer.h"
#include <stdlib.h>

static void expect_errors(const state *s, unsigned before, unsigned expected, const char *name)
{
    if (s->errors - before != expected) {
        fprintf(stderr, "%s: %u uncaptured errors, expected %u\n", name, s->errors - before, expected);
        abort();
    }
}
static WGPURenderPassEncoder begin_render(WGPUCommandEncoder encoder, WGPUTextureView view, WGPUStoreOp store)
{
    WGPURenderPassColorAttachment color = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    color.view = view;
    color.loadOp = WGPULoadOp_Clear;
    color.storeOp = store;
    WGPURenderPassDescriptor descriptor = {.colorAttachmentCount=1,.colorAttachments=&color};
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder,&descriptor);
    assert(pass); /* An invalid descriptor still returns an (invalid) pass. */
    return pass;
}
static void finish_rejected(state *s, WGPUCommandEncoder encoder, unsigned before, const char *name)
{
    expect_errors(s,before,0,name);
    assert(!wgpuCommandEncoderFinish(encoder,NULL));
    expect_errors(s,before,1,name);
    wgpuCommandEncoderRelease(encoder);
}

int main(void)
{
    state s = {0};
    consumer_init(&s);
    WGPUTexture target = texture(&s,WGPUTextureFormat_RGBA8Unorm,WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc,1);
    WGPUTextureView view = wgpuTextureCreateView(target,NULL);
    WGPUBuffer bytes = buffer(&s,1024,WGPUBufferUsage_CopyDst | WGPUBufferUsage_CopySrc);

    /* Invalid render pass descriptor, commands and End: one error, at Finish. */
    unsigned before = s.errors;
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s.device,NULL);
    WGPURenderPassEncoder pass = begin_render(encoder,view,WGPUStoreOp_Undefined);
    wgpuRenderPassEncoderSetViewport(pass,0,0,1,1,0,1);
    wgpuRenderPassEncoderDraw(pass,3,1,0,0);
    wgpuRenderPassEncoderEnd(pass);
    expect_errors(&s,before,0,"invalid render pass before Finish");
    /* A command on the ended pass reports at the call. */
    wgpuRenderPassEncoderEnd(pass);
    expect_errors(&s,before,1,"End on an ended render pass");
    wgpuRenderPassEncoderRelease(pass);
    before = s.errors;
    finish_rejected(&s,encoder,before,"invalid render pass");

    /* Compute pass with timestampWrites but no timestamp feature. */
    before = s.errors;
    encoder = wgpuDeviceCreateCommandEncoder(s.device,NULL);
    WGPUPassTimestampWrites writes = {.beginningOfPassWriteIndex=0,.endOfPassWriteIndex=1};
    WGPUComputePassDescriptor compute_desc = {.timestampWrites=&writes};
    WGPUComputePassEncoder compute = wgpuCommandEncoderBeginComputePass(encoder,&compute_desc);
    assert(compute);
    wgpuComputePassEncoderDispatchWorkgroups(compute,1,1,1);
    wgpuComputePassEncoderEnd(compute);
    expect_errors(&s,before,0,"invalid compute pass before Finish");
    wgpuComputePassEncoderDispatchWorkgroups(compute,1,1,1);
    expect_errors(&s,before,1,"dispatch on an ended compute pass");
    wgpuComputePassEncoderRelease(compute);
    before = s.errors;
    finish_rejected(&s,encoder,before,"invalid compute pass");

    /* One rejected copy, then valid render and compute passes: still one error. */
    const struct { const char *name; unsigned kind; } invalid[3] = {
        {"rowsPerImage 0 copy",0},{"unaligned buffer copy",1},{"unaligned buffer clear",2}};
    for (size_t i = 0; i < 3; ++i) {
        before = s.errors;
        encoder = wgpuDeviceCreateCommandEncoder(s.device,NULL);
        if (invalid[i].kind == 0) {
            WGPUTexelCopyTextureInfo source = {.texture=target,.aspect=WGPUTextureAspect_All};
            WGPUTexelCopyBufferInfo destination = {.buffer=bytes,.layout={.bytesPerRow=256,.rowsPerImage=0}};
            wgpuCommandEncoderCopyTextureToBuffer(encoder,&source,&destination,&(WGPUExtent3D){2,2,1});
        } else if (invalid[i].kind == 1) wgpuCommandEncoderCopyBufferToBuffer(encoder,bytes,0,bytes,512,3);
        else wgpuCommandEncoderClearBuffer(encoder,bytes,0,3);
        pass = begin_render(encoder,view,WGPUStoreOp_Store);
        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);
        compute = wgpuCommandEncoderBeginComputePass(encoder,NULL);
        wgpuComputePassEncoderEnd(compute);
        wgpuComputePassEncoderRelease(compute);
        finish_rejected(&s,encoder,before,invalid[i].name);
    }

    /* Every command on a finished (ended) encoder reports at the call. */
    encoder = wgpuDeviceCreateCommandEncoder(s.device,NULL);
    WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder,NULL);
    assert(commands);
    before = s.errors;
    WGPUTexelCopyTextureInfo source = {.texture=target,.aspect=WGPUTextureAspect_All};
    WGPUTexelCopyBufferInfo destination = {.buffer=bytes,.layout={.bytesPerRow=256,.rowsPerImage=2}};
    wgpuCommandEncoderCopyTextureToBuffer(encoder,&source,&destination,&(WGPUExtent3D){2,2,1});
    expect_errors(&s,before,1,"texel copy on an ended encoder");
    wgpuCommandEncoderCopyBufferToBuffer(encoder,bytes,0,bytes,512,4);
    expect_errors(&s,before,2,"buffer copy on an ended encoder");
    wgpuCommandEncoderClearBuffer(encoder,bytes,0,4);
    expect_errors(&s,before,3,"clear on an ended encoder");
    pass = begin_render(encoder,view,WGPUStoreOp_Store);
    expect_errors(&s,before,4,"render pass on an ended encoder");
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
    assert(!wgpuCommandEncoderFinish(encoder,NULL));
    expect_errors(&s,before,5,"second Finish");
    wgpuCommandBufferRelease(commands);
    wgpuCommandEncoderRelease(encoder);

    /* A valid encoder still records and submits after all of the above. */
    encoder = wgpuDeviceCreateCommandEncoder(s.device,NULL);
    pass = begin_render(encoder,view,WGPUStoreOp_Store);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
    before = s.errors;
    submit(&s,encoder);
    expect_errors(&s,before,0,"valid encoder");

    s.errors = 0;
    wgpuBufferRelease(bytes);
    wgpuTextureViewRelease(view);
    wgpuTextureRelease(target);
    consumer_finish(&s);

    /* core-features-and-limits: listed by the adapter and accepted when required. */
    state core = {0};
    const WGPUFeatureName requested = WGPUFeatureName_CoreFeaturesAndLimits;
    consumer_init_features(&core,&requested,1);
    assert(wgpuAdapterHasFeature(core.adapter,requested) && wgpuDeviceHasFeature(core.device,requested));
    consumer_finish(&core);
    printf("PASS public encoder errors: an invalid render/compute pass descriptor or command, an invalid copy or "
        "clear followed by valid passes, each reports exactly one uncaptured validation error at Finish; "
        "End/dispatch on ended passes, copies/clears/begin on a finished encoder and a second Finish report at "
        "the call; core-features-and-limits listed and accepted %s\n", adapter_class_scope());
    return 0;
}
