// SPDX-License-Identifier: MIT
#include "tsir_csl_kernel_body.h"

/* Port of emit_kernel_body_linear_attention.zig (169 lines):
 * gated DeltaNet linear-attention update + readout, single-Q
 * (decode-shape) canary surface. Multi-Q prefill is NOT in this
 * file in Zig either; it is a follow-up reusing this body inside
 * an outer query-row loop, so this port returns
 * CSL_ERR_UNSUPPORTED for nothing here and simply does not offer
 * a multi-Q entry point.
 *
 * Sharding, exactly as in Zig: value_dim is sharded across pe_y,
 * so each PE owns value_dim_per_pe rows of the state matrix and
 * the matching slices of query/gate/output; K and V are broadcast.
 * output[d] reduces only over k, so no cross-PE reduction is
 * emitted.
 * Math (shared-norm form, per PE owning a d-block):
 *   alpha = 1 - exp(-A_log); beta = sigmoid(gate)
 *   delta[d, k] = alpha * (q[d] - dot(state[d, :], k_in)) * k_in[k]
 *   state[d, k] = (1 - alpha) * state[d, k] + delta[d, k]
 *   output[d] = sum_k state[d, k] * v[k] + beta[d] * q[d]
 * Only norm_mode shared is admitted, and has_dt_bias must be
 * false; per_head and time-varying dt_bias return
 * CSL_ERR_INVALID (Zig InvalidBodyContract), because the conv1d
 * to A_log decay path has not landed as a separate composition.
 * Layout-supplied CSL params (not descriptor fields):
 * value_dim_per_pe (i16) and a_log (elem type), supplied by the
 * layout via @set_tile_code / host plan. The a_log value is
 * therefore not a descriptor field, mirroring Zig, where it is
 * only a param declaration. */

CslErr tsir_csl_emit_linear_attention(const TsirCslKernelDesc *desc,
    CslBuf *out) {
    TsirCslConfig defaults;
    const TsirCslConfig *config = NULL;
    const TsirCslBinding *query = NULL;
    const TsirCslBinding *key = NULL;
    const TsirCslBinding *value = NULL;
    const TsirCslBinding *gate = NULL;
    const TsirCslBinding *linear_state = NULL;
    const TsirCslBinding *output = NULL;
    TsirCslElem elem = TSIR_CSL_F32;
    const char *p = NULL;
    const char *ty = NULL;
    CslErr err = CSL_OK;
    if (desc == NULL || out == NULL) {
        return CSL_ERR_INVALID;
    }
    err = tsir_csl_resolve_config(desc, &defaults, &config);
    if (err != CSL_OK) {
        return err;
    }
    err = tsir_csl_binding_for_role(desc, TSIR_CSL_ROLE_QUERY, &query);
    if (err != CSL_OK) {
        return err;
    }
    err = tsir_csl_binding_for_role(desc, TSIR_CSL_ROLE_KEY, &key);
    if (err != CSL_OK) {
        return err;
    }
    err = tsir_csl_binding_for_role(desc, TSIR_CSL_ROLE_VALUE, &value);
    if (err != CSL_OK) {
        return err;
    }
    err = tsir_csl_binding_for_role(desc, TSIR_CSL_ROLE_GATE, &gate);
    if (err != CSL_OK) {
        return err;
    }
    err = tsir_csl_binding_for_role(desc, TSIR_CSL_ROLE_LINEAR_STATE,
        &linear_state);
    if (err != CSL_OK) {
        return err;
    }
    err = tsir_csl_binding_for_role(desc, TSIR_CSL_ROLE_OUTPUT,
        &output);
    if (err != CSL_OK) {
        return err;
    }
    elem = output->elem;
    err = tsir_csl_require_supported_compute_elem(elem);
    if (err != CSL_OK) {
        return err;
    }
    err = tsir_csl_require_elem(query, elem);
    if (err != CSL_OK) {
        return err;
    }
    err = tsir_csl_require_elem(key, elem);
    if (err != CSL_OK) {
        return err;
    }
    err = tsir_csl_require_elem(value, elem);
    if (err != CSL_OK) {
        return err;
    }
    err = tsir_csl_require_elem(gate, elem);
    if (err != CSL_OK) {
        return err;
    }
    err = tsir_csl_require_elem(linear_state, elem);
    if (err != CSL_OK) {
        return err;
    }
    err = tsir_csl_require_elem(output, elem);
    if (err != CSL_OK) {
        return err;
    }
    if (!linear_state->read_write) {
        return CSL_ERR_INVALID;
    }
    if (desc->body.lin_key_dim == 0 || desc->body.lin_value_dim == 0) {
        return CSL_ERR_INVALID;
    }
    if (desc->body.lin_key_heads == 0 ||
        desc->body.lin_value_heads == 0) {
        return CSL_ERR_INVALID;
    }
    if (desc->body.lin_norm_mode != TSIR_CSL_NORM_SHARED) {
        return CSL_ERR_INVALID;
    }
    if (desc->body.lin_has_dt_bias) {
        return CSL_ERR_INVALID;
    }
    p = config->var_prefix;
    ty = tsir_csl_elem_name(elem);
    if (ty == NULL) {
        return CSL_ERR_INVALID;
    }
    if ((err = csl_buf_puts(out, "param memcpy_params;\n")) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_printf(out, "const key_dim: i16 = %u;\n",
        desc->body.lin_key_dim)) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_printf(out, "const value_dim: i16 = %u;\n",
        desc->body.lin_value_dim)) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_puts(out,
        "param value_dim_per_pe: i16;\n")) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_printf(out, "param a_log: %s;\n", ty)) != CSL_OK) {
        return err;
    }
    err = tsir_csl_write_sys_mod(out);
    if (err != CSL_OK) {
        return err;
    }
    err = tsir_csl_write_math_import(out);
    if (err != CSL_OK) {
        return err;
    }
    if ((err = tsir_csl_write_buffer_array(out, p, query->name,
        "value_dim_per_pe", ty)) != CSL_OK) {
        return err;
    }
    if ((err = tsir_csl_write_buffer_array(out, p, key->name,
        "key_dim", ty)) != CSL_OK) {
        return err;
    }
    if ((err = tsir_csl_write_buffer_array(out, p, value->name,
        "key_dim", ty)) != CSL_OK) {
        return err;
    }
    if ((err = tsir_csl_write_buffer_array(out, p, gate->name,
        "value_dim_per_pe", ty)) != CSL_OK) {
        return err;
    }
    if ((err = tsir_csl_write_buffer_array(out, p, linear_state->name,
        "value_dim_per_pe * key_dim", ty)) != CSL_OK) {
        return err;
    }
    if ((err = tsir_csl_write_buffer_array(out, p, output->name,
        "value_dim_per_pe", ty)) != CSL_OK) {
        return err;
    }
    if ((err = tsir_csl_write_buffer_pointer(out, p, query->name,
        ty)) != CSL_OK) {
        return err;
    }
    if ((err = tsir_csl_write_buffer_pointer(out, p, key->name,
        ty)) != CSL_OK) {
        return err;
    }
    if ((err = tsir_csl_write_buffer_pointer(out, p, value->name,
        ty)) != CSL_OK) {
        return err;
    }
    if ((err = tsir_csl_write_buffer_pointer(out, p, gate->name,
        ty)) != CSL_OK) {
        return err;
    }
    if ((err = tsir_csl_write_buffer_pointer(out, p, linear_state->name,
        ty)) != CSL_OK) {
        return err;
    }
    if ((err = tsir_csl_write_buffer_pointer(out, p, output->name,
        ty)) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_puts(out, "\n")) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_puts(out, "fn compute() void {\n")) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_printf(out,
        "    const alpha: %s = 1.0 - math.exp(-a_log);\n", ty)) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_printf(out, "    const decay: %s = 1.0 - alpha;\n",
        ty)) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_puts(out,
        "    for (@range(i16, value_dim_per_pe)) |d| {\n")) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_printf(out, "        var prev_dot: %s = 0.0;\n",
        ty)) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_puts(out,
        "        for (@range(i16, key_dim)) |k| {\n")) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_printf(out,
        "            prev_dot += %s%s[@as(u32, d) * @as(u32, key_dim) + @as(u32, k)] * %s%s[@as(u32, k)];\n",
        p, linear_state->name, p, key->name)) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_puts(out, "        }\n")) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_printf(out,
        "        const q_d: %s = %s%s[@as(u32, d)];\n", ty, p,
        query->name)) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_printf(out,
        "        const correction: %s = q_d - prev_dot;\n", ty)) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_printf(out,
        "        const scaled_correction: %s = alpha * correction;\n",
        ty)) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_puts(out,
        "        for (@range(i16, key_dim)) |k| {\n")) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_printf(out,
        "            const k_in: %s = %s%s[@as(u32, k)];\n", ty, p,
        key->name)) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_puts(out,
        "            const idx: u32 = @as(u32, d) * @as(u32, key_dim) + @as(u32, k);\n")) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_printf(out,
        "            const prev_state: %s = %s%s[idx];\n", ty, p,
        linear_state->name)) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_printf(out,
        "            const delta: %s = scaled_correction * k_in;\n",
        ty)) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_printf(out,
        "            %s%s[idx] = decay * prev_state + delta;\n", p,
        linear_state->name)) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_puts(out, "        }\n")) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_puts(out, "    }\n")) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_puts(out,
        "    for (@range(i16, value_dim_per_pe)) |d| {\n")) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_printf(out, "        var acc: %s = 0.0;\n",
        ty)) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_puts(out,
        "        for (@range(i16, key_dim)) |k| {\n")) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_printf(out,
        "            acc += %s%s[@as(u32, d) * @as(u32, key_dim) + @as(u32, k)] * %s%s[@as(u32, k)];\n",
        p, linear_state->name, p, value->name)) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_puts(out, "        }\n")) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_printf(out,
        "        const g: %s = %s%s[@as(u32, d)];\n", ty, p,
        gate->name)) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_printf(out,
        "        const sigmoid_g: %s = 1.0 / (1.0 + math.exp(-g));\n",
        ty)) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_printf(out,
        "        %s%s[@as(u32, d)] = acc + sigmoid_g * %s%s[@as(u32, d)];\n",
        p, output->name, p, query->name)) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_puts(out, "    }\n")) != CSL_OK) {
        return err;
    }
    return tsir_csl_write_compute_tail(out, p,
        (const char *[]){query->name, key->name, value->name,
            gate->name, linear_state->name, output->name}, 6);
}
