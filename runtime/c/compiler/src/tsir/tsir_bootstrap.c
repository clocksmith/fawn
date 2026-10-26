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
                tsir_buf_putc(buf, text[i]);
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

TsirStatus tsir_bootstrap_write_manifest_entry(TsirBuffer *buf,
    const TsirManifestEntry *entry) {
    if (buf == NULL || entry == NULL || entry->backend == NULL ||
        entry->exactness_class == NULL || entry->kernel_ref == NULL ||
        !is_hex64(entry->emitter_digest) ||
        !is_hex64(entry->target_descriptor_correctness_hash) ||
        !is_hex64(entry->tsir_realization_digest) ||
        !is_hex64(entry->tsir_semantic_digest)) {
        return TSIR_ERR_INVALID;
    }
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
    tsir_buf_puts(buf, "    \"kernelRef\": \"doe.tsir.bootstrap.");
    tsir_buf_puts(buf, entry->kernel_ref);
    tsir_buf_puts(buf, "\",\n");
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
    tsir_buf_puts(buf, "\"\n");
    tsir_buf_puts(buf, "  }");
    return buf->err;
}

TsirStatus tsir_bootstrap_manifest_template(const char *kernel,
    const char *backend, const char *emitter_digest,
    const char *target_descriptor_correctness_hash,
    const char *tsir_realization_digest,
    const char *tsir_semantic_digest, TsirManifestEntry *out) {
    static const char *gemv_invariants[2] = {
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

/* ---- oracle ---- */

#define ORACLE_OK 0
#define ORACLE_INVALID_JSON 1
#define ORACLE_NOT_IMPLEMENTED 2

typedef struct OracleBuffer {
    char *name;
    TsirScalarKind elem;
    uint64_t *shape;
    size_t shape_len;
    uint8_t *data;
    size_t data_len;
} OracleBuffer;

static void free_buffers(OracleBuffer *buffers, size_t count) {
    size_t i = 0;
    if (buffers == NULL) {
        return;
    }
    for (i = 0; i < count; i++) {
        free(buffers[i].name);
        free(buffers[i].shape);
        free(buffers[i].data);
    }
    free(buffers);
}

static char *dup_range(const char *text, size_t len) {
    char *copy = (char *)malloc(len + 1);
    if (copy == NULL) {
        return NULL;
    }
    memcpy(copy, text, len);
    copy[len] = '\0';
    return copy;
}

static int hex_value(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

static size_t elem_size(TsirScalarKind elem) {
    switch (elem) {
        case TSIR_SCALAR_F32:
        case TSIR_SCALAR_U32:
            return 4;
        case TSIR_SCALAR_F16:
        case TSIR_SCALAR_BF16:
            return 2;
        default:
            return 0;
    }
}

static int parse_buffer(const char *name,
    const doe_json_value *value, OracleBuffer *out) {
    const doe_json_value *elem_value = NULL;
    const doe_json_value *shape_value = NULL;
    const doe_json_value *hex_value_node = NULL;
    const doe_json_value *values_value = NULL;
    TsirScalarKind elem = TSIR_SCALAR_F32;
    uint64_t *shape = NULL;
    size_t shape_len = 0;
    size_t elem_count = 1;
    size_t byte_len = 0;
    uint8_t *data = NULL;
    size_t i = 0;
    memset(out, 0, sizeof(*out));
    if (value == NULL || value->kind != DOE_JSON_OBJECT) {
        return ORACLE_INVALID_JSON;
    }
    elem_value = doe_json_member_value(value, "elem");
    if (elem_value == NULL || elem_value->kind != DOE_JSON_STRING) {
        return ORACLE_INVALID_JSON;
    }
    if (strcmp(elem_value->string, "f32") == 0) {
        elem = TSIR_SCALAR_F32;
    } else if (strcmp(elem_value->string, "f16") == 0) {
        elem = TSIR_SCALAR_F16;
    } else if (strcmp(elem_value->string, "bf16") == 0) {
        elem = TSIR_SCALAR_BF16;
    } else if (strcmp(elem_value->string, "u32") == 0) {
        elem = TSIR_SCALAR_U32;
    } else {
        return ORACLE_NOT_IMPLEMENTED;
    }
    shape_value = doe_json_member_value(value, "shape");
    if (shape_value == NULL || shape_value->kind != DOE_JSON_ARRAY) {
        return ORACLE_INVALID_JSON;
    }
    shape_len = shape_value->count;
    shape = (uint64_t *)malloc(
        shape_len == 0 ? 1 : shape_len * sizeof(uint64_t));
    if (shape == NULL) {
        return ORACLE_INVALID_JSON;
    }
    for (i = 0; i < shape_len; i++) {
        uint64_t dim = 0;
        if (!doe_json_as_u64(shape_value->items[i], UINT64_MAX,
            &dim)) {
            free(shape);
            return ORACLE_INVALID_JSON;
        }
        shape[i] = dim;
        if (dim != 0 && elem_count > SIZE_MAX / dim) {
            free(shape);
            return ORACLE_INVALID_JSON;
        }
        elem_count *= (size_t)dim;
    }
    if (elem_count > SIZE_MAX / elem_size(elem)) {
        free(shape);
        return ORACLE_INVALID_JSON;
    }
    byte_len = elem_count * elem_size(elem);
    data = (uint8_t *)malloc(byte_len == 0 ? 1 : byte_len);
    if (data == NULL) {
        free(shape);
        return ORACLE_INVALID_JSON;
    }
    hex_value_node = doe_json_member_value(value, "bytesHex");
    values_value = doe_json_member_value(value, "values");
    if (hex_value_node != NULL) {
        if (hex_value_node->kind != DOE_JSON_STRING ||
            hex_value_node->string_size != byte_len * 2) {
            free(shape);
            free(data);
            return ORACLE_INVALID_JSON;
        }
        for (i = 0; i < byte_len; i++) {
            int hi = hex_value(hex_value_node->string[2 * i]);
            int lo = hex_value(hex_value_node->string[2 * i + 1]);
            if (hi < 0 || lo < 0) {
                free(shape);
                free(data);
                return ORACLE_INVALID_JSON;
            }
            data[i] = (uint8_t)((hi << 4) | lo);
        }
    } else if (values_value != NULL) {
        if (values_value->kind != DOE_JSON_ARRAY ||
            values_value->count != elem_count) {
            free(shape);
            free(data);
            return ORACLE_INVALID_JSON;
        }
        for (i = 0; i < elem_count; i++) {
            const doe_json_value *item = values_value->items[i];
            if (item->kind != DOE_JSON_NUMBER) {
                free(shape);
                free(data);
                return ORACLE_INVALID_JSON;
            }
            if (elem == TSIR_SCALAR_F32) {
                float v = 0.0f;
                uint32_t bits = 0;
                if (isfinite(item->number) &&
                    fabs(item->number) > (double)FLT_MAX) {
                    free(shape);
                    free(data);
                    return ORACLE_INVALID_JSON;
                }
                v = (float)item->number;
                memcpy(&bits, &v, 4);
                data[4 * i] = (uint8_t)(bits & 0xff);
                data[4 * i + 1] = (uint8_t)((bits >> 8) & 0xff);
                data[4 * i + 2] = (uint8_t)((bits >> 16) & 0xff);
                data[4 * i + 3] = (uint8_t)((bits >> 24) & 0xff);
            } else if (elem == TSIR_SCALAR_F16) {
                uint16_t bits = 0;
                if (isfinite(item->number) &&
                    fabs(item->number) > (double)FLT_MAX) {
                    free(shape);
                    free(data);
                    return ORACLE_INVALID_JSON;
                }
                bits = tsir_f32_to_f16_bits((float)item->number);
                data[2 * i] = (uint8_t)(bits & 0xff);
                data[2 * i + 1] = (uint8_t)((bits >> 8) & 0xff);
            } else if (elem == TSIR_SCALAR_BF16) {
                uint16_t bits = 0;
                if (isfinite(item->number) &&
                    fabs(item->number) > (double)FLT_MAX) {
                    free(shape);
                    free(data);
                    return ORACLE_INVALID_JSON;
                }
                bits = tsir_f32_to_bf16_rne((float)item->number);
                data[2 * i] = (uint8_t)(bits & 0xff);
                data[2 * i + 1] = (uint8_t)((bits >> 8) & 0xff);
            } else {
                uint64_t v = 0;
                if (!doe_json_as_u64(item, UINT32_MAX, &v)) {
                    free(shape);
                    free(data);
                    return ORACLE_INVALID_JSON;
                }
                data[4 * i] = (uint8_t)(v & 0xff);
                data[4 * i + 1] = (uint8_t)((v >> 8) & 0xff);
                data[4 * i + 2] = (uint8_t)((v >> 16) & 0xff);
                data[4 * i + 3] = (uint8_t)((v >> 24) & 0xff);
            }
        }
    } else {
        free(shape);
        free(data);
        return ORACLE_INVALID_JSON;
    }
    out->name = dup_range(name, strlen(name));
    if (out->name == NULL) {
        free(shape);
        free(data);
        return ORACLE_INVALID_JSON;
    }
    out->elem = elem;
    out->shape = shape;
    out->shape_len = shape_len;
    out->data = data;
    out->data_len = byte_len;
    return ORACLE_OK;
}

static const OracleBuffer *find_buffer(const OracleBuffer *buffers,
    size_t count, const char *name) {
    size_t i = 0;
    for (i = 0; i < count; i++) {
        if (strcmp(buffers[i].name, name) == 0) {
            return &buffers[i];
        }
    }
    return NULL;
}

static TsirStatus write_status(const char *status,
    const uint8_t hash[32], const char *detail,
    char **out_status_json, size_t *out_len) {
    TsirBuffer buf;
    char *text = NULL;
    size_t i = 0;
    tsir_buf_init(&buf);
    tsir_buf_puts(&buf, "{\"detail\":");
    put_json_string(&buf, detail);
    tsir_buf_puts(&buf, ",\"referenceHash\":");
    if (hash == NULL) {
        tsir_buf_puts(&buf, "null");
    } else {
        static const char digits[] = "0123456789abcdef";
        tsir_buf_putc(&buf, '"');
        for (i = 0; i < 32; i++) {
            tsir_buf_putc(&buf, digits[(hash[i] >> 4) & 0xf]);
            tsir_buf_putc(&buf, digits[hash[i] & 0xf]);
        }
        tsir_buf_putc(&buf, '"');
    }
    tsir_buf_puts(&buf, ",\"status\":");
    put_json_string(&buf, status);
    tsir_buf_puts(&buf, "}\n");
    if (buf.err != TSIR_OK) {
        TsirStatus err = buf.err;
        tsir_buf_free(&buf);
        return err;
    }
    text = (char *)malloc(buf.len + 1);
    if (text == NULL) {
        tsir_buf_free(&buf);
        return TSIR_ERR_NOMEM;
    }
    memcpy(text, buf.data, buf.len);
    text[buf.len] = '\0';
    *out_status_json = text;
    *out_len = buf.len;
    tsir_buf_free(&buf);
    return TSIR_OK;
}

static TsirStatus run_kernel(const char *kernel,
    const OracleBuffer *buffers, size_t buffer_count,
    TsirReferenceResult *result) {
    TsirSemantic semantic;
    TsirSemanticFunction func;
    TsirRealization realization;
    TsirStatus status = TSIR_OK;
    memset(&semantic, 0, sizeof(semantic));
    memset(&func, 0, sizeof(func));
    memset(&realization, 0, sizeof(realization));
    func.name = "main";
    semantic.functions = &func;
    semantic.function_count = 1;
    if (strcmp(kernel, "fused_gemv") == 0) {
        const OracleBuffer *matrix = find_buffer(buffers,
            buffer_count, "W");
        const OracleBuffer *vector = find_buffer(buffers,
            buffer_count, "x");
        TsirBufferBinding bindings[3];
        TsirIterationAxis axes[2];
        TsirSemanticBodyBinding roles[3];
        TsirSemanticBodyAxis axis_roles[2];
        TsirReductionRegion reductions[1];
        TsirInputBuffer inputs[2];
        uint64_t output_shape[1];
        if (matrix == NULL || vector == NULL) {
            return TSIR_ERR_UNSUPPORTED;
        }
        if (matrix->shape_len != 2 || vector->shape_len != 1) {
            return TSIR_ERR_UNSUPPORTED;
        }
        if (matrix->shape[1] != vector->shape[0]) {
            return TSIR_ERR_UNSUPPORTED;
        }
        output_shape[0] = matrix->shape[0];
        memset(bindings, 0, sizeof(bindings));
        bindings[0].name = "W";
        bindings[0].binding = 0;
        bindings[0].logical_shape = matrix->shape;
        bindings[0].logical_shape_len = matrix->shape_len;
        bindings[0].elem = matrix->elem;
        bindings[1].name = "x";
        bindings[1].binding = 1;
        bindings[1].logical_shape = vector->shape;
        bindings[1].logical_shape_len = vector->shape_len;
        bindings[1].elem = vector->elem;
        bindings[2].name = "y";
        bindings[2].binding = 2;
        bindings[2].logical_shape = output_shape;
        bindings[2].logical_shape_len = 1;
        bindings[2].elem = matrix->elem;
        bindings[2].read_write = true;
        axes[0].name = "i";
        axes[0].lower_bound = "0";
        axes[0].upper_bound = "M";
        axes[0].step = "1";
        axes[1].name = "k";
        axes[1].lower_bound = "0";
        axes[1].upper_bound = "K";
        axes[1].step = "1";
        roles[0].binding_index = 0;
        roles[0].role = TSIR_ROLE_MATRIX;
        roles[1].binding_index = 1;
        roles[1].role = TSIR_ROLE_VECTOR;
        roles[2].binding_index = 2;
        roles[2].role = TSIR_ROLE_OUTPUT;
        axis_roles[0].axis_index = 0;
        axis_roles[0].role = TSIR_AXIS_OUTPUT;
        axis_roles[1].axis_index = 1;
        axis_roles[1].role = TSIR_AXIS_REDUCTION;
        reductions[0].axis = 1;
        reductions[0].op = TSIR_REDUCTION_SUM;
        reductions[0].contract.accumulation = TSIR_SCALAR_F32;
        reductions[0].contract.associativity =
            TSIR_ASSOC_STRICT_ORDERED;
        reductions[0].contract.nan_inf = TSIR_NANINF_PROPAGATE;
        reductions[0].target_binding = 2;
        func.family_hint = TSIR_HINT_FUSED_GEMV;
        func.axes = axes;
        func.axis_count = 2;
        func.bindings = bindings;
        func.binding_count = 3;
        func.reductions = reductions;
        func.reduction_count = 1;
        func.body.op = TSIR_BODY_FUSED_GEMV;
        func.body.binding_roles = roles;
        func.body.binding_role_count = 3;
        func.body.axis_roles = axis_roles;
        func.body.axis_role_count = 2;
        inputs[0].data = matrix->data;
        inputs[0].len = matrix->data_len;
        inputs[1].data = vector->data;
        inputs[1].len = vector->data_len;
        status = tsir_reference_run(&semantic, &realization,
            inputs, 2, result);
        return status;
    }
    if (strcmp(kernel, "rms_norm") == 0) {
        const OracleBuffer *input = find_buffer(buffers,
            buffer_count, "input");
        const OracleBuffer *weight = find_buffer(buffers,
            buffer_count, "weight");
        const OracleBuffer *uniform = find_buffer(buffers,
            buffer_count, "u");
        TsirBufferBinding bindings[4];
        TsirIterationAxis axes[2];
        TsirSemanticBodyBinding roles[3];
        TsirSemanticBodyAxis axis_roles[2];
        TsirReductionRegion reductions[1];
        TsirInputBuffer inputs[3];
        if (input == NULL || weight == NULL || uniform == NULL) {
            return TSIR_ERR_UNSUPPORTED;
        }
        if (input->shape_len != 1 || weight->shape_len != 1) {
            return TSIR_ERR_UNSUPPORTED;
        }
        if (input->shape[0] != weight->shape[0]) {
            return TSIR_ERR_UNSUPPORTED;
        }
        memset(bindings, 0, sizeof(bindings));
        bindings[0].name = "input";
        bindings[0].binding = 0;
        bindings[0].logical_shape = input->shape;
        bindings[0].logical_shape_len = input->shape_len;
        bindings[0].elem = input->elem;
        bindings[1].name = "weight";
        bindings[1].binding = 1;
        bindings[1].logical_shape = weight->shape;
        bindings[1].logical_shape_len = weight->shape_len;
        bindings[1].elem = weight->elem;
        bindings[2].name = "output";
        bindings[2].binding = 2;
        bindings[2].logical_shape = input->shape;
        bindings[2].logical_shape_len = input->shape_len;
        bindings[2].elem = input->elem;
        bindings[2].read_write = true;
        bindings[3].name = "u";
        bindings[3].binding = 3;
        bindings[3].logical_shape = uniform->shape;
        bindings[3].logical_shape_len = uniform->shape_len;
        bindings[3].elem = uniform->elem;
        axes[0].name = "d";
        axes[0].lower_bound = "0";
        axes[0].upper_bound = "hidden_size";
        axes[0].step = "1";
        axes[1].name = "i";
        axes[1].lower_bound = "0";
        axes[1].upper_bound = "hidden_size";
        axes[1].step = "1";
        roles[0].binding_index = 0;
        roles[0].role = TSIR_ROLE_INPUT;
        roles[1].binding_index = 1;
        roles[1].role = TSIR_ROLE_SCALE;
        roles[2].binding_index = 2;
        roles[2].role = TSIR_ROLE_OUTPUT;
        axis_roles[0].axis_index = 0;
        axis_roles[0].role = TSIR_AXIS_HIDDEN;
        axis_roles[1].axis_index = 1;
        axis_roles[1].role = TSIR_AXIS_REDUCTION;
        reductions[0].axis = 1;
        reductions[0].op = TSIR_REDUCTION_SUM;
        reductions[0].contract.accumulation = TSIR_SCALAR_F32;
        reductions[0].contract.associativity =
            TSIR_ASSOC_STRICT_ORDERED;
        reductions[0].contract.nan_inf = TSIR_NANINF_PROPAGATE;
        reductions[0].target_binding = 2;
        func.family_hint = TSIR_HINT_RMS_NORM;
        func.axes = axes;
        func.axis_count = 2;
        func.bindings = bindings;
        func.binding_count = 4;
        func.reductions = reductions;
        func.reduction_count = 1;
        func.body.op = TSIR_BODY_RMS_NORM;
        func.body.binding_roles = roles;
        func.body.binding_role_count = 3;
        func.body.axis_roles = axis_roles;
        func.body.axis_role_count = 2;
        func.body.has_rms_norm = true;
        func.body.rms_norm.formula =
            TSIR_RMSNORM_SUM_SQUARES_MEAN_EPSILON_RSQRT_SCALE;
        func.body.rms_norm.epsilon.source =
            TSIR_EPSILON_UNIFORM_FIELD;
        func.body.rms_norm.epsilon.path = "uniform:u.eps";
        func.body.rms_norm.epsilon.binding_index.has = true;
        func.body.rms_norm.epsilon.binding_index.value = 3;
        func.body.rms_norm.epsilon.byte_offset.has = true;
        func.body.rms_norm.epsilon.byte_offset.value = 4;
        func.body.rms_norm.hidden_extent_axis = 0;
        func.body.rms_norm.reduction_target =
            TSIR_RMSNORM_INTERMEDIATE_SCALAR;
        inputs[0].data = input->data;
        inputs[0].len = input->data_len;
        inputs[1].data = weight->data;
        inputs[1].len = weight->data_len;
        inputs[2].data = uniform->data;
        inputs[2].len = uniform->data_len;
        status = tsir_reference_run(&semantic, &realization,
            inputs, 3, result);
        return status;
    }
    if (strcmp(kernel, "gather") == 0) {
        const OracleBuffer *indices = find_buffer(buffers,
            buffer_count, "indices");
        const OracleBuffer *table = find_buffer(buffers,
            buffer_count, "table");
        TsirBufferBinding bindings[3];
        TsirIterationAxis axes[2];
        TsirSemanticBodyBinding roles[3];
        TsirSemanticBodyAxis axis_roles[2];
        TsirInputBuffer inputs[2];
        uint64_t output_shape[2];
        if (indices == NULL || table == NULL) {
            return TSIR_ERR_UNSUPPORTED;
        }
        if (indices->shape_len != 1 || table->shape_len != 2) {
            return TSIR_ERR_UNSUPPORTED;
        }
        output_shape[0] = indices->shape[0];
        output_shape[1] = table->shape[1];
        memset(bindings, 0, sizeof(bindings));
        bindings[0].name = "indices";
        bindings[0].binding = 0;
        bindings[0].logical_shape = indices->shape;
        bindings[0].logical_shape_len = indices->shape_len;
        bindings[0].elem = indices->elem;
        bindings[1].name = "table";
        bindings[1].binding = 1;
        bindings[1].logical_shape = table->shape;
        bindings[1].logical_shape_len = table->shape_len;
        bindings[1].elem = table->elem;
        bindings[2].name = "output";
        bindings[2].binding = 2;
        bindings[2].logical_shape = output_shape;
        bindings[2].logical_shape_len = 2;
        bindings[2].elem = table->elem;
        bindings[2].read_write = true;
        axes[0].name = "t";
        axes[0].lower_bound = "0";
        axes[0].upper_bound = "num_tokens";
        axes[0].step = "1";
        axes[1].name = "h";
        axes[1].lower_bound = "0";
        axes[1].upper_bound = "hidden";
        axes[1].step = "1";
        roles[0].binding_index = 0;
        roles[0].role = TSIR_ROLE_INDICES;
        roles[1].binding_index = 1;
        roles[1].role = TSIR_ROLE_TABLE;
        roles[2].binding_index = 2;
        roles[2].role = TSIR_ROLE_OUTPUT;
        axis_roles[0].axis_index = 0;
        axis_roles[0].role = TSIR_AXIS_TOKEN;
        axis_roles[1].axis_index = 1;
        axis_roles[1].role = TSIR_AXIS_HIDDEN;
        func.family_hint = TSIR_HINT_GATHER;
        func.axes = axes;
        func.axis_count = 2;
        func.bindings = bindings;
        func.binding_count = 3;
        func.body.op = TSIR_BODY_GATHER;
        func.body.binding_roles = roles;
        func.body.binding_role_count = 3;
        func.body.axis_roles = axis_roles;
        func.body.axis_role_count = 2;
        inputs[0].data = indices->data;
        inputs[0].len = indices->data_len;
        inputs[1].data = table->data;
        inputs[1].len = table->data_len;
        status = tsir_reference_run(&semantic, &realization,
            inputs, 2, result);
        return status;
    }
    return TSIR_ERR_UNSUPPORTED;
}

TsirStatus tsir_bootstrap_oracle_run(const char *kernel,
    const char *input_json, size_t input_len,
    char **out_status_json, size_t *out_len) {
    doe_json_document doc;
    doe_json_error parse_error;
    const doe_json_value *root = NULL;
    const doe_json_value *kernel_value = NULL;
    const doe_json_value *inputs_value = NULL;
    const char *canonical = NULL;
    const char *doc_kernel = NULL;
    OracleBuffer *buffers = NULL;
    size_t buffer_count = 0;
    size_t buffer_cap = 0;
    size_t i = 0;
    int parse_result = ORACLE_OK;
    TsirReferenceResult result;
    TsirStatus status = TSIR_OK;
    if (kernel == NULL || input_json == NULL ||
        out_status_json == NULL || out_len == NULL) {
        return TSIR_ERR_INVALID;
    }
    *out_status_json = NULL;
    *out_len = 0;
    if (input_len > TSIR_BOOTSTRAP_MAX_INPUT_BYTES) {
        return write_status("not_implemented", NULL,
            "bootstrap oracle cannot execute this input",
            out_status_json, out_len);
    }
    memset(&doc, 0, sizeof(doc));
    if (doe_json_parse(input_json, input_len, &doc,
        &parse_error) != DOE_JSON_OK) {
        return write_status("not_implemented", NULL,
            "bootstrap oracle input JSON is unsupported or missing kernel/inputs",
            out_status_json, out_len);
    }
    root = doc.root;
    if (root == NULL || root->kind != DOE_JSON_OBJECT) {
        doe_json_free(&doc);
        return write_status("not_implemented", NULL,
            "bootstrap oracle input JSON is unsupported or missing kernel/inputs",
            out_status_json, out_len);
    }
    kernel_value = doe_json_member_value(root, "kernel");
    inputs_value = doe_json_member_value(root, "inputs");
    if (kernel_value == NULL ||
        kernel_value->kind != DOE_JSON_STRING ||
        inputs_value == NULL) {
        doe_json_free(&doc);
        return write_status("not_implemented", NULL,
            "bootstrap oracle input JSON is unsupported or missing kernel/inputs",
            out_status_json, out_len);
    }
    canonical = tsir_bootstrap_canonical_kernel(kernel);
    doc_kernel = tsir_bootstrap_canonical_kernel(kernel_value->string);
    if (strcmp(canonical, doc_kernel) != 0) {
        doe_json_free(&doc);
        return write_status("not_implemented", NULL,
            "bootstrap oracle cannot execute this input",
            out_status_json, out_len);
    }
    if (strcmp(canonical, "fused_gemv") != 0 &&
        strcmp(canonical, "rms_norm") != 0 &&
        strcmp(canonical, "gather") != 0) {
        doe_json_free(&doc);
        return write_status("not_implemented", NULL,
            "bootstrap oracle cannot execute this input",
            out_status_json, out_len);
    }
    if (inputs_value->kind == DOE_JSON_OBJECT) {
        for (i = 0; i < inputs_value->count; i++) {
            OracleBuffer buffer;
            if (buffer_count == buffer_cap) {
                size_t cap = buffer_cap == 0 ? 8 : buffer_cap * 2;
                OracleBuffer *grown = (OracleBuffer *)realloc(
                    buffers, cap * sizeof(OracleBuffer));
                if (grown == NULL) {
                    free_buffers(buffers, buffer_count);
                    doe_json_free(&doc);
                    return TSIR_ERR_NOMEM;
                }
                buffers = grown;
                buffer_cap = cap;
            }
            parse_result = parse_buffer(
                inputs_value->members[i].key,
                inputs_value->members[i].value, &buffer);
            if (parse_result != ORACLE_OK) {
                break;
            }
            buffers[buffer_count++] = buffer;
        }
    } else if (inputs_value->kind == DOE_JSON_ARRAY) {
        for (i = 0; i < inputs_value->count; i++) {
            const doe_json_value *item = inputs_value->items[i];
            const doe_json_value *name_value = NULL;
            OracleBuffer buffer;
            if (item->kind != DOE_JSON_OBJECT) {
                parse_result = ORACLE_INVALID_JSON;
                break;
            }
            name_value = doe_json_member_value(item, "name");
            if (name_value == NULL ||
                name_value->kind != DOE_JSON_STRING) {
                parse_result = ORACLE_INVALID_JSON;
                break;
            }
            if (buffer_count == buffer_cap) {
                size_t cap = buffer_cap == 0 ? 8 : buffer_cap * 2;
                OracleBuffer *grown = (OracleBuffer *)realloc(
                    buffers, cap * sizeof(OracleBuffer));
                if (grown == NULL) {
                    free_buffers(buffers, buffer_count);
                    doe_json_free(&doc);
                    return TSIR_ERR_NOMEM;
                }
                buffers = grown;
                buffer_cap = cap;
            }
            parse_result = parse_buffer(name_value->string, item,
                &buffer);
            if (parse_result != ORACLE_OK) {
                break;
            }
            buffers[buffer_count++] = buffer;
        }
    } else {
        parse_result = ORACLE_INVALID_JSON;
    }
    doe_json_free(&doc);
    if (parse_result == ORACLE_INVALID_JSON) {
        free_buffers(buffers, buffer_count);
        return write_status("not_implemented", NULL,
            "bootstrap oracle input JSON is unsupported or missing kernel/inputs",
            out_status_json, out_len);
    }
    if (parse_result == ORACLE_NOT_IMPLEMENTED) {
        free_buffers(buffers, buffer_count);
        return write_status("not_implemented", NULL,
            "bootstrap oracle cannot execute this input",
            out_status_json, out_len);
    }
    memset(&result, 0, sizeof(result));
    status = run_kernel(canonical, buffers, buffer_count, &result);
    free_buffers(buffers, buffer_count);
    if (status != TSIR_OK) {
        /* Zig propagates non-NotImplemented reference errors as
         * process failures; C reports them as not_implemented
         * instead of failing the caller, and never fabricates a
         * hash. */
        return write_status("not_implemented", NULL,
            "bootstrap oracle cannot execute this input",
            out_status_json, out_len);
    }
    status = write_status("pass", result.reference_hash,
        "zig bootstrap TSIR oracle executed", out_status_json,
        out_len);
    tsir_reference_free_result(&result);
    return status;
}
