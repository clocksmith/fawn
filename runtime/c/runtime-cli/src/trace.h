/* SPDX-License-Identifier: MIT
 * Runtime trace artifacts in the doe-zig-runtime trace v1 format
 * (runtime/zig/src/runtime/trace/trace.zig, checked by pipeline/trace/replay.py):
 * one JSON row per executed command, chained by a 64-bit FNV-1a-style payload
 * hash seeded with 0x9e3779b97f4a7c15, plus a run-summary meta object. With no
 * quirk matched and no semantic op, the payload hash is identical to the Zig
 * runtime's for the same sequence, label, kind and kernel. */
#ifndef DOE_RUNTIME_TRACE_H
#define DOE_RUNTIME_TRACE_H
#include "quirks.h"
#include "stream.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define DOE_TRACE_HASH_SEED UINT64_C(0x9e3779b97f4a7c15)

/* The row hash for a command and its quirk decision (NULL: none matched); no
 * semantic op is hashed. */
uint64_t doe_trace_payload_hash(uint64_t previous, uint64_t seq, const char *label, const char *tag,
    const char *kernel, const doe_quirk_decision *decision);

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
 * on a write failure. */
bool doe_trace_write_row(FILE *out, const doe_trace_identity *identity, uint64_t seq, uint64_t timestamp_ns,
    uint64_t hash, uint64_t previous, const doe_command *command, const doe_quirk_decision *decision,
    const doe_trace_execution *execution);

/* Writes a JSON string literal with the required escapes. */
bool doe_trace_write_string(FILE *out, const char *text);
#endif
