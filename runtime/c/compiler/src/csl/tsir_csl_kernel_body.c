// SPDX-License-Identifier: MIT
#include "tsir_csl_kernel_body.h"

CslErr tsir_csl_emit_kernel_body(const TsirCslKernelDesc *desc,
    CslBuf *out) {
    if (desc == NULL || out == NULL) {
        return CSL_ERR_INVALID;
    }
    switch (desc->op) {
        case TSIR_CSL_OP_ATTENTION_SCORES:
            return tsir_csl_emit_attention_scores(desc, out);
        case TSIR_CSL_OP_CONV1D_DEPTHWISE:
            return tsir_csl_emit_conv1d_depthwise(desc, out);
        case TSIR_CSL_OP_GELU_GATED:
        case TSIR_CSL_OP_SILU_GATED:
        case TSIR_CSL_OP_SIGMOID_GATED:
            return tsir_csl_emit_gated(desc, out);
        case TSIR_CSL_OP_KV_WRITE:
            return tsir_csl_emit_kv_write(desc, out);
        case TSIR_CSL_OP_KV_READ:
            return tsir_csl_emit_kv_read(desc, out);
        case TSIR_CSL_OP_L2_NORMALIZE:
            return tsir_csl_emit_l2_normalize(desc, out);
        case TSIR_CSL_OP_LINEAR_ATTENTION:
            return tsir_csl_emit_linear_attention(desc, out);
        case TSIR_CSL_OP_UNKNOWN:
        default:
            return CSL_ERR_UNSUPPORTED;
    }
}
