/* SPDX-License-Identifier: MIT */
#ifndef DOE_METAL_DRAW_GUARD_H
#define DOE_METAL_DRAW_GUARD_H
#include "doe_metal.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct doe_mtl_draw_guard doe_mtl_draw_guard;
enum { DOE_MTL_DRAW_GUARD_SOURCE_VERSION = 2 };
/* Exact common/doe_draw_guard.h V2 normalizer and index scan, compiled upstream.
 * Slots are compiler metadata, never guessed from source/native namespaces. */
typedef struct doe_mtl_draw_guard_desc {
    uint32_t source_version; /* V2 common source and exact 40-byte argument record. */
    doe_mtl_pipeline_desc normalize, scan;
    uint32_t record_slot, scan_slots[3]; /* record, packed indices, window params */
    uint32_t max_groups_x;
} doe_mtl_draw_guard_desc;
doe_mtl_status doe_mtl_draw_guard_create(doe_mtl_context *, const doe_mtl_draw_guard_desc *,
    doe_mtl_draw_guard **, doe_mtl_error *);
void doe_mtl_draw_guard_release(doe_mtl_draw_guard *);
typedef struct doe_mtl_guarded_draw {
    doe_mtl_draw_guard *guard;
    doe_mtl_buffer *arguments; /* NULL seeds direct scalar draw arguments. */
    uint64_t argument_offset, index_size; /* Exact logical bound index span. */
    uint32_t first_index;
    bool first_instance_allowed;
} doe_mtl_guarded_draw;
#ifdef __cplusplus
}
#endif
#endif
