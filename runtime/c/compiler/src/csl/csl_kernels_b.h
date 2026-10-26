// SPDX-License-Identifier: MIT
// PE programs: attention (streaming/decode/tiled), fused GEMV-dequant,
// fused FFN, KV write/read, linear attention, distributed reduction.
// Pattern templates from descriptors; IR-derived buffer indices and
// per-variant body detail from the WGSL walk are NOT ported (see mapping).
#ifndef CSL_KERNELS_B_H
#define CSL_KERNELS_B_H
#include "csl_kernel.h"
CslErr csl_pe_attention_streaming(CslBuf *out, const CslKernelDesc *d);
CslErr csl_pe_attention_decode(CslBuf *out, const CslKernelDesc *d);
CslErr csl_pe_attention_tiled(CslBuf *out, const CslKernelDesc *d);
CslErr csl_pe_fused(CslBuf *out, const CslKernelDesc *d);
CslErr csl_pe_fused_ffn(CslBuf *out, const CslKernelDesc *d);
CslErr csl_pe_kv_write(CslBuf *out, const CslKernelDesc *d);
CslErr csl_pe_kv_read(CslBuf *out, const CslKernelDesc *d);
CslErr csl_pe_linear_attention(CslBuf *out, const CslKernelDesc *d);
CslErr csl_pe_reduce_dist(CslBuf *out, const CslKernelDesc *d);
CslErr csl_layout_reduce_dist(CslBuf *out, const CslKernelDesc *d);
CslErr csl_pe_kernels_b_emit(CslBuf *out, const CslKernelDesc *d);
#endif
