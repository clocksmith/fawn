// SPDX-License-Identifier: MIT
// Shared emit helpers. See csl_emit_common.h for the identity proof.
#include "csl_emit_common.h"
CslErr csl_emit_json_string(CslBuf *out, const char *s) {
    CslErr e = csl_buf_putc(out, '"'); if (e) return e;
    e = csl_buf_put_json_escaped(out, s ? s : ""); if (e) return e;
    return csl_buf_putc(out, '"');
}
CslErr csl_emit_pe_head(CslBuf *out, const CslKernelDesc *d, const char *comment) {
    CslErr e;
    if (!out || !d) return CSL_ERR_INVALID;
    e = csl_buf_puts(out, "// PE program: "); if (e) return e;
    e = csl_buf_puts(out, comment); if (e) return e;
    e = csl_buf_puts(out, "\nparam memcpy_params;\nparam pe_id: i16;\nparam num_pes: i16;\n"); if (e) return e;
    return csl_buf_puts(out, "const sys_mod = @import_module(\"<memcpy/memcpy>\", memcpy_params);\n\n");
}
const char *csl_emit_scalar(const CslKernelDesc *d) {
    return (d && d->scalar) ? d->scalar : "f32";
}
