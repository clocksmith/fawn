/* SPDX-License-Identifier: MIT
 * Actual host f16/f32/f64 serial matmul from numeric_stability_eval.zig.
 * Trigger and routing policy evaluation remains in runtime-cli's owner. */
#include "module.h"
#include "numeric_stability.h"
#include <inttypes.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Round directly from binary64 to binary16, ties to even. Half products and
 * sums are exactly representable in binary64 before each retained rounding. */
static double half(double value)
{
    if (!isfinite(value) || value == 0) return value;
    double magnitude = fabs(value);
    if (magnitude >= 65520) return copysign(INFINITY, value);
    int exponent;
    frexp(magnitude, &exponent);
    double quantum = ldexp(1.0, exponent < -13 ? -24 : exponent - 11);
    double scaled = magnitude / quantum, low = floor(scaled), remainder = scaled - low;
    if (remainder > 0.5 || (remainder == 0.5 && fmod(low, 2.0) != 0)) low++;
    return copysign(low * quantum, value);
}

static bool field(FILE *out, const char *key, const doe_json_value *v)
{
    return fputc(',', out) != EOF && module_key(out, key) && module_json(out, v);
}

static bool text_field(FILE *out, const char *key, const char *text)
{
    return fputc(',', out) != EOF && module_key(out, key) &&
        (text ? module_string(out, text, strlen(text)) : fputs("null", out) >= 0);
}

static bool selected(FILE *out, const doe_ns_evaluation *e)
{
    return e->has_selected_token ? fprintf(out, "%" PRIu32, e->selected_token) >= 0 : fputs("null", out) >= 0;
}

static bool proofs(FILE *out, const char *key, const doe_json_value *owner)
{
    return fputc(',', out) != EOF && module_key(out, key) &&
        module_normalize(out, module_get(owner, "proofLinks"), &module_proof_links);
}

static bool receipt(FILE *out, const doe_json_value *r, const doe_ns_policy *p, const doe_ns_evaluation *e)
{
    static const char *const identity[] = {"operatorFamily", "semanticOpId", "semanticStage", "semanticPhase"};
    static const char *const policies[] = {"triggerPolicyId", "routingPolicyId", "fastPolicyId", "stablePolicyId"};
    static const char *const checks[] = {"firstDivergencePresent", "sensitiveOperatorMatched", "selectedTokenDisagreement",
        "stableMatchesExactReference", "fastMissesExactReference"};
    if (fputs("{\"schemaVersion\":1,\"mode\":\"numeric-stability\"", out) < 0) return false;
    for (size_t i = 0; i < 4; i++) if (!field(out, identity[i], module_get(r, identity[i]))) return false;
    if (!text_field(out, "policyRegistryPath", p->path) || !field(out, "policyRegistryVersion", module_get(p->root, "registryVersion")) ||
        !field(out, "routeTaxonomyVersion", module_get(p->root, "routeTaxonomyVersion")) ||
        !field(out, "proofArtifactPath", module_get(p->root, "proofArtifactPath"))) return false;
    for (size_t i = 0; i < 4; i++) if (!field(out, policies[i], module_get(r, policies[i]))) return false;
    if (!text_field(out, "referencePolicyId", "lm-head-slice/cpu-f64-serial-v1") || fputs(",\"candidates\":[", out) < 0) return false;
    const doe_json_value *candidates = module_get(r, "candidates");
    for (size_t i = 0; i < candidates->count; i++) {
        const doe_ns_candidate *c = &e->candidates[i];
        if (fprintf(out, "%s{\"tokenId\":%" PRIu32, i ? "," : "", c->token_id) < 0 ||
            !field(out, "label", module_get(candidates->items[i], "label")) || fputs(",\"fastLogit\":", out) < 0 ||
            !doe_ns_write_f64(out, c->fast) || fputs(",\"stableLogit\":", out) < 0 || !doe_ns_write_f64(out, c->stable) ||
            fputs(",\"referenceLogit\":", out) < 0 || !doe_ns_write_f64(out, c->reference) || fputc('}', out) == EOF) return false;
    }
    if (fputs("],\"executionIdentity\":null,\"firstDivergence\":", out) < 0) return false;
    if (e->checks[0]) {
        if (fputc('{', out) == EOF || !module_key(out, "semanticOpId") || !module_json(out, module_get(r, "semanticOpId")) ||
            !field(out, "semanticStage", module_get(r, "semanticStage")) || !field(out, "semanticPhase", module_get(r, "semanticPhase")) ||
            !text_field(out, "fastDigest", e->fast_digest) || !text_field(out, "stableDigest", e->stable_digest) || fputc('}', out) == EOF)
            return false;
    } else if (fputs("null", out) < 0) return false;
    if (fprintf(out, ",\"decodeBoundary\":null,\"selectedToken\":{\"fast\":%" PRIu32 ",\"stable\":%" PRIu32
        ",\"reference\":%" PRIu32 ",\"fastMatchesReference\":%s,\"stableMatchesReference\":%s},\"trigger\":{\"fired\":%s,\"checks\":{",
        e->fast_token, e->stable_token, e->reference_token, e->fast_token == e->reference_token ? "true" : "false",
        e->stable_token == e->reference_token ? "true" : "false", e->fired ? "true" : "false") < 0) return false;
    for (size_t i = 0; i < 5; i++)
        if (fprintf(out, "%s\"%s\":%s", i ? "," : "", checks[i], e->checks[i] ? "true" : "false") < 0) return false;
    return fputc('}', out) != EOF && proofs(out, "proofLinks", e->trigger_policy) && fputs("},\"route\":{\"decision\":", out) >= 0 &&
        module_string(out, e->decision, strlen(e->decision)) && text_field(out, "selectionMode", e->selection_mode) &&
        text_field(out, "committedResultMode", e->committed_mode) && text_field(out, "downstreamAction", e->downstream_action) &&
        fputs(",\"effectApplied\":false", out) >= 0 && text_field(out, "selectedPolicyId", e->selected_policy_id) &&
        fputs(",\"selectedToken\":", out) >= 0 && selected(out, e) && proofs(out, "proofLinks", e->routing_policy) &&
        proofs(out, "selectionProofLinks", e->metadata) && fputs("}}", out) >= 0;
}

/* module-core-runner serializes this struct directly, including its snake_case
 * names/default null; the ordinary runtime's meta writer uses another shape. */
static bool summary(FILE *out, const doe_json_value *r, const doe_ns_policy *p, const doe_ns_evaluation *e)
{
    const doe_json_value *path = module_get(r, "receiptPath");
    bool has_receipt = path && path->kind == DOE_JSON_STRING;
    return fputs("{\"policy_registry_path\":", out) >= 0 && module_string(out, p->path, strlen(p->path)) &&
        field(out, "policy_registry_version", module_get(p->root, "registryVersion")) &&
        field(out, "route_taxonomy_version", module_get(p->root, "routeTaxonomyVersion")) &&
        fputs(",\"execution_profile_id\":null,\"receipt_path\":", out) >= 0 &&
        (has_receipt ? module_json(out, path) : fputs("\"\"", out) >= 0) &&
        fprintf(out, ",\"receipt_count\":%u,\"decision_counts\":{\"accept_fast\":%u,\"prefer_stable\":%u,\"abstain\":%u},"
            "\"first_divergence_present_count\":%u,\"annotation_count\":0,\"auto_detect_count\":0,"
            "\"committed_stable_rewrite_count\":0,\"downstream_stop_count\":0}", has_receipt ? 1u : 0u,
            !strcmp(e->decision, "accept-fast") ? 1u : 0u, !strcmp(e->decision, "prefer-stable") ? 1u : 0u,
            !strcmp(e->decision, "abstain") ? 1u : 0u, e->checks[0] ? 1u : 0u) >= 0;
}

static bool trace_meta(const char *path, const doe_json_value *r, const doe_ns_policy *p, const doe_ns_evaluation *e)
{
    const char *head = "{\"traceVersion\":1,\"module\":\"doe_numeric_stability\",\"seqMax\":0,\"rowCount\":0";
    char *bytes = NULL;
    size_t length = 0;
    doe_host_output output;
    FILE *stream = doe_host_output_open(&output);
    if (!stream) return false;
    bool ok = fputs(head, stream) >= 0 && fputs(",\"numericStability\":", stream) >= 0 && summary(stream, r, p, e) && fputc('}', stream) != EOF;
    if (!doe_host_output_close(&output, &bytes, &length)) ok = false;
    char digest[65];
    if (ok) module_hash(bytes, length, digest);
    free(bytes);
    bytes = NULL;
    stream = doe_host_output_open(&output);
    if (!stream) return false;
    ok = ok && fputs(head, stream) >= 0 && fprintf(stream, ",\"hash\":\"sha256:%s\",\"previousHash\":\"sha256:%064u\",\"numericStability\":",
        digest, 0u) >= 0 && summary(stream, r, p, e) && fputc('}', stream) != EOF;
    if (!doe_host_output_close(&output, &bytes, &length)) ok = false;
    if (ok) ok = module_write_file(path, bytes, length);
    free(bytes);
    return ok;
}

static bool valid_request(const doe_json_value *r)
{
    const doe_json_value *hidden = module_get(r, "hiddenState"), *candidates = module_get(r, "candidates");
    if (!module_equal(module_get(r, "serviceId"), "matmul_logits_slice") ||
        !module_equal(module_get(r, "operatorFamily"), "lm-head-slice") || !module_equal(module_get(r, "semanticOpId"), "matmul.logits") ||
        !module_equal(module_get(r, "fastPolicyId"), "lm-head-slice/forward-f16accum-v1") ||
        !module_equal(module_get(r, "stablePolicyId"), "lm-head-slice/forward-serial-v1") || !hidden->count || candidates->count < 2)
        return false;
    for (size_t i = 0; i < candidates->count; i++)
        if (module_get(candidates->items[i], "weights")->count != hidden->count) return false;
    return true;
}

static uint64_t clock_ns(void)
{
    return doe_host_monotonic_ns();
}

bool module_numeric(FILE *out, const doe_json_value *r, const char *policy_text, size_t policy_size,
    const char *policy_path, char *error, size_t error_capacity)
{
    if (!valid_request(r)) { snprintf(error, error_capacity, "numeric request contract invalid"); return false; }
    doe_ns_policy policy;
    if (!doe_ns_policy_load(policy_text, policy_size, policy_path, NULL, &policy, error, error_capacity)) return false;
    const doe_json_value *candidates = module_get(r, "candidates"), *hidden = module_get(r, "hiddenState");
    doe_command command = {.semantic = {.op_id = doe_ns_string(r, "semanticOpId"), .stage = doe_ns_string(r, "semanticStage"),
        .phase = doe_ns_string(r, "semanticPhase")}};
    doe_ns_evaluation e = {.profile = r, .routing_policy_id = doe_ns_string(r, "routingPolicyId"), .command = &command,
        .candidate_count = candidates->count};
    e.candidates = calloc(candidates->count, sizeof(*e.candidates));
    double *fast = calloc(candidates->count, sizeof(*fast)), *stable = calloc(candidates->count, sizeof(*stable));
    bool ok = e.candidates && fast && stable;
    uint64_t bytes_moved = hidden->count * 8, begin = clock_ns();
    size_t fast_index = 0, stable_index = 0, reference_index = 0;
    for (size_t i = 0; ok && i < candidates->count; i++) {
        const doe_json_value *candidate = candidates->items[i], *weights = module_get(candidate, "weights");
        const doe_json_value *bias = module_get(candidate, "bias");
        double acc16 = 0, acc64 = 0;
        float acc32 = 0;
        for (size_t j = 0; j < hidden->count; j++) {
            double lhs = hidden->items[j]->number, rhs = weights->items[j]->number;
            acc16 = half(acc16 + half(half(lhs) * half(rhs)));
            float lhs32 = (float)lhs, rhs32 = (float)rhs;
            float product32 = lhs32 * rhs32;
            acc32 = acc32 + product32;
            double product64 = lhs * rhs;
            acc64 = acc64 + product64;
        }
        double add_bias = bias && bias->kind == DOE_JSON_NUMBER ? bias->number : 0;
        doe_ns_candidate *c = &e.candidates[i];
        c->token_id = (uint32_t)module_uint(candidate, "tokenId");
        c->fast = acc16 + add_bias;
        c->stable = (double)acc32 + add_bias;
        c->reference = acc64 + add_bias;
        if (!isfinite(c->fast) || !isfinite(c->stable) || !isfinite(c->reference)) {
            snprintf(error, error_capacity, "numeric result is nonfinite; no JSON result or receipt published");
            ok = false;
            break;
        }
        fast[i] = c->fast;
        stable[i] = c->stable;
        if (c->fast > e.candidates[fast_index].fast) fast_index = i;
        if (c->stable > e.candidates[stable_index].stable) stable_index = i;
        if (c->reference > e.candidates[reference_index].reference) reference_index = i;
        ok = module_add(bytes_moved, weights->count * 8 + (bias && bias->kind == DOE_JSON_NUMBER ? 8 : 0), &bytes_moved);
    }
    uint64_t elapsed = clock_ns() - begin;
    if (ok) {
        e.fast_token = e.candidates[fast_index].token_id;
        e.stable_token = e.candidates[stable_index].token_id;
        e.reference_token = e.candidates[reference_index].token_id;
        ok = doe_ns_digest(fast, candidates->count, e.fast_digest) && doe_ns_digest(stable, candidates->count, e.stable_digest) &&
            doe_ns_decide(&policy, &e, error, error_capacity);
    }
    char *receipt_bytes = NULL;
    size_t receipt_length = 0;
    doe_host_output output;
    FILE *stream = ok ? doe_host_output_open(&output) : NULL;
    if (!stream) ok = false;
    if (stream) {
        ok = receipt(stream, r, &policy, &e);
        if (!doe_host_output_close(&output, &receipt_bytes, &receipt_length)) ok = false;
    }
    const doe_json_value *receipt_path = module_get(r, "receiptPath"), *meta_path = module_get(r, "traceMetaPath");
    if (ok && receipt_path && receipt_path->kind == DOE_JSON_STRING)
        ok = receipt_path->string_size == strlen(receipt_path->string) && module_write_file(receipt_path->string, receipt_bytes, receipt_length);
    if (ok && meta_path && meta_path->kind == DOE_JSON_STRING)
        ok = meta_path->string_size == strlen(meta_path->string) && trace_meta(meta_path->string, r, &policy, &e);
    ok = ok && field(out, "serviceId", module_get(r, "serviceId")) && fprintf(out,
        ",\"serviceResult\":{\"status\":\"ok\"},\"executionStats\":{\"dispatchCount\":0,\"bytesMoved\":%" PRIu64
        ",\"candidateCount\":%zu},\"timingStats\":{\"setupNs\":0,\"encodeNs\":0,\"submitNs\":0,\"dispatchNs\":%" PRIu64
        "},\"failureDetails\":{\"code\":\"none\"}", bytes_moved, candidates->count, elapsed) >= 0 &&
        text_field(out, "routeDecision", e.decision) && fputs(",\"selectedToken\":", out) >= 0 && selected(out, &e) &&
        field(out, "receiptPath", receipt_path) && field(out, "traceMetaPath", meta_path) && fputs(",\"receipt\":", out) >= 0 &&
        fwrite(receipt_bytes, 1, receipt_length, out) == receipt_length;
    free(receipt_bytes);
    free(e.candidates);
    free(fast);
    free(stable);
    doe_ns_policy_free(&policy);
    return ok;
}
