/* SPDX-License-Identifier: MIT
 * NSWindow/CAMetalLayer ownership for retained WebGPU surface commands. */
#include "surface_platform.h"
#include <stdio.h>
#include <stdlib.h>
#import <AppKit/AppKit.h>
#import <QuartzCore/CAMetalLayer.h>
#import <QuartzCore/CATransaction.h>

struct doe_surface_host {
    NSWindow *window;
    CAMetalLayer *layer;
};

void surface_host_release(doe_surface_host *host)
{
    if (!host) return;
    @autoreleasepool {
        [host->window orderOut:nil];
        [host->window close];
        [host->window release];
        [host->layer release];
    }
    free(host);
}

bool surface_host_resize(doe_surface_host *host, uint32_t width, uint32_t height, char *message, size_t capacity)
{
    if (!width || !height) { snprintf(message, capacity, "surface dimensions must be nonzero"); return false; }
    @autoreleasepool {
        [host->window setContentSize:NSMakeSize(width, height)];
        host->layer.drawableSize = CGSizeMake(width, height);
        [host->window displayIfNeeded];
        [CATransaction flush];
    }
    return true;
}

bool surface_host_create(WGPUInstance instance, doe_surface_host **out_host, WGPUSurface *out_surface,
    char *message, size_t capacity)
{
    *out_host = NULL;
    *out_surface = NULL;
    doe_surface_host *host = calloc(1, sizeof(*host));
    if (!host) { snprintf(message, capacity, "surface host allocation failed"); return false; }
    @autoreleasepool {
        [NSApplication sharedApplication];
        host->window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 64, 64)
            styleMask:NSWindowStyleMaskBorderless backing:NSBackingStoreBuffered defer:NO];
        [host->window setReleasedWhenClosed:NO];
        [host->window setTitle:@"Doe command-stream surface"];
        host->layer = [[CAMetalLayer alloc] init];
        NSView *view = [host->window contentView];
        [view setWantsLayer:YES];
        [view setLayer:host->layer];
        host->layer.drawableSize = CGSizeMake(64, 64);
        [host->window orderFront:nil];
        [host->window displayIfNeeded];
        [CATransaction flush];
        if (!host->window || !host->layer) {
            snprintf(message, capacity, "Metal surface window/layer creation failed");
            surface_host_release(host);
            return false;
        }
        WGPUSurfaceSourceMetalLayer source = WGPU_SURFACE_SOURCE_METAL_LAYER_INIT;
        source.layer = host->layer;
        WGPUSurfaceDescriptor descriptor = WGPU_SURFACE_DESCRIPTOR_INIT;
        descriptor.nextInChain = &source.chain;
        WGPUSurface surface = wgpuInstanceCreateSurface(instance, &descriptor);
        if (!surface) {
            snprintf(message, capacity, "WebGPU Metal surface creation failed");
            surface_host_release(host);
            return false;
        }
        *out_host = host;
        *out_surface = surface;
    }
    return true;
}
