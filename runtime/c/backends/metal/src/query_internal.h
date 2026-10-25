/* SPDX-License-Identifier: MIT */
#ifndef DOE_MTL_QUERY_INTERNAL_H
#define DOE_MTL_QUERY_INTERNAL_H
#include "internal.h"
struct doe_mtl_timestamps {
    doe_mtl_context *context;
    uint32_t references, count;
    void *native;
    bool *available;
    bool poisoned;
    doe_mtl_commands *owner;
};
typedef struct doe_mtl_timestamp_use {
    doe_mtl_timestamps *timestamps;
    bool *available, *written;
    void *resolve_event; /* +1 native lease, retained with this recording. */
    uint64_t resolve_value;
    struct doe_mtl_timestamp_use *next;
} doe_mtl_timestamp_use;
doe_mtl_status doe_mtl_timestamps_preflight(const doe_mtl_commands *commands,
    const doe_mtl_timestamps *timestamps, uint32_t index, bool write);
doe_mtl_status doe_mtl_use_timestamps(doe_mtl_commands *commands,
    doe_mtl_timestamps *timestamps, doe_mtl_timestamp_use **out);
void doe_mtl_timestamps_retire(doe_mtl_commands *commands, bool completed, bool success);
#endif
