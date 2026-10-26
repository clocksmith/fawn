// SPDX-License-Identifier: MIT
// See csl_text_buffer.h for the deliberate divergences from Zig.
#include "csl_text_buffer.h"
#include <stdio.h>
CslErr csl_text_write(CslBuf *buf, const char *text) { return csl_buf_puts(buf, text); }
CslErr csl_text_write_int(CslBuf *buf, int64_t value) { return csl_buf_put_int(buf, (long long)value); }
CslErr csl_text_write_u64(CslBuf *buf, uint64_t value) {
    char tmp[32]; int n;
    if (!buf) return CSL_ERR_INVALID;
    n = snprintf(tmp, sizeof(tmp), "%llu", (unsigned long long)value);
    if (n < 0 || (size_t)n >= sizeof(tmp)) return CSL_ERR_INVALID;
    return csl_buf_puts(buf, tmp);
}
CslErr csl_text_write_json_string(CslBuf *buf, const char *value) {
    CslErr e;
    if (!buf || !value) return CSL_ERR_INVALID;
    e = csl_buf_putc(buf, '"'); if (e) return e;
    e = csl_buf_put_json_escaped(buf, value); if (e) return e;
    return csl_buf_putc(buf, '"');
}
