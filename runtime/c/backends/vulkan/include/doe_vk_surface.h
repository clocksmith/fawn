#ifndef DOE_VK_SURFACE_H
#define DOE_VK_SURFACE_H
#include "doe_vk_texture.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct doe_vk_surface doe_vk_surface;
/* Platform handles are borrowed, never opened/closed by Doe; their window and
 * display/connection must outlive the surface and pending presentation. */
typedef struct doe_vk_surface_desc {
    uint32_t platform; /* Exactly one enabled DOE_VK_SURFACE_* bit. */
    void *display;
    union {
        void *wayland_surface;
        uint32_t xcb_window;
        uint64_t xlib_window;
    } window;
} doe_vk_surface_desc;
enum { DOE_VK_MAX_SURFACE_FORMATS = 32, DOE_VK_MAX_PRESENT_MODES = 8 };
typedef struct doe_vk_surface_format {
    uint32_t format, color_space;
} doe_vk_surface_format;
typedef struct doe_vk_surface_caps {
    uint32_t min_image_count, max_image_count;
    uint32_t current_extent[2], min_extent[2], max_extent[2];
    uint32_t supported_usage, supported_alpha, current_transform;
    uint32_t format_count, present_mode_count;
    doe_vk_surface_format formats[DOE_VK_MAX_SURFACE_FORMATS];
    uint32_t present_modes[DOE_VK_MAX_PRESENT_MODES];
    bool present_supported;
} doe_vk_surface_caps;
typedef enum doe_vk_present_mode {
    DOE_VK_PRESENT_IMMEDIATE = 0,
    DOE_VK_PRESENT_MAILBOX = 1,
    DOE_VK_PRESENT_FIFO = 2,
    DOE_VK_PRESENT_FIFO_RELAXED = 3
} doe_vk_present_mode;
typedef struct doe_vk_surface_config {
    uint32_t width, height, image_count; /* Zero image_count chooses bounded min+1. */
    doe_vk_texture_format format;
    uint32_t usage; /* Native texture usages; COLOR_ATTACHMENT is required. */
    doe_vk_present_mode present_mode;
    uint32_t alpha_mode; /* Exactly one native supported alpha bit. */
} doe_vk_surface_config;

doe_vk_status doe_vk_surface_create(doe_vk_context *context,
    const doe_vk_surface_desc *desc, doe_vk_surface **out, doe_vk_error *error);
doe_vk_status doe_vk_surface_retain(doe_vk_surface *surface);
/* BUSY leaves the reference intact while acquired textures, recorded command
 * leases or acquire/present synchronization are live. Never guesses completion. */
doe_vk_status doe_vk_surface_release(doe_vk_surface *surface);
doe_vk_status doe_vk_surface_capabilities(doe_vk_surface *surface,
    doe_vk_surface_caps *out, doe_vk_error *error);
/* Explicit unconfigure is required before replacement; no implicit retirement
 * of an old swapchain on failed creation. At most one frame is in flight, and
 * previous texture/view/command leases must be released before acquiring again. */
doe_vk_status doe_vk_surface_configure(doe_vk_surface *surface,
    const doe_vk_surface_config *config, doe_vk_error *error);
doe_vk_status doe_vk_surface_unconfigure(doe_vk_surface *surface, doe_vk_error *error);
/* Acquisition uses a real fence. Timeout/pending never publishes a texture and
 * retains pending ownership; a subsequent call waits the same acquisition.
 * Successful output is borrowed until present/discard. Explicit retains/views
 * and recorded commands keep the old generation alive but never make it valid
 * for future recording after present/discard. No headless image substitution. */
doe_vk_status doe_vk_surface_acquire(doe_vk_surface *surface, uint64_t timeout_ns,
    doe_vk_texture **out, bool *suboptimal, doe_vk_error *error);
/* Attaches this frame to the existing commands. Submit records canonical
 * zero-if-undefined and the final PRESENT transition, then signals a real binary semaphore.
 * One surface per command submission. No second queue/submit engine. */
doe_vk_status doe_vk_surface_prepare_present(doe_vk_surface *surface,
    doe_vk_commands *commands, doe_vk_error *error);
/* Commands must be the prepared, successfully submitted command object. A
 * successful queue-present call is NOT presentation-resource retirement. */
doe_vk_status doe_vk_surface_present(doe_vk_surface *surface,
    doe_vk_commands *commands, bool *suboptimal, doe_vk_error *error);
doe_vk_status doe_vk_surface_wait(doe_vk_surface *surface, uint64_t timeout_ns, doe_vk_error *error);
/* Releases a completed acquisition without presentation via maintenance1.
 * Refuses any unresolved render work; no CPU content fabrication. */
doe_vk_status doe_vk_surface_discard(doe_vk_surface *surface, doe_vk_error *error);
/* Retires only the current frame, preserving configured swapchain and original
 * presentation errors. No caller lease is released and no new image acquired. */
doe_vk_status doe_vk_surface_retire_frame(doe_vk_surface *surface,
    uint64_t timeout_ns, doe_vk_error *error);
/* Cleanup only: waits an already pending acquisition/presentation, discards a
 * quiescent unpresented frame, then unconfigures. Never starts a fresh acquire.
 * Does not settle/release caller command or image/view leases; BUSY preserves
 * them for the existing completion owner. Success means teardown, not rendering. */
doe_vk_status doe_vk_surface_retire(doe_vk_surface *surface,
    uint64_t timeout_ns, doe_vk_error *error);
#ifdef __cplusplus
}
#endif
#endif
