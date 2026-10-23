/* SPDX-License-Identifier: MIT
 * The other two pinned Linux sources: an Xlib window (compatibleSurface adapter,
 * capabilities, verified rendered/presented frames) and a Wayland wl_surface.
 * Without xdg-shell the wl_surface has no role and is never shown; its frame is
 * acquired, rendered, read back and presented to the compositor, which proves
 * native Wayland swapchain ownership, not on-screen display. A platform without
 * a reachable server is skipped and named; exit 77 when neither ran. */
#include "surface_consumer.h"
#include <string.h>
#include <time.h>
#include <X11/Xlib.h>
#include <wayland-client.h>

static WGPUSurface create_surface(WGPUInstance instance, WGPUChainedStruct *chain)
{
    WGPUSurfaceDescriptor descriptor = {.nextInChain=chain};
    WGPUSurface surface = wgpuInstanceCreateSurface(instance, &descriptor);
    assert(surface);
    return surface;
}

static void run_frames(WGPUSurface surface, uint32_t width, uint32_t height, unsigned frames, const char *name)
{
    state s = {0};
    s.instance = wgpuCreateInstance(NULL);
    request_adapter(&s, surface);
    describe_adapter(s.adapter);
    WGPUTextureFormat format = choose_format(surface, s.adapter);
    configure(&s, surface, format, width, height);
    WGPURenderPipeline pipeline = quadrant_pipeline(&s, format);
    for (unsigned i = 0; i < frames; ++i) render_frame(&s, surface, format, pipeline, width, height, i, i == 0);
    wgpuSurfaceUnconfigure(surface);
    wgpuRenderPipelineRelease(pipeline);
    wgpuSurfaceRelease(surface);
    consumer_finish(&s);
    printf("%s: %u verified frames at %ux%u\n", name, frames, width, height);
}

static bool run_xlib(void)
{
    const char *variable = getenv("DISPLAY");
    Display *display = variable && *variable ? XOpenDisplay(NULL) : NULL;
    if (!display) {
        fprintf(stderr, "SKIP Xlib: no reachable X display\n");
        return false;
    }
    Window window = XCreateSimpleWindow(display, DefaultRootWindow(display), 0, 0, 48, 32, 0, 0, 0);
    XStoreName(display, window, "Doe C Xlib surface test");
    XMapWindow(display, window);
    XSync(display, False);
    for (int i = 0; i < 20; ++i) {
        while (XPending(display)) {
            XEvent event;
            XNextEvent(display, &event);
        }
        struct timespec delay = {0, 10000000L};
        nanosleep(&delay, NULL);
    }
    XWindowAttributes attributes;
    assert(XGetWindowAttributes(display, window, &attributes));
    WGPUSurfaceSourceXlibWindow source = WGPU_SURFACE_SOURCE_XLIB_WINDOW_INIT;
    source.display = display;
    source.window = window;
    WGPUInstance instance = wgpuCreateInstance(NULL);
    run_frames(create_surface(instance, &source.chain), (uint32_t)attributes.width,
        (uint32_t)attributes.height, 3, "Xlib window");
    wgpuInstanceRelease(instance);
    XDestroyWindow(display, window);
    XCloseDisplay(display);
    return true;
}

static void global(void *data, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t version)
{
    (void)version;
    if (strcmp(interface, wl_compositor_interface.name) == 0)
        *(struct wl_compositor **)data = wl_registry_bind(registry, name, &wl_compositor_interface, 1);
}
static void global_remove(void *data, struct wl_registry *registry, uint32_t name)
{
    (void)data; (void)registry; (void)name;
}

static bool run_wayland(void)
{
    struct wl_display *display = getenv("WAYLAND_DISPLAY") ? wl_display_connect(NULL) : NULL;
    if (!display) {
        fprintf(stderr, "SKIP Wayland: no reachable compositor\n");
        return false;
    }
    struct wl_compositor *compositor = NULL;
    struct wl_registry *registry = wl_display_get_registry(display);
    const struct wl_registry_listener listener = {global, global_remove};
    wl_registry_add_listener(registry, &listener, &compositor);
    assert(wl_display_roundtrip(display) >= 0 && compositor);
    struct wl_surface *native = wl_compositor_create_surface(compositor);
    WGPUSurfaceSourceWaylandSurface source = WGPU_SURFACE_SOURCE_WAYLAND_SURFACE_INIT;
    source.display = display;
    source.surface = native;
    WGPUInstance instance = wgpuCreateInstance(NULL);
    run_frames(create_surface(instance, &source.chain), 32, 24, 1, "Wayland role-less wl_surface (not displayed)");
    wgpuInstanceRelease(instance);
    wl_surface_destroy(native);
    wl_compositor_destroy(compositor);
    wl_registry_destroy(registry);
    wl_display_disconnect(display);
    return true;
}

int main(void)
{
    setvbuf(stdout, NULL, _IOLBF, 0);
    bool xlib = run_xlib();
    bool wayland = run_wayland();
    if (!xlib && !wayland) {
        fprintf(stderr, "SKIP surface platforms: neither Xlib nor Wayland reachable\n");
        return 77;
    }
    printf("PASS public surface sources:%s%s compatibleSurface adapter, capabilities, configure, exact pixels "
        "read back in the presenting submission; %u retried Timeout statuses %s\n",
        xlib ? " Xlib window presented;" : " Xlib SKIPPED;",
        wayland ? " Wayland role-less wl_surface presented to the compositor (not displayed);" : " Wayland SKIPPED;",
        timeouts, adapter_class_scope());
    return 0;
}
