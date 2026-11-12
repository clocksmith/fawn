/* SPDX-License-Identifier: MIT
 * Same-execution output proof for direct plans (doe.plan-output-proof/v1), as
 * doe-plan-executor emits it (runtime/zig/src/plan/plan_output_proof.zig) and
 * bench/native_compare_modules/plan_output_proof.py admits it: after each
 * timed kernel_dispatch that declares a command-graph output_oracle, the
 * queue drains (its time belongs to that row), the bound buffer range is read
 * back outside timing and compared with the frozen oracle. Observations borrow
 * command and oracle strings until the plan document is released. */
#ifndef DOE_C_CLI_PLAN_PROOF_H
#define DOE_C_CLI_PLAN_PROOF_H
#include "oracle.h"
#include "plan.h"
#include <stdio.h>

/* A plan dispatch's declared readback, observed in the same execution. */
typedef struct doe_plan_capture_observation {
    size_t step;                     /* Plan command index. */
    const doe_plan_capture *capture; /* Borrowed from the plan. */
    char sha256[65];
    bool has_u32;
    uint32_t u32; /* Little-endian value of a 4-byte capture. */
} doe_plan_capture_observation;

typedef struct doe_plan_observation {
    size_t command_seq;
    const doe_command *command;       /* Borrowed from the plan stream. */
    const doe_kernel_binding *binding; /* The oracle's target binding. */
    doe_output_oracle oracle;
    char actual_sha256[65];
    bool matched;
    uint64_t compared_value_count, mismatch_count;
} doe_plan_observation;

typedef struct doe_plan_proof {
    char plan_file_sha256[65]; /* The exact plan bytes this run parsed. */
    doe_plan_observation *items; /* Owned. */
    size_t count, capacity;
    doe_plan_capture_observation *captures; /* Owned. */
    size_t capture_count, capture_capacity;
} doe_plan_proof;

void doe_plan_proof_init(doe_plan_proof *proof, const char *plan_bytes, size_t size);
void doe_plan_proof_free(doe_plan_proof *proof);
/* Observes one successfully executed dispatch's declared oracle. The drain's
 * duration is added to `execution`; capture and comparison are not. A mismatch
 * is recorded (and merged into `evidence`) with *matched false and returns
 * true; a drain, capture, comparison or allocation failure returns false with
 * the reason, and nothing is recorded for the command. */
bool doe_plan_proof_observe(doe_plan_proof *proof, doe_runtime *runtime, size_t seq, const doe_command *command,
    doe_trace_execution *execution, doe_oracle_evidence *evidence, bool *matched, char *message, size_t capacity);
/* Reads one successfully executed dispatch's declared capture range. As for
 * an oracle, the drain is added to `execution` and the readback is not timed.
 * Returns false with the reason when the drain or readback fails. */
bool doe_plan_proof_capture(doe_plan_proof *proof, doe_runtime *runtime, size_t step, const doe_plan_capture *capture,
    doe_trace_execution *execution, char *message, size_t capacity);
/* Writes ,"timedOutputProof":{...} for the plan's SHA-256 identity, then
 * ,"readbackCaptures":[...] when any dispatch captured. */
bool doe_plan_proof_write(FILE *out, const doe_plan_proof *proof, const char *plan_sha256);
#endif
