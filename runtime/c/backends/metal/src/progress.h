/* SPDX-License-Identifier: MIT */
#ifndef DOE_MTL_PROGRESS_H
#define DOE_MTL_PROGRESS_H
#include "../include/doe_metal.h"

/* Acquired/dropped under context serialization. Only block may run unlocked.
 * This owns native-handle/code residency, never resources or completion state. */
typedef struct doe_mtl_wait_lease {
    doe_mtl_context *context;
    void *command;
} doe_mtl_wait_lease;
bool doe_mtl_commands_in_flight(const doe_mtl_commands *commands);
doe_mtl_commands *doe_mtl_commands_conflict(const doe_mtl_commands *commands);
doe_mtl_commands *doe_mtl_buffer_owner(const doe_mtl_buffer *buffer);
bool doe_mtl_commands_matches(const doe_mtl_commands *commands, const doe_mtl_wait_lease *lease);
doe_mtl_status doe_mtl_commands_hold(doe_mtl_commands *commands, doe_mtl_wait_lease *out);
doe_mtl_status doe_mtl_commands_block(const doe_mtl_wait_lease *lease, uint64_t timeout_ns);
void doe_mtl_commands_unhold(doe_mtl_wait_lease *lease);
#endif
