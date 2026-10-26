/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "tsir_bootstrap_manifest.h"

#include "tsir_bootstrap.h"
#include "tsir_digest.h"
#include "tsir_frontend.h"
#include "tsir_planner.h"
#include "tsir_source.h"
#include "tsir_targets.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const struct {
    const char *kernel;
    const char *path;
} bootstrap_kernels[] = {
    { "fused_gemv", "runtime/zig/tests/tsir/bootstrap/fused_gemv.wgsl" },
    { "rms_norm", "runtime/zig/tests/tsir/bootstrap/rms_norm.wgsl" },
    { "gather", "runtime/zig/tests/tsir/bootstrap/gather.wgsl" }
};

static void digest_hex(const uint8_t bytes[32], char text[65]) {
    static const char digits[] = "0123456789abcdef";
    size_t i = 0;
    for (i = 0; i < 32; i++) {
        text[i * 2] = digits[bytes[i] >> 4];
        text[i * 2 + 1] = digits[bytes[i] & 15u];
    }
    text[64] = '\0';
}

static TsirStatus append_target(const char *root, const char *kernel,
    const TsirSemantic *semantic, TsirBootstrapTarget target, TsirBuffer *out,
    TsirBootstrapManifestError *error) {
    const TsirTargetIdentity *identity = tsir_target_descriptor(target);
    TsirPlannedRealization planned;
    TsirPlannerOptions options;
    TsirRealization realization;
    TsirDigests digests;
    TsirManifestEntry entry;
    TsirStatus status = TSIR_OK;
    uint8_t descriptor_hash[32];
    char emitter_hex[65];
    char descriptor_hex[65];
    char realization_hex[65];
    char semantic_hex[65];
    size_t i = 0;
    memset(&planned, 0, sizeof(planned));
    memset(&realization, 0, sizeof(realization));
    tsir_planner_default_options(&options);
    error->stage = "emitter identity";
    status = tsir_target_emitter_digest(target, root, options.emitter_digest);
    if (status != TSIR_OK) {
        error->system_error = errno;
        return status;
    }
    error->stage = "target descriptor";
    status = tsir_target_descriptor_hash(identity, descriptor_hash);
    if (status != TSIR_OK) {
        return status;
    }
    error->stage = "realization planning";
    status = tsir_plan_realization(semantic, &identity->planner, &options,
        &planned);
    if (status != TSIR_OK) {
        return status;
    }
    if (planned.rejection_count != 0) {
        snprintf(error->diagnostic.message, sizeof(error->diagnostic.message),
            "%s", planned.rejections[0].detail);
        tsir_plan_free_realization(&planned);
        return TSIR_ERR_REJECTED;
    }
    for (i = 0; i < planned.function_count; i++) {
        memcpy(planned.functions[i].target_descriptor_hash, descriptor_hash, 32);
    }
    realization.contract_version = planned.contract_version;
    realization.functions = planned.functions;
    realization.function_count = planned.function_count;
    memcpy(realization.emitter_digest, planned.emitter_digest, 32);
    realization.rejections = planned.rejections;
    realization.rejection_count = planned.rejection_count;
    error->stage = "TSIR digest";
    status = tsir_digests_compute(semantic, &realization,
        options.emitter_digest, &digests);
    if (status == TSIR_OK) {
        digest_hex(digests.emitter, emitter_hex);
        digest_hex(descriptor_hash, descriptor_hex);
        digest_hex(digests.realization, realization_hex);
        digest_hex(digests.semantic, semantic_hex);
        error->stage = "manifest serialization";
        status = tsir_bootstrap_manifest_template(kernel, identity->name,
            emitter_hex, descriptor_hex, realization_hex, semantic_hex, &entry);
        if (status == TSIR_OK) {
            status = tsir_bootstrap_write_manifest_entry(out, &entry);
        }
    }
    tsir_plan_free_realization(&planned);
    return status;
}

TsirStatus tsir_bootstrap_manifest_source(const char *root, const char *kernel,
    const char *source, size_t source_length, TsirBuffer *out,
    TsirBootstrapManifestError *error) {
    static const TsirBootstrapTarget targets[] = {
        TSIR_TARGET_WEBGPU_GENERIC, TSIR_TARGET_MSL, TSIR_TARGET_SPIR_V
    };
    TsirBootstrapManifestError local_error;
    TsirSemantic semantic;
    TsirBuffer staged;
    TsirManifestEntry metadata;
    TsirStatus status = TSIR_OK;
    size_t i = 0;
    memset(&local_error, 0, sizeof(local_error));
    if (error == NULL) {
        error = &local_error;
    }
    memset(error, 0, sizeof(*error));
    error->stage = "input validation";
    error->input = kernel;
    if (out != NULL && out->err != TSIR_OK) {
        return out->err;
    }
    if (root == NULL || kernel == NULL || source == NULL || out == NULL) {
        return TSIR_ERR_INVALID;
    }
    if (source_length > TSIR_BOOTSTRAP_MAX_WGSL_BYTES) {
        return TSIR_ERR_OVERFLOW;
    }
    status = tsir_bootstrap_manifest_template(kernel, "", NULL, NULL, NULL,
        NULL, &metadata);
    if (status != TSIR_OK) {
        return status;
    }
    memset(&semantic, 0, sizeof(semantic));
    tsir_buf_init(&staged);
    error->stage = "WGSL analysis";
    status = tsir_source_lower(source, source_length,
        tsir_bootstrap_frontend_version(), &semantic, &error->diagnostic);
    if (status == TSIR_OK && semantic.rejection_count != 0) {
        error->stage = "semantic lowering";
        snprintf(error->diagnostic.message, sizeof(error->diagnostic.message),
            "%s", semantic.rejections[0].detail);
        status = TSIR_ERR_REJECTED;
    }
    for (i = 0; status == TSIR_OK && i < sizeof(targets) / sizeof(targets[0]);
        i++) {
        if (i != 0) {
            tsir_buf_puts(&staged, ",\n");
        }
        status = append_target(root, kernel, &semantic, targets[i], &staged,
            error);
    }
    if (status == TSIR_OK) {
        error->stage = "output publication";
        tsir_buf_putn(out, staged.data, staged.len);
        status = out->err;
    }
    tsir_buf_free(&staged);
    tsir_frontend_free_semantic(&semantic);
    return status;
}

static TsirStatus read_source(const char *root, const char *relative,
    char **source, size_t *length, TsirBootstrapManifestError *error) {
    size_t root_length = strlen(root);
    size_t relative_length = strlen(relative);
    size_t capacity = (size_t)TSIR_BOOTSTRAP_MAX_WGSL_BYTES + 1u;
    char *path = NULL;
    char *bytes = NULL;
    FILE *input = NULL;
    TsirStatus status = TSIR_OK;
    int first_errno = 0;
    error->stage = "WGSL input read";
    error->input = relative;
    if (root_length > SIZE_MAX - relative_length - 2u) {
        return TSIR_ERR_OVERFLOW;
    }
    path = malloc(root_length + relative_length + 2u);
    bytes = malloc(capacity);
    if (path == NULL || bytes == NULL) {
        free(path);
        free(bytes);
        return TSIR_ERR_NOMEM;
    }
    memcpy(path, root, root_length);
    path[root_length] = '/';
    memcpy(path + root_length + 1u, relative, relative_length + 1u);
    input = fopen(path, "rb");
    first_errno = errno;
    free(path);
    if (input == NULL) {
        free(bytes);
        error->system_error = first_errno;
        return TSIR_ERR_INVALID;
    }
    *length = fread(bytes, 1u, capacity, input);
    if (ferror(input)) {
        first_errno = errno;
        status = TSIR_ERR_INVALID;
    } else if (*length > TSIR_BOOTSTRAP_MAX_WGSL_BYTES) {
        status = TSIR_ERR_OVERFLOW;
        first_errno = 0;
    } else {
        first_errno = 0;
    }
    if (fclose(input) != 0 && status == TSIR_OK) {
        first_errno = errno;
        status = TSIR_ERR_INVALID;
    }
    error->system_error = first_errno;
    if (status != TSIR_OK) {
        free(bytes);
        return status;
    }
    *source = bytes;
    return TSIR_OK;
}

TsirStatus tsir_bootstrap_manifest_generate(const char *root, TsirBuffer *out,
    TsirBootstrapManifestError *error) {
    TsirBootstrapManifestError local_error;
    TsirBuffer staged;
    TsirStatus status = TSIR_OK;
    size_t i = 0;
    memset(&local_error, 0, sizeof(local_error));
    if (error == NULL) {
        error = &local_error;
    }
    memset(error, 0, sizeof(*error));
    error->stage = "input validation";
    if (out != NULL && out->err != TSIR_OK) {
        return out->err;
    }
    if (root == NULL || out == NULL) {
        return TSIR_ERR_INVALID;
    }
    tsir_buf_init(&staged);
    tsir_buf_puts(&staged, "[\n");
    status = staged.err;
    for (i = 0; status == TSIR_OK &&
        i < sizeof(bootstrap_kernels) / sizeof(bootstrap_kernels[0]); i++) {
        char *source = NULL;
        size_t length = 0;
        status = read_source(root, bootstrap_kernels[i].path, &source, &length,
            error);
        if (status == TSIR_OK) {
            if (i != 0) {
                tsir_buf_puts(&staged, ",\n");
            }
            status = staged.err;
            if (status == TSIR_OK) {
                status = tsir_bootstrap_manifest_source(root,
                    bootstrap_kernels[i].kernel, source, length, &staged, error);
                error->input = bootstrap_kernels[i].path;
            }
        }
        free(source);
    }
    if (status == TSIR_OK) {
        tsir_buf_puts(&staged, "\n]\n");
        status = staged.err;
    }
    if (status == TSIR_OK) {
        error->stage = "output publication";
        tsir_buf_putn(out, staged.data, staged.len);
        status = out->err;
    }
    tsir_buf_free(&staged);
    return status;
}
