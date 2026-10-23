/* SPDX-License-Identifier: MIT
 * Public WebGPU surfaces over the Vulkan surface owner (doe_vk_surface.h).
 * The native frontend owns only public state and leases; acquisition,
 * presentation fences and swapchain lifetime stay with the backend owner. */
#ifndef DOE_NATIVE_SURFACE_H
#define DOE_NATIVE_SURFACE_H
#include "internal.h"
#include "doe_vk_surface.h"

/* Bounded native waits for one acquisition or one presentation retirement.
 * The backend may wait up to twice this per call (acquire plus its fence). */
#define NATIVE_SURFACE_TIMEOUT_NS UINT64_C(1000000000)

typedef struct native_surface {
    atomic_uint references; /* Public handles. Zero triggers public teardown once. */
    atomic_uint leases; /* One for all public handles plus one per live frame payload. */
    pthread_mutex_t mutex; /* Public state; always taken before any device mutex. */
    doe_instance *instance;
    doe_vk_surface_desc source; /* Borrowed platform handles; caller keeps them alive. */
    doe_label_store *labels;
    /* Native ownership. `vulkan` lives on `device`'s context and may outlive the
     * public configuration until caller frame/view leases drop (deferred retire). */
    native_device *device; /* Internal reference while `vulkan` exists. */
    doe_vk_surface *vulkan;
    /* Public configuration. */
    bool configured;
    WGPUTextureFormat format;
    WGPUTextureUsage usage;
    uint32_t width, height;
    /* Surface lease on the acquired, not yet presented frame texture. */
    doe_resource_texture *current;
    WGPUSurfaceGetCurrentTextureStatus current_status;
    /* Surface-level outcome observed at presentation (OUTDATED/LOST); the next
     * acquisition reports it until reconfiguration. NATIVE_OK otherwise. */
    native_result presented;
} native_surface;

/* Resource-owned payload of a frame texture. The first member keeps every
 * existing native_texture consumer (views, copies, passes) unchanged. */
typedef struct native_surface_frame {
    native_texture texture;
    native_surface *surface; /* Lease dropped after the borrowed frame texture. */
} native_surface_frame;

typedef struct native_presentation {
    doe_vk_status submission; /* Private present-command submission and settlement. */
    doe_vk_status presentation; /* Present enqueue, then its fence retirement. */
} native_presentation;
/* Caller holds the device mutex. Submits commands carrying a prepared present
 * through the existing pending owner, enqueues the present between submit and
 * settlement, then retires the frame before releasing the commands. Always
 * adopts commands; unknown completion stays retained on the pending list. */
native_presentation native_submit_present(native_device *device, doe_vk_commands *commands,
    doe_vk_surface *surface);
/* Exact DOE_VK_SURFACE_* bit of a live public surface; 0 for NULL. */
uint32_t native_surface_platform(WGPUSurface surface);
/* Creates and releases a probe surface on an adapter context built with this
 * surface's platform: proves the selected queue can present to the window. */
native_result native_surface_compatible(WGPUSurface surface, const native_backend *adapter);
void native_surface_release_lease(native_surface *surface);
/* Caller holds surface->mutex. Drops the public frame and retires/unconfigures
 * the native swapchain when no caller frame/view leases remain. */
void native_surface_unconfigure_locked(native_surface *surface);
/* Caller holds surface->mutex and the device mutex. Destroys and releases the
 * current public frame texture (the frame itself is retired separately). */
void native_surface_drop_current(native_surface *surface);
#endif
