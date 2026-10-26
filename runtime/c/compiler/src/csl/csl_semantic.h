// SPDX-License-Identifier: MIT
// emit_csl_semantic_ops.zig: direct CSL for model-semantic scalar kernels.
// Semantic pattern names (Zig): rms_norm, residual_add, gelu_gated,
// silu_gated, sigmoid_gated, conv1d_depthwise, l2_normalize,
// linear_attention, attention_prefill_kv_axis_sharded. RMS epsilon is
// 0.000001 (Zig). The TSIR kernel-body templates this Zig file imports
// are NOT ported here (other worker's scope); PE bodies below are
// descriptor templates with the semantic formulas, not TSIR bodies.
#ifndef CSL_SEMANTIC_H
#define CSL_SEMANTIC_H
#include "csl_kernel.h"
int csl_is_semantic_pattern(const char *name);
CslErr csl_semantic_layout(CslBuf *out, const CslKernelDesc *d, const char *semantic_name);
CslErr csl_semantic_pe(CslBuf *out, const CslKernelDesc *d, const char *semantic_name);
#endif
