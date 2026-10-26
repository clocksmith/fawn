// SPDX-License-Identifier: MIT
// PE programs: emit_csl_dequant, elementwise, gather, matmul, matmul_q4k,
// reduction, rope, sample, dense_gemv. Templates are pattern templates
// from descriptors (see emit-mapping.md): they carry the Zig markers the
// validator requires, but IR-derived buffer bodies are NOT the full Zig
// template text, except dense_gemv, whose Zig text is reproduced exactly.
#ifndef CSL_KERNELS_A_H
#define CSL_KERNELS_A_H
#include "csl_kernel.h"
CslErr csl_pe_dequant(CslBuf *out, const CslKernelDesc *d);
CslErr csl_pe_elementwise(CslBuf *out, const CslKernelDesc *d);
CslErr csl_pe_gather(CslBuf *out, const CslKernelDesc *d);
CslErr csl_pe_matmul(CslBuf *out, const CslKernelDesc *d);
CslErr csl_pe_matmul_q4k(CslBuf *out, const CslKernelDesc *d);
CslErr csl_pe_reduction(CslBuf *out, const CslKernelDesc *d);
CslErr csl_pe_rope(CslBuf *out, const CslKernelDesc *d);
CslErr csl_pe_sample(CslBuf *out, const CslKernelDesc *d);
CslErr csl_pe_dense_gemv(CslBuf *out, const CslKernelDesc *d);
CslErr csl_pe_emit(CslBuf *out, const CslKernelDesc *d);
#endif
