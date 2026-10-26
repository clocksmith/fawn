/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "tsir_emit.h"

#include <stdio.h>
#include <string.h>

const TsirBackendTextSpec tsir_spec_dxil = {
    "doe.tsir.dxil_skeleton.version",
    "; tsir mechanical skeleton: DXIL module body is emitted by later lowering."
};

const TsirBackendTextSpec tsir_spec_msl = {
    "doe.tsir.msl_skeleton.version",
    "// tsir mechanical skeleton: MSL kernel body is emitted by later lowering."
};

const TsirBackendTextSpec tsir_spec_spir_v = {
    "doe.tsir.spir_v_skeleton.version",
    "; tsir mechanical skeleton: SPIR-V module body is emitted by later lowering."
};

static void opt_u32_line(TsirBuffer *buf, const char *prefix, size_t index,
    const char *field, TsirOptU32 value) {
    char tmp[256];
    snprintf(tmp, sizeof(tmp), "%s[%zu].%s = ", prefix, index, field);
    tsir_buf_puts(buf, tmp);
    if (value.has) {
        snprintf(tmp, sizeof(tmp), "%u\n", value.value);
        tsir_buf_puts(buf, tmp);
    } else {
        tsir_buf_puts(buf, "none\n");
    }
}

static void opt_u64_line(TsirBuffer *buf, const char *prefix, size_t index,
    const char *field, TsirOptU64 value) {
    char tmp[256];
    snprintf(tmp, sizeof(tmp), "%s[%zu].%s = ", prefix, index, field);
    tsir_buf_puts(buf, tmp);
    if (value.has) {
        snprintf(tmp, sizeof(tmp), "%llu\n",
            (unsigned long long)value.value);
        tsir_buf_puts(buf, tmp);
    } else {
        tsir_buf_puts(buf, "none\n");
    }
}

void tsir_emit_write_residency(TsirBuffer *buf,
    const TsirResidencyDecision *decisions, size_t count) {
    size_t i = 0;
    char tmp[256];
    snprintf(tmp, sizeof(tmp), "// residency.count = %zu\n", count);
    tsir_buf_puts(buf, tmp);
    for (i = 0; i < count; i++) {
        const TsirResidencyDecision *d = &decisions[i];
        const char *class_name = tsir_residency_class_name(d->class);
        snprintf(tmp, sizeof(tmp),
            "// residency[%zu].binding_index = %u\n", i,
            d->binding_index);
        tsir_buf_puts(buf, tmp);
        snprintf(tmp, sizeof(tmp), "// residency[%zu].class = %s\n", i,
            class_name == NULL ? "" : class_name);
        tsir_buf_puts(buf, tmp);
        opt_u32_line(buf, "// residency", i, "axis", d->axis);
        opt_u32_line(buf, "// residency", i, "shards", d->shards);
        opt_u32_line(buf, "// residency", i, "fabric_color",
            d->fabric_color);
        opt_u64_line(buf, "// residency", i, "chunk_bytes",
            d->chunk_bytes);
    }
    tsir_buf_putc(buf, '\n');
}

void tsir_emit_write_tiles(TsirBuffer *buf, const uint32_t *tiles,
    size_t count) {
    size_t i = 0;
    char tmp[128];
    snprintf(tmp, sizeof(tmp), "// tiles.count = %zu\n", count);
    tsir_buf_puts(buf, tmp);
    for (i = 0; i < count; i++) {
        snprintf(tmp, sizeof(tmp), "// tiles.per_axis[%zu] = %u\n", i,
            tiles[i]);
        tsir_buf_puts(buf, tmp);
    }
    tsir_buf_putc(buf, '\n');
}

void tsir_emit_write_collectives(TsirBuffer *buf,
    const TsirCollectiveRealizationNode *nodes, size_t count) {
    size_t i = 0;
    char tmp[256];
    snprintf(tmp, sizeof(tmp), "// collectives.count = %zu\n", count);
    tsir_buf_puts(buf, tmp);
    for (i = 0; i < count; i++) {
        const TsirCollectiveRealizationNode *n = &nodes[i];
        const char *shape = tsir_tree_shape_name(n->tree_shape);
        snprintf(tmp, sizeof(tmp),
            "// collectives[%zu].semantic_index = %u\n", i,
            n->semantic_index);
        tsir_buf_puts(buf, tmp);
        snprintf(tmp, sizeof(tmp), "// collectives[%zu].tree_shape = %s\n",
            i, shape == NULL ? "" : shape);
        tsir_buf_puts(buf, tmp);
        opt_u32_line(buf, "// collectives", i, "fabric_color",
            n->fabric_color);
        snprintf(tmp, sizeof(tmp), "// collectives[%zu].group_size = %u\n",
            i, n->group_size);
        tsir_buf_puts(buf, tmp);
    }
    tsir_buf_putc(buf, '\n');
}

void tsir_emit_write_reductions(TsirBuffer *buf,
    const TsirReductionRealizationNode *nodes, size_t count) {
    size_t i = 0;
    char tmp[256];
    snprintf(tmp, sizeof(tmp), "// reductions.count = %zu\n", count);
    tsir_buf_puts(buf, tmp);
    for (i = 0; i < count; i++) {
        const TsirReductionRealizationNode *n = &nodes[i];
        const char *shape = tsir_tree_shape_name(n->tree_shape);
        snprintf(tmp, sizeof(tmp),
            "// reductions[%zu].semantic_index = %u\n", i,
            n->semantic_index);
        tsir_buf_puts(buf, tmp);
        snprintf(tmp, sizeof(tmp), "// reductions[%zu].tree_shape = %s\n",
            i, shape == NULL ? "" : shape);
        tsir_buf_puts(buf, tmp);
    }
    tsir_buf_putc(buf, '\n');
}

void tsir_emit_write_hash(TsirBuffer *buf, const uint8_t hash[32]) {
    static const char hex[] = "0123456789abcdef";
    size_t i = 0;
    for (i = 0; i < 32; i++) {
        tsir_buf_putc(buf, hex[(hash[i] >> 4) & 0x0f]);
        tsir_buf_putc(buf, hex[hash[i] & 0x0f]);
    }
}

void tsir_emit_write_quoted(TsirBuffer *buf, const char *text) {
    size_t i = 0;
    if (text == NULL) {
        if (buf->err == TSIR_OK) {
            buf->err = TSIR_ERR_INVALID;
        }
        return;
    }
    tsir_buf_putc(buf, '"');
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

static void write_contract_header(TsirBuffer *buf,
    const TsirRealizationFunction *function, const char *target_name,
    const uint8_t target_descriptor_hash[32],
    const TsirBackendTextSpec *spec) {
    char tmp[256];
    snprintf(tmp, sizeof(tmp), "// %s = 1\n", spec->version_key);
    tsir_buf_puts(buf, tmp);
    snprintf(tmp, sizeof(tmp), "// target.name = %s\n", target_name);
    tsir_buf_puts(buf, tmp);
    tsir_buf_puts(buf, "// target.descriptor_hash = ");
    tsir_emit_write_hash(buf, target_descriptor_hash);
    tsir_buf_putc(buf, '\n');
    snprintf(tmp, sizeof(tmp), "// semantic_index = %u\n",
        function->semantic_index);
    tsir_buf_puts(buf, tmp);
    snprintf(tmp, sizeof(tmp), "// pe_grid.width = %u\n",
        function->pe_grid.width);
    tsir_buf_puts(buf, tmp);
    snprintf(tmp, sizeof(tmp), "// pe_grid.height = %u\n",
        function->pe_grid.height);
    tsir_buf_puts(buf, tmp);
    tsir_buf_puts(buf, "// emitter_params_json = ");
    tsir_emit_write_quoted(buf, function->emitter_params_json);
    tsir_buf_puts(buf, "\n\n");
}

static void write_metadata(TsirBuffer *buf,
    const TsirRealizationFunction *function) {
    tsir_emit_write_residency(buf, function->residency,
        function->residency_count);
    tsir_emit_write_tiles(buf, function->tiles.per_axis,
        function->tiles.per_axis_count);
    tsir_emit_write_collectives(buf, function->collectives,
        function->collective_count);
    tsir_emit_write_reductions(buf, function->reductions,
        function->reduction_count);
}

static TsirStatus finish_emit(TsirBuffer *buf, char **out_text,
    size_t *out_len) {
    if (buf->err != TSIR_OK) {
        TsirStatus err = buf->err;
        tsir_buf_free(buf);
        return err;
    }
    if (buf->data == NULL) {
        *out_text = NULL;
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

TsirStatus tsir_emit_skeleton_function(
    const TsirRealizationFunction *function, const char *target_name,
    const uint8_t target_descriptor_hash[32],
    const TsirBackendTextSpec *spec, char **out_text, size_t *out_len) {
    TsirBuffer buf;
    if (function == NULL || target_name == NULL ||
        target_descriptor_hash == NULL || spec == NULL ||
        spec->version_key == NULL || spec->body_comment == NULL ||
        function->emitter_params_json == NULL || out_text == NULL ||
        out_len == NULL) {
        return TSIR_ERR_INVALID;
    }
    if (memcmp(target_descriptor_hash, function->target_descriptor_hash,
        32) != 0) {
        return TSIR_ERR_INVALID;
    }
    *out_text = NULL;
    *out_len = 0;
    tsir_buf_init(&buf);
    write_contract_header(&buf, function, target_name,
        target_descriptor_hash, spec);
    write_metadata(&buf, function);
    tsir_buf_puts(&buf, spec->body_comment);
    tsir_buf_putc(&buf, '\n');
    return finish_emit(&buf, out_text, out_len);
}

TsirStatus tsir_emit_skeleton(const TsirRealization *realization,
    size_t function_index, const char *target_name,
    const uint8_t target_descriptor_hash[32],
    const TsirBackendTextSpec *spec, char **out_text, size_t *out_len) {
    if (realization == NULL || out_text == NULL || out_len == NULL) {
        return TSIR_ERR_INVALID;
    }
    if (realization->rejection_count != 0) {
        return TSIR_ERR_REJECTED;
    }
    if (function_index >= realization->function_count) {
        return TSIR_ERR_RANGE;
    }
    return tsir_emit_skeleton_function(
        &realization->functions[function_index], target_name,
        target_descriptor_hash, spec, out_text, out_len);
}

TsirStatus tsir_emit_semantic_function(
    const TsirSemanticFunction *semantic_function,
    const TsirRealizationFunction *function, const char *target_name,
    const uint8_t target_descriptor_hash[32],
    const TsirBackendTextSpec *spec, char **out_text, size_t *out_len) {
    TsirBuffer buf;
    int backend = 0;
    TsirStatus status = TSIR_OK;
    if (semantic_function == NULL || function == NULL ||
        target_name == NULL || target_descriptor_hash == NULL ||
        spec == NULL || spec->version_key == NULL ||
        function->emitter_params_json == NULL || out_text == NULL ||
        out_len == NULL) {
        return TSIR_ERR_INVALID;
    }
    *out_text = NULL;
    *out_len = 0;
    if (memcmp(target_descriptor_hash, function->target_descriptor_hash,
        32) != 0) {
        return TSIR_ERR_INVALID;
    }
    if (strcmp(spec->version_key,
        "doe.tsir.dxil_skeleton.version") == 0) {
        backend = TSIR_KERNEL_DXIL;
    } else if (strcmp(spec->version_key,
        "doe.tsir.msl_skeleton.version") == 0) {
        backend = TSIR_KERNEL_MSL;
    } else if (strcmp(spec->version_key,
        "doe.tsir.spir_v_skeleton.version") == 0) {
        backend = TSIR_KERNEL_SPIR_V;
    } else if (strcmp(spec->version_key,
        "doe.tsir.webgpu.version") == 0) {
        backend = TSIR_KERNEL_WEBGPU;
    } else {
        return TSIR_ERR_UNSUPPORTED;
    }
    tsir_buf_init(&buf);
    write_contract_header(&buf, function, target_name,
        target_descriptor_hash, spec);
    write_metadata(&buf, function);
    status = tsir_emit_kernel_body(semantic_function, backend, &buf);
    if (status != TSIR_OK) {
        tsir_buf_free(&buf);
        return status;
    }
    return finish_emit(&buf, out_text, out_len);
}

TsirStatus tsir_emit_semantic(const TsirSemantic *semantic,
    const TsirRealization *realization, size_t function_index,
    const char *target_name, const uint8_t target_descriptor_hash[32],
    const TsirBackendTextSpec *spec, char **out_text, size_t *out_len) {
    const TsirRealizationFunction *function = NULL;
    if (semantic == NULL || realization == NULL || out_text == NULL ||
        out_len == NULL) {
        return TSIR_ERR_INVALID;
    }
    if (realization->rejection_count != 0) {
        return TSIR_ERR_REJECTED;
    }
    if (function_index >= realization->function_count) {
        return TSIR_ERR_RANGE;
    }
    function = &realization->functions[function_index];
    if (function->semantic_index >= semantic->function_count) {
        return TSIR_ERR_RANGE;
    }
    return tsir_emit_semantic_function(
        &semantic->functions[function->semantic_index], function,
        target_name, target_descriptor_hash, spec, out_text, out_len);
}

const char *tsir_csl_elem_name(TsirScalarKind elem) {
    return tsir_scalar_name(elem);
}

const char *tsir_wgsl_elem_name(TsirScalarKind elem) {
    switch (elem) {
        case TSIR_SCALAR_F32:
            return "f32";
        case TSIR_SCALAR_F16:
            return "f16";
        case TSIR_SCALAR_BF16:
            /* WGSL has no bf16; manifests are rejected at the
             * require step, but the spelling routes to f16. */
            return "f16";
        case TSIR_SCALAR_I32:
            return "i32";
        case TSIR_SCALAR_U32:
            return "u32";
        default:
            return NULL;
    }
}

bool tsir_is_supported_compute_elem(TsirScalarKind elem) {
    return elem == TSIR_SCALAR_F32 || elem == TSIR_SCALAR_F16;
}

TsirStatus tsir_require_supported_compute_elem(TsirScalarKind elem) {
    if (!tsir_is_supported_compute_elem(elem)) {
        return TSIR_ERR_UNSUPPORTED;
    }
    return TSIR_OK;
}

void tsir_write_wgsl_f16_enable(TsirBuffer *buf, TsirScalarKind elem) {
    if (elem == TSIR_SCALAR_F16) {
        tsir_buf_puts(buf, "enable f16;\n\n");
    }
}

void tsir_emit_default_config(TsirEmitConfig *config) {
    if (config == NULL) {
        return;
    }
    memset(config, 0, sizeof(*config));
    config->var_prefix = "tsir_";
    config->gemma_one_plus_weight_offset = false;
    config->kv_cache_pe_strategy = TSIR_KV_FULL_PER_PE;
    config->attention_pe_strategy = TSIR_ATTN_FULL_PER_PE;
    config->attention_pe_id_source = TSIR_ATTN_ID_TILE_PARAM;
}

TsirStatus tsir_binding_for_role(const TsirSemanticFunction *func,
    TsirSemanticBindingRole role, const TsirBufferBinding **out) {
    size_t i = 0;
    if (func == NULL || out == NULL) {
        return TSIR_ERR_INVALID;
    }
    for (i = 0; i < func->body.binding_role_count; i++) {
        if (func->body.binding_roles[i].role == role) {
            return tsir_binding_for_index(func,
                func->body.binding_roles[i].binding_index, out);
        }
    }
    return TSIR_ERR_UNSUPPORTED;
}

TsirStatus tsir_binding_for_index(const TsirSemanticFunction *func,
    uint32_t binding_index, const TsirBufferBinding **out) {
    if (func == NULL || out == NULL) {
        return TSIR_ERR_INVALID;
    }
    if (binding_index >= func->binding_count) {
        return TSIR_ERR_INVALID;
    }
    *out = &func->bindings[binding_index];
    return TSIR_OK;
}

TsirStatus tsir_require_elem(const TsirBufferBinding *binding,
    TsirScalarKind elem) {
    if (binding == NULL) {
        return TSIR_ERR_INVALID;
    }
    if (binding->elem != elem) {
        return TSIR_ERR_UNSUPPORTED;
    }
    return TSIR_OK;
}

void tsir_write_csl_sqrt_nr(TsirBuffer *buf, TsirScalarKind elem) {
    const char *ty = tsir_csl_elem_name(elem);
    char tmp[256];
    if (ty == NULL) {
        if (buf->err == TSIR_OK) {
            buf->err = TSIR_ERR_INVALID;
        }
        return;
    }
    if (elem == TSIR_SCALAR_F32) {
        tsir_buf_puts(buf, "fn sqrt_nr(x: f32) f32 {\n");
        tsir_buf_puts(buf, "    const y0: f32 = math.sqrt(x);\n");
        tsir_buf_puts(buf, "    return 0.5 * (y0 + x / y0);\n");
        tsir_buf_puts(buf, "}\n\n");
        return;
    }
    snprintf(tmp, sizeof(tmp), "fn sqrt_nr(x: %s) %s {\n", ty, ty);
    tsir_buf_puts(buf, tmp);
    tsir_buf_puts(buf, "    const x32: f32 = @as(f32, x);\n");
    tsir_buf_puts(buf, "    const y0: f32 = math.sqrt(x32);\n");
    tsir_buf_puts(buf, "    const refined: f32 = 0.5 * (y0 + x32 / y0);\n");
    snprintf(tmp, sizeof(tmp), "    return @as(%s, refined);\n", ty);
    tsir_buf_puts(buf, tmp);
    tsir_buf_puts(buf, "}\n\n");
}

void tsir_write_csl_buffer_array(TsirBuffer *buf, const char *prefix,
    const char *name, const char *extent, const char *elem_type) {
    char tmp[512];
    snprintf(tmp, sizeof(tmp),
        "var %s%s: [%s]%s = @zeros([%s]%s);\n", prefix, name, extent,
        elem_type, extent, elem_type);
    tsir_buf_puts(buf, tmp);
}

void tsir_write_csl_buffer_pointer(TsirBuffer *buf, const char *prefix,
    const char *name, const char *elem_type) {
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "var %s%s_ptr: [*]%s = &%s%s;\n", prefix,
        name, elem_type, prefix, name);
    tsir_buf_puts(buf, tmp);
}

void tsir_write_csl_export_symbol(TsirBuffer *buf, const char *prefix,
    const char *name) {
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "    @export_symbol(%s%s_ptr, \"%s\");\n",
        prefix, name, name);
    tsir_buf_puts(buf, tmp);
}

void tsir_write_csl_f16_buffer_array(TsirBuffer *buf, const char *prefix,
    const char *name, const char *extent) {
    char tmp[512];
    snprintf(tmp, sizeof(tmp),
        "var %s%s_bits: [%s / 2]u32 = @zeros([%s / 2]u32);\n", prefix,
        name, extent, extent);
    tsir_buf_puts(buf, tmp);
}

void tsir_write_csl_f16_buffer_pointer(TsirBuffer *buf, const char *prefix,
    const char *name) {
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "var %s%s_bits_ptr: [*]u32 = &%s%s_bits;\n",
        prefix, name, prefix, name);
    tsir_buf_puts(buf, tmp);
}

void tsir_write_csl_f16_pack_loop(TsirBuffer *buf, const char *prefix,
    const char *name, const char *extent) {
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "    for (@range(i16, %s / 2)) |pair| {\n",
        extent);
    tsir_buf_puts(buf, tmp);
    tsir_buf_puts(buf, "        const base = @as(u32, pair) * 2;\n");
    snprintf(tmp, sizeof(tmp),
        "        const lo: u32 = @as(u32, @bitcast(u16, %s%s[base]));\n",
        prefix, name);
    tsir_buf_puts(buf, tmp);
    snprintf(tmp, sizeof(tmp),
        "        const hi: u32 = @as(u32, @bitcast(u16, %s%s[base + 1]));\n",
        prefix, name);
    tsir_buf_puts(buf, tmp);
    snprintf(tmp, sizeof(tmp),
        "        %s%s_bits[@as(u32, pair)] = lo | (hi << 16);\n", prefix,
        name);
    tsir_buf_puts(buf, tmp);
    tsir_buf_puts(buf, "    }\n");
}

void tsir_write_csl_f16_export_symbol(TsirBuffer *buf, const char *prefix,
    const char *name) {
    char tmp[512];
    snprintf(tmp, sizeof(tmp),
        "    @export_symbol(%s%s_bits_ptr, \"%s\");\n", prefix, name,
        name);
    tsir_buf_puts(buf, tmp);
}
