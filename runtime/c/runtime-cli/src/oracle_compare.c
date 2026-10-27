/* SPDX-License-Identifier: MIT */
#include "oracle.h"
#include "host_io.h"
#include "sha256.h"
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

_Static_assert(sizeof(float) == 4 && FLT_RADIX == 2 && FLT_MANT_DIG == 24 && FLT_MAX_EXP == 128 && FLT_MIN_EXP == -125, "oracle files require IEEE binary32");

static bool reference_bytes(const doe_output_oracle *oracle, size_t size, uint8_t **out,
    char *message, size_t capacity)
{
    FILE *file = doe_host_fopen(oracle->reference_path, "rb");
    if (!file) {
        snprintf(message, capacity, "cannot read oracle reference %s", oracle->reference_path);
        return false;
    }
    uint8_t *bytes = malloc(size);
    if (!bytes) {
        fclose(file);
        snprintf(message, capacity, "cannot allocate %zu oracle reference bytes", size);
        return false;
    }
    bool ok = fread(bytes, 1, size, file) == size && fgetc(file) == EOF && !ferror(file);
    if (fclose(file) != 0) ok = false;
    if (!ok) {
        free(bytes);
        snprintf(message, capacity, "oracle reference must contain exactly %zu bytes", size);
        return false;
    }
    uint8_t digest[32];
    char hex[65];
    doe_sha256(bytes, size, digest);
    doe_sha256_hex(digest, hex);
    if (strcmp(hex, oracle->expected_sha256)) {
        free(bytes);
        snprintf(message, capacity, "oracle reference digest differs from declared reference_sha256");
        return false;
    }
    *out = bytes;
    return true;
}

static const doe_kernel_binding *target_binding(const doe_command *command, const doe_output_oracle *oracle)
{
    for (size_t i = 0; i < command->as.dispatch.binding_count; i++) {
        const doe_kernel_binding *binding = &command->as.dispatch.bindings[i];
        if (binding->group == oracle->group && binding->binding == oracle->binding && binding->resource == DOE_RESOURCE_BUFFER)
            return binding;
    }
    return NULL;
}

bool doe_oracle_preflight(const doe_stream *stream, bool validate_files, char *message, size_t capacity)
{
    for (size_t i = 0; i < stream->count; i++) {
        const doe_command *command = &stream->commands[i];
        if (command->kind != DOE_COMMAND_KERNEL_DISPATCH || !command->as.dispatch.oracle) continue;
        doe_output_oracle oracle;
        if (!doe_output_oracle_parse(command->as.dispatch.oracle, &oracle, message, capacity)) return false;
        const doe_kernel_binding *binding = target_binding(command, &oracle);
        if (!binding || binding->buffer_size == 0 || binding->buffer_size == UINT64_MAX || binding->buffer_size > SIZE_MAX || binding->buffer_size % 4 || binding->buffer_offset % 4 ||
            binding->buffer_offset > UINT64_MAX - binding->buffer_size) {
            snprintf(message, capacity, "command %zu oracle needs an explicit nonempty aligned buffer range", i);
            return false;
        }
        if (oracle.scope == DOE_ORACLE_ISOLATED_DISPATCH) {
            for (size_t j = 0; j < command->as.dispatch.binding_count; j++) {
                if (command->as.dispatch.bindings[j].resource != DOE_RESOURCE_BUFFER) {
                    snprintf(message, capacity, "command %zu isolated oracle over texture/sampler bindings is unsupported", i);
                    return false;
                }
            }
        }
        if (validate_files && oracle.kind == DOE_ORACLE_FLOAT32_TOLERANCE) {
            if (binding->buffer_size > SIZE_MAX) {
                snprintf(message, capacity, "command %zu oracle reference exceeds host addressing", i);
                return false;
            }
            uint8_t *bytes = NULL;
            if (!reference_bytes(&oracle, (size_t)binding->buffer_size, &bytes, message, capacity)) return false;
            free(bytes);
        }
    }
    return true;
}

static float float32_le(const uint8_t *bytes)
{
    uint32_t bits = (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 |
        (uint32_t)bytes[2] << 16 | (uint32_t)bytes[3] << 24;
    float value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

bool doe_oracle_compare_bytes(const doe_output_oracle *oracle, const uint8_t *actual, size_t size,
    doe_oracle_evidence *evidence, bool *matches, char *message, size_t capacity)
{
    *matches = false;
    uint8_t digest[32];
    doe_sha256(actual, size, digest);
    doe_sha256_hex(digest, evidence->actual_sha256);
    evidence->has_actual_sha256 = true;
    evidence->compared_value_count = 0;
    evidence->mismatch_count = 0;
    evidence->max_absolute_error = 0;
    evidence->max_relative_error = 0;
    switch (oracle->kind) {
    case DOE_ORACLE_EXACT_SHA256:
        *matches = !strcmp(evidence->actual_sha256, oracle->expected_sha256);
        return true;
    case DOE_ORACLE_FLOAT32_TOLERANCE: {
        if (size == 0 || size % 4) {
            snprintf(message, capacity, "float32 oracle capture must have a nonempty multiple-of-four byte count");
            return false;
        }
        uint8_t *reference = NULL;
        if (!reference_bytes(oracle, size, &reference, message, capacity)) return false;
        evidence->compared_value_count = size / 4;
        for (size_t offset = 0; offset < size; offset += 4) {
            double value = float32_le(actual + offset), expected = float32_le(reference + offset);
            if (!isfinite(value) || !isfinite(expected)) {
                evidence->mismatch_count++;
                continue;
            }
            double difference = fabs(value - expected), magnitude = fabs(expected);
            double relative = magnitude > 0 ? difference / magnitude : difference;
            if (difference > evidence->max_absolute_error) evidence->max_absolute_error = difference;
            if (relative > evidence->max_relative_error) evidence->max_relative_error = relative;
            double allowed = (double)oracle->absolute_tolerance + (double)oracle->relative_tolerance * magnitude;
            if (difference > allowed) evidence->mismatch_count++;
        }
        free(reference);
        *matches = evidence->mismatch_count == 0;
        return true;
    }
    }
    snprintf(message, capacity, "unknown oracle comparison kind");
    return false;
}
