/* SPDX-License-Identifier: MIT */
#ifndef DOE_MTL_PROGRESS_H
#define DOE_MTL_PROGRESS_H
#include "../include/doe_metal.h"

/* Completion notification sink (owner decision 2026-10-31). acquire runs once
 * per registration under context serialization; notify runs exactly once per
 * acquire, from a Metal-owned thread after the command completes or fails, or
 * synchronously when registration fails. It observes; it never retires. */
typedef struct doe_mtl_completion_sink {
    void *data;
    void (*acquire)(void *data);
    void (*notify)(void *data);
} doe_mtl_completion_sink;

/* Acquired/dropped under context serialization. Only block may run unlocked.
 * This owns native-handle/code residency, never resources or completion state. */
typedef struct doe_mtl_wait_lease {
    doe_mtl_context *context;
    void *command;
    void *event; /* Borrowed through the retained context. */
    uint64_t value;
} doe_mtl_wait_lease;
/* Before any command escapes the new context: every later submission registers
 * a native completed handler that reports to sink (see native.h). UNSUPPORTED
 * when the native boundary has no completed-handler registration. */
doe_mtl_status doe_mtl_context_set_completion(doe_mtl_context *context,
    doe_mtl_completion_sink sink);
bool doe_mtl_commands_in_flight(const doe_mtl_commands *commands);
doe_mtl_commands *doe_mtl_commands_conflict(const doe_mtl_commands *commands);
doe_mtl_commands *doe_mtl_buffer_owner(const doe_mtl_buffer *buffer);
bool doe_mtl_commands_matches(const doe_mtl_commands *commands, const doe_mtl_wait_lease *lease);
doe_mtl_status doe_mtl_commands_hold(doe_mtl_commands *commands, doe_mtl_wait_lease *out);
doe_mtl_status doe_mtl_commands_block(const doe_mtl_wait_lease *lease, uint64_t timeout_ns);
void doe_mtl_commands_unhold(doe_mtl_wait_lease *lease);
#endif
