// SPDX-License-Identifier: MIT
// UNRUN: this test file has NOT been executed or compiled as part of
// the emit-port delivery. It is a smoke sketch for the coordinator:
// spec constants, maps, pattern contract, and a dense_gemv core emit
// through descriptors, checked against the validator markers.
#include <assert.h>
#include <string.h>
#include "csl_spec.h"
#include "csl_maps.h"
#include "csl_kernel.h"
#include "csl_emit_core.h"
#include "csl_validate.h"

int main(void) {
    CslKernelDesc d;
    CslBuf out;
    const char *err = NULL;
    CslValidatePattern kind;
    assert(csl_validate_q4k_buffer_size(144) == NULL);
    assert(csl_validate_q4k_buffer_size(143) != NULL);
    assert(strcmp(csl_binary_op_text(CSL_BIN_LOGICAL_AND), "and") == 0);
    assert(csl_math_builtin("sqrt") != NULL && strcmp(csl_math_builtin("sqrt"), "math.sqrt") == 0);
    assert(csl_is_unsupported_builtin("textureSample") == 1);
    csl_kernel_desc_init(&d);
    d.pattern = CSL_PATTERN_DENSE_GEMV;
    d.in_dim_per_pe = 32; d.out_dim_per_pe = 16;
    assert(csl_pattern_contract_valid(&d) == 1);
    assert(csl_buf_init(&out, 65536) == CSL_OK);
    assert(csl_emit_core(&out, &d) == CSL_OK);
    assert(csl_pattern_kind_from_name("dense_gemv", &kind) == 1);
    assert(csl_validate_pattern(out.data, kind, &err) == 1);
    csl_buf_free(&out);
    return 0;
}
