// SPDX-License-Identifier: MIT
// Core dispatch mirrors emit_csl_core.zig's switch, in descriptor form.
// Dense GEMV and semantic patterns are not dispatched by Zig core; they
// are reachable here through csl_layout_emit / kernels directly, and via
// this facade only when the descriptor pattern names them, so that a
// single entry point exists for host-compile-source sections.
#include "csl_emit_core.h"
#include "csl_compile_sections.h"
#include "csl_spec.h"
#include "csl_layout.h"
#include "csl_kernels_a.h"
#include "csl_kernels_b.h"
#include "csl_semantic.h"
#include <string.h>
static CslErr emit_pe(CslBuf *out, const CslKernelDesc *d) {
    CslErr e;
    if (d->pattern == CSL_PATTERN_REDUCTION && d->distributed)
        return csl_pe_reduce_dist(out, d);
    e = csl_pe_emit(out, d);
    if (e == CSL_ERR_UNSUPPORTED) e = csl_pe_kernels_b_emit(out, d);
    if (e == CSL_ERR_UNSUPPORTED && d->pattern == CSL_PATTERN_SEMANTIC)
        e = csl_semantic_pe(out, d, d->pattern_name ? d->pattern_name : "rms_norm");
    return e;
}
CslErr csl_emit_core(CslBuf *out, const CslKernelDesc *d) {
    CslErr e;
    const char *err;
    if (!out || !d) return CSL_ERR_INVALID;
    if (d->pattern == CSL_PATTERN_UNSUPPORTED) return CSL_ERR_UNSUPPORTED;
    if (!csl_pattern_contract_valid(d)) return CSL_ERR_UNSUPPORTED;
    e = csl_section_write_header(out, CSL_LAYOUT_FILENAME); if (e) return e;
    if (d->pattern == CSL_PATTERN_REDUCTION && d->distributed)
        e = csl_layout_reduce_dist(out, d);
    else if (d->pattern == CSL_PATTERN_SEMANTIC)
        e = csl_semantic_layout(out, d, d->pattern_name ? d->pattern_name : "rms_norm");
    else e = csl_layout_emit(out, d);
    if (e) return e;
    e = csl_section_write_header(out, CSL_PE_PROGRAM_FILENAME); if (e) return e;
    e = emit_pe(out, d); if (e) return e;
    /* Structural validation, as in Zig core (which then also runs the
       per-pattern validator; that step is csl_validate_pattern and is
       invoked by callers that need per-pattern rules). */
    if (!out->data) return CSL_ERR_INVALID;
    err = csl_validate_output(out->data);
    if (err) return CSL_ERR_INVALID;
    return CSL_OK;
}
CslErr csl_emit(CslBuf *out, const CslKernelDesc *d) { return csl_emit_core(out, d); }
