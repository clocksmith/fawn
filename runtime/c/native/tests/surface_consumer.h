/* SPDX-License-Identifier: MIT
 * Public-ABI surface consumer helpers shared by the real-window presentation
 * clients: compatibleSurface adapter/device, capabilities, configuration,
 * Timeout-retrying acquisition and one verified rendered/presented frame. */
#ifndef DOE_NATIVE_TEST_SURFACE_CONSUMER_H
#define DOE_NATIVE_TEST_SURFACE_CONSUMER_H
#include "consumer.h"
#include <stdlib.h>

static inline void push_validation(state *s) { wgpuDevicePushErrorScope(s->device, WGPUErrorFilter_Validation); }

static inline WGPURenderPipeline quadrant_pipeline(state *s, WGPUTextureFormat format)
{
    WGPUShaderSourceWGSL source = WGPU_SHADER_SOURCE_WGSL_INIT;
    /* Top-left quadrant in WebGPU clip space (+y up): x in [-1,0], y in [0,1]. */
    source.code = (WGPUStringView){"@vertex fn vertex(@builtin(vertex_index) i:u32)->@builtin(position) vec4f{"
        "if(i==0u||i==3u){return vec4f(-1.0,0.0,0.0,1.0);}if(i==1u){return vec4f(0.0,0.0,0.0,1.0);}"
        "if(i==2u||i==4u){return vec4f(0.0,1.0,0.0,1.0);}return vec4f(-1.0,1.0,0.0,1.0);}"
        "@fragment fn fragment()->@location(0) vec4f{return vec4f(1.0,0.0,0.0,1.0);}", WGPU_STRLEN};
    WGPUShaderModuleDescriptor module_desc = {.nextInChain=&source.chain};
    WGPUShaderModule module = wgpuDeviceCreateShaderModule(s->device, &module_desc);
    assert(module);
    WGPUColorTargetState target = {.format=format,.writeMask=WGPUColorWriteMask_All};
    WGPUFragmentState fragment = {.module=module,.targetCount=1,.targets=&target};
    WGPURenderPipelineDescriptor descriptor = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    descriptor.vertex.module = module;
    descriptor.fragment = &fragment;
    WGPURenderPipeline pipeline = wgpuDeviceCreateRenderPipeline(s->device, &descriptor);
    wgpuShaderModuleRelease(module);
    assert(pipeline);
    return pipeline;
}

static inline void configure(state *s, WGPUSurface surface, WGPUTextureFormat format, uint32_t width, uint32_t height)
{
    WGPUSurfaceConfiguration config = WGPU_SURFACE_CONFIGURATION_INIT;
    config.device = s->device;
    config.format = format;
    config.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc;
    config.width = width;
    config.height = height;
    config.presentMode = WGPUPresentMode_Fifo;
    wgpuSurfaceConfigure(surface, &config);
}

static unsigned timeouts; /* Timeout statuses observed and retried. */
/* Timeout is a valid status (e.g. FIFO throttled by a compositor that is not
 * repainting the window); the backend retains the pending acquisition and
 * the retry waits that same acquisition. Bounded at ten retries. */
static inline WGPUSurfaceTexture acquire(WGPUSurface surface)
{
    WGPUSurfaceTexture frame = WGPU_SURFACE_TEXTURE_INIT;
    for (unsigned attempt = 0; attempt < 10; ++attempt) {
        wgpuSurfaceGetCurrentTexture(surface, &frame);
        if (frame.status != WGPUSurfaceGetCurrentTextureStatus_Timeout) break;
        assert(!frame.texture);
        ++timeouts;
    }
    return frame;
}

/* Expected unorm8 bytes in memory order for the configured format. */
static inline void pixel(WGPUTextureFormat format, const unsigned char rgba[4], unsigned char out[4])
{
    bool bgra = format == WGPUTextureFormat_BGRA8Unorm;
    out[0] = bgra ? rgba[2] : rgba[0];
    out[1] = rgba[1];
    out[2] = bgra ? rgba[0] : rgba[2];
    out[3] = rgba[3];
}

/* One frame: optional zero-initialization readback, clear + quadrant draw,
 * readback in the same submission before present, then exact pixel checks. */
static inline void render_frame(state *s, WGPUSurface surface, WGPUTextureFormat format, WGPURenderPipeline pipeline,
    uint32_t width, uint32_t height, unsigned frame_index, bool check_zero)
{
    WGPUSurfaceTexture frame = acquire(surface);
    if (frame.status != WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal &&
        frame.status != WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal)
        fprintf(stderr, "frame %u acquisition status %u\n", frame_index, (unsigned)frame.status);
    assert(frame.status == WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal ||
        frame.status == WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal);
    assert(frame.texture && wgpuTextureGetWidth(frame.texture) == width && wgpuTextureGetHeight(frame.texture) == height);
    assert(wgpuTextureGetFormat(frame.texture) == format && wgpuTextureGetMipLevelCount(frame.texture) == 1);
    assert(wgpuTextureGetUsage(frame.texture) == (WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc));
    WGPUSurfaceTexture again = acquire(surface); /* Same frame until presented. */
    assert(again.texture == frame.texture && again.status == frame.status);
    wgpuTextureRelease(again.texture);
    const uint32_t pitch = (width * 4 + 255) / 256 * 256;
    const uint64_t size = (uint64_t)pitch * height;
    WGPUBuffer readback = buffer(s, size, WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst);
    WGPUBuffer initial = check_zero ? buffer(s, size, WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst) : NULL;
    WGPUTextureView view = wgpuTextureCreateView(frame.texture, NULL);
    assert(view);
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device, NULL);
    WGPUTexelCopyTextureInfo source = {.texture=frame.texture};
    WGPUExtent3D extent = {width, height, 1};
    if (initial) {
        WGPUTexelCopyBufferInfo destination = {.buffer=initial,.layout={.bytesPerRow=pitch,.rowsPerImage=height}};
        wgpuCommandEncoderCopyTextureToBuffer(encoder, &source, &destination, &extent);
    }
    const unsigned char clear[4] = {0, (unsigned char)(51 * frame_index), 255, 255};
    WGPURenderPassColorAttachment attachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    attachment.view = view;
    attachment.loadOp = WGPULoadOp_Clear;
    attachment.storeOp = WGPUStoreOp_Store;
    attachment.clearValue = (WGPUColor){0, clear[1] / 255.0, 1, 1};
    WGPURenderPassDescriptor pass_desc = {.colorAttachmentCount=1,.colorAttachments=&attachment};
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &pass_desc);
    assert(pass);
    wgpuRenderPassEncoderSetPipeline(pass, pipeline);
    wgpuRenderPassEncoderDraw(pass, 6, 1, 0, 0);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
    WGPUTexelCopyBufferInfo destination = {.buffer=readback,.layout={.bytesPerRow=pitch,.rowsPerImage=height}};
    wgpuCommandEncoderCopyTextureToBuffer(encoder, &source, &destination, &extent);
    submit(s, encoder);
    assert(wgpuSurfacePresent(surface) == WGPUStatus_Success);
    wgpuTextureViewRelease(view);
    wgpuTextureRelease(frame.texture);
    unsigned char background[4], quadrant[4];
    pixel(format, clear, background);
    pixel(format, (const unsigned char[]){255, 0, 0, 255}, quadrant);
    const unsigned char *bytes = map(s, readback, size);
    for (uint32_t y = 0; y < height; ++y)
        for (uint32_t x = 0; x < width; ++x) {
            const unsigned char *expected = x < width / 2 && y < height / 2 ? quadrant : background;
            if (memcmp(bytes + (size_t)y * pitch + x * 4, expected, 4) != 0) {
                fprintf(stderr, "frame %u pixel (%u,%u) mismatch\n", frame_index, x, y);
                abort();
            }
        }
    wgpuBufferUnmap(readback);
    wgpuBufferRelease(readback);
    if (initial) {
        bytes = map(s, initial, size);
        for (uint32_t y = 0; y < height; ++y)
            for (uint32_t x = 0; x < width * 4; ++x) assert(bytes[(size_t)y * pitch + x] == 0);
        wgpuBufferUnmap(initial);
        wgpuBufferRelease(initial);
    }
}

static inline void request_adapter(state *s, WGPUSurface compatible)
{
    WGPURequestAdapterOptions options = WGPU_REQUEST_ADAPTER_OPTIONS_INIT;
    adapter_class_options(&options);
    options.compatibleSurface = compatible;
    WGPURequestAdapterCallbackInfo callback = {.mode=WGPUCallbackMode_AllowSpontaneous,.callback=adapter_reply,.userdata1=s};
    assert(wgpuInstanceRequestAdapter(s->instance, &options, callback).id && s->adapter);
    adapter_class_require(s->adapter);
    WGPUDeviceDescriptor descriptor = WGPU_DEVICE_DESCRIPTOR_INIT;
    descriptor.uncapturedErrorCallbackInfo = (WGPUUncapturedErrorCallbackInfo){.callback=error_reply,.userdata1=s};
    WGPURequestDeviceCallbackInfo device = {.mode=WGPUCallbackMode_AllowSpontaneous,.callback=device_reply,.userdata1=s};
    assert(wgpuAdapterRequestDevice(s->adapter, &descriptor, device).id && s->device);
    s->queue = wgpuDeviceGetQueue(s->device);
}

static inline WGPUTextureFormat choose_format(WGPUSurface surface, WGPUAdapter adapter)
{
    WGPUSurfaceCapabilities caps = WGPU_SURFACE_CAPABILITIES_INIT;
    assert(wgpuSurfaceGetCapabilities(surface, adapter, &caps) == WGPUStatus_Success);
    assert((caps.usages & WGPUTextureUsage_RenderAttachment) && (caps.usages & WGPUTextureUsage_CopySrc));
    bool fifo = false;
    for (size_t i = 0; i < caps.presentModeCount; ++i) fifo |= caps.presentModes[i] == WGPUPresentMode_Fifo;
    assert(fifo && caps.alphaModeCount && caps.alphaModes[0] != WGPUCompositeAlphaMode_Auto);
    WGPUTextureFormat chosen = WGPUTextureFormat_Undefined;
    printf("surface formats:");
    for (size_t i = 0; i < caps.formatCount; ++i) {
        printf(" 0x%x", (unsigned)caps.formats[i]);
        if (chosen == WGPUTextureFormat_Undefined && (caps.formats[i] == WGPUTextureFormat_BGRA8Unorm ||
            caps.formats[i] == WGPUTextureFormat_RGBA8Unorm)) chosen = caps.formats[i];
    }
    printf("; present modes %zu; alpha modes %zu (first 0x%x)\n", caps.presentModeCount, caps.alphaModeCount,
        (unsigned)caps.alphaModes[0]);
    wgpuSurfaceCapabilitiesFreeMembers(caps);
    assert(chosen != WGPUTextureFormat_Undefined);
    return chosen;
}

static inline void describe_adapter(WGPUAdapter adapter)
{
    WGPUAdapterInfo info = WGPU_ADAPTER_INFO_INIT;
    assert(wgpuAdapterGetInfo(adapter, &info) == WGPUStatus_Success);
    const char *icd = getenv("VK_ICD_FILENAMES");
    printf("adapter: %.*s vendor=0x%04x device=0x%04x type=%u backend=%s ICD=%s\n", (int)info.device.length,
        info.device.data, info.vendorID, info.deviceID, (unsigned)info.adapterType,
        info.backendType == WGPUBackendType_Vulkan ? "Vulkan" : "other", icd ? icd : "(system ICDs)");
    wgpuAdapterInfoFreeMembers(info);
}
#endif
