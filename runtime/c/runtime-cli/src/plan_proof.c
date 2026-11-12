/* SPDX-License-Identifier: MIT */
#include "plan_proof.h"
#include "sha256.h"
#include "trace.h"
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

void doe_plan_proof_init(doe_plan_proof *proof, const char *plan_bytes, size_t size)
{
    *proof = (doe_plan_proof){0};
    uint8_t digest[32];
    doe_sha256(plan_bytes, size, digest);
    doe_sha256_hex(digest, proof->plan_file_sha256);
}

void doe_plan_proof_free(doe_plan_proof *proof)
{
    free(proof->items);
    *proof = (doe_plan_proof){0};
}

static const doe_kernel_binding *oracle_target(const doe_command *command, const doe_output_oracle *oracle)
{
    for (size_t i = 0; i < command->as.dispatch.binding_count; i++) {
        const doe_kernel_binding *binding = &command->as.dispatch.bindings[i];
        if (binding->group == oracle->group && binding->binding == oracle->binding && binding->resource == DOE_RESOURCE_BUFFER)
            return binding;
    }
    return NULL;
}

static bool reserve(doe_plan_proof *proof)
{
    if (proof->count < proof->capacity) return true;
    size_t grown_capacity = proof->capacity ? proof->capacity * 2 : 4;
    if (grown_capacity > SIZE_MAX / sizeof(*proof->items)) return false;
    doe_plan_observation *grown = realloc(proof->items, grown_capacity * sizeof(*grown));
    if (!grown) return false;
    proof->items = grown;
    proof->capacity = grown_capacity;
    return true;
}

bool doe_plan_proof_observe(doe_plan_proof *proof, doe_runtime *runtime, size_t seq, const doe_command *command,
    doe_trace_execution *execution, doe_oracle_evidence *evidence, bool *matched, char *message, size_t capacity)
{
    *matched = false;
    doe_output_oracle oracle;
    if (!doe_output_oracle_parse(command->as.dispatch.oracle, &oracle, message, capacity)) return false;
    const doe_kernel_binding *binding = oracle_target(command, &oracle);
    if (!binding || binding->buffer_size == 0 || binding->buffer_size == UINT64_MAX || binding->buffer_size > SIZE_MAX) {
        snprintf(message, capacity, "command %zu oracle binding %u.%u has no explicit buffer range", seq, oracle.group,
            oracle.binding);
        return false;
    }
    if (!reserve(proof)) {
        snprintf(message, capacity, "out of memory");
        return false;
    }
    /* The dispatch's completion is execution time, as doe-plan-executor
     * attributes it; the staging readback and comparison are not. */
    doe_trace_execution drain;
    if (!doe_runtime_flush(runtime, &drain)) {
        snprintf(message, capacity, "command %zu oracle drain: %s: %s", seq, drain.status_code, drain.message);
        return false;
    }
    execution->duration_ns += drain.duration_ns;
    execution->submit_wait_ns += drain.submit_wait_ns;
    uint8_t *bytes = NULL;
    doe_trace_execution capture;
    if (!doe_runtime_read_buffer_range(runtime, binding->handle, binding->buffer_offset, binding->buffer_size, &bytes,
            &capture)) {
        snprintf(message, capacity, "command %zu oracle capture: %s: %s", seq, capture.status_code, capture.message);
        return false;
    }
    doe_oracle_evidence observed = {0};
    bool compared = doe_oracle_compare_bytes(&oracle, bytes, (size_t)binding->buffer_size, &observed, matched, message,
        capacity);
    free(bytes);
    if (!compared) return false;
    doe_plan_observation *item = &proof->items[proof->count++];
    *item = (doe_plan_observation){.command_seq = seq, .command = command, .binding = binding, .oracle = oracle,
        .matched = *matched, .compared_value_count = observed.compared_value_count,
        .mismatch_count = observed.mismatch_count};
    memcpy(item->actual_sha256, observed.actual_sha256, sizeof(item->actual_sha256));
    evidence->count++;
    if (*matched) evidence->matched_count++;
    else evidence->failed_count++;
    evidence->descriptor = oracle;
    memcpy(evidence->actual_sha256, observed.actual_sha256, sizeof(evidence->actual_sha256));
    evidence->has_actual_sha256 = true;
    evidence->compared_value_count = observed.compared_value_count;
    evidence->mismatch_count = observed.mismatch_count;
    evidence->max_absolute_error = observed.max_absolute_error;
    evidence->max_relative_error = observed.max_relative_error;
    return true;
}

static bool write_observation(FILE *out, const doe_plan_observation *item)
{
    const doe_output_oracle *oracle = &item->oracle;
    bool ok = fprintf(out, "{\"commandSeq\":%zu,\"kernel\":", item->command_seq) >= 0 &&
        doe_trace_write_string(out, item->command->as.dispatch.kernel) && fputs(",\"entryPoint\":", out) >= 0 &&
        doe_trace_write_string(out, item->command->as.dispatch.entry_point) &&
        fprintf(out, ",\"bindingGroup\":%" PRIu32 ",\"binding\":%" PRIu32 ",\"resourceHandle\":%" PRIu64
            ",\"byteLength\":%" PRIu64 ",\"dispatchCount\":1,\"oracle\":{\"kind\":", oracle->group, oracle->binding,
            item->binding->handle, item->binding->buffer_size) >= 0 &&
        doe_trace_write_string(out, doe_output_oracle_kind_name(oracle->kind)) &&
        fputs(",\"referenceClass\":", out) >= 0 &&
        doe_trace_write_string(out, doe_output_oracle_reference_class_name(oracle->reference_class)) &&
        fputs(",\"expectedSha256\":", out) >= 0 && doe_trace_write_string(out, oracle->expected_sha256) &&
        fputs(",\"referenceId\":", out) >= 0 && doe_trace_write_string(out, oracle->reference_id) &&
        fputs(",\"referencePath\":", out) >= 0;
    ok = ok && (oracle->reference_path ? doe_trace_write_string(out, oracle->reference_path) : fputs("null", out) >= 0);
    return ok && fprintf(out, ",\"absoluteTolerance\":%.9g,\"relativeTolerance\":%.9g},\"actualSha256\":\"%s\""
        ",\"matched\":%s,\"comparedValueCount\":%" PRIu64 ",\"mismatchCount\":%" PRIu64 "}",
        (double)oracle->absolute_tolerance, (double)oracle->relative_tolerance, item->actual_sha256,
        item->matched ? "true" : "false", item->compared_value_count, item->mismatch_count) >= 0;
}

bool doe_plan_proof_write(FILE *out, const doe_plan_proof *proof, const char *plan_sha256)
{
    bool ok = fputs(",\"timedOutputProof\":{\"schema\":\"doe.plan-output-proof/v1\","
        "\"executionScope\":\"same-execution\",\"planSha256\":", out) >= 0 &&
        doe_trace_write_string(out, plan_sha256) &&
        fprintf(out, ",\"planFileSha256\":\"%s\",\"observations\":[", proof->plan_file_sha256) >= 0;
    for (size_t i = 0; ok && i < proof->count; i++)
        ok = (!i || fputc(',', out) != EOF) && write_observation(out, &proof->items[i]);
    return ok && fputs("]}", out) >= 0;
}
