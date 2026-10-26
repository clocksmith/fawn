// SPDX-License-Identifier: MIT
#include "tsir_csl_kernel_body.h"

/* Port of emit_kernel_body_l2_normalize.zig (73 lines).
 *
 * Math, exactly as in Zig, two-pass with an f32 accumulator
 * spelling (the accumulator type is the lane elem type in the
 * emitted text, as Zig writes it):
 *   sq = sum_d input[d] * input[d]
 *   inv = 1 / sqrt(sq + eps)
 *   output[d] = input[d] * inv
 * sqrt uses the Newton-Raphson helper sqrt_nr (f32 direct, f16
 * widened through f32), shared with tsir_emit.c's
 * tsir_write_csl_sqrt_nr by interface, not copied from it.
 * Reduction is algorithm_exact over the hidden axis, declared at
 * the realization level, not in this body.
 * No layout-supplied CSL params. eps is f32 in Zig; the descriptor
 * carries it widened to double, and the literal is printed in the
 * %e form documented in tsir_csl_common.h (not Zig {e}
 * byte-identical). */

CslErr tsir_csl_emit_l2_normalize(const TsirCslKernelDesc *desc,
    CslBuf *out) {
    TsirCslConfig defaults;
    const TsirCslConfig *config = NULL;
    const TsirCslBinding *input = NULL;
    const TsirCslBinding *output = NULL;
    TsirCslElem elem = TSIR_CSL_F32;
    const char *p = NULL;
    const char *ty = NULL;
    char eps_text[64];
    CslErr err = CSL_OK;
    if (desc == NULL || out == NULL) {
        return CSL_ERR_INVALID;
    }
    err = tsir_csl_resolve_config(desc, &defaults, &config);
    if (err != CSL_OK) {
        return err;
    }
    err = tsir_csl_binding_for_role(desc, TSIR_CSL_ROLE_INPUT, &input);
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
    err = tsir_csl_require_elem(input, elem);
    if (err != CSL_OK) {
        return err;
    }
    err = tsir_csl_require_elem(output, elem);
    if (err != CSL_OK) {
        return err;
    }
    if (desc->body.l2_hidden == 0 || desc->body.l2_eps < 0) {
        return CSL_ERR_INVALID;
    }
    p = config->var_prefix;
    ty = tsir_csl_elem_name(elem);
    if (ty == NULL) {
        return CSL_ERR_INVALID;
    }
    err = tsir_csl_format_scientific(eps_text, sizeof(eps_text),
        desc->body.l2_eps);
    if (err != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_puts(out, "param memcpy_params;\n")) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_printf(out, "const hidden_size: i16 = %u;\n",
        desc->body.l2_hidden)) != CSL_OK) {
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
    if ((err = csl_buf_puts(out, "\n")) != CSL_OK) {
        return err;
    }
    err = tsir_csl_write_sqrt_nr(out, elem);
    if (err != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_printf(out, "const l2_eps: %s = %s;\n", ty,
        eps_text)) != CSL_OK) {
        return err;
    }
    if ((err = tsir_csl_write_buffer_array(out, p, input->name,
        "hidden_size", ty)) != CSL_OK) {
        return err;
    }
    if ((err = tsir_csl_write_buffer_array(out, p, output->name,
        "hidden_size", ty)) != CSL_OK) {
        return err;
    }
    if ((err = tsir_csl_write_buffer_pointer(out, p, input->name,
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
    if ((err = csl_buf_printf(out, "    var sq: %s = 0.0;\n",
        ty)) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_puts(out,
        "    for (@range(i16, hidden_size)) |d| {\n")) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_printf(out, "        const v = %s%s[@as(u32, d)];\n",
        p, input->name)) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_puts(out, "        sq += v * v;\n")) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_puts(out, "    }\n")) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_puts(out,
        "    const inv_norm = 1.0 / sqrt_nr(sq + l2_eps);\n")) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_puts(out,
        "    for (@range(i16, hidden_size)) |d| {\n")) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_printf(out,
        "        %s%s[@as(u32, d)] = %s%s[@as(u32, d)] * inv_norm;\n",
        p, output->name, p, input->name)) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_puts(out, "    }\n")) != CSL_OK) {
        return err;
    }
    return tsir_csl_write_compute_tail(out, p,
        (const char *[]){input->name, output->name}, 2);
}
