/* SPDX-License-Identifier: MIT */
#ifndef DOE_VK_EXTERNAL_H
#define DOE_VK_EXTERNAL_H
#include "doe_vk_texture.h"
#ifdef __cplusplus
extern "C" {
#endif

/* Cross-process/cross-device image sharing on Linux: an OPAQUE_FD memory
 * import of another Vulkan device's exported image, synchronized with SYNC_FD
 * binary semaphores. Requires caps.external_fd (doe_vk_options.enable_external_fd
 * on a device whose adapter reported caps.external_fd_available). Nothing here
 * waits on the CPU: producer fences become queue waits, and the release signals
 * an exportable semaphore whose sync file carries completion to the consumer. */

/* The producer's exact image creation parameters. OPAQUE_FD import requires an
 * image created identically on the same physical device and driver; this owner
 * never substitutes, widens or narrows any of them. */
typedef struct doe_vk_external_image_desc {
    doe_vk_texture_format format; /* Native VkFormat: RGBA8/BGRA8 unorm or RGBA16 float. */
    uint32_t width, height;       /* 2D, one mip, one layer, one sample, optimal tiling. */
    uint32_t flags;               /* Producer VkImageCreateFlags: zero or MUTABLE_FORMAT. */
    uint32_t usage;               /* Producer VkImageUsageFlags, exactly. */
    uint32_t handle_types;        /* Producer VkExternalMemoryImageCreateInfo; includes OPAQUE_FD. */
    int memory_fd;                /* Borrowed; a duplicate is consumed by the import. */
    uint64_t allocation_size;
    uint32_t memory_type_index;
    bool dedicated;               /* The producer bound a dedicated allocation. */
} doe_vk_external_image_desc;

/* Reads a producer's VkImageCreateInfo (borrowed, caller-validated pointer) into
 * `out`, leaving the memory fields zero. Admits exactly a 2D, one-mip, one-layer,
 * single-sample, optimal, exclusive image whose only chained structure is
 * VkExternalMemoryImageCreateInfo naming OPAQUE_FD; anything else is
 * UNSUPPORTED or INVALID_ARGUMENT and leaves `out` unchanged. */
doe_vk_status doe_vk_external_image_desc_parse(const void *vk_image_create_info,
    doe_vk_external_image_desc *out);
/* One owned reference. The image starts unacquired: every command use fails
 * (INVALID_STATE) until an acquire is recorded, and after a release until the
 * next acquire. Destroy (evict) is BUSY: the memory lives until the last release. */
doe_vk_status doe_vk_texture_import_opaque_fd(doe_vk_context *context,
    const doe_vk_external_image_desc *desc, doe_vk_texture **out, doe_vk_error *error);
bool doe_vk_texture_imported(const doe_vk_texture *texture);

/* One producer semaphore payload to wait for: a sync file (-1 is already
 * signaled) or an OPAQUE_FD binary semaphore (a pending or completed signal
 * must already be submitted by its producer). Borrowed; duplicated on import. */
typedef struct doe_vk_external_wait {
    int fd;
    bool opaque;
} doe_vk_external_wait;
/* Records the consumer side of an ownership transfer from VK_QUEUE_FAMILY_EXTERNAL
 * with the producer's release layouts (old_layout -> new_layout). Each wait is
 * imported temporarily into a fresh binary semaphore that this recording's
 * submission waits for at ALL_COMMANDS. A new
 * layout of UNDEFINED, an UNDEFINED old layout or initialized=false discards the
 * contents, which are then zero-initialized by the canonical attachment owner. */
enum { DOE_VK_EXTERNAL_WAITS = 8 };
doe_vk_status doe_vk_commands_external_acquire(doe_vk_commands *commands, doe_vk_texture *texture,
    const doe_vk_external_wait *waits, uint32_t wait_count, uint32_t old_layout, uint32_t new_layout,
    bool initialized);
/* Records the transfer back to VK_QUEUE_FAMILY_EXTERNAL without a layout change
 * (an UNDEFINED image is released as GENERAL with undefined contents) and arms
 * an exportable SYNC_FD semaphore signaled by this recording's submission.
 * Outputs the released layout and whether the image holds initialized contents. */
doe_vk_status doe_vk_commands_external_release(doe_vk_commands *commands, doe_vk_texture *texture,
    uint32_t *layout, bool *initialized);
/* After the queue accepted a release recording: a new owned sync file that
 * signals when that submission completes. The semaphore stays owned by the
 * recording until its completion is proven. */
doe_vk_status doe_vk_commands_export_sync_fd(doe_vk_commands *commands, int *fd, doe_vk_error *error);
/* The one POSIX descriptor owner for callers holding sync files: a duplicate
 * (-1 stays -1 or reports failure) and an idempotent close of a valid descriptor. */
int doe_vk_external_duplicate_fd(int fd);
void doe_vk_external_close_fd(int fd);
#ifdef __cplusplus
}
#endif
#endif
