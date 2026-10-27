/* SPDX-License-Identifier: MIT
 * Output-oracle command contracts. Strings borrow the owning JSON document. */
#include "stream_internal.h"
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static bool optional_string(const doe_json_value *object, const char *a, const char *b,
    const char **out, doe_stream_error *error)
{
    bool ambiguous = false;
    const doe_json_value *value = stream_either(object, a, b, &ambiguous);
    if (ambiguous) return stream_reject(error, 0, "both oracle spellings %s and %s are present", a, b);
    if (value && value->kind == DOE_JSON_NULL) {
        *out = NULL;
        return true;
    }
    return stream_read_string(object, a, b, out, 0, error);
}

static bool optional_f32(const doe_json_value *object, const char *a, const char *b,
    float *out, doe_stream_error *error)
{
    bool ambiguous = false;
    const doe_json_value *value = stream_either(object, a, b, &ambiguous);
    if (ambiguous) return stream_reject(error, 0, "both oracle spellings %s and %s are present", a, b);
    *out = 0;
    if (!value || value->kind == DOE_JSON_NULL) return true;
    if (value->kind != DOE_JSON_NUMBER || !isfinite(value->number) || fabs(value->number) > FLT_MAX)
        return stream_reject(error, 0, "oracle %s must be a finite float32", a);
    *out = (float)value->number;
    return true;
}

bool doe_output_oracle_parse(const doe_json_value *value, doe_output_oracle *out,
    char *message, size_t message_size)
{
    static const char *const allowed[] = {
        "schema_version", "schemaVersion", "scope", "reference_class", "referenceClass", "kind", "initialization",
        "binding_group", "bindingGroup", "binding", "dispatch_count", "dispatchCount", "expected_sha256", "expectedSha256",
        "reference_sha256", "referenceSha256", "reference_id", "referenceId", "reference_path", "referencePath",
        "absolute_tolerance", "absoluteTolerance", "relative_tolerance", "relativeTolerance", NULL};
    doe_stream_error error = {0};
    *out = (doe_output_oracle){0};
    bool ok = value && value->kind == DOE_JSON_OBJECT;
    if (!ok) snprintf(error.message, sizeof(error.message), "output_oracle must be an object");
    uint64_t schema = 0, group = 0, binding = 0;
    ok = ok && stream_only_members(value, allowed, 0, "output_oracle", &error) &&
        stream_read_u64(value, "schema_version", "schemaVersion", UINT32_MAX, true, 0, &schema, 0, &error);
    if (ok && (schema < 1 || schema > 3))
        ok = stream_reject(&error, 0, "output_oracle schema_version must be 1, 2 or 3");
    const char *scope = NULL, *reference_class = NULL, *kind = NULL, *initialization = NULL, *expected = NULL;
    ok = ok && optional_string(value, "scope", NULL, &scope, &error) &&
        optional_string(value, "reference_class", "referenceClass", &reference_class, &error) &&
        stream_read_string(value, "kind", NULL, &kind, 0, &error) &&
        stream_read_string(value, "initialization", NULL, &initialization, 0, &error) &&
        stream_read_string(value, "reference_id", "referenceId", &out->reference_id, 0, &error);
    if (ok && schema == 1 && (scope || reference_class))
        ok = stream_reject(&error, 0, "schema 1 has implicit isolated scope and independent reference class");
    if (ok && schema >= 2) {
        if (!scope || strcmp(scope, "command_graph"))
            ok = stream_reject(&error, 0, "schema 2/3 scope must be command_graph");
        else out->scope = DOE_ORACLE_COMMAND_GRAPH;
    }
    if (ok && schema >= 2) {
        if (!reference_class || (strcmp(reference_class, "independent_v1") &&
                strcmp(reference_class, "cross_runtime_consensus_v1")))
            ok = stream_reject(&error, 0, "schema 2/3 needs an explicit recognized reference_class");
        else out->reference_class = !strcmp(reference_class, "independent_v1") ?
            DOE_ORACLE_INDEPENDENT : DOE_ORACLE_CONSENSUS;
    }
    if (ok && (!initialization || strcmp(initialization, "zero_fill_v1")))
        ok = stream_reject(&error, 0, "output_oracle initialization must be zero_fill_v1");
    if (ok && (!out->reference_id || !kind))
        ok = stream_reject(&error, 0, "output_oracle needs kind and reference_id");
    if (ok && schema <= 2 && strcmp(kind, "sha256_exact_v1"))
        ok = stream_reject(&error, 0, "schema 1/2 kind must be sha256_exact_v1");
    if (ok && schema == 3 && strcmp(kind, "float32_reference_tolerance_v1"))
        ok = stream_reject(&error, 0, "schema 3 kind must be float32_reference_tolerance_v1");
    ok = ok && optional_string(value, "reference_path", "referencePath", &out->reference_path, &error) &&
        optional_f32(value, "absolute_tolerance", "absoluteTolerance", &out->absolute_tolerance, &error) &&
        optional_f32(value, "relative_tolerance", "relativeTolerance", &out->relative_tolerance, &error);
    if (ok && schema == 3 && (!out->reference_path || out->absolute_tolerance < 0 ||
            out->relative_tolerance < 0 || (out->absolute_tolerance == 0 && out->relative_tolerance == 0)))
        ok = stream_reject(&error, 0, "schema 3 needs a reference path and nonnegative, nonzero float32 tolerances");
    ok = ok && stream_read_string(value, schema == 3 ? "reference_sha256" : "expected_sha256",
        schema == 3 ? "referenceSha256" : "expectedSha256", &expected, 0, &error) &&
        stream_read_u64(value, "binding_group", "bindingGroup", UINT32_MAX, true, 0, &group, 0, &error) &&
        stream_read_u64(value, "binding", NULL, UINT32_MAX, true, 0, &binding, 0, &error) &&
        stream_read_u64(value, "dispatch_count", "dispatchCount", UINT32_MAX, true, 0, &out->dispatch_count, 0, &error);
    if (ok && out->dispatch_count == 0) ok = stream_reject(&error, 0, "output_oracle dispatch_count must be nonzero");
    if (ok && (!expected || strlen(expected) != 64 || strspn(expected, "0123456789abcdef") != 64))
        ok = stream_reject(&error, 0, "output_oracle digest must be 64 lowercase hex digits");
    if (!ok) {
        snprintf(message, message_size, "%s", error.message);
        return false;
    }
    out->schema_version = (uint32_t)schema;
    out->kind = schema == 3 ? DOE_ORACLE_FLOAT32_TOLERANCE : DOE_ORACLE_EXACT_SHA256;
    out->group = (uint32_t)group;
    out->binding = (uint32_t)binding;
    memcpy(out->expected_sha256, expected, 65);
    return true;
}

const char *doe_output_oracle_kind_name(doe_oracle_kind kind)
{
    switch (kind) {
    case DOE_ORACLE_EXACT_SHA256: return "sha256_exact_v1";
    case DOE_ORACLE_FLOAT32_TOLERANCE: return "float32_reference_tolerance_v1";
    }
    return NULL;
}

const char *doe_output_oracle_reference_class_name(doe_oracle_reference_class reference_class)
{
    switch (reference_class) {
    case DOE_ORACLE_INDEPENDENT: return "independent_v1";
    case DOE_ORACLE_CONSENSUS: return "cross_runtime_consensus_v1";
    }
    return NULL;
}
