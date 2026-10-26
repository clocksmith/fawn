// SPDX-License-Identifier: MIT
#include "csl_text.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
CslErr csl_buf_init(CslBuf *b, size_t cap) {
    if (!b) return CSL_ERR_INVALID;
    b->data = (char *)malloc(cap ? cap : 64);
    if (!b->data) return CSL_ERR_NOMEM;
    b->data[0] = '\0'; b->len = 0; b->cap = cap ? cap : 64;
    return CSL_OK;
}
void csl_buf_free(CslBuf *b) { if (b) { free(b->data); b->data = NULL; b->len = 0; b->cap = 0; } }
static CslErr grow(CslBuf *b, size_t need) {
    if (b->len + need + 1 <= b->cap) return CSL_OK;
    size_t ncap = b->cap ? b->cap : 64;
    while (ncap < b->len + need + 1) { if (ncap > (size_t)1 << 30) return CSL_ERR_TOO_LARGE; ncap *= 2; }
    char *nd = (char *)realloc(b->data, ncap);
    if (!nd) return CSL_ERR_NOMEM;
    b->data = nd; b->cap = ncap; return CSL_OK;
}
CslErr csl_buf_puts(CslBuf *b, const char *s) {
    if (!b || !s) return CSL_ERR_INVALID;
    size_t n = strlen(s); CslErr e = grow(b, n); if (e) return e;
    memcpy(b->data + b->len, s, n); b->len += n; b->data[b->len] = '\0'; return CSL_OK;
}
CslErr csl_buf_putc(CslBuf *b, char c) {
    if (!b) return CSL_ERR_INVALID;
    CslErr e = grow(b, 1); if (e) return e;
    b->data[b->len++] = c; b->data[b->len] = '\0'; return CSL_OK;
}
CslErr csl_buf_printf(CslBuf *b, const char *fmt, ...) {
    if (!b || !fmt) return CSL_ERR_INVALID;
    va_list ap; va_start(ap, fmt);
    char tmp[1024]; int n = vsnprintf(tmp, sizeof(tmp), fmt, ap); va_end(ap);
    if (n < 0) return CSL_ERR_INVALID;
    if ((size_t)n < sizeof(tmp)) return csl_buf_puts(b, tmp);
    char *big = (char *)malloc((size_t)n + 1); if (!big) return CSL_ERR_NOMEM;
    va_start(ap, fmt); vsnprintf(big, (size_t)n + 1, fmt, ap); va_end(ap);
    CslErr e = csl_buf_puts(b, big); free(big); return e;
}
CslErr csl_buf_put_int(CslBuf *b, long long v) { char tmp[32]; snprintf(tmp, sizeof(tmp), "%lld", v); return csl_buf_puts(b, tmp); }
CslErr csl_buf_put_json_escaped(CslBuf *b, const char *s) {
    if (!b || !s) return CSL_ERR_INVALID;
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        CslErr e;
        if (*p == '"') e = csl_buf_puts(b, "\\\"");
        else if (*p == '\\') e = csl_buf_puts(b, "\\\\");
        else if (*p == '\n') e = csl_buf_puts(b, "\\n");
        else if (*p == '\r') e = csl_buf_puts(b, "\\r");
        else if (*p == '\t') e = csl_buf_puts(b, "\\t");
        else if (*p < 0x20) { char tmp[8]; snprintf(tmp, sizeof(tmp), "\\u%04x", *p); e = csl_buf_puts(b, tmp); }
        else e = csl_buf_putc(b, (char)*p);
        if (e) return e;
    }
    return CSL_OK;
}
