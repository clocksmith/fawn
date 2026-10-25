/* SPDX-License-Identifier: MIT
 * doe-zig-runtime evaluates numeric stability (runtime/numeric_stability) for
 * every kernel_dispatch whose semantic op id, stage, phase and kernel basename
 * match an enabled auto-detect profile of the policy registry, whenever it
 * writes a trace meta. This runtime does not implement that evaluation; this
 * finds such a command so the run is refused before device work. */
#ifndef DOE_RUNTIME_NUMERIC_GATE_H
#define DOE_RUNTIME_NUMERIC_GATE_H
#include "stream.h"
#include <stdbool.h>
#include <stddef.h>

#define DOE_NUMERIC_STABILITY_DEFAULT_POLICY "config/numeric-stability-policy.json"

/* True when a kernel_dispatch carries the op id, stage and phase the
 * evaluation needs, so the policy must be consulted. */
bool doe_numeric_gate_needed(const doe_stream *stream);

typedef enum doe_numeric_gate_result {
    DOE_NUMERIC_GATE_CLEAR,    /* No command would be evaluated. */
    DOE_NUMERIC_GATE_OBSERVED, /* *command_index would be evaluated under `profile`. */
    DOE_NUMERIC_GATE_INVALID   /* The policy is not a readable registry; reason in `message`. */
} doe_numeric_gate_result;

doe_numeric_gate_result doe_numeric_gate_check(const char *policy_text, size_t policy_size, const doe_stream *stream,
    size_t *command_index, char *profile, size_t profile_size, char *message, size_t message_size);
#endif
