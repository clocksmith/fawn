// SPDX-License-Identifier: MIT
#ifndef DOE_TSIR_CSL_KERNEL_BODY_H
#define DOE_TSIR_CSL_KERNEL_BODY_H

#include "tsir_csl_common.h"

/* Dispatch mirroring emit_kernel_body.zig emitCsl, restricted to the
 * seven in-scope files. Every other op returns CSL_ERR_UNSUPPORTED:
 * fused_gemv, rms_norm, gather and residual_add live in the excluded
 * emit_kernel_body.zig and are NOT re-ported here, and kv_write /
 * kv_read with the full_per_pe strategy take the excluded
 * emit_kernel_body.zig path, so they also return
 * CSL_ERR_UNSUPPORTED here (only the slot_sharded KV bodies in
 * tsir_csl_kernel_kv.c are in scope). A future integration calls
 * this one function for all seven ops, in the same way Zig calls
 * emitWithConfig with backend .csl. */

CslErr tsir_csl_emit_kernel_body(const TsirCslKernelDesc *desc,
    CslBuf *out);

/* Per-op entry points, mirroring the Zig function names in
 * tsir-mapping.md. Callers normally use the dispatch above. */
CslErr tsir_csl_emit_attention_scores(const TsirCslKernelDesc *desc,
    CslBuf *out);
CslErr tsir_csl_emit_conv1d_depthwise(const TsirCslKernelDesc *desc,
    CslBuf *out);
CslErr tsir_csl_emit_gated(const TsirCslKernelDesc *desc, CslBuf *out);
CslErr tsir_csl_emit_kv_write(const TsirCslKernelDesc *desc,
    CslBuf *out);
CslErr tsir_csl_emit_kv_read(const TsirCslKernelDesc *desc,
    CslBuf *out);
CslErr tsir_csl_emit_l2_normalize(const TsirCslKernelDesc *desc,
    CslBuf *out);
CslErr tsir_csl_emit_linear_attention(const TsirCslKernelDesc *desc,
    CslBuf *out);

#endif
