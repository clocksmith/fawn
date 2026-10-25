/* SPDX-License-Identifier: MIT
 * Numeric stability evaluation in ordinary execution, as doe-zig-runtime's
 * runtime/numeric_stability Recorder does it, for matmul-logits, decode-final-logits, RMSNorm and attention capture plans
 * and explicit numeric annotations: after the dispatch runs, the fast (GPU)
 * logits are read back and compared with a serial f32 (stable) and an f64
 * (reference) evaluation on the host; the trigger and routing policies pick a
 * route, which may rewrite the logits with the stable values or stop the
 * stream; the following decode.sample_token dispatch is replayed as a decode
 * boundary. Receipts go to <meta>.numeric-stability.jsonl in the Zig receipt
 * format (field order, nulls and float spelling). Buffer access goes through
 * the caller's capture and write callbacks, so the evaluation is host-testable. */
#ifndef DOE_RUNTIME_NUMERIC_STABILITY_H
#define DOE_RUNTIME_NUMERIC_STABILITY_H
#include "numeric_policy.h"
#include "stream.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

typedef struct doe_ns_io {
    void *context;
    /* Bytes [offset, offset + size) of a buffer (size UINT64_MAX: to its end),
     * in a new allocation the caller of the callback frees. */
    bool (*capture)(void *context, uint64_t handle, uint64_t offset, uint64_t size, uint8_t **bytes, size_t *length,
        char *message, size_t message_size);
    /* Writes u32 words at a byte offset, as a buffer_write command. */
    bool (*write)(void *context, uint64_t handle, uint64_t offset, const uint32_t *words, size_t count, char *message,
        size_t message_size);
} doe_ns_io;

/* The receipt executionIdentity fields this runtime knows; the rest are null. */
typedef struct doe_ns_identity {
    const char *backend;
    const char *vendor, *api, *family, *driver;
} doe_ns_identity;

typedef struct doe_ns_candidate {
    uint32_t token_id;
    const char *label;
    double fast, stable, reference;
} doe_ns_candidate;

/* An evaluated observation and the route chosen for it. */
typedef struct doe_ns_evaluation {
    const doe_json_value *profile;
    const char *routing_policy_id;
    const doe_command *command;
    doe_ns_candidate *candidates;
    size_t candidate_count;
    uint32_t fast_token, stable_token, reference_token;
    char fast_digest[65], stable_digest[65], layout_fingerprint[65];
    uint64_t logits_handle, logits_offset;
    double *rewrite_values; /* Owned when output differs from decision logits (RMSNorm). */
    size_t rewrite_count;
    /* Route */
    bool checks[5]; /* firstDivergencePresent ... fastMissesExactReference */
    bool fired;
    const doe_json_value *trigger_policy, *routing_policy, *metadata;
    const char *decision, *selection_mode, *committed_mode, *downstream_action, *selected_policy_id;
    bool has_selected_token;
    uint32_t selected_token;
    bool effect_applied;
} doe_ns_evaluation;

typedef struct doe_ns_recorder {
    const doe_ns_policy *policy;
    char *receipt_path;
    FILE *receipts;
    uint32_t receipt_count, accept_fast, prefer_stable, abstain, first_divergence_count;
    uint32_t annotation_count, auto_detect_count, rewrite_count, stop_count;
    bool has_source; /* The last decode.final_logits evaluation, for its decode boundary. */
    doe_ns_evaluation source;
    doe_ns_identity source_identity;
} doe_ns_recorder;

/* Receipts for the trace meta at `meta_path`. False when out of memory. */
bool doe_ns_recorder_open(doe_ns_recorder *recorder, const doe_ns_policy *policy, const char *meta_path);
/* Evaluates one executed command: a decode boundary for a sample dispatch
 * after a decode.final_logits receipt, else an auto-detected observation;
 * `future` borrows the following commands for RMSNorm decision capture;
 * other commands record nothing. *stop is set when the route stops the
 * stream with `following` commands left. False with the reason in message. */
bool doe_ns_record(doe_ns_recorder *recorder, const doe_ns_io *io, const doe_ns_identity *identity,
    const doe_command *command, const doe_command *future, size_t following, bool *stop, char *message, size_t message_size);
/* ,"numericStability":{...} when any receipt was written (trace_meta.zig). */
bool doe_ns_write_summary(FILE *out, const doe_ns_recorder *recorder);
void doe_ns_recorder_free(doe_ns_recorder *recorder);

/* Appends explicit numeric metadata to a structural repro command. */
bool doe_ns_write_annotation(FILE *out, const doe_numeric_annotation *annotation);

/* Pure parts, exposed for host tests. */
/* Zig's std.json f64 spelling: shortest round-trip digits in plain decimal. */
bool doe_ns_write_f64(FILE *out, double value);
/* SHA-256 hex of the compact JSON array of `values`. */
bool doe_ns_digest(const double *values, size_t count, char out[65]);
/* runtime_plan.zig buildLayoutFingerprint for buffer bindings. */
bool doe_ns_layout_fingerprint(const doe_command *command, char out[65]);
/* Trigger checks, routing and route metadata for an evaluation with its
 * candidates, tokens and digests set. */
bool doe_ns_decide(const doe_ns_policy *policy, doe_ns_evaluation *evaluation, char *message, size_t message_size);
#endif
