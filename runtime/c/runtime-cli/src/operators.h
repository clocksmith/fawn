/* SPDX-License-Identifier: MIT
 * Operator records for semantic operations, in doe-zig-runtime's format
 * (runtime/zig/src/runtime/trace/operator_artifacts.zig): <anchor>.operators.json
 * lists one record per executed command with semantic identity, and each record
 * has a structural repro bundle (<anchor>.opNNNN.repro.commands.json and
 * .repro.meta.json) and, when the command requests one, a buffer capture
 * (<anchor>.opNNNN.capture.bin). Repro bundles are structural, not bitwise. */
#ifndef DOE_RUNTIME_OPERATORS_H
#define DOE_RUNTIME_OPERATORS_H
#include "stream.h"
#include "host_io.h"
#include "trace.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

typedef struct doe_operator_recorder {
    char *anchor;
    char *manifest_path;
    doe_host_output manifest; /* Private output bytes, published and hashed at finish. */
    char *manifest_bytes;
    size_t manifest_size;
    uint64_t row_count, capture_count, repro_count;
    char manifest_sha256[65];
} doe_operator_recorder;

/* The capture a command requested: "ok" with its bytes, or "error" with a code. */
typedef struct doe_operator_capture {
    bool ok;
    const uint8_t *data;
    size_t size;
    const char *error_code;
} doe_operator_capture;

typedef struct doe_operator_input {
    uint64_t source_index; /* The row's sequence number. */
    const doe_command *command;
    const doe_trace_execution *execution;
    const char *backend_id;
    const char *vendor, *api, *family, *driver; /* The quirk matching profile. */
    uint64_t hash, previous;
    const char *trace_meta_path; /* NULL: no trace meta is written. */
    const doe_operator_capture *capture; /* NULL: the command requests none. */
} doe_operator_input;

/* Starts an empty manifest for `anchor` (the trace meta path, else the trace
 * JSONL path). False when out of memory. */
bool doe_operator_recorder_open(doe_operator_recorder *recorder, const char *anchor);
/* Records one command with semantic identity and writes its repro bundle and
 * capture; commands without semantic identity are skipped. False with the
 * failing path in `message` when a file cannot be written. */
bool doe_operator_record(doe_operator_recorder *recorder, const doe_operator_input *input, char *message,
    size_t message_size);
/* Writes <anchor>.operators.json and its SHA-256. False on a write failure. */
bool doe_operator_recorder_finish(doe_operator_recorder *recorder);
void doe_operator_recorder_free(doe_operator_recorder *recorder);
#endif
