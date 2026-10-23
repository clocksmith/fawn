/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors
 * Frozen M7 job ABI contract. */
#ifndef DOE_COMPILER_JOB_H
#define DOE_COMPILER_JOB_H
#include "doe_compiler_v2.h"
#ifdef __cplusplus
extern "C" {
#endif
#define DOE_COMPILER_JOB_ABI_VERSION 3u
#define DOE_COMPILER_JOB_MAX_EXPRESSION_DEPTH 64u
#define DOE_COMPILER_JOB_MAX_CONTROL_DEPTH 32u

typedef struct {
    size_t source_bytes;
    size_t tokens;       /* Excludes the internally reserved end token. */
    size_t statements;  /* Source statements; see M7 accounting, not generated IR. */
    size_t expressions; /* Source scalar nodes; grouping adds depth, not nodes. */
    size_t words;       /* Private output staging capacity, in uint32_t words. */
    uint32_t expression_depth;
    uint32_t control_depth;
} DoeCompilerJobCapacities;

/* Caller-controlled limits, not language/device limits or serialized layout.
 * Names/resources/flat structs/compute inputs retain the v2 fixed envelope.
 * Source spans remain uint32_t: source_bytes must not exceed UINT32_MAX-1 (exact uint32 diagnostics).
 * Counts, derived capacities, byte products, offsets and alignment are checked.
 * Depths must be 1..the respective JOB_MAX; other zero capacities are valid.
 * NULL requirements -> INPUT_ERROR. Wrong version/exact public struct sizes ->
 * ABI_MISMATCH. Any failed query leaves requirements untouched. NULL/invalid
 * capacities or unrepresentable derived layout -> INPUT_ERROR.
 * Success writes size_bytes and alignment_bytes, the latter <= max_align_t. */
DoeCompilerStatus doe_compiler_job_requirements(
    uint32_t abi_version,
    const DoeCompilerJobCapacities *capacities, size_t capacities_size,
    DoeCompilerWorkspaceRequirements *requirements, size_t requirements_size);

/* Allocate/free workspace in the caller with malloc or sufficient aligned
 * allocation, using a successful query for the SAME capacities/library.
 * There are no library allocations, hidden heap fallback, persistent handles,
 * retained pointers, reset calls or destruction obligations. Therefore this
 * library cannot report allocator OOM. Caller allocation failure is separate.
 * One call owns the workspace until return; independent workspaces are safe to
 * use concurrently. Reuse after any admitted result requires no reset.
 * Capacities, source, workspace, words and result must be mutually disjoint valid
 * spans. Source need not be NUL-terminated. NULL source/words require zero length/
 * capacity. NULL result -> INPUT_ERROR without writing. Wrong version/exact
 * capacities/result size -> ABI_MISMATCH without touching any caller storage.
 * Otherwise result is cleared before validation. Invalid capacities, NULL,
 * misaligned or short workspace -> INPUT_ERROR at offset zero without workspace
 * access; words remain unchanged. The capacity query itself never reads source.
 * Source/token/statement/expression/depth exhaustion -> INPUT_ERROR with the
 * offending source offset and named exhausted budget. Internal/staging/caller
 * word-capacity exhaustion -> OUTPUT_TOO_SMALL at source end. No truncation.
 * Other failures retain existing PARSE_ERROR, UNSUPPORTED and SEMANTIC_ERROR.
 * Every failure preserves all words and leaves word_count/reflection zero.
 * A successful call publishes words and the existing v2 reflection only after
 * all fallible phases finish. The result owns no pointers into workspace.
 * Extra physical workspace bytes never expand the configured capacities. */
DoeCompilerStatus doe_compiler_compile_job(
    uint32_t abi_version,
    const DoeCompilerJobCapacities *capacities, size_t capacities_size,
    void *workspace, size_t workspace_size,
    const char *source, size_t source_length,
    uint32_t *words, size_t word_capacity,
    DoeCompilerResultV2 *result, size_t result_size);
#ifdef __cplusplus
}
#endif
#endif
