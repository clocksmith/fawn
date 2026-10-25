/* SPDX-License-Identifier: MIT
 * The numeric stability policy registry (config/numeric-stability-policy.json,
 * schema 3), as doe-zig-runtime's runtime/numeric_stability/policy.zig loads
 * it: auto-detect profiles, trigger and routing policies, route decision
 * metadata and the selected ordinary-execution profile. Values stay in the
 * parsed document; lookups return its objects. */
#ifndef DOE_RUNTIME_NUMERIC_POLICY_H
#define DOE_RUNTIME_NUMERIC_POLICY_H
#include "json.h"
#include "stream.h"
#include <stdbool.h>
#include <stddef.h>

#define DOE_NUMERIC_STABILITY_DEFAULT_POLICY "config/numeric-stability-policy.json"
/* The one capture plan this runtime evaluates (runtime_plan.zig
 * MATMUL_DECODE_CAPTURE_PLAN_ID); dispatches matching any other plan are refused. */
#define DOE_NS_DECODE_FINAL_LOGITS_PLAN "decode-final-logits-matmul-gemv-bindings-v1"

typedef struct doe_ns_policy {
    doe_json_document document;
    const char *path; /* As given; not owned. */
    const doe_json_value *root;
    const doe_json_value *execution_profile; /* The selected ordinary-execution profile. */
} doe_ns_policy;

/* Parses and validates the registry and selects `execution_profile_id` (NULL:
 * the registry default). False with the reason in message. */
bool doe_ns_policy_load(const char *text, size_t size, const char *path, const char *execution_profile_id,
    doe_ns_policy *policy, char *message, size_t message_size);
void doe_ns_policy_free(doe_ns_policy *policy);

/* A string member, or NULL. */
const char *doe_ns_string(const doe_json_value *object, const char *key);
/* The element of array `array_key` whose `id_key` equals `id`, or NULL. */
const doe_json_value *doe_ns_find(const doe_json_value *object, const char *array_key, const char *id_key, const char *id);
/* policy.zig resolveAutoDetectProfile: the first enabled profile matching the
 * dispatch's op id, stage, phase and kernel basename, or NULL. */
const doe_json_value *doe_ns_auto_detect(const doe_ns_policy *policy, const doe_command *command);

/* True when a kernel_dispatch carries the op id, stage and phase evaluation needs. */
bool doe_ns_semantic_complete(const doe_command *command);
#endif
