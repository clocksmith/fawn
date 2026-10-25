/* SPDX-License-Identifier: MIT */
#ifndef DOE_RUNTIME_NUMERIC_ANNOTATION_H
#define DOE_RUNTIME_NUMERIC_ANNOTATION_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
/* Strings borrow the owning stream document; candidates belong to the command. */
typedef struct doe_numeric_capture {
    uint64_t handle, offset;
    uint32_t count; /* Elements for vectors, row stride for weights. */
} doe_numeric_capture;
typedef struct doe_numeric_candidate {
    uint32_t token_id, row_index;
    const char *label;
    bool has_bias;
    double bias;
} doe_numeric_candidate;
typedef struct doe_numeric_annotation {
    const char *operator_family, *trigger_policy_id, *routing_policy_id, *fast_policy_id, *stable_policy_id;
    doe_numeric_capture hidden, logits, weights;
    doe_numeric_candidate *candidates;
    size_t candidate_count;
} doe_numeric_annotation;
#endif
