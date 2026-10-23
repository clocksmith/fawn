/* SPDX-License-Identifier: MIT
 * Public-ABI presentation to a real XCB window: compatibleSurface adapter,
 * capabilities, configure, acquire, render, same-submission readback, present,
 * resize/reconfigure, unconfigure, early releases and error cases. Needs a
 * reachable X server (DISPLAY); exits 77 (skip, never a pass) without one. */
#include "surface_consumer.h"
#include <time.h>
#include <xcb/xcb.h>

enum { FRAMES = 4 };
typedef struct window {
    xcb_connection_t *connection;
    xcb_window_t id;
    uint32_t width, height;
} window;

static void pause_ms(long milliseconds)
{
    struct timespec delay = {milliseconds / 1000, (milliseconds % 1000) * 1000000L};
    nanosleep(&delay, NULL);
}

/* Drains events for a bounded interval, then reads the actual server geometry. */
static void settle_window(window *w)
{
    for (int i = 0; i < 20; ++i) {
        xcb_generic_event_t *event;
        while ((event = xcb_poll_for_event(w->connection))) free(event);
        pause_ms(10);
    }
    xcb_get_geometry_reply_t *geometry = xcb_get_geometry_reply(w->connection,
        xcb_get_geometry(w->connection, w->id), NULL);
    assert(geometry);
    w->width = geometry->width;
    w->height = geometry->height;
    free(geometry);
}

static bool open_window(window *w)
{
    const char *display = getenv("DISPLAY");
    if (!display || !*display) return false;
    int screen_index = 0;
    w->connection = xcb_connect(NULL, &screen_index);
    if (xcb_connection_has_error(w->connection)) {
        xcb_disconnect(w->connection);
        return false;
    }
    xcb_screen_iterator_t screens = xcb_setup_roots_iterator(xcb_get_setup(w->connection));
    for (int i = 0; i < screen_index; ++i) xcb_screen_next(&screens);
    xcb_screen_t *screen = screens.data;
    w->id = xcb_generate_id(w->connection);
    uint32_t values[2] = {screen->black_pixel, XCB_EVENT_MASK_STRUCTURE_NOTIFY | XCB_EVENT_MASK_EXPOSURE};
    xcb_create_window(w->connection, XCB_COPY_FROM_PARENT, w->id, screen->root, 0, 0, 64, 48, 0,
        XCB_WINDOW_CLASS_INPUT_OUTPUT, screen->root_visual, XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK, values);
    const char title[] = "Doe C surface present test";
    xcb_change_property(w->connection, XCB_PROP_MODE_REPLACE, w->id, XCB_ATOM_WM_NAME, XCB_ATOM_STRING,
        8, sizeof(title) - 1, title);
    xcb_map_window(w->connection, w->id);
    xcb_flush(w->connection);
    settle_window(w);
    return true;
}

static void resize_window(window *w, uint32_t width, uint32_t height)
{
    uint32_t values[2] = {width, height};
    xcb_configure_window(w->connection, w->id, XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT, values);
    xcb_flush(w->connection);
    settle_window(w);
}

/* A device whose adapter lacked compatibleSurface has no WSI: explicit errors. */
static void incompatible_device(state *s, WGPUSurface surface, WGPUTextureFormat format, const window *w)
{
    state other = {.instance=s->instance};
    request_adapter(&other, NULL);
    WGPUSurfaceCapabilities caps = WGPU_SURFACE_CAPABILITIES_INIT;
    assert(wgpuSurfaceGetCapabilities(surface, other.adapter, &caps) == WGPUStatus_Error);
    push_validation(&other);
    configure(&other, surface, format, w->width, w->height);
    expect_validation(&other);
    assert(acquire(surface).status == WGPUSurfaceGetCurrentTextureStatus_Error);
    wgpuQueueRelease(other.queue);
    wgpuDeviceRelease(other.device);
    wgpuAdapterRelease(other.adapter);
    assert(!other.errors);
}

int main(void)
{
    setvbuf(stdout, NULL, _IOLBF, 0);
    window w = {0};
    if (!open_window(&w)) {
        fprintf(stderr, "SKIP surface presentation: no reachable X display (DISPLAY unset or connection refused)\n");
        return 77;
    }
    state s = {0};
    s.instance = wgpuCreateInstance(NULL);
    WGPUSurfaceSourceXCBWindow xcb = WGPU_SURFACE_SOURCE_XCB_WINDOW_INIT;
    xcb.connection = w.connection;
    xcb.window = w.id;
    WGPUSurfaceDescriptor surface_desc = {.nextInChain=&xcb.chain,.label={"present test",WGPU_STRLEN}};
    WGPUSurface surface = wgpuInstanceCreateSurface(s.instance, &surface_desc);
    assert(surface);
    wgpuSurfaceSetLabel(surface, (WGPUStringView){"relabelled surface",WGPU_STRLEN});
    /* Unsupported chains reject explicitly. */
    WGPUSurfaceSourceMetalLayer metal = WGPU_SURFACE_SOURCE_METAL_LAYER_INIT;
    metal.layer = &w;
    surface_desc.nextInChain = &metal.chain;
    assert(!wgpuInstanceCreateSurface(s.instance, &surface_desc));
    xcb.window = 0;
    surface_desc.nextInChain = &xcb.chain;
    assert(!wgpuInstanceCreateSurface(s.instance, &surface_desc));
    xcb.window = w.id;
    printf("XCB window 0x%x mapped at %ux%u on DISPLAY=%s\n", (unsigned)w.id, w.width, w.height, getenv("DISPLAY"));
    request_adapter(&s, surface);
    describe_adapter(s.adapter);
    WGPUTextureFormat format = choose_format(surface, s.adapter);
    /* Unconfigured surface: no texture and no present. */
    WGPUSurfaceTexture none = acquire(surface);
    assert(none.status == WGPUSurfaceGetCurrentTextureStatus_Error && !none.texture);
    assert(wgpuSurfacePresent(surface) == WGPUStatus_Error);
    /* Configuration negatives: unsupported format, usage and fixed-extent mismatch. */
    push_validation(&s);
    configure(&s, surface, WGPUTextureFormat_RGBA16Float, w.width, w.height);
    expect_validation(&s);
    push_validation(&s);
    WGPUSurfaceConfiguration bad = WGPU_SURFACE_CONFIGURATION_INIT;
    bad.device = s.device;
    bad.format = format;
    bad.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_StorageBinding;
    bad.width = w.width;
    bad.height = w.height;
    wgpuSurfaceConfigure(surface, &bad);
    expect_validation(&s);
    push_validation(&s);
    configure(&s, surface, format, w.width + 7, w.height);
    expect_validation(&s);
    assert(acquire(surface).status == WGPUSurfaceGetCurrentTextureStatus_Error);
    incompatible_device(&s, surface, format, &w);

    configure(&s, surface, format, w.width, w.height);
    assert(wgpuSurfacePresent(surface) == WGPUStatus_Error); /* Present without acquire. */
    WGPURenderPipeline pipeline = quadrant_pipeline(&s, format);
    for (unsigned i = 0; i < FRAMES; ++i) render_frame(&s, surface, format, pipeline, w.width, w.height, i, i == 0);

    /* Stale frame: a texture kept past present cannot be recorded, and its
     * lease blocks the next acquisition until released. */
    WGPUSurfaceTexture kept = acquire(surface);
    assert(kept.texture);
    assert(wgpuSurfacePresent(surface) == WGPUStatus_Success); /* Untouched frame: zero-initialized. */
    WGPUBuffer stale = buffer(&s, 256, WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst);
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s.device, NULL);
    WGPUTexelCopyTextureInfo stale_source = {.texture=kept.texture};
    WGPUTexelCopyBufferInfo stale_destination = {.buffer=stale,.layout={.bytesPerRow=256,.rowsPerImage=1}};
    push_validation(&s); /* The presented frame is destroyed: recording rejects it. */
    wgpuCommandEncoderCopyTextureToBuffer(encoder, &stale_source, &stale_destination, &(WGPUExtent3D){1, 1, 1});
    WGPUCommandBuffer rejected = wgpuCommandEncoderFinish(encoder, NULL);
    expect_validation(&s);
    assert(!rejected);
    wgpuCommandEncoderRelease(encoder);
    push_validation(&s);
    WGPUSurfaceTexture blocked = acquire(surface);
    expect_validation(&s);
    assert(blocked.status == WGPUSurfaceGetCurrentTextureStatus_Error && !blocked.texture);
    wgpuTextureRelease(kept.texture);
    wgpuBufferRelease(stale);
    render_frame(&s, surface, format, pipeline, w.width, w.height, 1, false);

    /* Resize: the swapchain reports Outdated (or a suboptimal/optimal frame,
     * which is presented); explicit reconfiguration then renders at the new size. */
    resize_window(&w, 80, 40);
    WGPUSurfaceTexture resized = acquire(surface);
    const char *observed = resized.status == WGPUSurfaceGetCurrentTextureStatus_Outdated ? "Outdated" :
        resized.status == WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal ? "SuccessSuboptimal" :
        resized.status == WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal ? "SuccessOptimal" : NULL;
    assert(observed);
    if (resized.texture) {
        assert(wgpuSurfacePresent(surface) == WGPUStatus_Success);
        wgpuTextureRelease(resized.texture);
    }
    configure(&s, surface, format, w.width, w.height);
    for (unsigned i = 0; i < 2; ++i) render_frame(&s, surface, format, pipeline, w.width, w.height, i + 2, i == 0);

    /* Unconfigure with a held frame and view: retirement defers until release. */
    WGPUSurfaceTexture held = acquire(surface);
    assert(held.texture);
    WGPUTextureView held_view = wgpuTextureCreateView(held.texture, NULL);
    wgpuSurfaceUnconfigure(surface);
    assert(acquire(surface).status == WGPUSurfaceGetCurrentTextureStatus_Error);
    assert(wgpuSurfacePresent(surface) == WGPUStatus_Error);
    push_validation(&s);
    configure(&s, surface, format, w.width, w.height); /* Leases still held: explicit rejection. */
    expect_validation(&s);
    wgpuTextureViewRelease(held_view);
    wgpuTextureRelease(held.texture);
    configure(&s, surface, format, w.width, w.height);
    render_frame(&s, surface, format, pipeline, w.width, w.height, 4, false);

    /* Early release: the last public surface reference drops while a frame
     * texture is still held; the frame payload keeps native ownership alive. */
    WGPUSurfaceTexture orphan = acquire(surface);
    assert(orphan.texture);
    wgpuSurfaceAddRef(surface);
    wgpuSurfaceRelease(surface);
    wgpuSurfaceRelease(surface);
    assert(wgpuTextureGetWidth(orphan.texture) == w.width); /* Handle stays valid, frame destroyed. */
    wgpuTextureRelease(orphan.texture);
    wgpuRenderPipelineRelease(pipeline);
    consumer_finish(&s);
    xcb_disconnect(w.connection);
    printf("PASS public surface presentation: XCB window %ux%u after resize (%s), compatibleSurface adapter, "
        "capabilities, %u verified frames with zero-init/asymmetric pixels/same-submission readback before present, "
        "stale-frame and lease rejection, reconfigure, deferred unconfigure, early releases and configure/acquire/present "
        "negatives; %u retried Timeout statuses %s\n", w.width, w.height, observed, FRAMES + 4, timeouts,
        adapter_class_scope());
    return 0;
}
