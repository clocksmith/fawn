/* SPDX-License-Identifier: MIT */
#include "doe_napi_library.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

enum { DOE_LIBRARY_MAX_WIDE = 32768 };
static void *failed(doe_library_error *error,const char *operation,DWORD code)
{
    if (error) {
        error->native_code = code;
        error->operation = operation;
        int prefix = snprintf(error->message,sizeof(error->message),"%s failed (Win32 %lu)",operation,(unsigned long)code);
        WCHAR system[256];
        DWORD count = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_IGNORE_INSERTS|
            FORMAT_MESSAGE_MAX_WIDTH_MASK,NULL,code,0,system,(DWORD)(sizeof(system)/sizeof(*system)),NULL);
        int bytes = count ? WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,system,(int)count,NULL,0,NULL,NULL) : 0;
        if (prefix > 0 && (size_t)prefix + 2 < sizeof(error->message) && bytes > 0 &&
            (size_t)bytes < sizeof(error->message) - (size_t)prefix - 2) {
            error->message[prefix++] = ':';
            error->message[prefix++] = ' ';
            int written = WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,system,(int)count,
                error->message + prefix,bytes,NULL,NULL);
            error->message[prefix + (written > 0 ? written : 0)] = 0;
        }
    }
    SetLastError(code);
    return NULL;
}

void *doe_library_open(const char *path,size_t length,doe_library_error *error)
{
    if (!path || !length || length > INT_MAX || memchr(path,0,length))
        return failed(error,"provider.path",ERROR_INVALID_PARAMETER);
    int units = MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,path,(int)length,NULL,0);
    if (!units) return failed(error,"MultiByteToWideChar.provider",GetLastError());
    if (units >= DOE_LIBRARY_MAX_WIDE) return failed(error,"provider.path.length",ERROR_FILENAME_EXCED_RANGE);
    WCHAR *wide = malloc(((size_t)units + 1) * sizeof(*wide));
    if (!wide) return failed(error,"provider.path.allocate",ERROR_NOT_ENOUGH_MEMORY);
    int converted = MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,path,(int)length,wide,units);
    if (converted != units) {
        DWORD code = converted ? ERROR_INSUFFICIENT_BUFFER : GetLastError();
        free(wide);
        return failed(error,"MultiByteToWideChar.provider",code);
    }
    wide[units] = 0;
    /* One bounded resolution snapshots relative-path meaning once. A separate
     * size query could observe a different process CWD before the fill call. */
    WCHAR *absolute = malloc((size_t)DOE_LIBRARY_MAX_WIDE * sizeof(*absolute));
    if (!absolute) {
        free(wide);
        return failed(error,"provider.absolute.allocate",ERROR_NOT_ENOUGH_MEMORY);
    }
    DWORD written = GetFullPathNameW(wide,DOE_LIBRARY_MAX_WIDE,absolute,NULL);
    if (!written || written >= DOE_LIBRARY_MAX_WIDE) {
        DWORD code = written ? ERROR_INSUFFICIENT_BUFFER : GetLastError();
        free(absolute);
        free(wide);
        return failed(error,"GetFullPathNameW.provider",code);
    }
    /* GetFullPathName normalizes ordinary forward slashes. Extended paths must
     * already use Win32 namespace spelling and remain explicit caller input. */
    HMODULE library = LoadLibraryExW(absolute,NULL,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
    DWORD code = library ? ERROR_SUCCESS : GetLastError();
    free(absolute);
    free(wide);
    if (!library) return failed(error,"LoadLibraryExW.provider",code);
    return library;
}
#else
#include <dlfcn.h>
void *doe_library_open(const char *path,size_t length,doe_library_error *error)
{
    if (!path || !length || length == SIZE_MAX || memchr(path,0,length)) {
        if (error) {
            error->native_code = 0;
            error->operation = "provider.path";
            snprintf(error->message,sizeof(error->message),"provider.path requires nonempty NUL-free bytes");
        }
        return NULL;
    }
    char *terminated = malloc(length + 1);
    if (!terminated) {
        if (error) {
            error->native_code = 0;
            error->operation = "provider.path.allocate";
            snprintf(error->message,sizeof(error->message),"provider.path allocation failed");
        }
        return NULL;
    }
    memcpy(terminated,path,length);
    terminated[length] = 0;
    void *library = dlopen(terminated,RTLD_NOW|RTLD_LOCAL);
    if (!library && error) {
        const char *diagnostic = dlerror();
        error->native_code = 0;
        error->operation = "dlopen.provider";
        snprintf(error->message,sizeof(error->message),"dlopen.provider failed: %s",diagnostic ? diagnostic : "unknown loader error");
    }
    free(terminated);
    return library;
}
#endif
