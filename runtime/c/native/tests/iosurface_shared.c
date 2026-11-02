/* SPDX-License-Identifier: MIT
 * Actual packed IOSurface producer bytes, native queue and newly signaled event.
 * Reuse the committed actual IOSurface fixture, not a synthetic surface. */
#include "consumer.h"
#include "doe_native_shared_memory.h"
#include "iosurface_fixture.h"
#include "../src/internal.h"
#include "../../backends/metal/src/internal.h"

static WGPURenderPipeline color_pipeline(state *s, WGPUShaderModule module,
    const char *entry, WGPUColorWriteMask mask)
{
    WGPUColorTargetState color = {.format=WGPUTextureFormat_BGRA8Unorm,.writeMask=mask};
    WGPUFragmentState fragment = {.module=module,.entryPoint={entry,WGPU_STRLEN},
        .targetCount=1,.targets=&color};
    WGPURenderPipelineDescriptor descriptor = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    descriptor.vertex = (WGPUVertexState){.module=module,.entryPoint={"vertex",WGPU_STRLEN}};
    descriptor.fragment = &fragment;
    WGPURenderPipeline pipeline = wgpuDeviceCreateRenderPipeline(s->device,&descriptor);
    assert(pipeline);
    return pipeline;
}

static void color_pass(WGPUCommandEncoder encoder, WGPUTextureView view,
    WGPURenderPipeline pipeline, WGPULoadOp load)
{
    WGPURenderPassColorAttachment color = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    color.view=view; color.loadOp=load; color.storeOp=WGPUStoreOp_Store;
    color.clearValue=(WGPUColor){0,0,1,1};
    WGPURenderPassDescriptor descriptor = {.colorAttachmentCount=1,.colorAttachments=&color};
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder,&descriptor);
    assert(pass);
    wgpuRenderPassEncoderSetPipeline(pass,pipeline);
    wgpuRenderPassEncoderDraw(pass,3,1,0,0);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
}

static WGPUSharedTextureMemory import_surface(state *s, IOSurfaceRef surface)
{
    WGPUSharedTextureMemoryIOSurfaceDescriptor native = {
        .chain={.sType=DOE_SHARED_STYPE_IOSURFACE},.ioSurface=surface};
    WGPUSharedTextureMemoryDescriptor descriptor = {.nextInChain=&native.chain};
    WGPUSharedTextureMemory memory = wgpuDeviceImportSharedTextureMemory(s->device,&descriptor);
    assert(memory);
    return memory;
}

static uint64_t committed_point(state *s)
{
    /* Test-only inspection of the accepted native queue timeline, independently
     * of EndAccess's per-texture point. No new public diagnostic/export. */
    const native_device *device = (const native_device *)s->device;
    assert(device->backend.kind==WGPUBackendType_Metal);
    return device->backend.value.metal.context->timeline_committed;
}

static void opaque_canvas_boundary(const WGPUFeatureName features[2])
{
    state producer={0},consumer={0};
    consumer_init_features(&producer,features,2);
    consumer_init_features(&consumer,features,2);
    IOSurfaceRef surface = doe_test_iosurface_create(UINT32_C(0x42475241));
    assert(surface && IOSurfaceLock(surface,0,NULL)==0);
    unsigned char *pixels=IOSurfaceGetBaseAddress(surface);
    size_t stride=IOSurfaceGetBytesPerRow(surface);
    const unsigned char red[8]={0,0,255,255,0,0,255,255};
    const unsigned char green[8]={0,255,0,255,0,255,0,255};
    assert(pixels && stride>=8);
    memcpy(pixels,red,8); memcpy(pixels+stride,green,8);
    assert(IOSurfaceUnlock(surface,0,NULL)==0);
    WGPUSharedTextureMemory produced=import_surface(&producer,surface);
    WGPUSharedTextureMemory consumed=import_surface(&consumer,surface);
    CFRelease(surface);
    WGPUTextureDescriptor td = WGPU_TEXTURE_DESCRIPTOR_INIT;
    td.format=WGPUTextureFormat_BGRA8Unorm; td.size=(WGPUExtent3D){2,2,1};
    td.usage=WGPUTextureUsage_RenderAttachment|WGPUTextureUsage_CopySrc;
    WGPUTexture output=wgpuSharedTextureMemoryCreateTexture(produced,&td);
    assert(output);
    WGPUSharedTextureMemoryBeginAccessDescriptor begin={.initialized=WGPU_TRUE};
    assert(wgpuSharedTextureMemoryBeginAccess(produced,output,&begin)==WGPUStatus_Success);
    const char *code =
        "@vertex fn vertex(@builtin(vertex_index)i:u32)->@builtin(position)vec4f{"
        "let p=array(vec2f(-1,-1),vec2f(3,-1),vec2f(-1,3));return vec4f(p[i],0,1);}"
        "@fragment fn producer(@builtin(position)p:vec4f)->@location(0)vec4f{"
        "if(p.y<1){return vec4f(1,0,0,0);}return vec4f(0,1,0,0);}"
        "@fragment fn opaque()->@location(0)vec4f{return vec4f(1,1,1,1);}";
    WGPUShaderSourceWGSL source = WGPU_SHADER_SOURCE_WGSL_INIT;
    source.code=(WGPUStringView){code,WGPU_STRLEN};
    WGPUShaderModuleDescriptor shader_desc={.nextInChain=&source.chain};
    WGPUShaderModule module=wgpuDeviceCreateShaderModule(producer.device,&shader_desc);
    assert(module);
    WGPURenderPipeline frame=color_pipeline(&producer,module,"producer",WGPUColorWriteMask_All);
    WGPURenderPipeline alpha=color_pipeline(&producer,module,"opaque",WGPUColorWriteMask_Alpha);
    wgpuShaderModuleRelease(module);
    WGPUTextureView view=wgpuTextureCreateView(output,NULL); assert(view);
    WGPUBuffer before=buffer(&producer,512,WGPUBufferUsage_CopyDst|WGPUBufferUsage_MapRead);
    WGPUCommandEncoder encoder=wgpuDeviceCreateCommandEncoder(producer.device,NULL);
    assert(encoder);
    color_pass(encoder,view,frame,WGPULoadOp_Clear);
    texture_to_buffer(encoder,output,before,0,2,2);
    submit(&producer,encoder);
    uint64_t frame_point=committed_point(&producer); assert(frame_point);
    /* Blink's opaque-canvas closure is a separate Load/Store alpha-only draw.
     * No WorkDone, MapAsync, CPU read or other host wait separates the submits. */
    encoder=wgpuDeviceCreateCommandEncoder(producer.device,NULL); assert(encoder);
    color_pass(encoder,view,alpha,WGPULoadOp_Load);
    submit(&producer,encoder);
    uint64_t alpha_point=committed_point(&producer); assert(alpha_point>frame_point);
    wgpuTextureViewRelease(view);
    wgpuRenderPipelineRelease(frame); wgpuRenderPipelineRelease(alpha);
    WGPUSharedTextureMemoryEndAccessState end={0};
    assert(wgpuSharedTextureMemoryEndAccess(produced,output,&end)==WGPUStatus_Success);
    assert(end.fenceCount==1 && end.signaledValueCount==1 && end.fences[0]);
    assert(end.signaledValues[0]==alpha_point);
    assert(end.initialized==WGPU_TRUE);
    WGPUSharedFenceMTLSharedEventExportInfo event={.chain={.sType=DOE_SHARED_STYPE_MTL_EVENT_EXPORT}};
    WGPUSharedFenceExportInfo exported={.nextInChain=&event.chain};
    wgpuSharedFenceExportInfo(end.fences[0],&exported);
    assert(exported.type==DOE_SHARED_FENCE_MTL_EVENT && event.sharedEvent);
    WGPUSharedFenceMTLSharedEventDescriptor imported_event={
        .chain={.sType=DOE_SHARED_STYPE_MTL_EVENT_DESCRIPTOR},.sharedEvent=event.sharedEvent};
    WGPUSharedFenceDescriptor fence_desc={.nextInChain=&imported_event.chain};
    WGPUSharedFence released=wgpuDeviceImportSharedFence(consumer.device,&fence_desc);
    assert(released);
    td.usage=WGPUTextureUsage_CopySrc;
    WGPUTexture input=wgpuSharedTextureMemoryCreateTexture(consumed,&td); assert(input);
    WGPUSharedTextureMemoryBeginAccessDescriptor acquire={.initialized=WGPU_TRUE,
        .fenceCount=1,.fences=&released,.signaledValueCount=1,.signaledValues=end.signaledValues};
    assert(wgpuSharedTextureMemoryBeginAccess(consumed,input,&acquire)==WGPUStatus_Success);
    wgpuSharedFenceRelease(released);
    WGPUBuffer after=buffer(&consumer,512,WGPUBufferUsage_CopyDst|WGPUBufferUsage_MapRead);
    encoder=wgpuDeviceCreateCommandEncoder(consumer.device,NULL); assert(encoder);
    texture_to_buffer(encoder,input,after,0,2,2);
    submit(&consumer,encoder);
    assert(wgpuSharedTextureMemoryEndAccess(consumed,input,NULL)==WGPUStatus_Success);
    wgpuTextureRelease(input); wgpuSharedTextureMemoryRelease(consumed);
    wgpuTextureRelease(output); wgpuSharedTextureMemoryRelease(produced);
    const unsigned char before_red[8]={0,0,255,0,0,0,255,0};
    const unsigned char before_green[8]={0,255,0,0,0,255,0,0};
    const unsigned char *bytes=map(&producer,before,512);
    assert(!memcmp(bytes,before_red,8) && !memcmp(bytes+256,before_green,8));
    wgpuBufferUnmap(before); wgpuBufferRelease(before);
    bytes=map(&consumer,after,512);
    assert(!memcmp(bytes,red,8) && !memcmp(bytes+256,green,8));
    wgpuBufferUnmap(after); wgpuBufferRelease(after);
    wgpuSharedTextureMemoryEndAccessStateFreeMembers(end);
    consumer_finish(&consumer); consumer_finish(&producer);
    puts("PASS opaque canvas: separate producer and alpha-only Load/Store submits, latest release point, independent consumer queue, exact RGB preserved and alpha0->255");
}
static void external_epoch_boundary(const WGPUFeatureName features[2])
{
    state s={0};consumer_init_features(&s,features,2);
    IOSurfaceRef surface=doe_test_iosurface_create(UINT32_C(0x42475241));assert(surface);
    WGPUSharedTextureMemory memory=import_surface(&s,surface);
    WGPUTextureDescriptor descriptor={.dimension=WGPUTextureDimension_2D,.format=WGPUTextureFormat_BGRA8Unorm,
        .size={2,2,1},.mipLevelCount=1,.sampleCount=1,.usage=WGPUTextureUsage_CopySrc};
    WGPUTexture texture=wgpuSharedTextureMemoryCreateTexture(memory,&descriptor);assert(texture);
    WGPUSharedTextureMemoryBeginAccessDescriptor discard={.initialized=WGPU_FALSE};
    assert(wgpuSharedTextureMemoryBeginAccess(memory,texture,&discard)==WGPUStatus_Success);
    WGPUSharedTextureMemoryEndAccessState idle={0};
    assert(wgpuSharedTextureMemoryEndAccess(memory,texture,&idle)==WGPUStatus_Success);
    assert(idle.initialized==WGPU_FALSE && idle.fenceCount==1 && idle.signaledValueCount==1);
    /* External CPU producer owns the surface between accesses. No GPU texture
     * work occurred in the discarded access; publish new valid, nonzero pixels. */
    const unsigned char blue[8]={255,0,0,255,255,0,0,255};
    const unsigned char yellow[8]={0,255,255,255,0,255,255,255};
    assert(IOSurfaceLock(surface,0,NULL)==0);
    unsigned char *pixels=IOSurfaceGetBaseAddress(surface);
    size_t stride=IOSurfaceGetBytesPerRow(surface);assert(pixels && stride>=8);
    memcpy(pixels,blue,8);memcpy(pixels+stride,yellow,8);
    assert(IOSurfaceUnlock(surface,0,NULL)==0);CFRelease(surface);
    WGPUSharedTextureMemoryBeginAccessDescriptor produced={.initialized=WGPU_TRUE,
        .fenceCount=idle.fenceCount,.fences=idle.fences,
        .signaledValueCount=idle.signaledValueCount,.signaledValues=idle.signaledValues};
    assert(wgpuSharedTextureMemoryBeginAccess(memory,texture,&produced)==WGPUStatus_Success);
    wgpuSharedTextureMemoryEndAccessStateFreeMembers(idle);
    WGPUBuffer readback=buffer(&s,512,WGPUBufferUsage_CopyDst|WGPUBufferUsage_MapRead);
    WGPUCommandEncoder encoder=wgpuDeviceCreateCommandEncoder(s.device,NULL);assert(encoder);
    texture_to_buffer(encoder,texture,readback,0,2,2);submit(&s,encoder);
    WGPUSharedTextureMemoryEndAccessState end={0};
    assert(wgpuSharedTextureMemoryEndAccess(memory,texture,&end)==WGPUStatus_Success);
    assert(end.initialized==WGPU_TRUE);
    wgpuTextureRelease(texture);wgpuSharedTextureMemoryRelease(memory);
    const unsigned char *bytes=map(&s,readback,512);
    assert(!memcmp(bytes,blue,8) && !memcmp(bytes+256,yellow,8));
    wgpuBufferUnmap(readback);wgpuBufferRelease(readback);
    wgpuSharedTextureMemoryEndAccessStateFreeMembers(end);consumer_finish(&s);
    puts("PASS external epoch: same discarded texture accepts initialized producer bytes without zeroing them");
}

int main(void)
{
    const WGPUFeatureName features[]={(WGPUFeatureName)DOE_SHARED_FEATURE_IOSURFACE,
        (WGPUFeatureName)DOE_SHARED_FEATURE_MTL_EVENT};
    state s={0};consumer_init_features(&s,features,2);
    for(size_t i=0;i<2;++i)assert(wgpuAdapterHasFeature(s.adapter,features[i]) && wgpuDeviceHasFeature(s.device,features[i]));
    IOSurfaceRef surface=doe_test_iosurface_create(UINT32_C(0x42475241));assert(surface);
    assert(IOSurfaceLock(surface,0,NULL)==0);
    unsigned char *pixels=IOSurfaceGetBaseAddress(surface);size_t stride=IOSurfaceGetBytesPerRow(surface);
    assert(pixels && stride>=8);
    const unsigned char red[8]={0,0,255,255,0,0,255,255},green[8]={0,255,0,255,0,255,0,255};
    memcpy(pixels,red,8);memcpy(pixels+stride,green,8);
    assert(IOSurfaceUnlock(surface,0,NULL)==0);
    WGPUSharedTextureMemoryIOSurfaceDescriptor native={.chain={.sType=DOE_SHARED_STYPE_IOSURFACE},.ioSurface=surface};
    WGPUSharedTextureMemoryDescriptor import={.nextInChain=&native.chain};
    WGPUSharedTextureMemory memory=wgpuDeviceImportSharedTextureMemory(s.device,&import);assert(memory);
    WGPUSharedTextureMemoryProperties properties={0};assert(wgpuSharedTextureMemoryGetProperties(memory,&properties)==WGPUStatus_Success);
    assert(properties.format==WGPUTextureFormat_BGRA8Unorm && properties.size.width==2 && properties.size.height==2);
    WGPUTextureDescriptor td={.dimension=WGPUTextureDimension_2D,.format=properties.format,.size=properties.size,.mipLevelCount=1,.sampleCount=1,.usage=WGPUTextureUsage_CopySrc|WGPUTextureUsage_TextureBinding};
    WGPUTexture texture=wgpuSharedTextureMemoryCreateTexture(memory,&td);assert(texture);
    WGPUSharedTextureMemoryBeginAccessDescriptor begin={.initialized=WGPU_TRUE};
    assert(wgpuSharedTextureMemoryBeginAccess(memory,texture,&begin)==WGPUStatus_Success);
    puts("CHECKPOINT imported real producer surface and began access");fflush(stdout);
    WGPUBufferDescriptor bd={.size=512,.usage=WGPUBufferUsage_CopyDst|WGPUBufferUsage_MapRead};
    WGPUBuffer readback=wgpuDeviceCreateBuffer(s.device,&bd);assert(readback);
    WGPUCommandEncoder encoder=wgpuDeviceCreateCommandEncoder(s.device,NULL);assert(encoder);
    WGPUTexelCopyTextureInfo source={.texture=texture,.aspect=WGPUTextureAspect_All};
    WGPUTexelCopyBufferInfo destination={.buffer=readback,.layout={.bytesPerRow=256,.rowsPerImage=2}};
    WGPUExtent3D extent={2,2,1};wgpuCommandEncoderCopyTextureToBuffer(encoder,&source,&destination,&extent);
    WGPUCommandBuffer command=wgpuCommandEncoderFinish(encoder,NULL);assert(command);wgpuCommandEncoderRelease(encoder);
    wgpuQueueSubmit(s.queue,1,&command);wgpuCommandBufferRelease(command);
    puts("CHECKPOINT submitted original imported texture readback");fflush(stdout);
    WGPUSharedTextureMemoryEndAccessState end={0};assert(wgpuSharedTextureMemoryEndAccess(memory,texture,&end)==WGPUStatus_Success);
    /* The release point is the queue timeline value after the readback's command
     * buffer, exported without waiting or new GPU work. */
    assert(end.fenceCount==1 && end.signaledValueCount==1 && end.fences[0]);
    WGPUSharedFenceMTLSharedEventExportInfo event={.chain={.sType=DOE_SHARED_STYPE_MTL_EVENT_EXPORT}};
    WGPUSharedFenceExportInfo exported={.nextInChain=&event.chain};wgpuSharedFenceExportInfo(end.fences[0],&exported);
    assert(exported.type==DOE_SHARED_FENCE_MTL_EVENT && event.sharedEvent);
    wgpuTextureRelease(texture);
    /* A consumer waiting on that exported point observes the producer's work:
     * the next access queues its dependency on that value. An access
     * that begins uninitialized reads zeros, not the earlier surface contents. */
    WGPUSharedFenceMTLSharedEventDescriptor imported_event={.chain={.sType=DOE_SHARED_STYPE_MTL_EVENT_DESCRIPTOR},.sharedEvent=event.sharedEvent};
    WGPUSharedFenceDescriptor fence_desc={.nextInChain=&imported_event.chain};
    WGPUSharedFence released=wgpuDeviceImportSharedFence(s.device,&fence_desc);assert(released);
    td.usage=WGPUTextureUsage_CopySrc|WGPUTextureUsage_RenderAttachment;
    WGPUTexture cleared=wgpuSharedTextureMemoryCreateTexture(memory,&td);assert(cleared);
    WGPUSharedTextureMemoryBeginAccessDescriptor after={.initialized=WGPU_FALSE,.fenceCount=1,.fences=&released,
        .signaledValueCount=1,.signaledValues=end.signaledValues};
    uint64_t idle_point=committed_point(&s);
    assert(wgpuSharedTextureMemoryBeginAccess(memory,cleared,&after)==WGPUStatus_Success);
    wgpuSharedFenceRelease(released);
    /* Begin may defer its producer dependency until End. Across the complete
     * no-work access exactly one wait-only submission forwards that point. */
    WGPUSharedTextureMemoryEndAccessState idle={0};
    assert(wgpuSharedTextureMemoryEndAccess(memory,cleared,&idle)==WGPUStatus_Success);
    assert(idle.initialized==WGPU_FALSE && idle.fenceCount==1 && idle.signaledValueCount==1);
    assert(committed_point(&s)==idle_point+1 && idle.signaledValues[0]==idle_point+1);
    /* A clear hidden inside the dependency command would still satisfy the
     * submission count. Settle that command, then independently inspect the
     * original producer bytes before any first-use initialization is allowed. */
    work_done(&s);
    assert(IOSurfaceLock(surface,0,NULL)==0);
    pixels=IOSurfaceGetBaseAddress(surface);stride=IOSurfaceGetBytesPerRow(surface);
    assert(pixels && stride>=8 && !memcmp(pixels,red,8) && !memcmp(pixels+stride,green,8));
    assert(IOSurfaceUnlock(surface,0,NULL)==0);CFRelease(surface);
    WGPUSharedTextureMemoryBeginAccessDescriptor resume={.initialized=idle.initialized,
        .fenceCount=idle.fenceCount,.fences=idle.fences,
        .signaledValueCount=idle.signaledValueCount,.signaledValues=idle.signaledValues};
    assert(wgpuSharedTextureMemoryBeginAccess(memory,cleared,&resume)==WGPUStatus_Success);
    wgpuSharedTextureMemoryEndAccessStateFreeMembers(idle);
    WGPUBuffer zeros=wgpuDeviceCreateBuffer(s.device,&bd);assert(zeros);
    encoder=wgpuDeviceCreateCommandEncoder(s.device,NULL);
    source.texture=cleared;destination.buffer=zeros;
    wgpuCommandEncoderCopyTextureToBuffer(encoder,&source,&destination,&extent);
    command=wgpuCommandEncoderFinish(encoder,NULL);assert(command);wgpuCommandEncoderRelease(encoder);
    wgpuQueueSubmit(s.queue,1,&command);wgpuCommandBufferRelease(command);
    assert(wgpuSharedTextureMemoryEndAccess(memory,cleared,NULL)==WGPUStatus_Success);
    wgpuTextureRelease(cleared);wgpuSharedTextureMemoryRelease(memory);
    work_done(&s);
    WGPUBufferMapCallbackInfo callback={.mode=WGPUCallbackMode_AllowSpontaneous,.callback=mapped,.userdata1=&s};
    WGPUFuture future=wgpuBufferMapAsync(readback,WGPUMapMode_Read,0,512,callback);
    wait_future(&s,future);assert(s.mapped==1);
    const unsigned char *bytes=wgpuBufferGetConstMappedRange(readback,0,512);assert(bytes);
    assert(!memcmp(bytes,red,8) && !memcmp(bytes+256,green,8));
    wgpuBufferUnmap(readback);wgpuBufferRelease(readback);
    s.mapped=0;
    future=wgpuBufferMapAsync(zeros,WGPUMapMode_Read,0,512,callback);
    wait_future(&s,future);assert(s.mapped==1);
    const unsigned char none[8]={0};bytes=wgpuBufferGetConstMappedRange(zeros,0,512);assert(bytes);
    assert(!memcmp(bytes,none,8) && !memcmp(bytes+256,none,8));
    wgpuBufferUnmap(zeros);wgpuBufferRelease(zeros);
    wgpuSharedTextureMemoryEndAccessStateFreeMembers(end);
    opaque_canvas_boundary(features);
    external_epoch_boundary(features);
    consumer_finish(&s);
    printf("PASS actual IOSurface producer red/green BGRA pixels, Begin/End, queue readback, exported queue-timeline release point honored by a later access, uninitialized access zeroed, and early native/public releases %s\n",adapter_class_scope());
    return 0;
}
