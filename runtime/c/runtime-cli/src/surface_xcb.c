/* SPDX-License-Identifier: MIT
 * Real XCB surface host for the command-stream lifecycle, including Xwayland. */
#include "surface_platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <xcb/xcb.h>
#ifdef DOE_SURFACE_XLIB
#include <X11/Xlib.h>
#include <X11/Xlib-xcb.h>
#endif

struct doe_surface_host {
    xcb_connection_t *connection;
    xcb_window_t window;
#ifdef DOE_SURFACE_XLIB
    Display *display;
#endif
};

void surface_host_release(doe_surface_host *host)
{
    if (!host) return;
    if (host->window) xcb_destroy_window(host->connection, host->window);
#ifdef DOE_SURFACE_XLIB
    if (host->display) XCloseDisplay(host->display);
#else
    if (host->connection) xcb_disconnect(host->connection);
#endif
    free(host);
}

bool surface_host_resize(doe_surface_host *host, uint32_t width, uint32_t height, char *message, size_t capacity)
{
    /* X11's window geometry is u16 even when WebGPU extents are u32. */
    if (!width || !height || width > UINT16_MAX || height > UINT16_MAX) {
        snprintf(message, capacity, "XCB window extent exceeds its nonzero u16 geometry");
        return false;
    }
    uint32_t dimensions[] = {width, height};
    xcb_void_cookie_t cookie = xcb_configure_window_checked(host->connection, host->window,
        XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT, dimensions);
    xcb_generic_error_t *error = xcb_request_check(host->connection, cookie);
    if (error) { free(error); snprintf(message, capacity, "XCB window resize was rejected"); return false; }
    xcb_flush(host->connection);
    for (unsigned attempt = 0; attempt < 40; attempt++) {
        xcb_generic_event_t *event;
        while ((event = xcb_poll_for_event(host->connection))) free(event);
        xcb_get_geometry_reply_t *geometry = xcb_get_geometry_reply(host->connection,
            xcb_get_geometry(host->connection, host->window), NULL);
        bool ready = geometry && geometry->width == width && geometry->height == height;
        free(geometry);
        if (ready) return true;
        struct timespec delay = {.tv_nsec = 5000000};
        nanosleep(&delay, NULL);
    }
    snprintf(message, capacity, "XCB window geometry did not settle to %ux%u", width, height);
    return false;
}

bool surface_host_create(WGPUInstance instance, doe_surface_host **out_host, WGPUSurface *out_surface,
    char *message, size_t capacity)
{
    *out_host = NULL;
    *out_surface = NULL;
    doe_surface_host *host = calloc(1, sizeof(*host));
    if (!host) { snprintf(message, capacity, "surface host allocation failed"); return false; }
    int screen_index = 0;
#ifdef DOE_SURFACE_XLIB
    /* The pinned Dawn public ABI supports Xlib, not the XCB surface chain.
     * Window requests still use XCB on the display's shared connection. */
    host->display = XOpenDisplay(NULL);
    host->connection = host->display ? XGetXCBConnection(host->display) : NULL;
    if (host->display) {
        screen_index = DefaultScreen(host->display);
        XSetEventQueueOwner(host->display, XCBOwnsEventQueue);
    }
#else
    host->connection = xcb_connect(NULL, &screen_index);
#endif
    if (!host->connection || xcb_connection_has_error(host->connection)) {
        snprintf(message, capacity, "surface requires a reachable X display (DISPLAY)");
        surface_host_release(host);
        return false;
    }
    xcb_screen_iterator_t screens = xcb_setup_roots_iterator(xcb_get_setup(host->connection));
    for (int i = 0; i < screen_index && screens.rem; i++) xcb_screen_next(&screens);
    if (!screens.rem) { snprintf(message, capacity, "XCB display has no selected screen"); surface_host_release(host); return false; }
    xcb_screen_t *screen = screens.data;
    host->window = xcb_generate_id(host->connection);
    uint32_t values[] = {screen->black_pixel, XCB_EVENT_MASK_STRUCTURE_NOTIFY | XCB_EVENT_MASK_EXPOSURE};
    xcb_void_cookie_t cookie = xcb_create_window_checked(host->connection, XCB_COPY_FROM_PARENT, host->window,
        screen->root, 0, 0, 64, 64, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT, screen->root_visual,
        XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK, values);
    xcb_generic_error_t *error = xcb_request_check(host->connection, cookie);
    if (error) { free(error); snprintf(message, capacity, "XCB window creation failed"); surface_host_release(host); return false; }
    const char title[] = "Doe command-stream surface";
    xcb_change_property(host->connection, XCB_PROP_MODE_REPLACE, host->window, XCB_ATOM_WM_NAME, XCB_ATOM_STRING,
        8, sizeof(title) - 1, title);
    xcb_map_window(host->connection, host->window);
    xcb_flush(host->connection);
    if (!surface_host_resize(host, 64, 64, message, capacity)) { surface_host_release(host); return false; }
#ifdef DOE_SURFACE_XLIB
    WGPUSurfaceSourceXlibWindow source = WGPU_SURFACE_SOURCE_XLIB_WINDOW_INIT;
    source.display = host->display;
    source.window = host->window;
#else
    WGPUSurfaceSourceXCBWindow source = WGPU_SURFACE_SOURCE_XCB_WINDOW_INIT;
    source.connection = host->connection;
    source.window = host->window;
#endif
    WGPUSurfaceDescriptor descriptor = WGPU_SURFACE_DESCRIPTOR_INIT;
    descriptor.nextInChain = &source.chain;
    WGPUSurface surface = wgpuInstanceCreateSurface(instance, &descriptor);
    if (!surface) { snprintf(message, capacity, "WebGPU XCB surface creation failed"); surface_host_release(host); return false; }
    *out_host = host;
    *out_surface = surface;
    return true;
}
