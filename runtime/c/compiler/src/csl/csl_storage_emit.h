// SPDX-License-Identifier: MIT
// Storage declarations mirroring csl_storage_emit.zig. The Zig file
// walks an ir.Module; here the caller supplies the global list, which
// is the same information with the IR dependency removed (see G-IR).
#ifndef CSL_STORAGE_EMIT_H
#define CSL_STORAGE_EMIT_H

#include "csl_text.h"
#include "csl_kernel.h"

CslErr csl_storage_emit_buffer(CslBuf *buf, const char *name, const char *ty);
CslErr csl_storage_emit_pointer(CslBuf *buf, const char *name, const char *elem);
CslErr csl_storage_emit_export(CslBuf *buf, const char *name);
CslErr csl_storage_emit_pointer_exports(CslBuf *buf,
    const CslGlobal *globals, size_t count);
const char *csl_storage_export_name(const CslGlobal *globals, size_t count,
    size_t target_index, const char *fallback);

#endif
