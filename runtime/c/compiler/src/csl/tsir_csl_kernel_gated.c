// SPDX-License-Identifier: MIT
#include "tsir_csl_kernel_body.h"

/* Port of emit_kernel_body_gated.zig (115 lines): the gated
 * activation family gelu_gated, silu_gated, sigmoid_gated.
 *
 * All three share binding shape (gate, input, output) and the outer
 * kernel output[i] = act(gate[i]) * input[i]; only the helper
 * differs, and each kind keeps its own helper name (gelu / silu /
 * sigmoid) so existing byte-comparisons for gelu_gated stay stable,
 * exactly as in Zig.
 *   gelu:    tanh-approximation GELU (Doppler MLP form), with the
 *            +/-15 saturation clamp matching emit_csl_semantic_ops
 *            emitGeluPe, as Zig comments. That semantic-op file is
 *            NOT duplicated here; this body only reproduces the
 *            clamp Zig chose for parity with it.
 *   silu:    x / (1 + exp(-x)), negation clamped to +/-15.
 *   sigmoid: 1 / (1 + exp(-x)), negation clamped to +/-15.
 * Layout-supplied CSL param (not a descriptor field): chunk_size
 * (i16, or the config chunk_size_default). */

CslErr tsir_csl_emit_gated(const TsirCslKernelDesc *desc,
    CslBuf *out) {
    TsirCslConfig defaults;
    const TsirCslConfig *config = NULL;
    const TsirCslBinding *gate = NULL;
    const TsirCslBinding *input = NULL;
    const TsirCslBinding *output = NULL;
    TsirCslElem elem = TSIR_CSL_F32;
    const char *p = NULL;
    const char *ty = NULL;
    const char *fn_name = NULL;
    CslErr err = CSL_OK;
    if (desc == NULL || out == NULL) {
        return CSL_ERR_INVALID;
    }
    switch (desc->op) {
        case TSIR_CSL_OP_GELU_GATED:
            fn_name = "gelu";
            break;
        case TSIR_CSL_OP_SILU_GATED:
            fn_name = "silu";
            break;
        case TSIR_CSL_OP_SIGMOID_GATED:
            fn_name = "sigmoid";
            break;
        default:
            return CSL_ERR_UNSUPPORTED;
    }
    err = tsir_csl_resolve_config(desc, &defaults, &config);
    if (err != CSL_OK) {
        return err;
    }
    err = tsir_csl_binding_for_role(desc, TSIR_CSL_ROLE_GATE, &gate);
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
    err = tsir_csl_require_elem(gate, elem);
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
    p = config->var_prefix;
    ty = tsir_csl_elem_name(elem);
    if (ty == NULL) {
        return CSL_ERR_INVALID;
    }
    if ((err = csl_buf_puts(out, "param memcpy_params;\n")) != CSL_OK) {
        return err;
    }
    if (config->has_chunk_size_default) {
        if ((err = csl_buf_printf(out,
            "param chunk_size: i16 = %u;\n",
            config->chunk_size_default)) != CSL_OK) {
            return err;
        }
    } else {
        if ((err = csl_buf_puts(out,
            "param chunk_size: i16;\n")) != CSL_OK) {
            return err;
        }
    }
    err = tsir_csl_write_sys_mod(out);
    if (err != CSL_OK) {
        return err;
    }
    err = tsir_csl_write_math_import(out);
    if (err != CSL_OK) {
        return err;
    }
    if (desc->op == TSIR_CSL_OP_GELU_GATED) {
        if ((err = csl_buf_printf(out,
            "const GELU_A: %s = 0.7978845608028654;\n", ty)) != CSL_OK) {
            return err;
        }
        if ((err = csl_buf_printf(out, "const GELU_B: %s = 0.044715;\n",
            ty)) != CSL_OK) {
            return err;
        }
    }
    if ((err = tsir_csl_write_buffer_array(out, p, gate->name,
        "chunk_size", ty)) != CSL_OK) {
        return err;
    }
    if ((err = tsir_csl_write_buffer_array(out, p, input->name,
        "chunk_size", ty)) != CSL_OK) {
        return err;
    }
    if ((err = tsir_csl_write_buffer_array(out, p, output->name,
        "chunk_size", ty)) != CSL_OK) {
        return err;
    }
    if ((err = tsir_csl_write_buffer_pointer(out, p, gate->name,
        ty)) != CSL_OK) {
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
    if ((err = csl_buf_printf(out, "fn %s(x: %s) %s {\n", fn_name,
        ty, ty)) != CSL_OK) {
        return err;
    }
    if (desc->op == TSIR_CSL_OP_GELU_GATED) {
        if ((err = csl_buf_puts(out,
            "    var inner = GELU_A * (x + GELU_B * x * x * x);\n")) != CSL_OK) {
            return err;
        }
        if ((err = csl_buf_puts(out,
            "    if (inner < -15.0) inner = -15.0;\n")) != CSL_OK) {
            return err;
        }
        if ((err = csl_buf_puts(out,
            "    if (inner > 15.0) inner = 15.0;\n")) != CSL_OK) {
            return err;
        }
        if ((err = csl_buf_puts(out,
            "    return 0.5 * x * (1.0 + math.tanh(inner));\n")) != CSL_OK) {
            return err;
        }
    } else {
        /* silu and sigmoid share this body exactly in Zig except
         * for the final return line; the difference is data (the
         * return expression), not logic, so it is collapsed here
         * into one shared clamp block plus a data-driven return.
         * The emitted lines are unchanged for both kinds. */
        if ((err = csl_buf_puts(out, "    var z = -x;\n")) != CSL_OK) {
            return err;
        }
        if ((err = csl_buf_puts(out,
            "    if (z < -15.0) z = -15.0;\n")) != CSL_OK) {
            return err;
        }
        if ((err = csl_buf_puts(out,
            "    if (z > 15.0) z = 15.0;\n")) != CSL_OK) {
            return err;
        }
        if (desc->op == TSIR_CSL_OP_SILU_GATED) {
            if ((err = csl_buf_puts(out,
                "    return x / (1.0 + math.exp(z));\n")) != CSL_OK) {
                return err;
            }
        } else {
            if ((err = csl_buf_puts(out,
                "    return 1.0 / (1.0 + math.exp(z));\n")) != CSL_OK) {
                return err;
            }
        }
    }
    if ((err = csl_buf_puts(out, "}\n\n")) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_puts(out, "fn compute() void {\n")) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_puts(out,
        "    for (@range(i16, chunk_size)) |i| {\n")) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_puts(out,
        "        const idx = @as(u32, i);\n")) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_printf(out,
        "        %s%s[idx] = %s(%s%s[idx]) * %s%s[idx];\n", p,
        output->name, fn_name, p, gate->name, p, input->name)) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_puts(out, "    }\n")) != CSL_OK) {
        return err;
    }
    return tsir_csl_write_compute_tail(out, p,
        (const char *[]){gate->name, input->name, output->name}, 3);
}
