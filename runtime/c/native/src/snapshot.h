/* SPDX-License-Identifier: MIT
 * Internal owner of GPU-state snapshots (include/doe_native_snapshot.h).
 * snapshot.c owns creation, slot preparation and marks readback through the
 * public API; snapshot_marks.c owns the device-mutex hooks that queue lowering
 * calls. Every hook below requires the caller to hold the device mutex. */
#ifndef DOE_NATIVE_SNAPSHOT_INTERNAL_H
#define DOE_NATIVE_SNAPSHOT_INTERNAL_H
#include "internal.h"
#include "doe_native_snapshot.h"

enum { NATIVE_SNAPSHOT_PAGE_BYTES = 1u << DOE_COMPILER_WRITE_MARK_PAGE_SHIFT,
    NATIVE_SNAPSHOT_TILE_TEXELS = 1u << DOE_COMPILER_WRITE_MARK_TILE_SHIFT };

typedef struct native_snapshot_resource {
    doe_resource_buffer *buffer;       /* Base buffer, retained; NULL for a texture. */
    doe_resource_texture *texture;     /* Base texture, retained; NULL for a buffer. */
    WGPUBuffer slot_buffer;            /* Private slot copy. */
    WGPUTexture slot_texture;
    uint64_t size;                     /* Bytes of the resource. */
    uint32_t width, height, texel_bytes, tiles_x, tiles_y;
    uint32_t first, units;             /* Mark range: first index and page or tile count. */
    native_buffer_handle saved_buffer; /* Base handles while a branch is lowered. */
    doe_vk_texture *saved_texture;
} native_snapshot_resource;

/* One per base view a lowering meets, released by native_snapshot_lower_end. */
typedef struct native_snapshot_view {
    doe_resource_view *view;           /* Base view, kept alive by the submission's commands. */
    doe_vk_texture_view *slot;         /* Same view description over the slot image. */
    doe_vk_texture_view *saved;
    size_t resource;
} native_snapshot_view;

typedef struct native_snapshot {
    native_device *device;             /* The snapshot holds one device reference. */
    size_t count, buffer_count;
    native_snapshot_resource *resources;
    native_snapshot_view *views;
    size_t view_count, view_capacity;
    WGPUBuffer marks;                  /* One u32 per page or tile, then the sink word. */
    uint32_t mark_words;
    uint8_t *host_marks;               /* Submission-time marks of the open branch. */
    uint8_t *last_marks;               /* GPU and host marks of the last ended branch. */
    bool active, filled, swapped;
    DoeNativeSnapshotLedger ledger;
} native_snapshot;

/* Pipeline creation: the module's writable resources and whether the selected
 * SPIR-V carries marks. False when a marked pipeline cannot be represented. */
bool native_marks_pipeline(native_pipeline *pipeline, const doe_shader_view *view);
/* Dispatch lowering: the hidden mark binding and immediate words. */
doe_vk_status native_marks_dispatch(native_device *device, const native_pipeline *pipeline,
    const doe_vk_binding_resource *bindings, doe_vk_binding_resource *mark, uint32_t *words);
/* Submission lowering of an open branch: refuses unmarked writes, records the
 * host marks of fixed-function writes and lowers tracked resources against
 * the slot until native_snapshot_lower_end. A no-op outside a branch. */
native_failure native_snapshot_lower_begin(native_device *device, doe_resource_commands *const *commands,
    size_t count);
void native_snapshot_lower_end(native_device *device);
/* True when an open branch tracks the queue-write destination. */
bool native_snapshot_tracks_buffer(const native_device *device, const doe_resource_buffer *buffer);
bool native_snapshot_tracks_texture(const native_device *device, const doe_resource_texture *texture);
/* The texel size of a format the snapshot can copy by tiles, else 0. */
uint32_t native_snapshot_texel_bytes(WGPUTextureFormat format);
#endif
