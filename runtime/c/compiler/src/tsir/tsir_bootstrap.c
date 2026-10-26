/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "tsir_bootstrap.h"

#include "tsir_json.h"
#include "tsir_reference.h"
#include "tsir_scalar.h"

#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

const char *tsir_bootstrap_frontend_version(void) {
    return "frontend-bootstrap-pipeline-v1";
}

const char *tsir_bootstrap_compiler_version(void) {
    return "doe-tsir-bootstrap-2026-04-24";
}

const char *tsir_bootstrap_canonical_kernel(const char *kernel) {
    static const char prefix[] = "doe.tsir.bootstrap.";
    size_t prefix_len = sizeof(prefix) - 1;
    const char *text = NULL;
    if (kernel == NULL) {
        return "";
    }
    text = kernel;
    if (strncmp(text, prefix, prefix_len) == 0) {
        text += prefix_len;
    }
    if (strcmp(text, "rmsnorm") == 0 || strcmp(text, "rms-norm") == 0) {
        return "rms_norm";
    }
    if (strcmp(text, "fused-gemv") == 0) {
        return "fused_gemv";
    }
    return text;
}

static bool is_hex64(const char *text) {
    size_t i = 0;
    if (text == NULL) {
        return false;
    }
    if (strlen(text) != 64) {
        return false;
    }
    for (i = 0; i < 64; i++) {
        char c = text[i];
        bool ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
        if (!ok) {
            return false;
        }
    }
    return true;
}

static void put_json_string(TsirBuffer *buf, const char *text) {
    size_t i = 0;
    tsir_buf_putc(buf, '"');
    if (text == NULL) {
        tsir_buf_putc(buf, '"');
        return;
    }
    for (i = 0; text[i] != '\0'; i++) {
        switch (text[i]) {
            case '\\':
                tsir_buf_puts(buf, "\\\\");
                break;
            case '"':
                tsir_buf_puts(buf, "\\\"");
                break;
            case '\n':
                tsir_buf_puts(buf, "\\n");
                break;
            case '\r':
                tsir_buf_puts(buf, "\\r");
                break;
            case '\t':
                tsir_buf_puts(buf, "\\t");
                break;
            default:
                if ((unsigned char)text[i] < 0x20u) {
                    static const char digits[] = "0123456789abcdef";
                    unsigned char value = (unsigned char)text[i];
                    tsir_buf_puts(buf, "\\u00");
                    tsir_buf_putc(buf, digits[value >> 4]);
                    tsir_buf_putc(buf, digits[value & 15u]);
                } else {
                    tsir_buf_putc(buf, text[i]);
                }
                break;
        }
    }
    tsir_buf_putc(buf, '"');
}

static void put_string_array(TsirBuffer *buf,
    const char *const *items, size_t count) {
    size_t i = 0;
    tsir_buf_putc(buf, '[');
    for (i = 0; i < count; i++) {
        if (i != 0) {
            tsir_buf_puts(buf, ", ");
        }
        put_json_string(buf, items[i]);
    }
    tsir_buf_putc(buf, ']');
}

static void write_manifest_entry(TsirBuffer *buf,
    const TsirManifestEntry *entry) {
    tsir_buf_puts(buf, "  {\n");
    tsir_buf_puts(buf, "    \"backend\": ");
    put_json_string(buf, entry->backend);
    tsir_buf_puts(buf, ",\n");
    tsir_buf_puts(buf, "    \"compilerVersion\": ");
    put_json_string(buf, tsir_bootstrap_compiler_version());
    tsir_buf_puts(buf, ",\n");
    tsir_buf_puts(buf, "    \"emitterDigest\": ");
    put_json_string(buf, entry->emitter_digest);
    tsir_buf_puts(buf, ",\n");
    tsir_buf_puts(buf, "    \"exactnessClass\": ");
    put_json_string(buf, entry->exactness_class);
    tsir_buf_puts(buf, ",\n");
    tsir_buf_puts(buf, "    \"algorithmExactInvariants\": ");
    put_string_array(buf, entry->algorithm_exact_invariants,
        entry->invariant_count);
    tsir_buf_puts(buf, ",\n");
    tsir_buf_puts(buf, "    \"toleranceMetric\": ");
    put_json_string(buf, entry->tolerance_metric == NULL ? "" :
        entry->tolerance_metric);
    tsir_buf_puts(buf, ",\n");
    tsir_buf_puts(buf, "    \"toleranceEpsilon\": ");
    tsir_buf_puts(buf, entry->tolerance_epsilon == NULL ? "0" :
        entry->tolerance_epsilon);
    tsir_buf_puts(buf, ",\n");
    tsir_buf_puts(buf, "    \"frontendVersion\": ");
    put_json_string(buf, tsir_bootstrap_frontend_version());
    tsir_buf_puts(buf, ",\n");
    tsir_buf_puts(buf, "    \"kernelRef\": ");
    /* Quote the complete reference through the same JSON string owner. */
    TsirBuffer reference;
    tsir_buf_init(&reference);
    tsir_buf_puts(&reference, "doe.tsir.bootstrap.");
    tsir_buf_puts(&reference, entry->kernel_ref);
    if (reference.err == TSIR_OK) {
        put_json_string(buf, reference.data);
    } else if (buf->err == TSIR_OK) {
        buf->err = reference.err;
    }
    tsir_buf_free(&reference);
    tsir_buf_puts(buf, ",\n");
    tsir_buf_puts(buf, "    \"rejectionReasons\": ");
    put_string_array(buf, entry->rejection_reasons,
        entry->rejection_count);
    tsir_buf_puts(buf, ",\n");
    tsir_buf_puts(buf, "    \"targetDescriptorCorrectnessHash\": ");
    put_json_string(buf, entry->target_descriptor_correctness_hash);
    tsir_buf_puts(buf, ",\n");
    tsir_buf_puts(buf, "    \"tsirRealizationDigest\": ");
    put_json_string(buf, entry->tsir_realization_digest);
    tsir_buf_puts(buf, ",\n");
    tsir_buf_puts(buf, "    \"tsirSemanticDigest\": ");
    put_json_string(buf, entry->tsir_semantic_digest);
    tsir_buf_puts(buf, "\n");
    tsir_buf_puts(buf, "  }");
}

static bool valid_string_array(const char *const *values, size_t count) {
    if (values == NULL && count != 0) {
        return false;
    }
    for (size_t i = 0; i < count; i++) {
        if (values[i] == NULL) {
            return false;
        }
    }
    return true;
}

TsirStatus tsir_bootstrap_write_manifest_entry(TsirBuffer *buf,
    const TsirManifestEntry *entry) {
    if (buf == NULL) {
        return TSIR_ERR_INVALID;
    }
    if (buf->err != TSIR_OK) {
        return buf->err;
    }
    if (entry == NULL || entry->backend == NULL ||
        entry->exactness_class == NULL || entry->kernel_ref == NULL ||
        !is_hex64(entry->emitter_digest) ||
        !is_hex64(entry->target_descriptor_correctness_hash) ||
        !is_hex64(entry->tsir_realization_digest) ||
        !is_hex64(entry->tsir_semantic_digest) ||
        !valid_string_array(entry->algorithm_exact_invariants,
            entry->invariant_count) ||
        !valid_string_array(entry->rejection_reasons,
            entry->rejection_count)) {
        return TSIR_ERR_INVALID;
    }
    const char *epsilon = entry->tolerance_epsilon == NULL ? "0" :
        entry->tolerance_epsilon;
    doe_json_document number = {0};
    doe_json_status parsed = doe_json_parse(epsilon, strlen(epsilon),
        &number, NULL);
    if (parsed != DOE_JSON_OK) {
        return parsed == DOE_JSON_OUT_OF_MEMORY ? TSIR_ERR_NOMEM :
            TSIR_ERR_INVALID;
    }
    bool valid = number.root->kind == DOE_JSON_NUMBER;
    doe_json_free(&number);
    if (!valid) {
        return TSIR_ERR_INVALID;
    }
    TsirBuffer staged;
    tsir_buf_init(&staged);
    write_manifest_entry(&staged, entry);
    TsirStatus status = staged.err;
    if (status == TSIR_OK) {
        tsir_buf_putn(buf, staged.data, staged.len);
        status = buf->err;
    }
    tsir_buf_free(&staged);
    return status;
}

TsirStatus tsir_bootstrap_manifest_template(const char *kernel,
    const char *backend, const char *emitter_digest,
    const char *target_descriptor_correctness_hash,
    const char *tsir_realization_digest,
    const char *tsir_semantic_digest, TsirManifestEntry *out) {
    static const char *const gemv_invariants[2] = {
        "reduction_order", "accum_dtype"
    };
    const char *canonical = tsir_bootstrap_canonical_kernel(kernel);
    if (out == NULL || backend == NULL) {
        return TSIR_ERR_INVALID;
    }
    memset(out, 0, sizeof(*out));
    out->backend = backend;
    out->emitter_digest = emitter_digest;
    out->target_descriptor_correctness_hash =
        target_descriptor_correctness_hash;
    out->tsir_realization_digest = tsir_realization_digest;
    out->tsir_semantic_digest = tsir_semantic_digest;
    out->kernel_ref = canonical;
    if (strcmp(canonical, "fused_gemv") == 0) {
        out->exactness_class = "algorithm_exact";
        out->algorithm_exact_invariants = gemv_invariants;
        out->invariant_count = 2;
        out->tolerance_metric = "";
        out->tolerance_epsilon = "0";
        return TSIR_OK;
    }
    if (strcmp(canonical, "rms_norm") == 0) {
        out->exactness_class = "tolerance_bounded";
        out->tolerance_metric = "max_abs";
        out->tolerance_epsilon = "0.000001";
        return TSIR_OK;
    }
    if (strcmp(canonical, "gather") == 0) {
        out->exactness_class = "bit_exact_solo";
        out->tolerance_metric = "";
        out->tolerance_epsilon = "0";
        return TSIR_OK;
    }
    return TSIR_ERR_UNSUPPORTED;
}
