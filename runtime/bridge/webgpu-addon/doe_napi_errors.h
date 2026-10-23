/*
 * doe_napi_errors.h — formats native library error text with its stage and
 * kind for JavaScript errors. Header-only; included by doe_napi_internal.h.
 */
#ifndef DOE_NAPI_ERRORS_H
#define DOE_NAPI_ERRORS_H
#include <stdbool.h>
#include <stddef.h>

static inline void doe_append_truncated(char* dst, size_t* offset, size_t cap, const char* src) {
    if (!dst || !offset || cap == 0 || *offset >= cap - 1) return;
    if (!src) src = "";
    while (*src != '\0' && *offset < cap - 1) {
        dst[*offset] = *src;
        *offset += 1;
        src += 1;
    }
    dst[*offset] = '\0';
}

static inline void doe_format_library_error_message(
    char* dst,
    size_t cap,
    const char* stage,
    const char* kind,
    const char* msg
) {
    if (!dst || cap == 0) return;
    size_t offset = 0;
    dst[0] = '\0';
    const bool has_stage = stage && stage[0] != '\0';
    const bool has_kind = kind && kind[0] != '\0';

    if (has_stage && has_kind) {
        doe_append_truncated(dst, &offset, cap, "[");
        doe_append_truncated(dst, &offset, cap, stage);
        doe_append_truncated(dst, &offset, cap, "/");
        doe_append_truncated(dst, &offset, cap, kind);
        doe_append_truncated(dst, &offset, cap, "] ");
    } else if (has_stage) {
        doe_append_truncated(dst, &offset, cap, "[");
        doe_append_truncated(dst, &offset, cap, stage);
        doe_append_truncated(dst, &offset, cap, "] ");
    }
    doe_append_truncated(dst, &offset, cap, msg);
}

#endif
