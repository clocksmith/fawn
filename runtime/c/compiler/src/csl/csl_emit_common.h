// SPDX-License-Identifier: MIT
// Shared emit helpers for the emit/csl port ONLY (not a Zig file).
// This header exists to collapse helpers that were duplicated across
// emitter files in the first draft: the PUT/PUTF/PUTI macros, the
// quoted-JSON-string writer, the PE-program preamble, and the scalar
// name selector. All four are behaviour-identical to the copies they
// replace: same macro bodies, same JSON quoting (via the shared
// csl_buf_put_json_escaped), same PE preamble text, same f32 default.
// Macros operate on a variable named `out` (CslBuf *) in the caller,
// which every emitter in this port already uses.
#ifndef CSL_EMIT_COMMON_H
#define CSL_EMIT_COMMON_H
#include "csl_kernel.h"
#define PUT(s) do { CslErr _e = csl_buf_puts(out, (s)); if (_e) return _e; } while (0)
#define PUTF(...) do { CslErr _e = csl_buf_printf(out, __VA_ARGS__); if (_e) return _e; } while (0)
#define PUTI(v) do { CslErr _e = csl_buf_put_int(out, (long long)(v)); if (_e) return _e; } while (0)
CslErr csl_emit_json_string(CslBuf *out, const char *s);
CslErr csl_emit_pe_head(CslBuf *out, const CslKernelDesc *d, const char *comment);
const char *csl_emit_scalar(const CslKernelDesc *d);
#endif
