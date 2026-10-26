// SPDX-License-Identifier: MIT
// emit_csl_layout.zig: layout.csl emission per pattern, from descriptors.
// Grid sizes, tile params and export names come from CslKernelDesc / its
// globals; the Zig layout reads scalar types from IR globals, here the
// caller supplies names and the scalar is f32 unless desc->scalar says f16.
#ifndef CSL_LAYOUT_H
#define CSL_LAYOUT_H
#include "csl_kernel.h"
CslErr csl_layout_emit(CslBuf *out, const CslKernelDesc *d);
CslErr csl_layout_element_wise(CslBuf *out, const CslKernelDesc *d);
CslErr csl_layout_reduction(CslBuf *out, const CslKernelDesc *d);
CslErr csl_layout_matmul(CslBuf *out, const CslKernelDesc *d);
CslErr csl_layout_matmul_q4k(CslBuf *out, const CslKernelDesc *d);
CslErr csl_layout_gather(CslBuf *out, const CslKernelDesc *d);
CslErr csl_layout_rope(CslBuf *out, const CslKernelDesc *d);
CslErr csl_layout_dequant(CslBuf *out, const CslKernelDesc *d);
CslErr csl_layout_sample(CslBuf *out, const CslKernelDesc *d);
CslErr csl_layout_fused_gemv(CslBuf *out, const CslKernelDesc *d);
CslErr csl_layout_streaming_attention(CslBuf *out, const CslKernelDesc *d);
CslErr csl_layout_decode_attention(CslBuf *out, const CslKernelDesc *d);
CslErr csl_layout_tiled_attention(CslBuf *out, const CslKernelDesc *d);
CslErr csl_layout_linear_attention(CslBuf *out, const CslKernelDesc *d);
CslErr csl_layout_kv_write(CslBuf *out, const CslKernelDesc *d);
CslErr csl_layout_kv_read(CslBuf *out, const CslKernelDesc *d);
CslErr csl_layout_fused_ffn(CslBuf *out, const CslKernelDesc *d);
CslErr csl_layout_dense_gemv(CslBuf *out, const CslKernelDesc *d);
#endif
