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
static WGPURenderPipeline target_pipeline(state *s, WGPUTextureFormat format)
{
    WGPUShaderSourceWGSL source = WGPU_SHADER_SOURCE_WGSL_INIT;
    source.code = (WGPUStringView){"@vertex fn vertex(@builtin(vertex_index) i:u32)->@builtin(position) vec4f{"
        "return vec4f(f32(i),0.0,0.0,1.0);}@fragment fn fragment()->@location(0) vec4f{return vec4f(1.0);}",WGPU_STRLEN};
    WGPUShaderModuleDescriptor module_desc = {.nextInChain=&source.chain};
    WGPUShaderModule module = wgpuDeviceCreateShaderModule(s->device,&module_desc);
    WGPUColorTargetState target = {.format=format,.writeMask=WGPUColorWriteMask_All};
    WGPUFragmentState fragment = {.module=module,.entryPoint={"fragment",WGPU_STRLEN},.targetCount=1,.targets=&target};
    WGPURenderPipelineDescriptor descriptor = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    descriptor.vertex = (WGPUVertexState){.module=module,.entryPoint={"vertex",WGPU_STRLEN}};
    descriptor.fragment = &fragment;
    WGPURenderPipeline pipeline_handle = wgpuDeviceCreateRenderPipeline(s->device,&descriptor);
    wgpuShaderModuleRelease(module);
    assert(pipeline_handle);
    return pipeline_handle;
}
static WGPURenderBundleEncoder bundle_encoder(state *s)
{
    static const WGPUTextureFormat format = WGPUTextureFormat_RGBA8Unorm;
    WGPURenderBundleEncoderDescriptor descriptor = WGPU_RENDER_BUNDLE_ENCODER_DESCRIPTOR_INIT;
    descriptor.colorFormatCount = 1;
    descriptor.colorFormats = &format;
    WGPURenderBundleEncoder encoder = wgpuDeviceCreateRenderBundleEncoder(s->device,&descriptor);
    assert(encoder);
    return encoder;
}
static void finish_rejected(state *s, WGPUCommandEncoder encoder, unsigned before, const char *name)
{
    expect_errors(s,before,0,name);
    finish_invalid(encoder);
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
    wgpuRenderPassEncoderEnd(pass); /* its parent is not locked */
    expect_errors(&s,before,5,"End of a pass begun on an ended encoder");
    wgpuRenderPassEncoderRelease(pass);
    finish_invalid(encoder);
    expect_errors(&s,before,6,"second Finish");
    wgpuCommandBufferRelease(commands);
    wgpuCommandEncoderRelease(encoder);

    /* Ended passes report at the call and leave a valid parent valid: End twice,
     * Draw after End and Dispatch after End each give one error, and Finish
     * returns a command buffer that submits without another error. */
    before = s.errors;
    encoder = wgpuDeviceCreateCommandEncoder(s.device,NULL);
    pass = begin_render(encoder,view,WGPUStoreOp_Store);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderEnd(pass);
    expect_errors(&s,before,1,"second End on a valid encoder");
    wgpuRenderPassEncoderDraw(pass,3,1,0,0);
    expect_errors(&s,before,2,"Draw after End on a valid encoder");
    wgpuRenderPassEncoderRelease(pass);
    compute = wgpuCommandEncoderBeginComputePass(encoder,NULL);
    wgpuComputePassEncoderEnd(compute);
    wgpuComputePassEncoderDispatchWorkgroups(compute,1,1,1);
    expect_errors(&s,before,3,"Dispatch after End on a valid encoder");
    wgpuComputePassEncoderRelease(compute);
    submit(&s,encoder);
    expect_errors(&s,before,3,"valid encoder after ended-pass commands");

    /* Locked encoder: a second begin while a pass is open invalidates silently;
     * ending the second pass unlocks the parent, so ending the first reports at
     * the call; Finish reports the invalid encoder once. */
    before = s.errors;
    encoder = wgpuDeviceCreateCommandEncoder(s.device,NULL);
    pass = begin_render(encoder,view,WGPUStoreOp_Store);
    WGPURenderPassEncoder second = begin_render(encoder,view,WGPUStoreOp_Store);
    expect_errors(&s,before,0,"second begin while locked");
    wgpuRenderPassEncoderEnd(second);
    expect_errors(&s,before,0,"End of the second pass");
    wgpuRenderPassEncoderEnd(pass);
    expect_errors(&s,before,1,"End of the first pass after the parent unlocked");
    wgpuRenderPassEncoderRelease(second);
    wgpuRenderPassEncoderRelease(pass);
    finish_rejected(&s,encoder,before + 1,"second begin while locked");
    /* Finish while a pass is open: one error at Finish, then End reports (the
     * parent ended), and submitting the invalid command buffer reports again. */
    before = s.errors;
    encoder = wgpuDeviceCreateCommandEncoder(s.device,NULL);
    compute = wgpuCommandEncoderBeginComputePass(encoder,NULL);
    commands = wgpuCommandEncoderFinish(encoder,NULL);
    assert(commands);
    expect_errors(&s,before,1,"Finish while a pass is open");
    wgpuComputePassEncoderEnd(compute);
    expect_errors(&s,before,2,"End after Finish");
    wgpuComputePassEncoderRelease(compute);
    wgpuQueueSubmit(s.queue,1,&commands);
    expect_errors(&s,before,3,"submit of the invalid command buffer");
    wgpuCommandBufferRelease(commands);
    wgpuCommandEncoderRelease(encoder);
    /* Finish with a chained descriptor still ends the encoder. */
    before = s.errors;
    encoder = wgpuDeviceCreateCommandEncoder(s.device,NULL);
    WGPUChainedStruct chain = {.sType=WGPUSType_ShaderSourceWGSL};
    WGPUCommandBufferDescriptor chained = {.nextInChain=&chain};
    commands = wgpuCommandEncoderFinish(encoder,&chained);
    assert(commands);
    expect_errors(&s,before,1,"chained Finish descriptor");
    wgpuCommandEncoderClearBuffer(encoder,bytes,0,4);
    expect_errors(&s,before,2,"clear after a chained Finish");
    wgpuCommandBufferRelease(commands);
    wgpuCommandEncoderRelease(encoder);

    /* A failed submit invalidates every command buffer of its batch, in either
     * order: resubmitting the valid buffer reports again. */
    for (uint32_t order = 0; order < 2; ++order) {
        encoder = wgpuDeviceCreateCommandEncoder(s.device,NULL);
        wgpuCommandEncoderClearBuffer(encoder,bytes,0,3); /* rejected: unaligned */
        WGPUCommandBuffer bad_buffer = wgpuCommandEncoderFinish(encoder,NULL);
        wgpuCommandEncoderRelease(encoder);
        encoder = wgpuDeviceCreateCommandEncoder(s.device,NULL);
        wgpuCommandEncoderClearBuffer(encoder,bytes,0,4);
        WGPUCommandBuffer good_buffer = wgpuCommandEncoderFinish(encoder,NULL);
        wgpuCommandEncoderRelease(encoder);
        assert(bad_buffer && good_buffer);
        before = s.errors;
        WGPUCommandBuffer batch[2] = {order ? good_buffer : bad_buffer, order ? bad_buffer : good_buffer};
        wgpuQueueSubmit(s.queue,2,batch);
        expect_errors(&s,before,1,order ? "submit [valid, invalid]" : "submit [invalid, valid]");
        wgpuQueueSubmit(s.queue,1,&good_buffer);
        expect_errors(&s,before,2,"resubmit of a buffer from a failed batch");
        wgpuCommandBufferRelease(bad_buffer);
        wgpuCommandBufferRelease(good_buffer);
    }

    /* A valid encoder still records and submits after all of the above. */
    encoder = wgpuDeviceCreateCommandEncoder(s.device,NULL);
    pass = begin_render(encoder,view,WGPUStoreOp_Store);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
    before = s.errors;
    submit(&s,encoder);
    expect_errors(&s,before,0,"valid encoder");

    /* Render bundle encoders follow the same rule: an invalid setPipeline (a
     * target-format mismatch) and a draw without a pipeline report nothing until
     * Finish, which reports one error and returns an invalid bundle. */
    WGPURenderPipeline matching = target_pipeline(&s,WGPUTextureFormat_RGBA8Unorm);
    WGPURenderPipeline mismatched = target_pipeline(&s,WGPUTextureFormat_R32Float);
    before = s.errors;
    WGPURenderBundleEncoder builder = bundle_encoder(&s);
    wgpuRenderBundleEncoderSetPipeline(builder,mismatched);
    wgpuRenderBundleEncoderDraw(builder,3,1,0,0);
    expect_errors(&s,before,0,"invalid bundle commands");
    WGPURenderBundle invalid_bundle = wgpuRenderBundleEncoderFinish(builder,NULL);
    assert(invalid_bundle);
    expect_errors(&s,before,1,"invalid bundle Finish");
    /* Commands and a second Finish on the finished bundle encoder report at the call. */
    wgpuRenderBundleEncoderSetPipeline(builder,matching);
    expect_errors(&s,before,2,"setPipeline on a finished bundle encoder");
    wgpuRenderBundleEncoderDraw(builder,3,1,0,0);
    expect_errors(&s,before,3,"draw on a finished bundle encoder");
    WGPURenderBundle again = wgpuRenderBundleEncoderFinish(builder,NULL);
    expect_errors(&s,before,4,"second bundle Finish");
    wgpuRenderBundleRelease(again);
    wgpuRenderBundleEncoderRelease(builder);
    /* An invalid bundle in an otherwise valid pass: one error, at the command
     * encoder's Finish; a valid bundle in the same kind of pass submits cleanly. */
    builder = bundle_encoder(&s);
    wgpuRenderBundleEncoderSetPipeline(builder,matching);
    wgpuRenderBundleEncoderDraw(builder,3,1,0,0);
    WGPURenderBundle valid_bundle = wgpuRenderBundleEncoderFinish(builder,NULL);
    assert(valid_bundle);
    wgpuRenderBundleEncoderRelease(builder);
    for (uint32_t which = 0; which < 2; ++which) {
        before = s.errors;
        encoder = wgpuDeviceCreateCommandEncoder(s.device,NULL);
        pass = begin_render(encoder,view,WGPUStoreOp_Store);
        wgpuRenderPassEncoderExecuteBundles(pass,1,which ? &valid_bundle : &invalid_bundle);
        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);
        if (which) {
            submit(&s,encoder);
            expect_errors(&s,before,0,"valid bundle execution");
        } else finish_rejected(&s,encoder,before,"invalid bundle execution");
    }
    /* An invalid descriptor: one error at creation and an invalid encoder whose
     * commands add nothing; Finish reports ("this must be valid") and returns an
     * invalid bundle that invalidates the pass's command encoder, whose Finish
     * reports once; a second Finish reports again. */
    before = s.errors;
    WGPURenderBundleEncoderDescriptor bad = WGPU_RENDER_BUNDLE_ENCODER_DESCRIPTOR_INIT;
    bad.depthStencilFormat = WGPUTextureFormat_RGBA8Unorm; /* a color format as depth/stencil */
    builder = wgpuDeviceCreateRenderBundleEncoder(s.device,&bad);
    assert(builder);
    expect_errors(&s,before,1,"invalid bundle encoder descriptor");
    wgpuRenderBundleEncoderSetPipeline(builder,matching);
    wgpuRenderBundleEncoderDraw(builder,3,1,0,0);
    WGPURenderBundle born_invalid = wgpuRenderBundleEncoderFinish(builder,NULL);
    assert(born_invalid);
    expect_errors(&s,before,2,"invalid-descriptor encoder Finish");
    WGPURenderBundle finished_twice = wgpuRenderBundleEncoderFinish(builder,NULL);
    expect_errors(&s,before,3,"second Finish of an invalid-descriptor encoder");
    wgpuRenderBundleRelease(finished_twice);
    wgpuRenderBundleEncoderRelease(builder);
    before = s.errors;
    encoder = wgpuDeviceCreateCommandEncoder(s.device,NULL);
    pass = begin_render(encoder,view,WGPUStoreOp_Store);
    wgpuRenderPassEncoderExecuteBundles(pass,1,&born_invalid);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
    finish_rejected(&s,encoder,before,"invalid-descriptor bundle execution");
    wgpuRenderBundleRelease(born_invalid);
    wgpuRenderBundleRelease(valid_bundle);
    wgpuRenderBundleRelease(invalid_bundle);
    wgpuRenderPipelineRelease(mismatched);
    wgpuRenderPipelineRelease(matching);

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
        "the call; render bundle encoders: invalid setPipeline/draw give one error at bundle Finish and an "
        "invalid bundle, executing it in a valid pass gives one error at the command encoder's Finish, commands "
        "and a second Finish after bundle Finish report at the call, ended passes on a valid encoder report at "
        "the call and leave it valid, End with an unlocked parent and invalid-command-buffer submits report, "
        "a chained Finish ends the encoder, an invalid bundle encoder descriptor reports "
        "at creation and at its Finish and its bundle once at the executing encoder's Finish, a failed submit "
        "invalidates its whole batch in either order; core-features-and-limits listed and "
        "accepted %s\n", adapter_class_scope());
    return 0;
}
