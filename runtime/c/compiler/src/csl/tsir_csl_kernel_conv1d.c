// SPDX-License-Identifier: MIT
#include "tsir_csl_kernel_body.h"

/* Port of emit_kernel_body_conv1d.zig (110 lines): depthwise causal
 * 1-D convolution, single-PE bootstrap canary surface.
 *
 * Layout, exactly as in Zig:
 *   input:  [num_tokens * channels] row-major (token, channel)
 *   weight: [channels * kernel_size] row-major (channel, k)
 *   bias:   [channels] (only when has_bias; the bias binding is
 *           then required, and a missing bias returns
 *           CSL_ERR_INVALID, matching Zig's catch-to-InvalidBodyContract)
 *   output: [num_tokens * channels] row-major (token, channel)
 * Math: acc = bias[c] or 0; for k, t_in = t - (kernel_size - 1 - k);
 * only t_in >= 0 contributes input[t_in * channels + c] *
 * weight[c * kernel_size + k]. Causal pad is left-only.
 * Layout-supplied CSL param (not a descriptor field): num_tokens. */

CslErr tsir_csl_emit_conv1d_depthwise(const TsirCslKernelDesc *desc,
    CslBuf *out) {
    TsirCslConfig defaults;
    const TsirCslConfig *config = NULL;
    const TsirCslBinding *input = NULL;
    const TsirCslBinding *weight = NULL;
    const TsirCslBinding *output = NULL;
    const TsirCslBinding *bias = NULL;
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
    err = tsir_csl_binding_for_role(desc, TSIR_CSL_ROLE_INPUT, &input);
    if (err != CSL_OK) {
        return err;
    }
    err = tsir_csl_binding_for_role(desc, TSIR_CSL_ROLE_WEIGHT,
        &weight);
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
    err = tsir_csl_require_elem(weight, elem);
    if (err != CSL_OK) {
        return err;
    }
    err = tsir_csl_require_elem(output, elem);
    if (err != CSL_OK) {
        return err;
    }
    if (desc->body.conv_channels == 0 ||
        desc->body.conv_kernel_size == 0) {
        return CSL_ERR_INVALID;
    }
    if (desc->body.conv_has_bias) {
        err = tsir_csl_binding_for_role(desc, TSIR_CSL_ROLE_BIAS,
            &bias);
        if (err != CSL_OK) {
            return CSL_ERR_INVALID;
        }
        err = tsir_csl_require_elem(bias, elem);
        if (err != CSL_OK) {
            return err;
        }
    }
    p = config->var_prefix;
    ty = tsir_csl_elem_name(elem);
    if (ty == NULL) {
        return CSL_ERR_INVALID;
    }
    if ((err = csl_buf_puts(out, "param memcpy_params;\n")) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_printf(out, "const channels: i16 = %u;\n",
        desc->body.conv_channels)) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_printf(out, "const kernel_size: i16 = %u;\n",
        desc->body.conv_kernel_size)) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_puts(out, "param num_tokens: i16;\n")) != CSL_OK) {
        return err;
    }
    err = tsir_csl_write_sys_mod(out);
    if (err != CSL_OK) {
        return err;
    }
    if ((err = tsir_csl_write_buffer_array(out, p, input->name,
        "num_tokens * channels", ty)) != CSL_OK) {
        return err;
    }
    if ((err = tsir_csl_write_buffer_array(out, p, weight->name,
        "channels * kernel_size", ty)) != CSL_OK) {
        return err;
    }
    if ((err = tsir_csl_write_buffer_array(out, p, output->name,
        "num_tokens * channels", ty)) != CSL_OK) {
        return err;
    }
    if (bias != NULL) {
        if ((err = tsir_csl_write_buffer_array(out, p, bias->name,
            "channels", ty)) != CSL_OK) {
            return err;
        }
    }
    if ((err = tsir_csl_write_buffer_pointer(out, p, input->name,
        ty)) != CSL_OK) {
        return err;
    }
    if ((err = tsir_csl_write_buffer_pointer(out, p, weight->name,
        ty)) != CSL_OK) {
        return err;
    }
    if ((err = tsir_csl_write_buffer_pointer(out, p, output->name,
        ty)) != CSL_OK) {
        return err;
    }
    if (bias != NULL) {
        if ((err = tsir_csl_write_buffer_pointer(out, p, bias->name,
            ty)) != CSL_OK) {
            return err;
        }
    }
    if ((err = csl_buf_puts(out, "\n")) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_puts(out, "fn compute() void {\n")) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_puts(out,
        "    for (@range(i16, num_tokens)) |t| {\n")) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_puts(out,
        "        for (@range(i16, channels)) |c| {\n")) != CSL_OK) {
        return err;
    }
    if (bias != NULL) {
        if ((err = csl_buf_printf(out,
            "            var acc: %s = %s%s[@as(u32, c)];\n", ty, p,
            bias->name)) != CSL_OK) {
            return err;
        }
    } else {
        if ((err = csl_buf_printf(out, "            var acc: %s = 0.0;\n",
            ty)) != CSL_OK) {
            return err;
        }
    }
    if ((err = csl_buf_puts(out,
        "            for (@range(i16, kernel_size)) |k| {\n")) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_puts(out,
        "                const t_in: i16 = t - (kernel_size - 1 - k);\n")) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_puts(out,
        "                if (t_in >= 0) {\n")) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_printf(out,
        "                    acc += %s%s[@as(u32, t_in) * @as(u32, channels) + @as(u32, c)] * %s%s[@as(u32, c) * @as(u32, kernel_size) + @as(u32, k)];\n",
        p, input->name, p, weight->name)) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_puts(out, "                }\n")) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_puts(out, "            }\n")) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_printf(out,
        "            %s%s[@as(u32, t) * @as(u32, channels) + @as(u32, c)] = acc;\n",
        p, output->name)) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_puts(out, "        }\n")) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_puts(out, "    }\n")) != CSL_OK) {
        return err;
    }
    {
        const char *names[4];
        size_t count = 3;
        names[0] = input->name;
        names[1] = weight->name;
        names[2] = output->name;
        if (bias != NULL) {
            names[3] = bias->name;
            count = 4;
        }
        return tsir_csl_write_compute_tail(out, p, names, count);
    }
}
