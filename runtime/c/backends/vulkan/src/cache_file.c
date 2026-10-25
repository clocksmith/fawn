/* SPDX-License-Identifier: MIT */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include "cache_file.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <direct.h>
#include <io.h>
#include <process.h>
static wchar_t *wide(const char *path) {
    int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, NULL, 0);
    if (!count || (size_t)count > SIZE_MAX / sizeof(wchar_t)) return NULL;
    wchar_t *out = malloc((size_t)count * sizeof(*out));
    if (out && !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, out, count)) { free(out); return NULL; }
    return out;
}
static FILE *open_file(const char *path, const wchar_t *mode) {
    wchar_t *name = wide(path); FILE *file = name ? _wfopen(name, mode) : NULL; free(name); return file;
}
static bool directory_one(const char *path) {
    wchar_t *name = wide(path); if (!name) return false;
    int made = _wmkdir(name); struct _stat state;
    bool ok = made == 0 || (errno == EEXIST && _wstat(name, &state) == 0 && (state.st_mode & _S_IFDIR));
    free(name); return ok;
}
#else
#include <unistd.h>
static bool directory_one(const char *path) {
    struct stat state;
    return mkdir(path, 0700) == 0 || (errno == EEXIST && stat(path, &state) == 0 && S_ISDIR(state.st_mode));
}
#endif
bool doe_vk_cache_directory(const char *directory) {
    size_t length = strlen(directory);
    char *copy = malloc(length + 1); if (!copy) return false;
    memcpy(copy, directory, length + 1);
    bool ok = true;
    for (size_t i = 1; i < length && ok; ++i) {
        if (copy[i] != '/'
#if defined(_WIN32)
            && copy[i] != '\\'
#endif
        ) continue;
#if defined(_WIN32)
        if (i == 2 && copy[1] == ':') continue;
#endif
        char saved = copy[i]; copy[i] = 0; ok = directory_one(copy); copy[i] = saved;
    }
    if (ok) ok = directory_one(copy);
    free(copy); return ok;
}
bool doe_vk_cache_read(const char *path, uint8_t **bytes, size_t *size) {
#if defined(_WIN32)
    FILE *file = open_file(path, L"rb");
#else
    FILE *file = fopen(path, "rb");
#endif
    if (!file) return false;
    bool ok = fseek(file, 0, SEEK_END) == 0;
    long length = ok ? ftell(file) : -1;
    ok = length >= 0 && (unsigned long)length <= DOE_VK_CACHE_MAX_BYTES && fseek(file, 0, SEEK_SET) == 0;
    uint8_t *data = ok ? malloc((size_t)length + 1) : NULL;
    ok = data && fread(data, 1, (size_t)length, file) == (size_t)length && fgetc(file) == EOF && !ferror(file);
    if (fclose(file) != 0) ok = false;
    if (!ok) { free(data); return false; }
    *bytes = data; *size = (size_t)length; return true;
}
bool doe_vk_cache_replace(const char *path, const uint8_t *bytes, size_t size) {
    size_t length = strlen(path);
    if (length > SIZE_MAX - 64 || size > DOE_VK_CACHE_MAX_BYTES) return false;
    char *temporary = malloc(length + 64); if (!temporary) return false;
    FILE *file = NULL;
#if defined(_WIN32)
    unsigned long pid = (unsigned long)_getpid();
#else
    unsigned long pid = (unsigned long)getpid();
#endif
    for (unsigned i = 0; i < 128 && !file; ++i) {
        snprintf(temporary, length + 64, "%s.tmp.%lu.%u", path, pid, i);
#if defined(_WIN32)
        file = open_file(temporary, L"wbx");
#else
        file = fopen(temporary, "wbx");
#endif
        if (!file && errno != EEXIST) break;
    }
    if (!file) { free(temporary); return false; }
    bool ok = fwrite(bytes, 1, size, file) == size && fflush(file) == 0;
#if defined(_WIN32)
    if (ok) ok = _commit(_fileno(file)) == 0;
#else
    if (ok) ok = fsync(fileno(file)) == 0;
#endif
    if (fclose(file) != 0) ok = false;
#if defined(_WIN32)
    wchar_t *from = wide(temporary), *to = wide(path);
    if (ok) ok = from && to && MoveFileExW(from, to, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    if (!ok && from) (void)_wremove(from);
    free(from); free(to);
#else
    if (ok) ok = rename(temporary, path) == 0;
    if (!ok) (void)remove(temporary);
#endif
    free(temporary); return ok;
}
