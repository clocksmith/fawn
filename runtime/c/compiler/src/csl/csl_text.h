// SPDX-License-Identifier: MIT
#ifndef DOE_CSL_TEXT_H
#define DOE_CSL_TEXT_H
#include <stddef.h>
#include "csl_error.h"
typedef struct CslBuf { char *data; size_t len; size_t cap; } CslBuf;
CslErr csl_buf_init(CslBuf *b, size_t cap);
void csl_buf_free(CslBuf *b);
CslErr csl_buf_puts(CslBuf *b, const char *s);
CslErr csl_buf_putc(CslBuf *b, char c);
CslErr csl_buf_printf(CslBuf *b, const char *fmt, ...);
CslErr csl_buf_put_int(CslBuf *b, long long v);
CslErr csl_buf_put_json_escaped(CslBuf *b, const char *s);
#endif
