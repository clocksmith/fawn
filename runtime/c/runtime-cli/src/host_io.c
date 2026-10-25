/* SPDX-License-Identifier: MIT */
#include "../../platform/include/doe_platform.h"
#include "host_io.h"
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>

static wchar_t *wide_path(const char *path)
{
    int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, NULL, 0);
    if (!size || (size_t)size > SIZE_MAX / sizeof(wchar_t)) return NULL;
    wchar_t *wide = malloc((size_t)size * sizeof(*wide));
    if (wide && !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, wide, size)) {
        free(wide);
        wide = NULL;
    }
    return wide;
}

/* Windows CRT tmpfile requires the drive root. Use the user's temporary
 * directory and transfer one delete-on-close HANDLE into one CRT stream.
 * https://learn.microsoft.com/en-us/cpp/c-runtime-library/reference/open-osfhandle */
static FILE *temporary_output(void)
{
    wchar_t directory[MAX_PATH + 1], path[MAX_PATH + 1];
    DWORD length = GetTempPathW(MAX_PATH + 1, directory);
    if (!length || length > MAX_PATH || !GetTempFileNameW(directory, L"doe", 0, path)) return NULL;
    HANDLE handle = CreateFileW(path, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING,
        FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, NULL);
    if (handle == INVALID_HANDLE_VALUE) { DeleteFileW(path); return NULL; }
    int descriptor = _open_osfhandle((intptr_t)handle, _O_RDWR | _O_BINARY);
    if (descriptor < 0) { CloseHandle(handle); return NULL; }
    FILE *file = _fdopen(descriptor, "w+b");
    if (!file) _close(descriptor);
    return file;
}
#endif

FILE *doe_host_output_open(doe_host_output *output)
{
    *output = (doe_host_output){0};
#ifdef _WIN32
    output->file = temporary_output();
#elif defined(DOE_HOST_FILE_OUTPUT)
    output->file = tmpfile();
#else
    output->file = open_memstream(&output->bytes, &output->size);
#endif
    return output->file;
}

bool doe_host_output_close(doe_host_output *output, char **bytes, size_t *size)
{
    if (!output->file) return false;
    bool ok = !ferror(output->file) && fflush(output->file) == 0;
#if defined(_WIN32) || defined(DOE_HOST_FILE_OUTPUT)
#ifdef _WIN32
    int64_t length = ok ? _ftelli64(output->file) : -1;
    if (length < 0 || (uint64_t)length > SIZE_MAX - 1 || _fseeki64(output->file, 0, SEEK_SET)) ok = false;
#else
    int64_t length = ok ? (int64_t)ftello(output->file) : -1;
    if (length < 0 || (uint64_t)length > SIZE_MAX - 1 || fseeko(output->file, 0, SEEK_SET)) ok = false;
#endif
    if (ok) {
        output->size = (size_t)length;
        output->bytes = malloc(output->size + 1);
        if (!output->bytes || fread(output->bytes, 1, output->size, output->file) != output->size) ok = false;
        if (ok) output->bytes[output->size] = '\0';
    }
#endif
    if (fclose(output->file)) ok = false;
    output->file = NULL;
    if (ok) {
        *bytes = output->bytes;
        *size = output->size;
    } else free(output->bytes);
    *output = (doe_host_output){0};
    return ok;
}

void doe_host_output_discard(doe_host_output *output)
{
    if (output->file) fclose(output->file);
    free(output->bytes);
    *output = (doe_host_output){0};
}

FILE *doe_host_fopen(const char *path, const char *mode)
{
#ifdef _WIN32
    wchar_t *wide = wide_path(path), wide_mode[8];
    size_t size = strlen(mode);
    if (!wide || size >= sizeof(wide_mode) / sizeof(*wide_mode)) { free(wide); return NULL; }
    for (size_t i = 0; i <= size; i++) wide_mode[i] = (wchar_t)(unsigned char)mode[i];
    FILE *file = _wfopen(wide, wide_mode);
    free(wide);
    return file;
#else
    return fopen(path, mode);
#endif
}

char *doe_host_strdup(const char *text)
{
    size_t size = strlen(text) + 1;
    char *copy = malloc(size);
    if (copy) memcpy(copy, text, size);
    return copy;
}

int doe_host_casecmp(const char *left, const char *right)
{
    for (;;) {
        unsigned a = (unsigned char)*left++, b = (unsigned char)*right++;
        if (a >= 'A' && a <= 'Z') a += 'a' - 'A';
        if (b >= 'A' && b <= 'Z') b += 'a' - 'A';
        if (a != b || !a) return a < b ? -1 : a > b;
    }
}

uint64_t doe_host_monotonic_ns(void)
{
    struct timespec now;
    if (doe_monotonic_now(&now).status != DOE_HOST_OK) return 0;
    return (uint64_t)now.tv_sec * UINT64_C(1000000000) + (uint64_t)now.tv_nsec;
}

uint64_t doe_host_realtime_ns(void)
{
#ifdef _WIN32
    FILETIME now;
    GetSystemTimePreciseAsFileTime(&now);
    uint64_t ticks = (uint64_t)now.dwHighDateTime << 32 | now.dwLowDateTime;
    return (ticks - UINT64_C(116444736000000000)) * 100;
#else
    struct timespec now;
    if (clock_gettime(CLOCK_REALTIME, &now)) return 0;
    return (uint64_t)now.tv_sec * UINT64_C(1000000000) + (uint64_t)now.tv_nsec;
#endif
}

void doe_host_pause_ns(uint64_t duration)
{
#ifdef _WIN32
    uint64_t ms = duration / 1000000 + (duration % 1000000 != 0);
    while (ms > UINT32_MAX - 1) {
        Sleep(UINT32_MAX - 1);
        ms -= UINT32_MAX - 1;
    }
    Sleep((DWORD)ms);
#else
    struct timespec pause = {.tv_sec = (time_t)(duration / 1000000000), .tv_nsec = (long)(duration % 1000000000)};
    nanosleep(&pause, NULL);
#endif
}

bool doe_host_regular_file(const char *path)
{
#ifdef _WIN32
    wchar_t *wide = wide_path(path);
    if (!wide) return false;
    struct _stat64 status;
    bool regular = !_wstat64(wide, &status) && (status.st_mode & _S_IFMT) == _S_IFREG;
    free(wide);
    return regular;
#else
    struct stat status;
    return !stat(path, &status) && S_ISREG(status.st_mode);
#endif
}

bool doe_host_path_separator(char value)
{
#ifdef _WIN32
    return value == '/' || value == '\\';
#else
    return value == '/';
#endif
}

bool doe_host_make_parent_directories(const char *path)
{
    char *parent = doe_host_strdup(path);
    if (!parent) return false;
    size_t start = 0;
#ifdef _WIN32
    if (parent[0] && parent[1] == ':') start = 2;
    else if (doe_host_path_separator(parent[0]) && doe_host_path_separator(parent[1])) {
        start = 2;
        for (unsigned part = 0; part < 2; part++) {
            while (parent[start] && !doe_host_path_separator(parent[start])) start++;
            if (parent[start]) start++;
        }
    }
#endif
    while (doe_host_path_separator(parent[start])) start++;
    bool ok = true;
    for (size_t i = start; parent[i] && ok; i++) {
        if (!doe_host_path_separator(parent[i])) continue;
        char saved = parent[i];
        parent[i] = 0;
#ifdef _WIN32
        wchar_t *wide = wide_path(parent);
        if (!wide) ok = false;
        else if (!CreateDirectoryW(wide, NULL)) {
            DWORD attributes = GetFileAttributesW(wide);
            ok = attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY);
        }
        free(wide);
#else
        if (mkdir(parent, 0777) && errno != EEXIST) ok = false;
#endif
        parent[i] = saved;
    }
    free(parent);
    return ok;
}

const char *doe_host_basename(const char *path)
{
    const char *base = path;
    for (const char *p = path; *p; p++) if (doe_host_path_separator(*p)) base = p + 1;
    return base;
}
