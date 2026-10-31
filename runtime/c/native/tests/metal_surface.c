/* SPDX-License-Identifier: MIT
 * Actual CAMetalLayer public lifecycle and pixel checks. The host helper owns
 * its window until after every surface frame/view is released. */
#include "surface_consumer.h"
#include "../../runtime-cli/src/surface_platform.h"

int main(void)
{
    consumer_line_output();
    state s = {.instance=wgpuCreateInstance(NULL)};
    doe_surface_host *host = NULL;
    WGPUSurface surface = NULL;
    char message[512] = {0};
    if (!surface_host_create(s.instance, &host, &surface, message, sizeof(message))) {
        fprintf(stderr, "SKIP Metal surface host unavailable: %s\n", message);
        wgpuInstanceRelease(s.instance);
        return 77;
    }
    request_adapter(&s, surface);
    WGPUTextureFormat format = choose_format(surface, s.adapter);
    assert(!acquire(surface).texture && wgpuSurfacePresent(surface) == WGPUStatus_Error);
    configure(&s, surface, format, 64, 64);
    WGPURenderPipeline pipeline = quadrant_pipeline(&s, format);
    render_frame(&s, surface, format, pipeline, 64, 64, 0, true);
    WGPUSurfaceTexture kept = acquire(surface);
    assert(kept.texture);
    WGPUTextureView view = wgpuTextureCreateView(kept.texture, NULL);
    assert(view);
    push_validation(&s);
    configure(&s, surface, WGPUTextureFormat_RGBA16Float, 64, 64);
    expect_validation(&s);
    WGPUSurfaceTexture same = acquire(surface);
    assert(same.texture == kept.texture);
    wgpuTextureRelease(same.texture);
    assert(wgpuSurfacePresent(surface) == WGPUStatus_Success);
    /* A held old texture/view must not consume the drawable pool forever. */
    for (unsigned i = 1; i < 5; ++i) render_frame(&s, surface, format, pipeline, 64, 64, i, false);
    assert(surface_host_resize(host, 48, 32, message, sizeof(message)));
    configure(&s, surface, format, 48, 32);
    render_frame(&s, surface, format, pipeline, 48, 32, 5, true);
    wgpuSurfaceUnconfigure(surface);
    assert(!acquire(surface).texture && wgpuSurfacePresent(surface) == WGPUStatus_Error);
    wgpuRenderPipelineRelease(pipeline);
    wgpuSurfaceRelease(surface);
    /* Public surface release precedes the last old frame/view release. */
    wgpuTextureRelease(kept.texture);
    wgpuTextureViewRelease(view);
    surface_host_release(host);
    wgpuQueueRelease(s.queue);
    wgpuDeviceRelease(s.device);
    wgpuAdapterRelease(s.adapter);
    wgpuInstanceRelease(s.instance);
    assert(!s.errors);
    printf("PASS Metal surface: six pixel-checked frames, initialization, invalid-config preservation, "
        "held views, resize and teardown %s\n", adapter_class_scope());
    return 0;
}
