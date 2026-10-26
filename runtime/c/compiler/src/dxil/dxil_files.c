/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif
#include "dxil_files.h"

#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <direct.h>
#include <io.h>
#include <process.h>
#include <share.h>

static int wide_path(const char *path, wchar_t out[DXIL_FILES_PATH_CAP]) {
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1,
            out, (int)DXIL_FILES_PATH_CAP) != 0) {
        return 0;
    }
    errno = GetLastError() == ERROR_INSUFFICIENT_BUFFER
        ? ENAMETOOLONG : EILSEQ;
    return -1;
}

int dxil_files_mkdir(const char *path) {
    wchar_t wide[DXIL_FILES_PATH_CAP];
    return wide_path(path, wide) == 0 ? _wmkdir(wide) : -1;
}

int dxil_files_rmdir(const char *path) {
    wchar_t wide[DXIL_FILES_PATH_CAP];
    return wide_path(path, wide) == 0 ? _wrmdir(wide) : -1;
}

int dxil_files_remove(const char *path) {
    wchar_t wide[DXIL_FILES_PATH_CAP];
    return wide_path(path, wide) == 0 ? _wremove(wide) : -1;
}

FILE *dxil_files_create(const char *path) {
    wchar_t wide[DXIL_FILES_PATH_CAP];
    FILE *file = NULL;
    int descriptor = -1;
    if (wide_path(path, wide) != 0) return NULL;
    errno_t error = _wsopen_s(&descriptor, wide,
        _O_WRONLY | _O_CREAT | _O_EXCL | _O_BINARY | _O_NOINHERIT,
        _SH_DENYNO, _S_IREAD | _S_IWRITE);
    if (error != 0) {
        errno = error;
        return NULL;
    }
    file = _fdopen(descriptor, "wb");
    if (file == NULL) {
        int saved = errno;
        (void)_close(descriptor);
        (void)_wremove(wide);
        errno = saved;
    }
    return file;
}

FILE *dxil_files_read(const char *path) {
    wchar_t wide[DXIL_FILES_PATH_CAP];
    FILE *file = NULL;
    if (wide_path(path, wide) != 0) return NULL;
    errno_t error = _wfopen_s(&file, wide, L"rb");
    if (error != 0) errno = error;
    return file;
}

unsigned long dxil_files_process_id(void) {
    return (unsigned long)GetCurrentProcessId();
}
#else
#include <unistd.h>

int dxil_files_mkdir(const char *path) {
    return mkdir(path, 0700);
}

int dxil_files_rmdir(const char *path) {
    return rmdir(path);
}

int dxil_files_remove(const char *path) {
    return unlink(path);
}

FILE *dxil_files_create(const char *path) {
    FILE *file = NULL;
    int descriptor = open(path, O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (descriptor < 0) return NULL;
    if (fcntl(descriptor, F_SETFD, FD_CLOEXEC) == 0) {
        file = fdopen(descriptor, "wb");
    }
    if (file == NULL) {
        int saved = errno;
        (void)close(descriptor);
        (void)unlink(path);
        errno = saved;
    }
    return file;
}

FILE *dxil_files_read(const char *path) {
    return fopen(path, "rb");
}

unsigned long dxil_files_process_id(void) {
    return (unsigned long)getpid();
}
#endif
