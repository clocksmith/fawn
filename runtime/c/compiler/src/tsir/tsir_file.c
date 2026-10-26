/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#define _POSIX_C_SOURCE 200809L
#include "tsir_file.h"

#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <io.h>
#else
#include <unistd.h>
#endif
#include <errno.h>
#include <stdlib.h>
#include <string.h>

/* File identity is a host operation. Keep one bounded hashing implementation
 * with thin CRT handle adapters; neither platform invokes an executable. */
#ifdef _WIN32
typedef struct _stat64 SourceFileStatus;
static TsirStatus source_open(const char *path, int *out) {
    /* The public repository root is UTF-8, independent of the active code page.
     * Windows paths have at most 32,767 UTF-16 code units plus termination. */
    int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, NULL, 0);
    if (count == 0) { errno = EILSEQ; return TSIR_ERR_INVALID; }
    if (count > 32768) { errno = ENAMETOOLONG; return TSIR_ERR_OVERFLOW; }
    wchar_t *wide = malloc((size_t)count * sizeof(*wide));
    if (wide == NULL) return TSIR_ERR_NOMEM;
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, wide, count) != count) {
        free(wide);
        errno = EILSEQ;
        return TSIR_ERR_INVALID;
    }
    HANDLE handle = CreateFileW(wide, GENERIC_READ, FILE_SHARE_READ, NULL,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    DWORD failure = handle == INVALID_HANDLE_VALUE ? GetLastError() : ERROR_SUCCESS;
    free(wide);
    if (handle == INVALID_HANDLE_VALUE) {
        errno = failure == ERROR_FILE_NOT_FOUND || failure == ERROR_PATH_NOT_FOUND ? ENOENT :
            failure == ERROR_ACCESS_DENIED || failure == ERROR_SHARING_VIOLATION ? EACCES :
            failure == ERROR_NOT_ENOUGH_MEMORY || failure == ERROR_OUTOFMEMORY ? ENOMEM : EIO;
        return TSIR_ERR_INVALID;
    }
    if (GetFileType(handle) != FILE_TYPE_DISK) {
        CloseHandle(handle);
        errno = EINVAL;
        return TSIR_ERR_INVALID;
    }
    int descriptor = _open_osfhandle((intptr_t)handle, _O_RDONLY | _O_BINARY | _O_NOINHERIT);
    if (descriptor < 0) {
        int first = errno;
        CloseHandle(handle);
        errno = first;
        return TSIR_ERR_INVALID;
    }
    *out = descriptor;
    return TSIR_OK;
}
static int source_stat(int descriptor, SourceFileStatus *status) {
    return _fstat64(descriptor, status);
}
static bool source_regular(const SourceFileStatus *status) {
    return (status->st_mode & _S_IFMT) == _S_IFREG;
}
static int source_close(int descriptor) { return _close(descriptor); }
static FILE *source_stream(int descriptor) { return _fdopen(descriptor, "rb"); }
static int source_descriptor(FILE *stream) { return _fileno(stream); }
#else
typedef struct stat SourceFileStatus;
static TsirStatus source_open(const char *path, int *out) {
    int descriptor = open(path, O_RDONLY | O_NONBLOCK);
    if (descriptor < 0) return TSIR_ERR_INVALID;
    *out = descriptor;
    return TSIR_OK;
}
static int source_stat(int descriptor, SourceFileStatus *status) {
    return fstat(descriptor, status);
}
static bool source_regular(const SourceFileStatus *status) { return S_ISREG(status->st_mode); }
static int source_close(int descriptor) { return close(descriptor); }
static FILE *source_stream(int descriptor) { return fdopen(descriptor, "rb"); }
static int source_descriptor(FILE *stream) { return fileno(stream); }
#endif

static TsirStatus descriptor_stamp(int descriptor, TsirFileStamp *stamp) {
    SourceFileStatus status;
    if (source_stat(descriptor, &status) != 0) return TSIR_ERR_INVALID;
    if (!source_regular(&status) || status.st_size < 0) {
        errno = EINVAL;
        return TSIR_ERR_INVALID;
    }
    *stamp = (TsirFileStamp){(uintmax_t)status.st_size, (int64_t)status.st_mtime};
    return TSIR_OK;
}
TsirStatus tsir_file_stamp(FILE *stream, TsirFileStamp *stamp) {
    if (stream == NULL || stamp == NULL) { errno = EINVAL; return TSIR_ERR_INVALID; }
    return descriptor_stamp(source_descriptor(stream), stamp);
}
TsirStatus tsir_file_open(const char *path, FILE **out, TsirFileStamp *stamp) {
    if (path == NULL || out == NULL || stamp == NULL) { errno = EINVAL; return TSIR_ERR_INVALID; }
    int descriptor;
    TsirStatus status = source_open(path, &descriptor);
    if (status != TSIR_OK) return status;
    TsirFileStamp snapshot;
    status = descriptor_stamp(descriptor, &snapshot);
    FILE *stream = status == TSIR_OK ? source_stream(descriptor) : NULL;
    if (stream == NULL) {
        int first = errno;
        source_close(descriptor);
        errno = first;
        return TSIR_ERR_INVALID;
    }
    *out = stream;
    *stamp = snapshot;
    return TSIR_OK;
}
