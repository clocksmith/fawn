/* SPDX-License-Identifier: MIT */
#ifndef DOE_CLI_SURFACE_PLATFORM_H
#define DOE_CLI_SURFACE_PLATFORM_H
#include "webgpu.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct doe_surface_host doe_surface_host;
/* The host owns its native window/layer; it outlives the returned WebGPU surface.
 * On failure both outputs remain NULL. Release the WebGPU surface first. */
bool surface_host_create(WGPUInstance instance, doe_surface_host **host, WGPUSurface *surface, char *message, size_t capacity);
bool surface_host_resize(doe_surface_host *host, uint32_t width, uint32_t height, char *message, size_t capacity);
void surface_host_release(doe_surface_host *host);
#endif
