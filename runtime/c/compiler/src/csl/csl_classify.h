// SPDX-License-Identifier: MIT
// emit_csl_classify.zig: pattern contract + IR-walk classification seam.
// The contract rules live in csl_kernel.c (csl_pattern_contract_valid).
// classify() itself walks ir.Module in Zig and is NOT ported: it returns
// CSL_ERR_UNSUPPORTED. Callers supply a filled CslKernelDesc instead.
#ifndef CSL_CLASSIFY_H
#define CSL_CLASSIFY_H
#include "csl_kernel.h"
CslErr csl_classify_ir(void);
int csl_classify_contract_valid(const CslKernelDesc *d);
#endif
