// SPDX-License-Identifier: MIT
// See header: contract is in csl_kernel.c; IR classification is unsupported.
#include "csl_classify.h"
CslErr csl_classify_ir(void) { return CSL_ERR_UNSUPPORTED; }
int csl_classify_contract_valid(const CslKernelDesc *d) { return csl_pattern_contract_valid(d); }
