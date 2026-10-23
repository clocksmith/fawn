/* SPDX-License-Identifier: MIT
 * Public depth/stencil texel copies: rendered depth32float/depth16unorm readback,
 * stencil readback after stencil operations, depth16unorm/stencil8 uploads,
 * all-aspect texture copies (including uncopyable-to-buffer depth24plus checked
 * by a depth test) and wrong-but-typed format/aspect/usage/layout controls.
 * Every 4x2 target splits at x=0 into a left half (pixels 0,1) and right half. */
#include "consumer.h"

enum { WIDTH = 4, HEIGHT = 2, SLOT = 512, SLOTS = 10 };
typedef struct fixture {
    WGPUShaderModule module;
    WGPUBuffer vertices, readback, source;
} fixture;

static WGPUTexture image(state *s, WGPUTextureFormat format, WGPUTextureUsage usage)
{
    WGPUTextureDescriptor descriptor = WGPU_TEXTURE_DESCRIPTOR_INIT;
    descriptor.format = format;
    descriptor.usage = usage;
    descriptor.size = (WGPUExtent3D){WIDTH,HEIGHT,1};
    WGPUTexture result = wgpuDeviceCreateTexture(s->device,&descriptor);
    assert(result);
    return result;
}
static WGPURenderPipeline pipeline_for(state *s, const fixture *f, WGPUDepthStencilState *depth, bool color)
{
    WGPUVertexAttribute attribute = {.format=WGPUVertexFormat_Float32x4,.shaderLocation=0};
    WGPUVertexBufferLayout layout = {.arrayStride=16,.attributeCount=1,.attributes=&attribute};
    WGPUColorTargetState target = {.format=WGPUTextureFormat_RGBA8Unorm,.writeMask=WGPUColorWriteMask_All};
    WGPUFragmentState fragment = {.module=f->module,.targetCount=1,.targets=&target};
    WGPURenderPipelineDescriptor descriptor = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    descriptor.vertex = (WGPUVertexState){.module=f->module,.entryPoint={"vertex",WGPU_STRLEN},
        .bufferCount=1,.buffers=&layout};
    if (color) {
        fragment.entryPoint = (WGPUStringView){"fragment",WGPU_STRLEN};
        descriptor.fragment = &fragment;
    }
    descriptor.depthStencil = depth;
    WGPURenderPipeline result = wgpuDeviceCreateRenderPipeline(s->device,&descriptor);
    assert(result);
    return result;
}
/* Vertex-only depth writer: compare Always, so each draw writes its constant z. */
static WGPURenderPipeline depth_writer(state *s, const fixture *f, WGPUTextureFormat format)
{
    WGPUDepthStencilState depth = WGPU_DEPTH_STENCIL_STATE_INIT;
    depth.format = format;
    depth.depthWriteEnabled = WGPUOptionalBool_True;
    depth.depthCompare = WGPUCompareFunction_Always;
    return pipeline_for(s,f,&depth,false);
}
static WGPURenderPipeline stencil_writer(state *s, const fixture *f, WGPUTextureFormat format, WGPUStencilOperation op)
{
    WGPUDepthStencilState depth = WGPU_DEPTH_STENCIL_STATE_INIT;
    depth.format = format;
    if (format != WGPUTextureFormat_Stencil8) {
        depth.depthWriteEnabled = WGPUOptionalBool_False;
        depth.depthCompare = WGPUCompareFunction_Always;
    }
    depth.stencilFront.compare = depth.stencilBack.compare = WGPUCompareFunction_Always;
    depth.stencilFront.passOp = depth.stencilBack.passOp = op;
    return pipeline_for(s,f,&depth,false);
}
static WGPURenderPassEncoder depth_pass(WGPUCommandEncoder encoder, WGPUTextureView view,
    bool depth, WGPULoadOp depth_load, bool stencil, WGPULoadOp stencil_load, uint32_t stencil_clear)
{
    WGPURenderPassDepthStencilAttachment attachment = WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
    attachment.view = view;
    if (depth) {
        attachment.depthLoadOp = depth_load;
        attachment.depthStoreOp = WGPUStoreOp_Store;
        attachment.depthClearValue = 1;
    }
    if (stencil) {
        attachment.stencilLoadOp = stencil_load;
        attachment.stencilStoreOp = WGPUStoreOp_Store;
        attachment.stencilClearValue = stencil_clear;
    }
    WGPURenderPassDescriptor descriptor = {.depthStencilAttachment=&attachment};
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder,&descriptor);
    assert(pass);
    return pass;
}
static void quad(WGPURenderPassEncoder pass, WGPURenderPipeline pipeline, const fixture *f, uint32_t first)
{
    wgpuRenderPassEncoderSetPipeline(pass,pipeline);
    wgpuRenderPassEncoderSetVertexBuffer(pass,0,f->vertices,0,WGPU_WHOLE_SIZE);
    wgpuRenderPassEncoderDraw(pass,6,1,first,0);
}
static void finish(WGPURenderPassEncoder pass)
{
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
}
static void read_aspect(WGPUCommandEncoder encoder, const fixture *f, WGPUTexture texture,
    WGPUTextureAspect aspect, uint32_t slot)
{
    WGPUTexelCopyTextureInfo source = {.texture=texture,.aspect=aspect};
    WGPUTexelCopyBufferInfo destination = {.buffer=f->readback,
        .layout={.offset=slot * SLOT,.bytesPerRow=256,.rowsPerImage=HEIGHT}};
    WGPUExtent3D size = {WIDTH,HEIGHT,1};
    wgpuCommandEncoderCopyTextureToBuffer(encoder,&source,&destination,&size);
}
static void write_aspect(state *s, WGPUTexture texture, WGPUTextureAspect aspect, const void *data, uint32_t texel)
{
    WGPUTexelCopyTextureInfo destination = {.texture=texture,.aspect=aspect};
    WGPUTexelCopyBufferLayout layout = {.bytesPerRow=WIDTH * texel,.rowsPerImage=HEIGHT};
    WGPUExtent3D size = {WIDTH,HEIGHT,1};
    wgpuQueueWriteTexture(s->queue,&destination,data,WIDTH * HEIGHT * texel,&layout,&size);
}
static void copy_all(WGPUCommandEncoder encoder, WGPUTexture source, WGPUTexture destination)
{
    WGPUTexelCopyTextureInfo from = {.texture=source,.aspect=WGPUTextureAspect_All};
    WGPUTexelCopyTextureInfo to = {.texture=destination,.aspect=WGPUTextureAspect_All};
    WGPUExtent3D size = {WIDTH,HEIGHT,1};
    wgpuCommandEncoderCopyTextureToTexture(encoder,&from,&to,&size);
}
static WGPUTextureView view_of(WGPUTexture texture)
{
    WGPUTextureView view = wgpuTextureCreateView(texture,NULL);
    assert(view);
    return view;
}

/* Expect exactly one validation error from one public operation. */
typedef enum control {
    D24_TO_BUFFER, D32_FROM_BUFFER, D32_WRITE, D24S8_DEPTH_ASPECT, D24S8_ALL_ASPECT, D32_PARTIAL,
    S8_OFFSET, D32_NO_COPY_SRC, D24S8_STENCIL_T2T, D32_STENCIL_ASPECT, D16_PARTIAL_WRITE, CONTROL_COUNT
} control;
static void rejected(state *s, const fixture *f, control which)
{
    WGPUTextureFormat format = which == D24_TO_BUFFER ? WGPUTextureFormat_Depth24Plus :
        which == D24S8_DEPTH_ASPECT || which == D24S8_ALL_ASPECT || which == D24S8_STENCIL_T2T ?
            WGPUTextureFormat_Depth24PlusStencil8 :
        which == S8_OFFSET ? WGPUTextureFormat_Stencil8 :
        which == D16_PARTIAL_WRITE ? WGPUTextureFormat_Depth16Unorm : WGPUTextureFormat_Depth32Float;
    WGPUTextureUsage usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopyDst |
        (which == D32_NO_COPY_SRC ? 0 : WGPUTextureUsage_CopySrc);
    WGPUTexture texture = image(s,format,usage);
    WGPUTexture other = which == D24S8_STENCIL_T2T ? image(s,format,usage) : NULL;
    const uint8_t data[64] = {0};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device,NULL);
    WGPUExtent3D size = {WIDTH,HEIGHT,1};
    wgpuDevicePushErrorScope(s->device,WGPUErrorFilter_Validation);
    switch (which) {
    case D24_TO_BUFFER: read_aspect(encoder,f,texture,WGPUTextureAspect_All,0); break;
    case D32_FROM_BUFFER: {
        WGPUTexelCopyBufferInfo source = {.buffer=f->source,.layout={.bytesPerRow=256,.rowsPerImage=HEIGHT}};
        WGPUTexelCopyTextureInfo destination = {.texture=texture,.aspect=WGPUTextureAspect_DepthOnly};
        wgpuCommandEncoderCopyBufferToTexture(encoder,&source,&destination,&size);
        break;
    }
    case D32_WRITE: write_aspect(s,texture,WGPUTextureAspect_DepthOnly,data,4); break;
    case D24S8_DEPTH_ASPECT: read_aspect(encoder,f,texture,WGPUTextureAspect_DepthOnly,0); break;
    case D24S8_ALL_ASPECT: read_aspect(encoder,f,texture,WGPUTextureAspect_All,0); break;
    case D32_PARTIAL: {
        WGPUTexelCopyTextureInfo source = {.texture=texture,.aspect=WGPUTextureAspect_DepthOnly};
        WGPUTexelCopyBufferInfo destination = {.buffer=f->readback,.layout={.bytesPerRow=256,.rowsPerImage=HEIGHT}};
        WGPUExtent3D half = {WIDTH / 2,HEIGHT,1};
        wgpuCommandEncoderCopyTextureToBuffer(encoder,&source,&destination,&half);
        break;
    }
    case S8_OFFSET: {
        WGPUTexelCopyBufferInfo source = {.buffer=f->source,.layout={.offset=2,.bytesPerRow=256,.rowsPerImage=HEIGHT}};
        WGPUTexelCopyTextureInfo destination = {.texture=texture,.aspect=WGPUTextureAspect_All};
        wgpuCommandEncoderCopyBufferToTexture(encoder,&source,&destination,&size);
        break;
    }
    case D32_NO_COPY_SRC: read_aspect(encoder,f,texture,WGPUTextureAspect_DepthOnly,0); break;
    case D24S8_STENCIL_T2T: {
        WGPUTexelCopyTextureInfo from = {.texture=texture,.aspect=WGPUTextureAspect_StencilOnly};
        WGPUTexelCopyTextureInfo to = {.texture=other,.aspect=WGPUTextureAspect_StencilOnly};
        wgpuCommandEncoderCopyTextureToTexture(encoder,&from,&to,&size);
        break;
    }
    case D32_STENCIL_ASPECT: read_aspect(encoder,f,texture,WGPUTextureAspect_StencilOnly,0); break;
    case D16_PARTIAL_WRITE: {
        WGPUTexelCopyTextureInfo destination = {.texture=texture,.aspect=WGPUTextureAspect_DepthOnly};
        WGPUTexelCopyBufferLayout layout = {.bytesPerRow=4,.rowsPerImage=HEIGHT};
        WGPUExtent3D half = {WIDTH / 2,HEIGHT,1};
        wgpuQueueWriteTexture(s->queue,&destination,data,sizeof(data),&layout,&half);
        break;
    }
    case CONTROL_COUNT: assert(false);
    }
    bool queue_write = which == D32_WRITE || which == D16_PARTIAL_WRITE;
    if (queue_write) expect_validation(s);
    WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder,NULL);
    assert(queue_write ? commands != NULL : commands == NULL);
    if (!queue_write) expect_validation(s);
    wgpuCommandBufferRelease(commands);
    wgpuCommandEncoderRelease(encoder);
    wgpuTextureRelease(texture);
    if (other) wgpuTextureRelease(other);
}
static void creation_controls(state *s)
{
    /* Depth storage remains unsupported and fails visibly (sampling is admitted;
     * see depth_sampling.c). */
    const WGPUTextureUsage usages[] = {WGPUTextureUsage_StorageBinding | WGPUTextureUsage_CopySrc};
    for (size_t i = 0; i < sizeof(usages) / sizeof(*usages); ++i) {
        WGPUTextureDescriptor descriptor = WGPU_TEXTURE_DESCRIPTOR_INIT;
        descriptor.format = WGPUTextureFormat_Depth32Float;
        descriptor.usage = usages[i];
        descriptor.size = (WGPUExtent3D){WIDTH,HEIGHT,1};
        wgpuDevicePushErrorScope(s->device,WGPUErrorFilter_Validation);
        assert(!wgpuDeviceCreateTexture(s->device,&descriptor));
        expect_validation(s);
    }
}
static void build(state *s, fixture *f)
{
    WGPUShaderSourceWGSL source = WGPU_SHADER_SOURCE_WGSL_INIT;
    source.code = (WGPUStringView){
        "@vertex fn vertex(@location(0) position:vec4f)->@builtin(position) vec4f{return position;}"
        "@fragment fn fragment()->@location(0) vec4f{return vec4f(1.0,0.0,0.0,1.0);}",WGPU_STRLEN};
    WGPUShaderModuleDescriptor shader = {.nextInChain=&source.chain};
    f->module = wgpuDeviceCreateShaderModule(s->device,&shader);
    assert(f->module);
    /* Quads: 0 left z.25, 6 right z.75, 12 left z0, 18 full z.5. */
    const float spans[4][3] = {{-1,0,0.25f},{0,1,0.75f},{-1,0,0},{-1,1,0.5f}};
    float data[24][4];
    for (size_t q = 0; q < 4; ++q) {
        const float x0 = spans[q][0], x1 = spans[q][1], z = spans[q][2];
        const float corners[6][2] = {{x0,-1},{x1,-1},{x0,1},{x1,-1},{x1,1},{x0,1}};
        for (size_t v = 0; v < 6; ++v) {
            float *out = data[q * 6 + v];
            out[0] = corners[v][0];
            out[1] = corners[v][1];
            out[2] = z;
            out[3] = 1;
        }
    }
    f->vertices = buffer(s,sizeof(data),WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst);
    wgpuQueueWriteBuffer(s->queue,f->vertices,0,data,sizeof(data));
    f->readback = buffer(s,SLOTS * SLOT,WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst);
    f->source = buffer(s,SLOT,WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst);
}
int main(void)
{
    state s = {0};
    consumer_init(&s);
    fixture f = {0};
    build(&s,&f);
    creation_controls(&s);
    for (int which = 0; which < CONTROL_COUNT; ++which) rejected(&s,&f,(control)which);

    const WGPUTextureUsage attach_copy = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc;
    const WGPUTextureUsage all = attach_copy | WGPUTextureUsage_CopyDst;
    /* The integration consumer's exact shape: depth32float attachment with CopySrc. */
    WGPUTexture d32 = image(&s,WGPUTextureFormat_Depth32Float,attach_copy);
    WGPUTexture d32_copy = image(&s,WGPUTextureFormat_Depth32Float,WGPUTextureUsage_CopySrc | WGPUTextureUsage_CopyDst);
    WGPUTexture d16 = image(&s,WGPUTextureFormat_Depth16Unorm,all);
    WGPUTexture d16_written = image(&s,WGPUTextureFormat_Depth16Unorm,WGPUTextureUsage_CopySrc | WGPUTextureUsage_CopyDst);
    WGPUTexture d16_uploaded = image(&s,WGPUTextureFormat_Depth16Unorm,WGPUTextureUsage_CopySrc | WGPUTextureUsage_CopyDst);
    WGPUTexture d16_copy = image(&s,WGPUTextureFormat_Depth16Unorm,WGPUTextureUsage_CopySrc | WGPUTextureUsage_CopyDst);
    WGPUTexture s8 = image(&s,WGPUTextureFormat_Stencil8,all);
    WGPUTexture d24s8 = image(&s,WGPUTextureFormat_Depth24PlusStencil8,attach_copy);
    WGPUTexture d24s8_written = image(&s,WGPUTextureFormat_Depth24PlusStencil8,
        WGPUTextureUsage_CopySrc | WGPUTextureUsage_CopyDst);
    WGPUTexture d24 = image(&s,WGPUTextureFormat_Depth24Plus,attach_copy);
    WGPUTexture d24_copy = image(&s,WGPUTextureFormat_Depth24Plus,WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopyDst);
    WGPUTexture color = image(&s,WGPUTextureFormat_RGBA8Unorm,WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc);

    /* Queue uploads execute before the later submission. */
    const uint16_t written16[8] = {0,1,2,3,1000,30000,65534,65535};
    write_aspect(&s,d16_written,WGPUTextureAspect_DepthOnly,written16,2);
    const uint8_t written8[8] = {10,20,30,40,50,60,70,80};
    write_aspect(&s,s8,WGPUTextureAspect_All,written8,1);
    const uint8_t stencil_aspect[8] = {1,2,3,4,5,6,7,8};
    write_aspect(&s,d24s8_written,WGPUTextureAspect_StencilOnly,stencil_aspect,1);
    /* copyBufferToTexture source rows use the WebGPU 256-byte pitch. */
    uint8_t upload[SLOT] = {0};
    const uint16_t uploaded16[8] = {5,6,7,8,9,10,11,12};
    memcpy(upload,uploaded16,8);
    memcpy(upload + 256,uploaded16 + 4,8);
    WGPUBuffer staging = buffer(&s,SLOT,WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst);
    wgpuQueueWriteBuffer(s.queue,staging,0,upload,sizeof(upload));

    WGPURenderPipeline d32_writer = depth_writer(&s,&f,WGPUTextureFormat_Depth32Float);
    WGPURenderPipeline d16_writer = depth_writer(&s,&f,WGPUTextureFormat_Depth16Unorm);
    WGPURenderPipeline d24_writer = depth_writer(&s,&f,WGPUTextureFormat_Depth24Plus);
    WGPURenderPipeline s8_increment = stencil_writer(&s,&f,WGPUTextureFormat_Stencil8,WGPUStencilOperation_IncrementClamp);
    WGPURenderPipeline d24s8_replace = stencil_writer(&s,&f,WGPUTextureFormat_Depth24PlusStencil8,WGPUStencilOperation_Replace);
    WGPUDepthStencilState tester_state = WGPU_DEPTH_STENCIL_STATE_INIT;
    tester_state.format = WGPUTextureFormat_Depth24Plus;
    tester_state.depthWriteEnabled = WGPUOptionalBool_False;
    tester_state.depthCompare = WGPUCompareFunction_Less;
    WGPURenderPipeline d24_tester = pipeline_for(&s,&f,&tester_state,true);

    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s.device,NULL);
    WGPUTextureView view = view_of(d32);
    WGPURenderPassEncoder pass = depth_pass(encoder,view,true,WGPULoadOp_Clear,false,WGPULoadOp_Undefined,0);
    quad(pass,d32_writer,&f,0);
    quad(pass,d32_writer,&f,6);
    finish(pass);
    wgpuTextureViewRelease(view);
    read_aspect(encoder,&f,d32,WGPUTextureAspect_DepthOnly,0);
    read_aspect(encoder,&f,d32,WGPUTextureAspect_All,1);
    copy_all(encoder,d32,d32_copy);
    read_aspect(encoder,&f,d32_copy,WGPUTextureAspect_All,8);

    view = view_of(d16);
    pass = depth_pass(encoder,view,true,WGPULoadOp_Clear,false,WGPULoadOp_Undefined,0);
    quad(pass,d16_writer,&f,12);
    finish(pass);
    wgpuTextureViewRelease(view);
    read_aspect(encoder,&f,d16,WGPUTextureAspect_DepthOnly,3);
    read_aspect(encoder,&f,d16_written,WGPUTextureAspect_All,2);
    WGPUTexelCopyBufferInfo upload_source = {.buffer=staging,.layout={.bytesPerRow=256,.rowsPerImage=HEIGHT}};
    WGPUTexelCopyTextureInfo upload_destination = {.texture=d16_uploaded,.aspect=WGPUTextureAspect_DepthOnly};
    WGPUExtent3D size = {WIDTH,HEIGHT,1};
    wgpuCommandEncoderCopyBufferToTexture(encoder,&upload_source,&upload_destination,&size);
    copy_all(encoder,d16_uploaded,d16_copy);
    read_aspect(encoder,&f,d16_copy,WGPUTextureAspect_DepthOnly,4);

    view = view_of(s8);
    pass = depth_pass(encoder,view,false,WGPULoadOp_Undefined,true,WGPULoadOp_Load,0);
    quad(pass,s8_increment,&f,0);
    finish(pass);
    wgpuTextureViewRelease(view);
    read_aspect(encoder,&f,s8,WGPUTextureAspect_StencilOnly,5);

    view = view_of(d24s8);
    pass = depth_pass(encoder,view,true,WGPULoadOp_Clear,true,WGPULoadOp_Clear,3);
    wgpuRenderPassEncoderSetStencilReference(pass,7);
    quad(pass,d24s8_replace,&f,0);
    finish(pass);
    wgpuTextureViewRelease(view);
    read_aspect(encoder,&f,d24s8,WGPUTextureAspect_StencilOnly,6);
    read_aspect(encoder,&f,d24s8_written,WGPUTextureAspect_StencilOnly,7);

    /* depth24plus cannot reach a buffer; its all-aspect copy is observed by a
     * depth test on the destination: z.5 passes only where the copy holds .75. */
    view = view_of(d24);
    pass = depth_pass(encoder,view,true,WGPULoadOp_Clear,false,WGPULoadOp_Undefined,0);
    quad(pass,d24_writer,&f,0);
    quad(pass,d24_writer,&f,6);
    finish(pass);
    wgpuTextureViewRelease(view);
    copy_all(encoder,d24,d24_copy);
    WGPUTextureView color_view = view_of(color);
    WGPUTextureView tested = view_of(d24_copy);
    WGPURenderPassColorAttachment color_attachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    color_attachment.view = color_view;
    color_attachment.loadOp = WGPULoadOp_Clear;
    color_attachment.storeOp = WGPUStoreOp_Store;
    color_attachment.clearValue = (WGPUColor){0,0,1,1};
    WGPURenderPassDepthStencilAttachment depth_attachment = WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
    depth_attachment.view = tested;
    depth_attachment.depthLoadOp = WGPULoadOp_Load;
    depth_attachment.depthStoreOp = WGPUStoreOp_Store;
    WGPURenderPassDescriptor tested_pass = {.colorAttachmentCount=1,.colorAttachments=&color_attachment,
        .depthStencilAttachment=&depth_attachment};
    pass = wgpuCommandEncoderBeginRenderPass(encoder,&tested_pass);
    assert(pass);
    quad(pass,d24_tester,&f,18);
    finish(pass);
    wgpuTextureViewRelease(color_view);
    wgpuTextureViewRelease(tested);
    WGPUTexelCopyTextureInfo color_source = {.texture=color};
    WGPUTexelCopyBufferInfo color_destination = {.buffer=f.readback,
        .layout={.offset=9 * SLOT,.bytesPerRow=256,.rowsPerImage=HEIGHT}};
    wgpuCommandEncoderCopyTextureToBuffer(encoder,&color_source,&color_destination,&size);

    /* Early caller releases; the submission owns every resource until retirement. */
    WGPUTexture textures[] = {d32,d32_copy,d16,d16_written,d16_uploaded,d16_copy,s8,d24s8,d24s8_written,d24,d24_copy,color};
    for (size_t i = 0; i < sizeof(textures) / sizeof(*textures); ++i) wgpuTextureRelease(textures[i]);
    WGPURenderPipeline pipelines[] = {d32_writer,d16_writer,d24_writer,s8_increment,d24s8_replace,d24_tester};
    for (size_t i = 0; i < sizeof(pipelines) / sizeof(*pipelines); ++i) wgpuRenderPipelineRelease(pipelines[i]);
    wgpuBufferRelease(staging);
    submit(&s,encoder);

    const unsigned char *bytes = map(&s,f.readback,SLOTS * SLOT);
    for (size_t y = 0; y < HEIGHT; ++y) for (size_t x = 0; x < WIDTH; ++x) {
        size_t row = y * 256;
        bool left = x < 2;
        float depth32;
        for (uint32_t slot = 0; slot < 9; slot += 1) {
            if (slot != 0 && slot != 1 && slot != 8) continue;
            memcpy(&depth32,bytes + slot * SLOT + row + x * 4,4);
            assert(depth32 == (left ? 0.25f : 0.75f));
        }
        uint16_t depth16;
        memcpy(&depth16,bytes + 2 * SLOT + row + x * 2,2);
        assert(depth16 == written16[y * WIDTH + x]);
        memcpy(&depth16,bytes + 3 * SLOT + row + x * 2,2);
        assert(depth16 == (left ? 0 : 65535));
        memcpy(&depth16,bytes + 4 * SLOT + row + x * 2,2);
        assert(depth16 == uploaded16[y * WIDTH + x]);
        assert(bytes[5 * SLOT + row + x] == written8[y * WIDTH + x] + (left ? 1 : 0));
        assert(bytes[6 * SLOT + row + x] == (left ? 7 : 3));
        assert(bytes[7 * SLOT + row + x] == stencil_aspect[y * WIDTH + x]);
        const unsigned char red[4] = {255,0,0,255}, blue[4] = {0,0,255,255};
        assert(!memcmp(bytes + 9 * SLOT + row + x * 4,left ? blue : red,4));
    }
    wgpuBufferUnmap(f.readback);
    wgpuBufferRelease(f.readback);
    wgpuBufferRelease(f.vertices);
    wgpuBufferRelease(f.source);
    wgpuShaderModuleRelease(f.module);
    consumer_finish(&s);
    printf("PASS public depth/stencil copies: rendered depth32float .25/.75 via DepthOnly/All readback and texture copy, depth16unorm rendered 0/65535, writeTexture and buffer upload plus texture copy, stencil8 upload then IncrementClamp, depth24plus-stencil8 stencil Replace and StencilOnly upload, depth24plus texture copy observed by depth test, 11 copy and 1 creation validation controls, early releases; exact 72 depth/stencil texels and 8 color pixels %s\n",
        adapter_class_scope());
}
