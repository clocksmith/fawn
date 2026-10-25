/* SPDX-License-Identifier: MIT */
#include "stream_internal.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static bool numeric_capture(const doe_json_value *object, doe_numeric_capture *out, bool weights,
    size_t index, doe_stream_error *error)
{
    static const char *const vector_keys[] = {"buffer_handle", "bufferHandle", "offset", "element_count", "elementCount", NULL};
    static const char *const weight_keys[] = {"buffer_handle", "bufferHandle", "offset", "row_stride_elements", "rowStrideElements", NULL};
    uint64_t count;
    if (!object || object->kind != DOE_JSON_OBJECT)
        return stream_reject(error, index, "numeric capture must be an object");
    if (!stream_only_members(object, weights ? weight_keys : vector_keys, index, "numeric capture", error)) return false;
    if (!stream_read_u64(object, "buffer_handle", "bufferHandle", UINT64_MAX, true, 0, &out->handle, index, error) ||
        !stream_read_u64(object, "offset", NULL, UINT64_MAX, false, 0, &out->offset, index, error) ||
        !stream_read_u64(object, weights ? "row_stride_elements" : "element_count", weights ? "rowStrideElements" : "elementCount",
            UINT32_MAX, true, 0, &count, index, error)) return false;
    if (!count || count > (UINT64_MAX - out->offset) / 4)
        return stream_reject(error, index, "numeric capture extent is empty or overflows");
    out->count = (uint32_t)count;
    return true;
}

bool stream_parse_numeric(const doe_json_value *object, doe_command *command, size_t index, doe_stream_error *error)
{
    bool ambiguous = false;
    const doe_json_value *value = stream_either(object, "numeric_stability", "numericStability", &ambiguous);
    if (ambiguous) return stream_reject(error, index, "numeric stability names both aliases");
    if (!value || value->kind == DOE_JSON_NULL) return true;
    static const char *const keys[] = {"operator_family", "operatorFamily", "trigger_policy_id", "triggerPolicyId",
        "routing_policy_id", "routingPolicyId", "fast_policy_id", "fastPolicyId", "stable_policy_id", "stablePolicyId",
        "hidden_state", "hiddenState", "logits", "weights", "candidates", NULL};
    if (value->kind != DOE_JSON_OBJECT) return stream_reject(error, index, "numeric stability must be an object");
    if (!stream_only_members(value, keys, index, "numeric stability", error)) return false;
    command->numeric = calloc(1, sizeof(doe_numeric_annotation));
    if (!command->numeric) return stream_reject(error, index, "out of memory");
    doe_numeric_annotation *out = command->numeric;
    if (!stream_read_string(value, "operator_family", "operatorFamily", &out->operator_family, index, error) ||
        !stream_read_string(value, "trigger_policy_id", "triggerPolicyId", &out->trigger_policy_id, index, error) ||
        !stream_read_string(value, "routing_policy_id", "routingPolicyId", &out->routing_policy_id, index, error) ||
        !stream_read_string(value, "fast_policy_id", "fastPolicyId", &out->fast_policy_id, index, error) ||
        !stream_read_string(value, "stable_policy_id", "stablePolicyId", &out->stable_policy_id, index, error)) return false;
    if (!out->operator_family) out->operator_family = "lm-head-slice";
    if (!out->trigger_policy_id) out->trigger_policy_id = "numeric-instability/selected-token-disagreement-with-reference-improvement-v1";
    if (!out->routing_policy_id) out->routing_policy_id = "numeric-stability/prefer-stable-on-selected-token-disagreement-v1";
    if (!out->fast_policy_id) out->fast_policy_id = "lm-head-slice/forward-f16accum-v1";
    if (!out->stable_policy_id) out->stable_policy_id = "lm-head-slice/forward-serial-v1";
    const doe_json_value *hidden = stream_either(value, "hidden_state", "hiddenState", &ambiguous);
    if (ambiguous) return stream_reject(error, index, "numeric hidden state names both aliases");
    if (!numeric_capture(hidden, &out->hidden, false, index, error) ||
        !numeric_capture(doe_json_member_value(value, "logits"), &out->logits, false, index, error) ||
        !numeric_capture(doe_json_member_value(value, "weights"), &out->weights, true, index, error)) return false;
    const doe_json_value *candidates = doe_json_member_value(value, "candidates");
    if (!candidates || candidates->kind != DOE_JSON_ARRAY || candidates->count < 2 ||
        candidates->count > SIZE_MAX / sizeof(doe_numeric_candidate))
        return stream_reject(error, index, "numeric stability needs at least two candidates");
    out->candidates = calloc(candidates->count, sizeof(doe_numeric_candidate));
    if (!out->candidates) return stream_reject(error, index, "out of memory");
    out->candidate_count = candidates->count;
    static const char *const candidate_keys[] = {"token_id", "tokenId", "row_index", "rowIndex", "label", "bias", NULL};
    for (size_t i = 0; i < candidates->count; i++) {
        const doe_json_value *item = candidates->items[i];
        doe_numeric_candidate *candidate = &out->candidates[i];
        uint64_t token, row;
        if (item->kind != DOE_JSON_OBJECT) return stream_reject(error, index, "numeric candidate must be an object");
        if (!stream_only_members(item, candidate_keys, index, "numeric candidate", error) ||
            !stream_read_u64(item, "token_id", "tokenId", UINT32_MAX, true, 0, &token, index, error) ||
            !stream_read_u64(item, "row_index", "rowIndex", UINT32_MAX, true, 0, &row, index, error)) return false;
        candidate->token_id = (uint32_t)token;
        candidate->row_index = (uint32_t)row;
        const doe_json_value *label = doe_json_member_value(item, "label");
        if (label && label->kind != DOE_JSON_NULL && !stream_read_string(item, "label", NULL, &candidate->label, index, error)) return false;
        const doe_json_value *bias = doe_json_member_value(item, "bias");
        if (bias && bias->kind != DOE_JSON_NULL) {
            if (bias->kind != DOE_JSON_NUMBER || !isfinite(bias->number)) return stream_reject(error, index, "numeric bias must be finite");
            candidate->has_bias = true;
            candidate->bias = bias->number;
        }
    }
    return true;
}
