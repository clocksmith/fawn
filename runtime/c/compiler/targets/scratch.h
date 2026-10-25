/* SPDX-License-Identifier: MIT
 * Caller-owned byte maps for private text-target closure analysis. */
#ifndef DOE_TARGET_SCRATCH_H
#define DOE_TARGET_SCRATCH_H
#include "../src/internal.h"
typedef struct { unsigned char *data; size_t capacity; } DoeTargetScratch;
/* No alignment beyond unsigned char. Borrow exclusively for one emit call;
 * disjoint from source, IR, result and output. Contents unspecified after return,
 * reusable without reset, no retained pointer or allocation. The current module
 * requires one byte per function and one per semantic type including primitives. */
static inline bool doe_target_scratch_bytes(const CompilerWorkspace *c, size_t *bytes) {
    if (c == NULL || bytes == NULL || c->type_count > UINT32_MAX - IR_PRIMITIVE_COUNT) return false;
    const uint64_t total = (uint64_t)c->function_count + c->type_count + IR_PRIMITIVE_COUNT;
    if (total > SIZE_MAX) return false;
    *bytes = (size_t)total;
    return true;
}
#endif
