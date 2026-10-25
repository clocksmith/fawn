/* SPDX-License-Identifier: MIT */
#include "numeric_stability.h"
#include "sha256.h"
#include "trace.h"
#include <inttypes.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define RECEIPT_SUFFIX ".numeric-stability.jsonl"
#define SAMPLE_FLAG_ENABLE_SAMPLING 1u
#define DRAW_EPSILON 1e-12

/* ---- JSON spelling ---- */

bool doe_ns_write_f64(FILE *out, double value)
{
    if (isnan(value)) return fputs("nan", out) >= 0;
    if (isinf(value)) return fputs(value < 0 ? "-inf" : "inf", out) >= 0;
    if (value == 0) return fputs(signbit(value) ? "-0" : "0", out) >= 0;
    char text[64];
    int precision = 1;
    for (; precision <= 17; precision++) {
        snprintf(text, sizeof(text), "%.*e", precision - 1, value);
        if (strtod(text, NULL) == value) break;
    }
    /* text: [-]d[.ddd]e[+-]XX; render the digits positionally. */
    const char *cursor = text;
    bool negative = *cursor == '-';
    if (negative) cursor++;
    char digits[32];
    size_t count = 0;
    for (; *cursor && *cursor != 'e'; cursor++)
        if (*cursor != '.') digits[count++] = *cursor;
    int exponent = atoi(cursor + 1);
    while (count > 1 && digits[count - 1] == '0') count--;
    if (negative && fputc('-', out) == EOF) return false;
    bool ok = true;
    if (exponent >= (int)count - 1) {
        ok = fwrite(digits, 1, count, out) == count;
        for (int i = 0; ok && i < exponent - ((int)count - 1); i++) ok = fputc('0', out) != EOF;
    } else if (exponent >= 0) {
        ok = fwrite(digits, 1, (size_t)exponent + 1, out) == (size_t)exponent + 1 && fputc('.', out) != EOF &&
            fwrite(digits + exponent + 1, 1, count - (size_t)exponent - 1, out) == count - (size_t)exponent - 1;
    } else {
        ok = fputs("0.", out) >= 0;
        for (int i = 0; ok && i < -exponent - 1; i++) ok = fputc('0', out) != EOF;
        ok = ok && fwrite(digits, 1, count, out) == count;
    }
    return ok;
}

static bool hash_stream(FILE *stream, char **bytes, size_t *length, char out[65])
{
    bool ok = fclose(stream) == 0;
    if (ok) {
        uint8_t digest[32];
        doe_sha256(*bytes, *length, digest);
        doe_sha256_hex(digest, out);
    }
    free(*bytes);
    return ok;
}

bool doe_ns_digest(const double *values, size_t count, char out[65])
{
    char *bytes = NULL;
    size_t length = 0;
    FILE *stream = open_memstream(&bytes, &length);
    if (!stream) return false;
    bool ok = fputc('[', stream) != EOF;
    for (size_t i = 0; ok && i < count; i++) ok = (!i || fputc(',', stream) != EOF) && doe_ns_write_f64(stream, values[i]);
    ok = fputc(']', stream) != EOF && ok;
    return hash_stream(stream, &bytes, &length, out) && ok;
}

/* The Zig KernelBinding fields stableHashJson sees: buffer bindings with
 * compute visibility (4), WGPU buffer binding types, and the texture fields'
 * Undefined defaults (sample type and storage access 1, the rest 0). */
bool doe_ns_layout_fingerprint(const doe_command *command, char out[65])
{
    char *bytes = NULL;
    size_t length = 0;
    FILE *stream = open_memstream(&bytes, &length);
    if (!stream) return false;
    static const unsigned types[] = {[DOE_BINDING_UNIFORM] = 2, [DOE_BINDING_STORAGE] = 3, [DOE_BINDING_READ_ONLY_STORAGE] = 4};
    bool ok = fputc('[', stream) != EOF;
    for (size_t i = 0; ok && i < command->as.dispatch.binding_count; i++) {
        const doe_kernel_binding *b = &command->as.dispatch.bindings[i];
        ok = (!i || fputc(',', stream) != EOF) &&
            fprintf(stream, "{\"binding\":%" PRIu32 ",\"group\":%" PRIu32 ",\"resourceKind\":0,\"visibility\":4,"
                "\"bufferOffset\":%" PRIu64 ",\"bufferSize\":%" PRIu64 ",\"bufferType\":%u,\"textureSampleType\":1,"
                "\"textureViewDimension\":0,\"storageTextureAccess\":1,\"textureAspect\":0,\"textureFormat\":0,"
                "\"textureMultisampled\":false}", b->binding, b->group, b->buffer_offset, b->buffer_size,
                types[b->buffer_type]) >= 0;
    }
    ok = fputc(']', stream) != EOF && ok;
    return hash_stream(stream, &bytes, &length, out) && ok;
}

static bool write_string_or_null(FILE *out, const char *text)
{
    return text ? doe_trace_write_string(out, text) : fputs("null", out) >= 0;
}

static bool write_key(FILE *out, const char *key, bool comma)
{
    return (!comma || fputc(',', out) != EOF) && fprintf(out, "\"%s\":", key) >= 0;
}

static bool write_field(FILE *out, const char *key, const char *value)
{
    return write_key(out, key, true) && write_string_or_null(out, value);
}

static bool write_bool(FILE *out, const char *key, bool value, bool comma)
{
    return write_key(out, key, comma) && fputs(value ? "true" : "false", out) >= 0;
}

/* ProofLink objects in policy.zig field order. */
static bool write_proof_links(FILE *out, const char *key, const doe_json_value *owner, const char *member)
{
    const doe_json_value *links = owner ? doe_json_member_value(owner, member) : NULL;
    bool ok = write_key(out, key, true) && fputc('[', out) != EOF;
    static const char *const fields[] = {"theorem", "module", "category", "relation", "artifactPath"};
    for (size_t i = 0; ok && links && links->kind == DOE_JSON_ARRAY && i < links->count; i++) {
        ok = (!i || fputc(',', out) != EOF) && fputc('{', out) != EOF;
        for (size_t f = 0; ok && f < 5; f++)
            ok = write_key(out, fields[f], f > 0) && write_string_or_null(out, doe_ns_string(links->items[i], fields[f]));
        ok = ok && fputc('}', out) != EOF;
    }
    return ok && fputc(']', out) != EOF;
}

/* ---- Evaluation ---- */

static uint32_t argmax_token(const doe_ns_candidate *candidates, size_t count, int lane)
{
    size_t best = 0;
    for (size_t i = 1; i < count; i++) {
        double value = lane == 0 ? candidates[i].fast : lane == 1 ? candidates[i].stable : candidates[i].reference;
        double best_value = lane == 0 ? candidates[best].fast : lane == 1 ? candidates[best].stable : candidates[best].reference;
        if (value > best_value) best = i;
    }
    return candidates[best].token_id;
}

static bool contains_string(const doe_json_value *array, const char *needle)
{
    for (size_t i = 0; array && array->kind == DOE_JSON_ARRAY && i < array->count; i++)
        if (array->items[i]->kind == DOE_JSON_STRING && !strcmp(array->items[i]->string, needle)) return true;
    return false;
}

static bool flag(const doe_json_value *object, const char *key)
{
    const doe_json_value *value = doe_json_member_value(object, key);
    return value && value->kind == DOE_JSON_BOOL && value->boolean;
}

bool doe_ns_decide(const doe_ns_policy *policy, doe_ns_evaluation *e, char *message, size_t message_size)
{
    const char *trigger_id = doe_ns_string(e->profile, "triggerPolicyId");
    e->trigger_policy = doe_ns_find(policy->root, "triggerPolicies", "triggerPolicyId", trigger_id);
    e->routing_policy = doe_ns_find(policy->root, "routingPolicies", "policyId", e->routing_policy_id);
    if (!e->trigger_policy || !e->routing_policy || strcmp(doe_ns_string(e->routing_policy, "triggerPolicyId"), trigger_id)) {
        snprintf(message, message_size, "routing policy %.80s does not resolve for trigger policy %.80s",
            e->routing_policy_id ? e->routing_policy_id : "(none)", trigger_id ? trigger_id : "(none)");
        return false;
    }
    bool diverged = strcmp(e->fast_digest, e->stable_digest) != 0;
    const char *op = e->command->semantic.op_id;
    e->checks[0] = diverged;
    e->checks[1] = contains_string(doe_json_member_value(e->trigger_policy, "allowedSensitiveOperators"), op) && diverged;
    e->checks[2] = e->fast_token != e->stable_token;
    e->checks[3] = e->stable_token == e->reference_token;
    e->checks[4] = e->fast_token != e->reference_token;
    e->fired = (!flag(e->trigger_policy, "requireFirstDivergence") || e->checks[0]) && e->checks[1] &&
        (!flag(e->trigger_policy, "requireSelectedTokenDisagreement") || e->checks[2]) &&
        (!flag(e->trigger_policy, "requireStableMatchesExactReference") || e->checks[3]) &&
        (!flag(e->trigger_policy, "requireFastMissesExactReference") || e->checks[4]);
    e->decision = doe_ns_string(e->routing_policy, e->fired ? "triggeredDecision" : "fallbackDecision");
    e->metadata = doe_ns_find(policy->root, "routeDecisionMetadata", "decision", e->decision);
    if (!e->metadata) {
        snprintf(message, message_size, "route decision %.80s has no metadata", e->decision);
        return false;
    }
    e->selection_mode = doe_ns_string(e->metadata, "selectionMode");
    e->committed_mode = doe_ns_string(e->metadata, "committedResultMode");
    e->downstream_action = doe_ns_string(e->metadata, "downstreamAction");
    bool stable = !strcmp(e->selection_mode, "stable"), fast = !strcmp(e->selection_mode, "fast");
    e->selected_policy_id = stable ? doe_ns_string(e->profile, "stablePolicyId") : fast ? doe_ns_string(e->profile, "fastPolicyId") : NULL;
    e->has_selected_token = stable || fast;
    e->selected_token = stable ? e->stable_token : e->fast_token;
    return true;
}

/* ---- Receipts ---- */

typedef struct decode_boundary {
    bool sampling;
    uint32_t vocab_size, live_token;
    bool has_temperature, has_top_k, has_top_p, has_draw, has_seed;
    double temperature, top_p, draw;
    uint32_t top_k;
    uint64_t seed;
    uint32_t *surviving; /* NULL: every token survives */
    size_t surviving_count;
    double margins[3];
    bool has_top_k_gap, has_top_p_gap, has_cdf;
    double top_k_gap, top_p_gap, cdf;
    uint32_t tokens[3];
    bool live_matches_committed;
} decode_boundary;

static bool write_receipt(FILE *out, const doe_ns_recorder *r, const doe_ns_evaluation *e, const doe_ns_identity *id,
    const char *family, const doe_semantic *semantic, const decode_boundary *boundary)
{
    const doe_json_value *root = r->policy->root;
    const doe_json_value *profile = e->profile;
    bool ok = fprintf(out, "{\"schemaVersion\":1,\"mode\":\"numeric-stability\"") >= 0 && write_field(out, "operatorFamily", family) &&
        write_field(out, "semanticOpId", semantic->op_id) && write_field(out, "semanticStage", semantic->stage) &&
        write_field(out, "semanticPhase", semantic->phase) && write_field(out, "policyRegistryPath", r->policy->path) &&
        write_field(out, "policyRegistryVersion", doe_ns_string(root, "registryVersion")) &&
        write_field(out, "routeTaxonomyVersion", doe_ns_string(root, "routeTaxonomyVersion")) &&
        write_field(out, "proofArtifactPath", doe_ns_string(root, "proofArtifactPath")) &&
        write_field(out, "triggerPolicyId", doe_ns_string(profile, "triggerPolicyId")) &&
        write_field(out, "routingPolicyId", e->routing_policy_id) &&
        write_field(out, "fastPolicyId", doe_ns_string(profile, "fastPolicyId")) &&
        write_field(out, "stablePolicyId", doe_ns_string(profile, "stablePolicyId")) &&
        write_field(out, "referencePolicyId", doe_ns_string(profile, "referencePolicyId")) &&
        write_key(out, "candidates", true) && fputc('[', out) != EOF;
    for (size_t i = 0; ok && i < e->candidate_count; i++) {
        const doe_ns_candidate *c = &e->candidates[i];
        ok = (!i || fputc(',', out) != EOF) && fprintf(out, "{\"tokenId\":%" PRIu32 ",\"label\":null,\"fastLogit\":", c->token_id) >= 0 &&
            doe_ns_write_f64(out, c->fast) && fputs(",\"stableLogit\":", out) >= 0 && doe_ns_write_f64(out, c->stable) &&
            fputs(",\"referenceLogit\":", out) >= 0 && doe_ns_write_f64(out, c->reference) && fputc('}', out) != EOF;
    }
    const char *kernel = e->command->as.dispatch.kernel;
    const char *slash = strrchr(kernel, '/');
    ok = ok && fputc(']', out) != EOF && write_key(out, "executionIdentity", true) && fputc('{', out) != EOF &&
        write_key(out, "kernelPath", false) && doe_trace_write_string(out, kernel) &&
        write_field(out, "kernelBasename", slash ? slash + 1 : kernel) && write_field(out, "layoutFingerprint", e->layout_fingerprint) &&
        write_field(out, "compiledPlanHash", e->command->semantic.execution_plan_hash) && write_field(out, "backend", id->backend) &&
        fputs(",\"backendLane\":null,\"adapterOrdinal\":null,\"queueFamilyIndex\":null,\"presentCapable\":null", out) >= 0 &&
        write_field(out, "profileVendor", id->vendor) && write_field(out, "profileApi", id->api) &&
        write_field(out, "profileFamily", id->family) && write_field(out, "profileDriver", id->driver) &&
        fputs(",\"selectionPolicyHash\":null,\"hostPlanArtifactHash\":null}", out) >= 0 && write_key(out, "firstDivergence", true);
    if (ok && e->checks[0]) {
        const doe_semantic *source = &e->command->semantic;
        ok = fputc('{', out) != EOF && write_key(out, "semanticOpId", false) && doe_trace_write_string(out, source->op_id) &&
            write_field(out, "semanticStage", source->stage) && write_field(out, "semanticPhase", source->phase) &&
            write_field(out, "fastDigest", e->fast_digest) && write_field(out, "stableDigest", e->stable_digest) &&
            fputc('}', out) != EOF;
    } else if (ok) {
        ok = fputs("null", out) >= 0;
    }
    ok = ok && write_key(out, "decodeBoundary", true);
    uint32_t tokens[3] = {e->fast_token, e->stable_token, e->reference_token};
    if (ok && boundary) {
        const decode_boundary *b = boundary;
        memcpy(tokens, b->tokens, sizeof(tokens));
        ok = fprintf(out, "{\"decodeMode\":\"%s\",\"logitsCoverage\":\"full-vocab\",\"vocabSize\":%" PRIu32
            ",\"residualMassUpperBound\":0,\"temperature\":", b->sampling ? "sampled-cdf" : "greedy-argmax", b->vocab_size) >= 0;
        ok = ok && (b->has_temperature ? doe_ns_write_f64(out, b->temperature) : fputs("null", out) >= 0) &&
            fputs(",\"topK\":", out) >= 0 && (b->has_top_k ? fprintf(out, "%" PRIu32, b->top_k) >= 0 : fputs("null", out) >= 0) &&
            fputs(",\"topP\":", out) >= 0 && (b->has_top_p ? doe_ns_write_f64(out, b->top_p) : fputs("null", out) >= 0);
        /* The short uniform (vocab size alone) has no sampling fields. */
        ok = ok && fputs(",\"rngSeed\":", out) >= 0 &&
            (b->has_seed ? fprintf(out, "%" PRIu64, b->seed) >= 0 : fputs("null", out) >= 0) &&
            fputs(",\"rngDraw\":", out) >= 0 && (b->has_draw ? doe_ns_write_f64(out, b->draw) : fputs("null", out) >= 0) &&
            fprintf(out, ",\"survivingTokenSetKind\":\"%s\",\"survivingTokenIds\":", b->surviving ? "filtered-token-ids" : "all-vocab") >= 0;
        if (ok && b->surviving) {
            ok = fputc('[', out) != EOF;
            for (size_t i = 0; ok && i < b->surviving_count; i++)
                ok = (!i || fputc(',', out) != EOF) && fprintf(out, "%" PRIu32, b->surviving[i]) >= 0;
            ok = ok && fputc(']', out) != EOF;
        } else if (ok) {
            ok = fputs("null", out) >= 0;
        }
        ok = ok && fprintf(out, ",\"liveSelectedToken\":%" PRIu32, b->live_token) >= 0 &&
            write_bool(out, "liveSelectedMatchesCommittedSelection", b->live_matches_committed, true) &&
            fputs(",\"metrics\":{\"fastTop1Margin\":", out) >= 0 && doe_ns_write_f64(out, b->margins[0]) &&
            fputs(",\"stableTop1Margin\":", out) >= 0 && doe_ns_write_f64(out, b->margins[1]) &&
            fputs(",\"referenceTop1Margin\":", out) >= 0 && doe_ns_write_f64(out, b->margins[2]) &&
            fputs(",\"topKBoundaryGap\":", out) >= 0 && (b->has_top_k_gap ? doe_ns_write_f64(out, b->top_k_gap) : fputs("null", out) >= 0) &&
            fputs(",\"topPBoundaryGap\":", out) >= 0 && (b->has_top_p_gap ? doe_ns_write_f64(out, b->top_p_gap) : fputs("null", out) >= 0) &&
            fputs(",\"cdfDistanceToDraw\":", out) >= 0 && (b->has_cdf ? doe_ns_write_f64(out, b->cdf) : fputs("null", out) >= 0) &&
            fputs(",\"adjacentDecodePersistence\":null", out) >= 0 &&
            write_bool(out, "actualSelectedTokenChanged", b->tokens[0] != b->tokens[1] || b->tokens[0] != b->tokens[2], true) &&
            write_bool(out, "liveSelectedMatchesFast", b->live_token == b->tokens[0], true) &&
            write_bool(out, "liveSelectedMatchesStable", b->live_token == b->tokens[1], true) &&
            write_bool(out, "liveSelectedMatchesReference", b->live_token == b->tokens[2], true) &&
            fputs("},\"upstreamLinks\":[{\"semanticOpId\":", out) >= 0 && doe_trace_write_string(out, e->command->semantic.op_id) &&
            write_field(out, "semanticStage", e->command->semantic.stage) &&
            write_field(out, "semanticPhase", e->command->semantic.phase) &&
            write_field(out, "selectedPolicyId", e->selected_policy_id) && write_field(out, "decision", e->decision) &&
            fputs("}]}", out) >= 0;
    } else if (ok) {
        ok = fputs("null", out) >= 0;
    }
    bool stable = !strcmp(e->selection_mode, "stable"), fast = !strcmp(e->selection_mode, "fast");
    ok = ok && fprintf(out, ",\"selectedToken\":{\"fast\":%" PRIu32 ",\"stable\":%" PRIu32 ",\"reference\":%" PRIu32,
        tokens[0], tokens[1], tokens[2]) >= 0 &&
        write_bool(out, "fastMatchesReference", tokens[0] == tokens[2], true) &&
        write_bool(out, "stableMatchesReference", tokens[1] == tokens[2], true) &&
        fputs("},\"trigger\":{", out) >= 0 && write_bool(out, "fired", e->fired, false) && fputs(",\"checks\":{", out) >= 0 &&
        write_bool(out, "firstDivergencePresent", e->checks[0], false) && write_bool(out, "sensitiveOperatorMatched", e->checks[1], true) &&
        write_bool(out, "selectedTokenDisagreement", e->checks[2], true) &&
        write_bool(out, "stableMatchesExactReference", e->checks[3], true) &&
        write_bool(out, "fastMissesExactReference", e->checks[4], true) && fputc('}', out) != EOF &&
        write_proof_links(out, "proofLinks", e->trigger_policy, "proofLinks") && fputs("},\"route\":{", out) >= 0 &&
        write_key(out, "decision", false) && doe_trace_write_string(out, e->decision) &&
        write_field(out, "selectionMode", e->selection_mode) && write_field(out, "committedResultMode", e->committed_mode) &&
        write_field(out, "downstreamAction", e->downstream_action) && write_bool(out, "effectApplied", e->effect_applied, true) &&
        write_field(out, "selectedPolicyId", e->selected_policy_id) && write_key(out, "selectedToken", true) &&
        (stable || fast ? fprintf(out, "%" PRIu32, stable ? tokens[1] : tokens[0]) >= 0 : fputs("null", out) >= 0) &&
        write_proof_links(out, "proofLinks", e->routing_policy, "proofLinks") &&
        write_proof_links(out, "selectionProofLinks", e->metadata, "proofLinks") && fputs("}}\n", out) >= 0;
    return ok;
}

bool doe_ns_recorder_open(doe_ns_recorder *recorder, const doe_ns_policy *policy, const char *meta_path)
{
    *recorder = (doe_ns_recorder){.policy = policy};
    size_t length = strlen(meta_path);
    recorder->receipt_path = malloc(length + sizeof(RECEIPT_SUFFIX));
    if (!recorder->receipt_path) return false;
    memcpy(recorder->receipt_path, meta_path, length);
    memcpy(recorder->receipt_path + length, RECEIPT_SUFFIX, sizeof(RECEIPT_SUFFIX));
    return true;
}

static bool open_receipts(doe_ns_recorder *r, char *message, size_t message_size)
{
    if (r->receipts) return true;
    r->receipts = fopen(r->receipt_path, "wb");
    if (!r->receipts) snprintf(message, message_size, "cannot write %s", r->receipt_path);
    return r->receipts != NULL;
}

static bool binding_of(const doe_command *command, uint32_t index, const doe_kernel_binding **out, char *message,
    size_t message_size)
{
    for (size_t i = 0; i < command->as.dispatch.binding_count; i++) {
        const doe_kernel_binding *b = &command->as.dispatch.bindings[i];
        if (b->binding == index && b->group == 0) { *out = b; return true; }
    }
    snprintf(message, message_size, "numeric stability: %s has no group 0 binding %u", command->as.dispatch.kernel, index);
    return false;
}

static bool capture_f32(const doe_ns_io *io, uint64_t handle, uint64_t offset, size_t count, double *out, char *message,
    size_t message_size)
{
    uint8_t *bytes = NULL;
    size_t length = 0;
    if (!io->capture(io->context, handle, offset, (uint64_t)count * 4, &bytes, &length, message, message_size)) return false;
    bool ok = length == count * 4;
    for (size_t i = 0; ok && i < count; i++) {
        uint32_t bits = (uint32_t)bytes[4 * i] | (uint32_t)bytes[4 * i + 1] << 8 | (uint32_t)bytes[4 * i + 2] << 16 |
            (uint32_t)bytes[4 * i + 3] << 24;
        float value;
        memcpy(&value, &bits, sizeof(value));
        out[i] = (double)value;
    }
    free(bytes);
    if (!ok) snprintf(message, message_size, "numeric stability: buffer %llu capture size mismatch", (unsigned long long)handle);
    return ok;
}

static bool capture_uniform(const doe_ns_io *io, const doe_kernel_binding *b, uint8_t **bytes, size_t *length, char *message,
    size_t message_size)
{
    return io->capture(io->context, b->handle, b->buffer_offset, b->buffer_size, bytes, length, message, message_size);
}

static uint32_t read_u32(const uint8_t *bytes, size_t length, size_t word)
{
    if ((word + 1) * 4 > length) return 0;
    const uint8_t *b = bytes + word * 4;
    return (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24;
}

static float read_f32(const uint8_t *bytes, size_t length, size_t word)
{
    uint32_t bits = read_u32(bytes, length, word);
    float value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

/* runtime_plan.zig buildAutoDecodeFinalLogitsObservation and
 * runtime_eval.zig evaluateMatmulObservation: uniform (rows, cols), weights
 * binding 1, hidden binding 2, logits binding 3. */
static bool evaluate_decode_final_logits(const doe_ns_io *io, doe_ns_evaluation *e, char *message, size_t message_size)
{
    const doe_command *command = e->command;
    const doe_kernel_binding *uniform, *weights, *hidden, *logits;
    if (!binding_of(command, 0, &uniform, message, message_size) || !binding_of(command, 1, &weights, message, message_size) ||
        !binding_of(command, 2, &hidden, message, message_size) || !binding_of(command, 3, &logits, message, message_size))
        return false;
    uint8_t *bytes = NULL;
    size_t length = 0;
    if (!capture_uniform(io, uniform, &bytes, &length, message, message_size)) return false;
    uint32_t rows = read_u32(bytes, length, 0), cols = read_u32(bytes, length, 1);
    free(bytes);
    if (!rows || !cols || rows > (1u << 24) || cols > (1u << 24)) {
        snprintf(message, message_size, "numeric stability: logits shape %" PRIu32 "x%" PRIu32 " is not admitted", rows, cols);
        return false;
    }
    double *hidden_values = calloc(cols, sizeof(double)), *fast = calloc(rows, sizeof(double));
    double *stable = calloc(rows, sizeof(double)), *row = calloc(cols, sizeof(double));
    e->candidates = calloc(rows, sizeof(doe_ns_candidate));
    e->candidate_count = rows;
    bool ok = hidden_values && fast && stable && row && e->candidates &&
        capture_f32(io, hidden->handle, hidden->buffer_offset, cols, hidden_values, message, message_size) &&
        capture_f32(io, logits->handle, logits->buffer_offset, rows, fast, message, message_size);
    if (!ok && !message[0]) snprintf(message, message_size, "out of memory");
    for (uint32_t r = 0; ok && r < rows; r++) {
        uint64_t offset = weights->buffer_offset + (uint64_t)r * cols * 4;
        ok = capture_f32(io, weights->handle, offset, cols, row, message, message_size);
        float f32_acc = 0;
        double f64_acc = 0;
        for (uint32_t c = 0; ok && c < cols; c++) {
            f32_acc = f32_acc + (float)hidden_values[c] * (float)row[c];
            f64_acc += hidden_values[c] * row[c];
        }
        stable[r] = (double)f32_acc;
        e->candidates[r] = (doe_ns_candidate){.token_id = r, .fast = fast[r], .stable = stable[r], .reference = f64_acc};
    }
    ok = ok && doe_ns_digest(fast, rows, e->fast_digest) && doe_ns_digest(stable, rows, e->stable_digest) &&
        doe_ns_layout_fingerprint(command, e->layout_fingerprint);
    if (ok) {
        e->fast_token = argmax_token(e->candidates, rows, 0);
        e->stable_token = argmax_token(e->candidates, rows, 1);
        e->reference_token = argmax_token(e->candidates, rows, 2);
        e->logits_handle = logits->handle;
        e->logits_offset = logits->buffer_offset;
    }
    free(hidden_values);
    free(fast);
    free(stable);
    free(row);
    return ok;
}

typedef struct replay_entry {
    uint32_t token;
    double scaled, probability;
} replay_entry;

static int replay_order(const void *a, const void *b)
{
    const replay_entry *x = a, *y = b;
    if (x->scaled == y->scaled) return x->token < y->token ? -1 : x->token > y->token;
    return x->scaled > y->scaled ? -1 : 1;
}

/* runtime_decode.zig replayDecodeLane: softmax at the temperature, top-k then
 * top-p surviving set, and the CDF draw when sampling. */
static bool replay_lane(const doe_ns_candidate *candidates, size_t count, int lane, const decode_boundary *config,
    uint32_t *selected, double *margin, bool *has_k_gap, double *k_gap, bool *has_p_gap, double *p_gap, bool *has_cdf,
    double *cdf, uint32_t **surviving, size_t *surviving_count)
{
    replay_entry *entries = calloc(count, sizeof(replay_entry));
    if (!entries) return false;
    double temperature = config->has_temperature && config->temperature > 0 ? config->temperature : 1.0;
    for (size_t i = 0; i < count; i++) {
        double logit = lane == 0 ? candidates[i].fast : lane == 1 ? candidates[i].stable : candidates[i].reference;
        entries[i] = (replay_entry){.token = candidates[i].token_id, .scaled = logit / temperature};
    }
    qsort(entries, count, sizeof(replay_entry), replay_order);
    double maximum = entries[0].scaled, sum = 0;
    for (size_t i = 0; i < count; i++) sum += entries[i].probability = exp(entries[i].scaled - maximum);
    for (size_t i = 0; i < count; i++) entries[i].probability /= sum;
    *margin = count >= 2 ? fmax(0.0, entries[0].probability - entries[1].probability) : 0;
    size_t k_count = config->has_top_k && config->top_k < count ? config->top_k : count;
    *has_k_gap = config->has_top_k && config->top_k > 0 && config->top_k < count;
    if (*has_k_gap) *k_gap = fmax(0.0, entries[config->top_k - 1].probability - entries[config->top_k].probability);
    size_t p_count = k_count;
    if (config->has_top_p) {
        double cumulative = 0;
        for (size_t i = 0; i < k_count; i++) {
            cumulative += entries[i].probability;
            if (cumulative >= config->top_p) { p_count = i + 1; break; }
        }
    }
    *has_p_gap = config->has_top_p && p_count > 0 && p_count < k_count && p_count < count;
    if (*has_p_gap) *p_gap = fmax(0.0, entries[p_count - 1].probability - entries[p_count].probability);
    double surviving_sum = 0;
    for (size_t i = 0; i < p_count; i++) surviving_sum += entries[i].probability;
    *selected = entries[0].token;
    *has_cdf = false;
    if (config->sampling && config->has_draw) {
        double draw = config->draw < 0 ? 0 : config->draw > 1 ? 1 : config->draw;
        if (draw >= 1.0) draw = 1.0 - DRAW_EPSILON;
        double cumulative = 0;
        for (size_t i = 0; i < p_count; i++) {
            double lower = cumulative;
            cumulative += entries[i].probability / surviving_sum;
            double upper = i + 1 == p_count ? 1.0 : cumulative;
            if (draw <= upper || i + 1 == p_count) {
                *selected = entries[i].token;
                *has_cdf = true;
                *cdf = fmax(0.0, fmin(draw - lower, upper - draw));
                break;
            }
        }
    }
    if (surviving) {
        *surviving_count = p_count;
        *surviving = NULL;
        if (p_count < config->vocab_size) {
            *surviving = malloc(p_count * sizeof(uint32_t));
            if (!*surviving) { free(entries); return false; }
            for (size_t i = 0; i < p_count; i++) (*surviving)[i] = entries[i].token;
        }
    }
    free(entries);
    return true;
}

/* runtime_decode.zig readDecodeReceipt for a sample.wgsl dispatch reading the
 * stored decode.final_logits logits: uniform binding 0, token binding 2. */
static bool record_decode_boundary(doe_ns_recorder *r, const doe_ns_io *io, const doe_command *command, char *message,
    size_t message_size)
{
    doe_ns_evaluation *source = &r->source;
    const doe_kernel_binding *uniform, *output;
    if (!binding_of(command, 0, &uniform, message, message_size) || !binding_of(command, 2, &output, message, message_size))
        return false;
    uint8_t *bytes = NULL;
    size_t length = 0;
    if (!capture_uniform(io, uniform, &bytes, &length, message, message_size)) return false;
    decode_boundary b = {.vocab_size = read_u32(bytes, length, 0)};
    if (length >= 32) {
        uint32_t top_k = read_u32(bytes, length, 1), flags = read_u32(bytes, length, 7);
        double top_p = read_f32(bytes, length, 2), temperature = read_f32(bytes, length, 3), draw = read_f32(bytes, length, 6);
        b.has_seed = true;
        b.seed = (uint64_t)read_u32(bytes, length, 4) | (uint64_t)read_u32(bytes, length, 5) << 32;
        b.has_temperature = temperature > 0;
        b.temperature = temperature;
        b.has_top_k = top_k > 0;
        b.top_k = top_k;
        b.has_top_p = top_p > 0 && top_p <= 1.0;
        b.top_p = top_p;
        b.sampling = (flags & SAMPLE_FLAG_ENABLE_SAMPLING) != 0;
        b.has_draw = b.sampling;
        b.draw = draw < 0 ? 0 : draw > 1 ? 1 : draw;
    }
    free(bytes);
    bool ok = true;
    uint32_t *surviving = NULL;
    size_t surviving_count = 0;
    for (int lane = 0; ok && lane < 3; lane++) {
        bool has_k, has_p, has_cdf;
        double k_gap = 0, p_gap = 0, cdf = 0;
        ok = replay_lane(source->candidates, source->candidate_count, lane, &b, &b.tokens[lane], &b.margins[lane], &has_k, &k_gap,
            &has_p, &p_gap, &has_cdf, &cdf, lane == 0 ? &surviving : NULL, lane == 0 ? &surviving_count : NULL);
        if (ok && lane == 0) {
            b.has_top_k_gap = has_k;
            b.top_k_gap = k_gap;
            b.has_top_p_gap = has_p;
            b.top_p_gap = p_gap;
            b.has_cdf = has_cdf;
            b.cdf = cdf;
        }
    }
    b.surviving = surviving;
    b.surviving_count = surviving_count;
    if (!ok) snprintf(message, message_size, "out of memory");
    /* Sampling replays the draw on the host and commits it to the token buffer. */
    if (ok && b.sampling) {
        ok = io->write(io->context, output->handle, output->buffer_offset, &b.tokens[0], 1, message, message_size);
        if (ok && !strcmp(source->selection_mode, "stable"))
            ok = io->write(io->context, output->handle, output->buffer_offset, &b.tokens[1], 1, message, message_size);
    }
    if (ok) {
        ok = io->capture(io->context, output->handle, output->buffer_offset, output->buffer_size, &bytes, &length, message,
            message_size);
        if (ok && length < 4) {
            snprintf(message, message_size, "numeric stability: sampled token buffer holds no token");
            ok = false;
        }
        if (ok) b.live_token = read_u32(bytes, length, 0);
        free(bytes);
    }
    bool stable = !strcmp(source->selection_mode, "stable"), fast = !strcmp(source->selection_mode, "fast");
    b.live_matches_committed = (stable && b.live_token == b.tokens[1]) || (fast && b.live_token == b.tokens[0]);
    if (ok) {
        ok = open_receipts(r, message, message_size) &&
            write_receipt(r->receipts, r, source, &r->source_identity, "decode-sample-token", &command->semantic, &b);
        if (!ok && !message[0]) snprintf(message, message_size, "cannot write %s", r->receipt_path);
    }
    if (ok) {
        r->receipt_count++;
        if (source->checks[0]) r->first_divergence_count++;
        if (!strcmp(source->decision, "accept-fast")) r->accept_fast++;
        else if (!strcmp(source->decision, "prefer-stable")) r->prefer_stable++;
        else if (!strcmp(source->decision, "abstain")) r->abstain++;
    }
    free(surviving);
    return ok;
}

static bool is_sample_dispatch(const doe_command *command)
{
    const char *kernel = command->as.dispatch.kernel;
    const char *slash = strrchr(kernel, '/');
    if (strcmp(slash ? slash + 1 : kernel, "sample.wgsl")) return false;
    const doe_semantic *s = &command->semantic;
    return s->op_id ? !strcmp(s->op_id, "decode.sample_token") : s->phase && !strcmp(s->phase, "sample_token");
}

bool doe_ns_record(doe_ns_recorder *r, const doe_ns_io *io, const doe_ns_identity *identity, const doe_command *command,
    size_t following, bool *stop, char *message, size_t message_size)
{
    *stop = false;
    message[0] = '\0';
    if (!doe_ns_semantic_complete(command)) return true;
    if (r->has_source && is_sample_dispatch(command)) {
        const doe_kernel_binding *logits;
        if (binding_of(command, 1, &logits, message, message_size) && logits->handle == r->source.logits_handle)
            return record_decode_boundary(r, io, command, message, message_size);
        message[0] = '\0';
    }
    const doe_json_value *profile = doe_ns_auto_detect(r->policy, command);
    if (!profile) return true;
    const char *plan = doe_ns_string(profile, "capturePlanId");
    if (!plan || strcmp(plan, DOE_NS_DECODE_FINAL_LOGITS_PLAN)) {
        snprintf(message, message_size, "numeric stability capture plan %.100s is not implemented", plan ? plan : "(none)");
        return false;
    }
    doe_ns_evaluation e = {.profile = profile, .command = command,
        .routing_policy_id = doe_ns_string(r->policy->execution_profile, "routingPolicyId")};
    bool ok = evaluate_decode_final_logits(io, &e, message, message_size) && doe_ns_decide(r->policy, &e, message, message_size);
    bool rewrite = ok && !strcmp(e.committed_mode, "stable");
    if (rewrite) {
        uint32_t *words = malloc(e.candidate_count * sizeof(uint32_t));
        ok = words != NULL;
        for (size_t i = 0; ok && i < e.candidate_count; i++) {
            float value = (float)e.candidates[i].stable;
            memcpy(&words[i], &value, sizeof(value));
        }
        ok = ok && io->write(io->context, e.logits_handle, e.logits_offset, words, e.candidate_count, message, message_size);
        free(words);
    }
    *stop = ok && !strcmp(e.downstream_action, "stop") && following > 0;
    e.effect_applied = rewrite || *stop;
    ok = ok && open_receipts(r, message, message_size) &&
        write_receipt(r->receipts, r, &e, identity, doe_ns_string(profile, "operatorFamily"), &command->semantic, NULL);
    if (!ok) {
        if (!message[0]) snprintf(message, message_size, "cannot write %s", r->receipt_path);
        free(e.candidates);
        return false;
    }
    r->receipt_count++;
    r->auto_detect_count++;
    if (e.checks[0]) r->first_divergence_count++;
    if (!strcmp(e.decision, "accept-fast")) r->accept_fast++;
    else if (!strcmp(e.decision, "prefer-stable")) r->prefer_stable++;
    else if (!strcmp(e.decision, "abstain")) r->abstain++;
    if (rewrite) r->rewrite_count++;
    if (*stop) r->stop_count++;
    if (!strcmp(command->semantic.op_id, "decode.final_logits")) {
        if (r->has_source) free(r->source.candidates);
        r->source = e;
        r->source_identity = *identity;
        r->has_source = true;
    } else {
        free(e.candidates);
    }
    return true;
}

bool doe_ns_write_summary(FILE *out, const doe_ns_recorder *r)
{
    if (!r->receipt_count) return true;
    return fputs(",\"numericStability\":{\"policyRegistryPath\":", out) >= 0 && doe_trace_write_string(out, r->policy->path) &&
        write_field(out, "policyRegistryVersion", doe_ns_string(r->policy->root, "registryVersion")) &&
        write_field(out, "routeTaxonomyVersion", doe_ns_string(r->policy->root, "routeTaxonomyVersion")) &&
        write_field(out, "executionProfileId", doe_ns_string(r->policy->execution_profile, "profileId")) &&
        write_field(out, "receiptPath", r->receipt_path) &&
        fprintf(out, ",\"receiptCount\":%" PRIu32 ",\"decisionCounts\":{\"accept-fast\":%" PRIu32 ",\"prefer-stable\":%" PRIu32
            ",\"abstain\":%" PRIu32 "},\"firstDivergencePresentCount\":%" PRIu32 ",\"annotationCount\":0,\"autoDetectCount\":%" PRIu32
            ",\"committedStableRewriteCount\":%" PRIu32 ",\"downstreamStopCount\":%" PRIu32 "}", r->receipt_count, r->accept_fast,
            r->prefer_stable, r->abstain, r->first_divergence_count, r->auto_detect_count, r->rewrite_count, r->stop_count) >= 0;
}

void doe_ns_recorder_free(doe_ns_recorder *r)
{
    if (r->receipts) fclose(r->receipts);
    if (r->has_source) free(r->source.candidates);
    free(r->receipt_path);
    *r = (doe_ns_recorder){0};
}
