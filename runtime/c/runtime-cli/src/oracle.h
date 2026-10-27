/* SPDX-License-Identifier: MIT
 * Oracle validation is outside measured work. Evidence borrows descriptor
 * strings until the command stream is released; capture/reference bytes are
 * privately allocated and released by this owner. */
#ifndef DOE_C_CLI_ORACLE_H
#define DOE_C_CLI_ORACLE_H
#include "execute.h"

typedef struct doe_oracle_evidence {
    uint64_t count, matched_count, failed_count;
    char actual_sha256[65];
    bool has_actual_sha256, completion_unknown;
    doe_output_oracle descriptor;
    uint64_t compared_value_count, mismatch_count;
    double max_absolute_error, max_relative_error;
} doe_oracle_evidence;

/* Shape/target validation, plus reference-file size and digest validation when
 * validate_files is true. No device is created. */
bool doe_oracle_preflight(const doe_stream *stream, bool validate_files, char *message, size_t capacity);
/* A mismatch is a successful comparison with *matches false. I/O/shape failure
 * returns false. Actual digest and finite-value metrics are never fabricated. */
bool doe_oracle_compare_bytes(const doe_output_oracle *oracle, const uint8_t *actual, size_t size,
    doe_oracle_evidence *evidence, bool *matches, char *message, size_t capacity);
/* Fresh command-graph session, or isolated kernel buffers, matching the declared
 * scope. All submissions complete before capture. Unconfirmed close retains its
 * runtime and sets completion_unknown; it never authorizes another GPU row. */
bool doe_oracle_validate(doe_runtime *timed_runtime, const doe_runtime_options *options,
    const doe_stream *stream, doe_oracle_evidence *evidence, char *message, size_t capacity);
#endif
