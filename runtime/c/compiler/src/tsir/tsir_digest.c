/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "tsir_digest.h"
#include "tsir_format.h"

#include "tsir_sha256.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- primitive emitters, all preserving the first error ---- */

static void emit_key(TsirBuffer *buf, const char *key) {
    tsir_buf_putc(buf, '"');
    tsir_buf_puts(buf, key);
    tsir_buf_puts(buf, "\":");
}

static void emit_string(TsirBuffer *buf, const char *text) {
    size_t i = 0;
    char tmp[8];
    if (text == NULL) {
        if (buf->err == TSIR_OK) {
            buf->err = TSIR_ERR_INVALID;
        }
        return;
    }
    tsir_buf_putc(buf, '"');
    for (i = 0; text[i] != '\0'; i++) {
        unsigned char c = (unsigned char)text[i];
        switch (c) {
            case '"':
                tsir_buf_puts(buf, "\\\"");
                break;
            case '\\':
                tsir_buf_puts(buf, "\\\\");
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
                if (c < 0x20) {
                    snprintf(tmp, sizeof(tmp), "\\u%04x", c);
                    tsir_buf_puts(buf, tmp);
                } else {
                    tsir_buf_putc(buf, (char)c);
                }
                break;
        }
    }
    tsir_buf_putc(buf, '"');
}

static void emit_u32(TsirBuffer *buf, uint32_t value) {
    char tmp[16];
    snprintf(tmp, sizeof(tmp), "%u", value);
    tsir_buf_puts(buf, tmp);
}

static void emit_u64(TsirBuffer *buf, uint64_t value) {
    char tmp[24];
    snprintf(tmp, sizeof(tmp), "%llu", (unsigned long long)value);
    tsir_buf_puts(buf, tmp);
}

static void emit_i32(TsirBuffer *buf, int32_t value) {
    char tmp[16];
    snprintf(tmp, sizeof(tmp), "%d", (int)value);
    tsir_buf_puts(buf, tmp);
}

static void emit_bool(TsirBuffer *buf, bool value) {
    tsir_buf_puts(buf, value ? "true" : "false");
}

static void emit_hex_digest(TsirBuffer *buf, const uint8_t digest[32]) {
    static const char hex[] = "0123456789abcdef";
    size_t i = 0;
    tsir_buf_putc(buf, '"');
    for (i = 0; i < 32; i++) {
        tsir_buf_putc(buf, hex[(digest[i] >> 4) & 0x0f]);
        tsir_buf_putc(buf, hex[digest[i] & 0x0f]);
    }
    tsir_buf_putc(buf, '"');
}

static void emit_opt_u32(TsirBuffer *buf, TsirOptU32 value) {
    if (value.has) {
        emit_u32(buf, value.value);
    } else {
        tsir_buf_puts(buf, "null");
    }
}

static void emit_opt_u64(TsirBuffer *buf, TsirOptU64 value) {
    if (value.has) {
        emit_u64(buf, value.value);
    } else {
        tsir_buf_puts(buf, "null");
    }
}

static void emit_opt_f64(TsirBuffer *buf, bool has, double value) {
    if (has) {
        tsir_format_f64(buf, value);
    } else {
        tsir_buf_puts(buf, "null");
    }
}

static void emit_enum(TsirBuffer *buf, const char *name) {
    if (name == NULL) {
        if (buf->err == TSIR_OK) {
            buf->err = TSIR_ERR_INVALID;
        }
        return;
    }
    emit_string(buf, name);
}

/* ---- composite emitters in Zig field order ---- */

static void emit_exactness(TsirBuffer *buf, const TsirExactness *value) {
    size_t i = 0;
    tsir_buf_putc(buf, '{');
    emit_key(buf, "algorithmExactInvariants");
    tsir_buf_putc(buf, '[');
    for (i = 0; i < value->invariant_count; i++) {
        if (i > 0) {
            tsir_buf_putc(buf, ',');
        }
        emit_enum(buf, tsir_invariant_name(value->invariants[i]));
    }
    tsir_buf_putc(buf, ']');
    tsir_buf_putc(buf, ',');
    emit_key(buf, "class");
    emit_enum(buf, tsir_exactness_class_name(value->class));
    tsir_buf_putc(buf, ',');
    emit_key(buf, "toleranceEpsilon");
    tsir_format_f64(buf, value->tolerance_epsilon);
    tsir_buf_putc(buf, ',');
    emit_key(buf, "toleranceMetric");
    emit_string(buf, value->tolerance_metric);
    tsir_buf_putc(buf, '}');
}

static void emit_rejection(TsirBuffer *buf, const TsirRejectionEntry *value) {
    tsir_buf_putc(buf, '{');
    emit_key(buf, "detail");
    emit_string(buf, value->detail);
    tsir_buf_putc(buf, ',');
    emit_key(buf, "nodePath");
    emit_string(buf, value->node_path);
    tsir_buf_putc(buf, ',');
    emit_key(buf, "reason");
    emit_enum(buf, tsir_rejection_reason_name(value->reason));
    tsir_buf_putc(buf, '}');
}

static void emit_axis(TsirBuffer *buf, const TsirIterationAxis *value) {
    tsir_buf_putc(buf, '{');
    emit_key(buf, "lowerBound");
    emit_string(buf, value->lower_bound);
    tsir_buf_putc(buf, ',');
    emit_key(buf, "name");
    emit_string(buf, value->name);
    tsir_buf_putc(buf, ',');
    emit_key(buf, "step");
    emit_string(buf, value->step);
    tsir_buf_putc(buf, ',');
    emit_key(buf, "upperBound");
    emit_string(buf, value->upper_bound);
    tsir_buf_putc(buf, '}');
}

static void emit_binding(TsirBuffer *buf, const TsirBufferBinding *value) {
    size_t i = 0;
    tsir_buf_putc(buf, '{');
    emit_key(buf, "binding");
    emit_u32(buf, value->binding);
    tsir_buf_putc(buf, ',');
    emit_key(buf, "elem");
    emit_enum(buf, tsir_scalar_name(value->elem));
    tsir_buf_putc(buf, ',');
    emit_key(buf, "group");
    emit_u32(buf, value->group);
    tsir_buf_putc(buf, ',');
    emit_key(buf, "logicalShape");
    tsir_buf_putc(buf, '[');
    for (i = 0; i < value->logical_shape_len; i++) {
        if (i > 0) {
            tsir_buf_putc(buf, ',');
        }
        emit_u64(buf, value->logical_shape[i]);
    }
    tsir_buf_putc(buf, ']');
    tsir_buf_putc(buf, ',');
    emit_key(buf, "name");
    emit_string(buf, value->name);
    tsir_buf_putc(buf, ',');
    emit_key(buf, "readWrite");
    emit_bool(buf, value->read_write);
    tsir_buf_putc(buf, '}');
}

static void emit_contract(TsirBuffer *buf, const TsirNumericalContract *value) {
    tsir_buf_putc(buf, '{');
    emit_key(buf, "accumulation");
    emit_enum(buf, tsir_scalar_name(value->accumulation));
    tsir_buf_putc(buf, ',');
    emit_key(buf, "associativity");
    emit_enum(buf, tsir_associativity_name(value->associativity));
    tsir_buf_putc(buf, ',');
    emit_key(buf, "nanInf");
    emit_enum(buf, tsir_nan_inf_name(value->nan_inf));
    tsir_buf_putc(buf, '}');
}

static void emit_reduction_region(TsirBuffer *buf,
    const TsirReductionRegion *value) {
    tsir_buf_putc(buf, '{');
    emit_key(buf, "axis");
    emit_u32(buf, value->axis);
    tsir_buf_putc(buf, ',');
    emit_key(buf, "contract");
    emit_contract(buf, &value->contract);
    tsir_buf_putc(buf, ',');
    emit_key(buf, "op");
    emit_enum(buf, tsir_reduction_op_name(value->op));
    tsir_buf_putc(buf, ',');
    emit_key(buf, "targetBinding");
    emit_u32(buf, value->target_binding);
    tsir_buf_putc(buf, '}');
}

static void emit_collective_semantic(TsirBuffer *buf,
    const TsirCollectiveSemanticNode *value) {
    tsir_buf_putc(buf, '{');
    emit_key(buf, "axis");
    emit_i32(buf, value->axis);
    tsir_buf_putc(buf, ',');
    emit_key(buf, "dtype");
    emit_enum(buf, tsir_scalar_name(value->dtype));
    tsir_buf_putc(buf, ',');
    emit_key(buf, "exactness");
    emit_exactness(buf, &value->exactness);
    tsir_buf_putc(buf, ',');
    emit_key(buf, "kind");
    emit_enum(buf, tsir_collective_kind_name(value->kind));
    tsir_buf_putc(buf, '}');
}

static void emit_rms_norm_epsilon(TsirBuffer *buf,
    const TsirRmsNormEpsilon *value) {
    tsir_buf_putc(buf, '{');
    emit_key(buf, "bindingIndex");
    emit_opt_u32(buf, value->binding_index);
    tsir_buf_putc(buf, ',');
    emit_key(buf, "byteOffset");
    emit_opt_u32(buf, value->byte_offset);
    tsir_buf_putc(buf, ',');
    emit_key(buf, "literalF32");
    emit_opt_f64(buf, value->has_literal_f64, value->literal_f64);
    tsir_buf_putc(buf, ',');
    emit_key(buf, "path");
    emit_string(buf, value->path);
    tsir_buf_putc(buf, ',');
    emit_key(buf, "source");
    emit_enum(buf, tsir_epsilon_source_name(value->source));
    tsir_buf_putc(buf, '}');
}

static void emit_rms_norm_body(TsirBuffer *buf, const TsirRmsNormBody *value) {
    tsir_buf_putc(buf, '{');
    emit_key(buf, "epsilon");
    emit_rms_norm_epsilon(buf, &value->epsilon);
    tsir_buf_putc(buf, ',');
    emit_key(buf, "formula");
    emit_enum(buf, tsir_rms_norm_formula_name(value->formula));
    tsir_buf_putc(buf, ',');
    emit_key(buf, "hiddenExtentAxis");
    emit_u32(buf, value->hidden_extent_axis);
    tsir_buf_putc(buf, ',');
    emit_key(buf, "reductionTarget");
    emit_enum(buf, tsir_rms_norm_target_name(value->reduction_target));
    tsir_buf_putc(buf, '}');
}

static void emit_body_binding(TsirBuffer *buf,
    const TsirSemanticBodyBinding *value) {
    tsir_buf_putc(buf, '{');
    emit_key(buf, "bindingIndex");
    emit_u32(buf, value->binding_index);
    tsir_buf_putc(buf, ',');
    emit_key(buf, "role");
    emit_enum(buf, tsir_binding_role_name(value->role));
    tsir_buf_putc(buf, '}');
}

static void emit_body_axis(TsirBuffer *buf, const TsirSemanticBodyAxis *value) {
    tsir_buf_putc(buf, '{');
    emit_key(buf, "axisIndex");
    emit_u32(buf, value->axis_index);
    tsir_buf_putc(buf, ',');
    emit_key(buf, "role");
    emit_enum(buf, tsir_axis_role_name(value->role));
    tsir_buf_putc(buf, '}');
}

static void emit_body(TsirBuffer *buf, const TsirSemanticBody *value) {
    size_t i = 0;
    tsir_buf_putc(buf, '{');
    emit_key(buf, "axisRoles");
    tsir_buf_putc(buf, '[');
    for (i = 0; i < value->axis_role_count; i++) {
        if (i > 0) {
            tsir_buf_putc(buf, ',');
        }
        emit_body_axis(buf, &value->axis_roles[i]);
    }
    tsir_buf_putc(buf, ']');
    tsir_buf_putc(buf, ',');
    emit_key(buf, "bindingRoles");
    tsir_buf_putc(buf, '[');
    for (i = 0; i < value->binding_role_count; i++) {
        if (i > 0) {
            tsir_buf_putc(buf, ',');
        }
        emit_body_binding(buf, &value->binding_roles[i]);
    }
    tsir_buf_putc(buf, ']');
    tsir_buf_putc(buf, ',');
    emit_key(buf, "op");
    emit_enum(buf, tsir_body_op_name(value->op));
    /* Zig digest emits rmsNorm only, even when other body payloads
     * are present. Preserve that restriction verbatim. */
    if (value->has_rms_norm) {
        tsir_buf_putc(buf, ',');
        emit_key(buf, "rmsNorm");
        emit_rms_norm_body(buf, &value->rms_norm);
    }
    tsir_buf_putc(buf, '}');
}

static void emit_semantic_function(TsirBuffer *buf,
    const TsirSemanticFunction *value) {
    size_t i = 0;
    tsir_buf_putc(buf, '{');
    emit_key(buf, "axes");
    tsir_buf_putc(buf, '[');
    for (i = 0; i < value->axis_count; i++) {
        if (i > 0) {
            tsir_buf_putc(buf, ',');
        }
        emit_axis(buf, &value->axes[i]);
    }
    tsir_buf_putc(buf, ']');
    tsir_buf_putc(buf, ',');
    emit_key(buf, "bindings");
    tsir_buf_putc(buf, '[');
    for (i = 0; i < value->binding_count; i++) {
        if (i > 0) {
            tsir_buf_putc(buf, ',');
        }
        emit_binding(buf, &value->bindings[i]);
    }
    tsir_buf_putc(buf, ']');
    tsir_buf_putc(buf, ',');
    emit_key(buf, "body");
    emit_body(buf, &value->body);
    tsir_buf_putc(buf, ',');
    emit_key(buf, "collectives");
    tsir_buf_putc(buf, '[');
    for (i = 0; i < value->collective_count; i++) {
        if (i > 0) {
            tsir_buf_putc(buf, ',');
        }
        emit_collective_semantic(buf, &value->collectives[i]);
    }
    tsir_buf_putc(buf, ']');
    tsir_buf_putc(buf, ',');
    emit_key(buf, "familyHint");
    emit_enum(buf, tsir_family_hint_name(value->family_hint));
    tsir_buf_putc(buf, ',');
    emit_key(buf, "name");
    emit_string(buf, value->name);
    tsir_buf_putc(buf, ',');
    emit_key(buf, "reductions");
    tsir_buf_putc(buf, '[');
    for (i = 0; i < value->reduction_count; i++) {
        if (i > 0) {
            tsir_buf_putc(buf, ',');
        }
        emit_reduction_region(buf, &value->reductions[i]);
    }
    tsir_buf_putc(buf, ']');
    tsir_buf_putc(buf, ',');
    emit_key(buf, "sourceDigest");
    emit_hex_digest(buf, value->source_digest);
    tsir_buf_putc(buf, '}');
}

static void emit_semantic(TsirBuffer *buf, const TsirSemantic *value) {
    size_t i = 0;
    tsir_buf_putc(buf, '{');
    emit_key(buf, "contractVersion");
    emit_u32(buf, value->contract_version);
    tsir_buf_putc(buf, ',');
    emit_key(buf, "frontendVersion");
    emit_string(buf, value->frontend_version);
    tsir_buf_putc(buf, ',');
    emit_key(buf, "functions");
    tsir_buf_putc(buf, '[');
    for (i = 0; i < value->function_count; i++) {
        if (i > 0) {
            tsir_buf_putc(buf, ',');
        }
        emit_semantic_function(buf, &value->functions[i]);
    }
    tsir_buf_putc(buf, ']');
    tsir_buf_putc(buf, ',');
    emit_key(buf, "rejections");
    tsir_buf_putc(buf, '[');
    for (i = 0; i < value->rejection_count; i++) {
        if (i > 0) {
            tsir_buf_putc(buf, ',');
        }
        emit_rejection(buf, &value->rejections[i]);
    }
    tsir_buf_putc(buf, ']');
    tsir_buf_putc(buf, '}');
}

static void emit_collective_realization(TsirBuffer *buf,
    const TsirCollectiveRealizationNode *value) {
    tsir_buf_putc(buf, '{');
    emit_key(buf, "fabricColor");
    emit_opt_u32(buf, value->fabric_color);
    tsir_buf_putc(buf, ',');
    emit_key(buf, "groupSize");
    emit_u32(buf, value->group_size);
    tsir_buf_putc(buf, ',');
    emit_key(buf, "semanticIndex");
    emit_u32(buf, value->semantic_index);
    tsir_buf_putc(buf, ',');
    emit_key(buf, "treeShape");
    emit_enum(buf, tsir_tree_shape_name(value->tree_shape));
    tsir_buf_putc(buf, '}');
}

static void emit_reduction_realization(TsirBuffer *buf,
    const TsirReductionRealizationNode *value) {
    tsir_buf_putc(buf, '{');
    emit_key(buf, "semanticIndex");
    emit_u32(buf, value->semantic_index);
    tsir_buf_putc(buf, ',');
    emit_key(buf, "treeShape");
    emit_enum(buf, tsir_tree_shape_name(value->tree_shape));
    tsir_buf_putc(buf, '}');
}

static void emit_residency(TsirBuffer *buf, const TsirResidencyDecision *value) {
    tsir_buf_putc(buf, '{');
    emit_key(buf, "axis");
    emit_opt_u32(buf, value->axis);
    tsir_buf_putc(buf, ',');
    emit_key(buf, "bindingIndex");
    emit_u32(buf, value->binding_index);
    tsir_buf_putc(buf, ',');
    emit_key(buf, "chunkBytes");
    emit_opt_u64(buf, value->chunk_bytes);
    tsir_buf_putc(buf, ',');
    emit_key(buf, "class");
    emit_enum(buf, tsir_residency_class_name(value->class));
    tsir_buf_putc(buf, ',');
    emit_key(buf, "fabricColor");
    emit_opt_u32(buf, value->fabric_color);
    tsir_buf_putc(buf, ',');
    emit_key(buf, "shards");
    emit_opt_u32(buf, value->shards);
    tsir_buf_putc(buf, '}');
}

static void emit_realization_function(TsirBuffer *buf,
    const TsirRealizationFunction *value) {
    size_t i = 0;
    tsir_buf_putc(buf, '{');
    emit_key(buf, "collectives");
    tsir_buf_putc(buf, '[');
    for (i = 0; i < value->collective_count; i++) {
        if (i > 0) {
            tsir_buf_putc(buf, ',');
        }
        emit_collective_realization(buf, &value->collectives[i]);
    }
    tsir_buf_putc(buf, ']');
    tsir_buf_putc(buf, ',');
    emit_key(buf, "emitterParamsJson");
    emit_string(buf, value->emitter_params_json);
    tsir_buf_putc(buf, ',');
    emit_key(buf, "peGrid");
    tsir_buf_putc(buf, '{');
    emit_key(buf, "height");
    emit_u32(buf, value->pe_grid.height);
    tsir_buf_putc(buf, ',');
    emit_key(buf, "width");
    emit_u32(buf, value->pe_grid.width);
    tsir_buf_putc(buf, '}');
    tsir_buf_putc(buf, ',');
    emit_key(buf, "reductions");
    tsir_buf_putc(buf, '[');
    for (i = 0; i < value->reduction_count; i++) {
        if (i > 0) {
            tsir_buf_putc(buf, ',');
        }
        emit_reduction_realization(buf, &value->reductions[i]);
    }
    tsir_buf_putc(buf, ']');
    tsir_buf_putc(buf, ',');
    emit_key(buf, "residency");
    tsir_buf_putc(buf, '[');
    for (i = 0; i < value->residency_count; i++) {
        if (i > 0) {
            tsir_buf_putc(buf, ',');
        }
        emit_residency(buf, &value->residency[i]);
    }
    tsir_buf_putc(buf, ']');
    tsir_buf_putc(buf, ',');
    emit_key(buf, "semanticIndex");
    emit_u32(buf, value->semantic_index);
    tsir_buf_putc(buf, ',');
    emit_key(buf, "targetDescriptorHash");
    emit_hex_digest(buf, value->target_descriptor_hash);
    tsir_buf_putc(buf, ',');
    emit_key(buf, "tiles");
    tsir_buf_putc(buf, '{');
    emit_key(buf, "perAxis");
    tsir_buf_putc(buf, '[');
    for (i = 0; i < value->tiles.per_axis_count; i++) {
        if (i > 0) {
            tsir_buf_putc(buf, ',');
        }
        emit_u32(buf, value->tiles.per_axis[i]);
    }
    tsir_buf_putc(buf, ']');
    tsir_buf_putc(buf, '}');
    tsir_buf_putc(buf, '}');
}

static void emit_realization(TsirBuffer *buf, const TsirRealization *value) {
    size_t i = 0;
    tsir_buf_putc(buf, '{');
    emit_key(buf, "contractVersion");
    emit_u32(buf, value->contract_version);
    tsir_buf_putc(buf, ',');
    emit_key(buf, "emitterDigest");
    emit_hex_digest(buf, value->emitter_digest);
    tsir_buf_putc(buf, ',');
    emit_key(buf, "functions");
    tsir_buf_putc(buf, '[');
    for (i = 0; i < value->function_count; i++) {
        if (i > 0) {
            tsir_buf_putc(buf, ',');
        }
        emit_realization_function(buf, &value->functions[i]);
    }
    tsir_buf_putc(buf, ']');
    tsir_buf_putc(buf, ',');
    emit_key(buf, "rejections");
    tsir_buf_putc(buf, '[');
    for (i = 0; i < value->rejection_count; i++) {
        if (i > 0) {
            tsir_buf_putc(buf, ',');
        }
        emit_rejection(buf, &value->rejections[i]);
    }
    tsir_buf_putc(buf, ']');
    tsir_buf_putc(buf, '}');
}

static void emit_manifest_entry(TsirBuffer *buf,
    const TsirManifestLoweringEntry *value) {
    size_t i = 0;
    tsir_buf_putc(buf, '{');
    emit_key(buf, "backend");
    emit_string(buf, value->backend);
    tsir_buf_putc(buf, ',');
    emit_key(buf, "compilerVersion");
    emit_string(buf, value->compiler_version);
    tsir_buf_putc(buf, ',');
    emit_key(buf, "emitterDigest");
    emit_hex_digest(buf, value->emitter_digest);
    tsir_buf_putc(buf, ',');
    emit_key(buf, "exactness");
    emit_exactness(buf, &value->exactness);
    tsir_buf_putc(buf, ',');
    emit_key(buf, "frontendVersion");
    emit_string(buf, value->frontend_version);
    tsir_buf_putc(buf, ',');
    emit_key(buf, "kernelRef");
    emit_string(buf, value->kernel_ref);
    tsir_buf_putc(buf, ',');
    emit_key(buf, "rejectionReasons");
    tsir_buf_putc(buf, '[');
    for (i = 0; i < value->rejection_reason_count; i++) {
        if (i > 0) {
            tsir_buf_putc(buf, ',');
        }
        emit_enum(buf, tsir_rejection_reason_name(value->rejection_reasons[i]));
    }
    tsir_buf_putc(buf, ']');
    tsir_buf_putc(buf, ',');
    emit_key(buf, "targetDescriptorCorrectnessHash");
    emit_hex_digest(buf, value->target_descriptor_correctness_hash);
    tsir_buf_putc(buf, ',');
    emit_key(buf, "tsirRealizationDigest");
    emit_hex_digest(buf, value->tsir_realization_digest);
    tsir_buf_putc(buf, ',');
    emit_key(buf, "tsirSemanticDigest");
    emit_hex_digest(buf, value->tsir_semantic_digest);
    tsir_buf_putc(buf, '}');
}

static TsirStatus finish_text(TsirBuffer *buf, char **out_text,
    size_t *out_len) {
    if (buf->err != TSIR_OK) {
        TsirStatus err = buf->err;
        tsir_buf_free(buf);
        return err;
    }
    if (buf->data == NULL) {
        *out_text = (char *)calloc(1, 1);
        if (*out_text == NULL) {
            return TSIR_ERR_NOMEM;
        }
        *out_len = 0;
        return TSIR_OK;
    }
    *out_text = buf->data;
    *out_len = buf->len;
    buf->data = NULL;
    buf->len = 0;
    buf->cap = 0;
    return TSIR_OK;
}

static bool inputs_valid_semantic(const TsirSemantic *semantic) {
    size_t i = 0;
    if (semantic == NULL || semantic->frontend_version == NULL) {
        return false;
    }
    if (semantic->function_count > 0 && semantic->functions == NULL) {
        return false;
    }
    if (semantic->rejection_count > 0 && semantic->rejections == NULL) {
        return false;
    }
    for (i = 0; i < semantic->function_count; i++) {
        const TsirSemanticFunction *f = &semantic->functions[i];
        if (f->name == NULL) {
            return false;
        }
        if (f->axis_count > 0 && f->axes == NULL) {
            return false;
        }
        if (f->binding_count > 0 && f->bindings == NULL) {
            return false;
        }
        if (f->reduction_count > 0 && f->reductions == NULL) {
            return false;
        }
        if (f->collective_count > 0 && f->collectives == NULL) {
            return false;
        }
        if (f->body.binding_role_count > 0 &&
            f->body.binding_roles == NULL) {
            return false;
        }
        if (f->body.axis_role_count > 0 && f->body.axis_roles == NULL) {
            return false;
        }
        if (f->body.has_rms_norm &&
            f->body.rms_norm.epsilon.path == NULL) {
            return false;
        }
    }
    return true;
}

TsirStatus tsir_canonicalize_semantic(const TsirSemantic *semantic,
    char **out_text, size_t *out_len) {
    TsirBuffer buf;
    if (out_text == NULL || out_len == NULL || !inputs_valid_semantic(semantic)) {
        return TSIR_ERR_INVALID;
    }
    tsir_buf_init(&buf);
    emit_semantic(&buf, semantic);
    return finish_text(&buf, out_text, out_len);
}

TsirStatus tsir_canonicalize_realization(const TsirRealization *realization,
    char **out_text, size_t *out_len) {
    TsirBuffer buf;
    size_t i = 0;
    if (out_text == NULL || out_len == NULL || realization == NULL) {
        return TSIR_ERR_INVALID;
    }
    if (realization->function_count > 0 && realization->functions == NULL) {
        return TSIR_ERR_INVALID;
    }
    if (realization->rejection_count > 0 && realization->rejections == NULL) {
        return TSIR_ERR_INVALID;
    }
    for (i = 0; i < realization->function_count; i++) {
        const TsirRealizationFunction *f = &realization->functions[i];
        if (f->emitter_params_json == NULL) {
            return TSIR_ERR_INVALID;
        }
        if (f->tiles.per_axis_count > 0 && f->tiles.per_axis == NULL) {
            return TSIR_ERR_INVALID;
        }
        if (f->residency_count > 0 && f->residency == NULL) {
            return TSIR_ERR_INVALID;
        }
        if (f->collective_count > 0 && f->collectives == NULL) {
            return TSIR_ERR_INVALID;
        }
        if (f->reduction_count > 0 && f->reductions == NULL) {
            return TSIR_ERR_INVALID;
        }
    }
    tsir_buf_init(&buf);
    emit_realization(&buf, realization);
    return finish_text(&buf, out_text, out_len);
}

TsirStatus tsir_canonicalize_manifest_entry(
    const TsirManifestLoweringEntry *entry, char **out_text,
    size_t *out_len) {
    TsirBuffer buf;
    if (out_text == NULL || out_len == NULL || entry == NULL ||
        entry->kernel_ref == NULL || entry->backend == NULL ||
        entry->frontend_version == NULL || entry->compiler_version == NULL ||
        entry->exactness.tolerance_metric == NULL) {
        return TSIR_ERR_INVALID;
    }
    if (entry->rejection_reason_count > 0 &&
        entry->rejection_reasons == NULL) {
        return TSIR_ERR_INVALID;
    }
    tsir_buf_init(&buf);
    emit_manifest_entry(&buf, entry);
    return finish_text(&buf, out_text, out_len);
}

TsirStatus tsir_manifest_entry_digest(
    const TsirManifestLoweringEntry *entry, uint8_t out[32]) {
    char *text = NULL;
    size_t len = 0;
    TsirStatus status = TSIR_OK;
    if (out == NULL) {
        return TSIR_ERR_INVALID;
    }
    status = tsir_canonicalize_manifest_entry(entry, &text, &len);
    if (status != TSIR_OK) {
        return status;
    }
    tsir_sha256((const uint8_t *)text, len, out);
    free(text);
    return TSIR_OK;
}

TsirStatus tsir_digests_compute(const TsirSemantic *semantic,
    const TsirRealization *realization, const uint8_t emitter_digest[32],
    TsirDigests *out) {
    char *semantic_text = NULL;
    char *realization_text = NULL;
    size_t semantic_len = 0;
    size_t realization_len = 0;
    TsirStatus status = TSIR_OK;
    if (out == NULL || emitter_digest == NULL) {
        return TSIR_ERR_INVALID;
    }
    status = tsir_canonicalize_semantic(semantic, &semantic_text,
        &semantic_len);
    if (status != TSIR_OK) {
        return status;
    }
    status = tsir_canonicalize_realization(realization, &realization_text,
        &realization_len);
    if (status != TSIR_OK) {
        free(semantic_text);
        return status;
    }
    tsir_sha256((const uint8_t *)semantic_text, semantic_len, out->semantic);
    tsir_sha256((const uint8_t *)realization_text, realization_len,
        out->realization);
    memcpy(out->emitter, emitter_digest, 32);
    free(semantic_text);
    free(realization_text);
    return TSIR_OK;
}

TsirStatus tsir_digests_compute_version(const TsirSemantic *semantic,
    const TsirRealization *realization, const char *emitter_version,
    TsirDigests *out) {
    uint8_t digest[32];
    if (emitter_version == NULL) {
        return TSIR_ERR_INVALID;
    }
    tsir_sha256((const uint8_t *)emitter_version, strlen(emitter_version),
        digest);
    return tsir_digests_compute(semantic, realization, digest, out);
}
