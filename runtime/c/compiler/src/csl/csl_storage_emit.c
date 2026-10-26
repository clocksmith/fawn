// SPDX-License-Identifier: MIT
// Storage declarations mirroring csl_storage_emit.zig.
#include "csl_storage_emit.h"
#include "csl_text.h"

static CslErr puts2(CslBuf *buf, const char *a, const char *b) {
    CslErr err = csl_buf_puts(buf, a);
    if (err != CSL_OK) {
        return err;
    }
    return csl_buf_puts(buf, b);
}

CslErr csl_storage_emit_buffer(CslBuf *buf, const char *name, const char *ty) {
    CslErr err = puts2(buf, "var ", name);
    if (err != CSL_OK) return err;
    err = puts2(buf, ": ", ty);
    if (err != CSL_OK) return err;
    err = puts2(buf, " = @zeros(", ty);
    if (err != CSL_OK) return err;
    return csl_buf_puts(buf, ");\n");
}

CslErr csl_storage_emit_pointer(CslBuf *buf, const char *name, const char *elem) {
    CslErr err = puts2(buf, "var ", name);
    if (err != CSL_OK) return err;
    err = puts2(buf, "_ptr: [*]", elem);
    if (err != CSL_OK) return err;
    err = puts2(buf, " = &", name);
    if (err != CSL_OK) return err;
    return csl_buf_puts(buf, ";\n");
}

CslErr csl_storage_emit_export(CslBuf *buf, const char *name) {
    CslErr err = puts2(buf, "    @export_symbol(", name);
    if (err != CSL_OK) return err;
    err = puts2(buf, "_ptr, \"", name);
    if (err != CSL_OK) return err;
    return csl_buf_puts(buf, "\");\n");
}

CslErr csl_storage_emit_pointer_exports(CslBuf *buf,
    const CslGlobal *globals, size_t count) {
    size_t i;
    for (i = 0; i < count; i++) {
        CslErr err;
        if (!globals[i].has_binding) {
            continue;
        }
        if (globals[i].addr_space != CSL_ADDR_STORAGE) {
            continue;
        }
        err = csl_storage_emit_export(buf, globals[i].name);
        if (err != CSL_OK) {
            return err;
        }
    }
    return CSL_OK;
}

const char *csl_storage_export_name(const CslGlobal *globals, size_t count,
    size_t target_index, const char *fallback) {
    size_t index = 0;
    size_t i;
    for (i = 0; i < count; i++) {
        if (!globals[i].has_binding) {
            continue;
        }
        if (globals[i].addr_space != CSL_ADDR_STORAGE) {
            continue;
        }
        if (index == target_index) {
            return globals[i].name;
        }
        index += 1;
    }
    return fallback;
}
