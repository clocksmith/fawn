/* SPDX-License-Identifier: MIT */
#ifndef DOE_NAPI_LIBRARY_H
#define DOE_NAPI_LIBRARY_H
#include <stddef.h>
#include <stdint.h>

typedef struct doe_library_error {
    uint32_t native_code; /* Win32 code; zero for portable input/dlopen failure. */
    const char *operation; /* Static operation identity, never borrowed OS text. */
    char message[512]; /* Owned bounded diagnostic. */
} doe_library_error;
/* Borrow exactly length UTF-8 bytes, without an embedded NUL. A returned handle
 * owns one platform loader reference; the existing addon library owner retains
 * it for process lifetime or closes a rejected candidate. On failure no handle
 * escapes and error snapshots the first operation/native code before cleanup.
 * Success leaves error untouched. Windows resolves relative paths once to an
 * absolute UTF-16 path and limits additional filesystem import search to its DLL
 * directory plus System32; existing OS module-cache behavior is unchanged. No
 * PATH/CWD search mutation, fallback compiler or provider selection. */
void *doe_library_open(const char *path,size_t length,doe_library_error *error);
#endif
