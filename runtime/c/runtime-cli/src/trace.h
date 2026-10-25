/* SPDX-License-Identifier: MIT
 * Runtime trace artifacts in the doe-zig-runtime trace v1 format
 * (runtime/zig/src/runtime/trace/trace.zig, checked by pipeline/trace/replay.py):
 * one JSON row per command, chained by a 64-bit FNV-1a-style payload
 * hash seeded with 0x9e3779b97f4a7c15, plus a run-summary meta object. The
 * payload hash covers the sequence, label, kind, kernel, quirk decision and
 * semantic identity in doe-zig-runtime's order and byte encoding. */
#ifndef DOE_RUNTIME_TRACE_H
#define DOE_RUNTIME_TRACE_H
#include "quirks.h"
#include "stream.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define DOE_TRACE_HASH_SEED UINT64_C(0x9e3779b97f4a7c15)

/* The row hash for a command, its quirk decision (NULL: none matched) and its
 * semantic identity (NULL: none). */
uint64_t doe_trace_payload_hash(uint64_t previous, uint64_t seq, const char *label, const char *tag,
    const char *kernel, const doe_quirk_decision *decision, const doe_semantic *semantic);

typedef struct doe_trace_execution {
    bool ok;
    const char *status_code; /* Static or caller-owned for the call. */
    char message[256];
    uint64_t duration_ns, setup_ns, encode_ns, submit_wait_ns;
    uint64_t dispatch_count, submit_count;
} doe_trace_execution;

typedef struct doe_trace_identity {
    const char *module;     /* "doe-c-runtime" */
    const char *backend_id; /* "doe_c_vulkan" */
} doe_trace_identity;

/* Writes one row (no trailing newline handling beyond one '\n'). Returns false
 * on a write failure. A NULL execution writes a host-only row with no native
 * execution fields. */
bool doe_trace_write_row(FILE *out, const doe_trace_identity *identity, uint64_t seq, uint64_t timestamp_ns,
    uint64_t hash, uint64_t previous, const doe_command *command, const doe_quirk_decision *decision,
    const doe_trace_execution *execution);

/* Writes one plan row as doe-plan-executor does (trace_jsonl_emit.zig
 * writePlanTraceRow): opCode is the command label and there are no quirk
 * decision fields. */
bool doe_trace_write_plan_row(FILE *out, const doe_trace_identity *identity, uint64_t seq, uint64_t timestamp_ns,
    uint64_t hash, uint64_t previous, const doe_command *command, const doe_trace_execution *execution);

/* Command must be a kernel_dispatch. Writes its normalized fields without braces. */
bool doe_trace_write_dispatch_fields(FILE *out, const doe_command *command);

/* Writes a JSON string literal with the required escapes. */
bool doe_trace_write_string(FILE *out, const char *text);
/* Writes ,"semanticOpId":... for each present semantic field, in
 * doe-zig-runtime's order (op id, stage, phase, token, layer, plan hash). */
bool doe_trace_write_semantic(FILE *out, const doe_semantic *semantic);
#endif
