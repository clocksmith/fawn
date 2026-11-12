/* SPDX-License-Identifier: MIT
 * Direct execution plans (bench/plans/generated/NAME.plan.json), as
 * doe-plan-executor reads and lowers them (runtime/zig/src/plan): buffer_write,
 * buffer_load (bytes from the synthetic asset cache) and kernel_dispatch with
 * buffer bindings, each carrying the plan's semantic identity (op id
 * step-NNNNNN, stage runtime_plan, the command kind as phase, the plan SHA-256
 * as execution plan hash). A dispatch's command-graph output_oracle stays on
 * the command for the same-execution proof (plan_proof.h), and its
 * captureBufferHandle/captureOffset/captureSize readback is kept for the
 * same-execution capture record. Plan fields this runtime does not know are
 * rejected. */
#ifndef DOE_RUNTIME_PLAN_H
#define DOE_RUNTIME_PLAN_H
#include "stream.h"
#include <stdbool.h>
#include <stddef.h>

/* Bytes [offset, offset + size) of buffer `handle` after the dispatch, with the
 * compatibility-stream semantic labels that name the value (document-owned,
 * NULL when absent). The row keeps the plan step identity. */
typedef struct doe_plan_capture {
    bool present;
    uint64_t handle, offset, size;
    const char *op_id, *stage, *phase;
    bool has_token_index;
    uint32_t token_index;
} doe_plan_capture;

typedef struct doe_plan {
    doe_stream stream;       /* The lowered commands; labels "buffer_load" for loads. */
    const char *workload_id; /* Document-owned. */
    const char *ir_path;
    const char *plan_sha256;
    char (*op_ids)[32];      /* step-NNNNNN per command (Zig formats into 32 bytes). */
    doe_plan_capture *captures; /* One per command; NULL when no command captures. */
    size_t capture_count;
    /* kernel_dispatch commands whose own semantic fields the plan step
     * identity supersedes in rows, as doe-plan-executor does. */
    size_t superseded_count;
    size_t first_superseded;
} doe_plan;

/* The synthetic asset cache: DOE_BENCH_ASSET_CACHE_DIR, else
 * $HOME/.cache/doe/bench_synthetic_assets. Caller frees. */
char *doe_plan_asset_root(void);
/* Parses and lowers a plan, reading buffer_load assets under `asset_root`.
 * Counts must equal the commands, as plan_validation.zig requires. On failure
 * *plan is left empty and error names the first rejected command. */
bool doe_plan_parse(const char *text, size_t size, const char *asset_root, doe_plan *plan, doe_stream_error *error);
/* The same validation and identities, without reading buffer_load assets.
 * These commands are for dry_run traces only: load payloads are not materialized
 * and must never be submitted to an execution backend. */
bool doe_plan_parse_dry_run(const char *text, size_t size, doe_plan *plan, doe_stream_error *error);
void doe_plan_free(doe_plan *plan);
/* The IR's shared.kernelRoot, else bench/inference-pipeline/kernels (also when
 * the IR cannot be read), as doe-plan-executor resolves it. Caller frees. */
char *doe_plan_kernel_root(const char *ir_path);
#endif
