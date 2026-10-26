// SPDX-License-Identifier: MIT
// emit_csl.zig (facade) + emit_csl_core.zig (core emitter).
// Zig signature emit(module, out) becomes emit(desc, out): the caller
// supplies a classified CslKernelDesc (see csl_kernel.h). MAX_OUTPUT is
// 512 KiB, as in Zig. Output is two sections: layout.csl then
// pe_program.csl, then the structural validation from csl_spec is
// applied; a failing validation returns CSL_ERR_INVALID, as in Zig
// (which returns InvalidIr). Only compute entry points are supported:
// a non-compute / unsupported descriptor returns CSL_ERR_UNSUPPORTED.
#ifndef CSL_EMIT_CORE_H
#define CSL_EMIT_CORE_H
#include "csl_kernel.h"
#define CSL_EMIT_MAX_OUTPUT ((size_t)(512 * 1024))
CslErr csl_emit(CslBuf *out, const CslKernelDesc *d);
CslErr csl_emit_core(CslBuf *out, const CslKernelDesc *d);
#endif
