/* SPDX-License-Identifier: MIT
 * Public WebGPU surfaces over the Vulkan surface owner (doe_vk_surface.h).
 * The native frontend owns only public state and leases; acquisition,
 * presentation fences and swapchain lifetime stay with the backend owner. */
#ifndef DOE_NATIVE_SURFACE_H
#define DOE_NATIVE_SURFACE_H
#include "internal.h"
#include "doe_vk_surface.h"
#include "doe_metal_surface.h"
#include "../../backends/d3d12/include/doe_d3d12_surface.h"

/* Bounded native waits for one acquisition or one presentation retirement.
 * The backend may wait up to twice this per call (acquire plus its fence). */
#define NATIVE_SURFACE_TIMEOUT_NS UINT64_C(1000000000)
enum { NATIVE_SURFACE_METAL_LAYER = 8, NATIVE_SURFACE_WINDOWS_HWND = 16 };

typedef struct native_surface {
    atomic_uint references; /* Public handles. Zero triggers public teardown once. */
    atomic_uint leases; /* Public group, frame payloads and pending retirement/progress. */
    doe_mutex mutex; /* Public state; always taken before any device mutex. */
    doe_instance *instance;
    doe_vk_surface_desc source; /* Borrowed platform handles; caller keeps them alive. */
    doe_label_store *labels;
    /* Native ownership. `vulkan` lives on `device`'s context and may outlive the
     * public configuration until caller frame/view leases drop (deferred retire). */
    native_device *device; /* Internal reference while a backend configuration exists. */
    doe_vk_surface *vulkan;
    doe_mtl_surface *metal;
    doe_dx_surface *d3d12;
    native_pending *retirement; /* Reserved before configuration publication. */
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
} native_surface_frame;

/* Public texture/view release holds existing owners across resource finalizers,
 * then retires completed generations before borrowed window handles may die. */
typedef struct native_surface_release_hold {
    native_surface *surface;
    native_device *device;
} native_surface_release_hold;
native_surface_release_hold native_surface_hold_release(doe_resource_texture *texture);
void native_surface_finish_release(native_surface_release_hold hold);

typedef struct native_presentation {
    native_failure submission; /* Private present-command submission and settlement. */
    native_failure presentation; /* Present enqueue, then its fence retirement. */
} native_presentation;
/* Caller holds the device mutex. Submits commands carrying a prepared present
 * through the existing pending owner, enqueues the present between submit and
 * settlement, then retires the frame before releasing the commands. Always
 * adopts commands; unknown completion stays retained on the pending list. */
native_presentation native_submit_present(native_device *device, doe_vk_commands *commands,
    doe_vk_surface *surface);
/* A closed variant of the existing pending owner, not another queue. Surface
 * retirement reserves storage before any acquisition can become uncertain. */
typedef enum native_pending_kind {
    NATIVE_PENDING_COMMANDS,
    NATIVE_PENDING_SURFACE
} native_pending_kind;
struct native_pending {
    native_pending *next;
    native_device *device;
    native_command_handle native;
    doe_resource_commands **resources;
    size_t count;
    native_pending_kind kind;
    bool replay; /* Command variant: settle the resource owner's open replay. */
    doe_vk_surface *frame; /* Present commands borrow their backend surface lease. */
    native_surface *surface;
    bool armed, ready;
};
/* All list operations require the device mutex. Arm also requires surface mutex;
 * a ready record is transferred, never copied, to the caller for final cleanup. */
void native_surface_retirement_arm(native_surface *surface);
native_surface *native_surface_retirement_peek(native_device *device);
native_pending *native_surface_retirement_take(native_device *device, native_pending *only);
void native_surface_retirement_finish_locked(native_pending *pending);
/* Drop the transferred configuration/retirement leases after both locks drop. */
void native_surface_retirement_release(native_pending *pending);
/* Exact DOE_VK_SURFACE_* bit of a live public surface; 0 for NULL. */
uint32_t native_surface_platform(WGPUSurface surface);
/* Every Linux WSI platform this build composes, enabled on a default Vulkan
 * adapter when its loader and device provide them; 0 on other builds. */
uint32_t native_surface_default_platforms(void);
/* Creates and releases a probe surface on an adapter context built with this
 * surface's platform: proves the selected queue can present to the window. */
native_result native_surface_compatible(WGPUSurface surface, const native_backend *adapter);
void native_surface_release_lease(native_surface *surface);
/* Caller holds surface->mutex. Returns any completed configuration obligation;
 * release it only after both mutexes drop. Unknown work is armed before return. */
native_pending *native_surface_unconfigure_locked(native_surface *surface);
/* Caller holds surface->mutex and the device mutex. Destroys and releases the
 * current public frame texture (the frame itself is retired separately). */
void native_surface_drop_current(native_surface *surface);
WGPUStatus native_metal_surface_capabilities(native_surface *surface, native_adapter *adapter,
    WGPUSurfaceCapabilities *out);
void native_metal_surface_configure(native_surface *surface, const WGPUSurfaceConfiguration *config);
native_pending *native_metal_surface_unconfigure_locked(native_surface *surface);
void native_metal_surface_acquire(native_surface *surface, WGPUSurfaceTexture *out);
WGPUStatus native_metal_surface_present(native_surface *surface);
WGPUStatus native_d3d12_surface_capabilities(native_surface *,native_adapter *,WGPUSurfaceCapabilities *);
void native_d3d12_surface_configure(native_surface *,const WGPUSurfaceConfiguration *);
native_pending *native_d3d12_surface_unconfigure_locked(native_surface *);
void native_d3d12_surface_acquire(native_surface *,WGPUSurfaceTexture *);
WGPUStatus native_d3d12_surface_present(native_surface *);
#endif
