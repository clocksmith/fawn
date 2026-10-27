// SPDX-License-Identifier: MIT
/* Process/argv/CRT descriptor fixture only. This does not implement a browser. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <io.h>
#include <stdint.h>
#include <stdlib.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

static void json_string(const wchar_t *wide)
{
    int bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide, -1, NULL, 0, NULL, NULL);
    char *text = HeapAlloc(GetProcessHeap(), 0, (SIZE_T)bytes);
    if (!bytes || !text || !WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide, -1, text, bytes, NULL, NULL)) ExitProcess(2);
    putchar('"');
    for (int index = 0; index < bytes - 1; ++index) {
        unsigned char byte = (unsigned char)text[index];
        if (byte == '"' || byte == '\\') putchar('\\');
        if (byte < 0x20) printf("\\u%04x", byte);
        else putchar(byte);
    }
    putchar('"');
    HeapFree(GetProcessHeap(), 0, text);
}

static int launch_empty_descriptors(const wchar_t *launcher, const wchar_t *record)
{
    SECURITY_ATTRIBUTES attributes = {sizeof(attributes), NULL, TRUE};
    HANDLE ambient = CreateEventW(&attributes, TRUE, TRUE, NULL);
    if (!ambient) return 10;
    wchar_t command[32767];
    int written = swprintf(command, 32767,
        L"\"%ls\" --use-webgpu-runtime=dawn --probe-check-handle=%llu --probe-record=\"%ls\"",
        launcher, (unsigned long long)(uintptr_t)ambient, record);
    if (written < 0) { CloseHandle(ambient); return 11; }
    STARTUPINFOW startup = {0};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    PROCESS_INFORMATION child = {0};
    if (!CreateProcessW(launcher, command, NULL, NULL, TRUE, CREATE_NO_WINDOW,
                        NULL, NULL, &startup, &child)) { CloseHandle(ambient); return 12; }
    DWORD wait = WaitForSingleObject(child.hProcess, 10000);
    if (wait != WAIT_OBJECT_0) TerminateProcess(child.hProcess, 13);
    DWORD code = 14;
    GetExitCodeProcess(child.hProcess, &code);
    CloseHandle(child.hThread);
    CloseHandle(child.hProcess);
    CloseHandle(ambient);
    return (int)code;
}

/* Exact pinned Dawn flags, exercised with a loadable PE fixture only. This is
 * loader-search evidence, never a real compiler or browser implementation. */
static int dawn_search_borrows_private_image(const wchar_t *private_path, int *loadable)
{
    char filename[32767];
    DWORD size = GetModuleFileNameA(NULL, filename, 32767);
    if (!size || size >= 32767) return -1;
    char *slash = strrchr(filename, '\\');
    if (!slash) return -1;
    slash[1] = 0;
    if (strcat_s(filename, sizeof(filename), "dxcompiler.dll")) return -1;
    const DWORD flags = LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS;
    /* This fixture is one executable image: module and executable dirs agree. */
    const char *candidates[] = {filename, filename, "dxcompiler.dll"};
    int borrowed = 0;
    for (size_t index = 0; index < 3; ++index) {
        HMODULE image = LoadLibraryExA(candidates[index], NULL, flags);
        if (image) { borrowed = 1; FreeLibrary(image); }
    }
    BOOL changed = FALSE;
    if (!WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, private_path, -1,
                            filename, 32767, NULL, &changed) || changed) return -1;
    HMODULE image = LoadLibraryExA(filename, NULL, flags);
    *loadable = image != NULL;
    if (image) FreeLibrary(image);
    return borrowed;
}

int wmain(int argc, wchar_t **argv)
{
    if (argc == 4 && !wcscmp(argv[1], L"--probe-empty-descriptors"))
        return launch_empty_descriptors(argv[2], argv[3]);
    int hold = 0, pipes = 0, exit_code = 0;
    uintptr_t check_handle = 0;
    const wchar_t *record = NULL, *private_image = NULL, *relative_file = NULL;
    for (int index = 1; index < argc; ++index) {
        if (!wcscmp(argv[index], L"--probe-descendant")) { Sleep(INFINITE); return 0; }
        if (!wcsncmp(argv[index], L"--probe-check-handle=", 21))
            check_handle = (uintptr_t)_wcstoui64(argv[index] + 21, NULL, 10);
        if (!wcsncmp(argv[index], L"--probe-record=", 15)) record = argv[index] + 15;
        if (!wcsncmp(argv[index], L"--probe-private-image=", 22)) private_image = argv[index] + 22;
        if (!wcsncmp(argv[index], L"--probe-relative-file=", 22)) relative_file = argv[index] + 22;
        if (!wcscmp(argv[index], L"--probe-hold")) hold = 1;
        if (!wcscmp(argv[index], L"--probe-pipes")) pipes = 1;
        if (!wcsncmp(argv[index], L"--probe-exit=", 13)) exit_code = _wtoi(argv[index] + 13);
    }
    if (record) {
        DWORD flags = 0;
        int inherited = GetHandleInformation((HANDLE)check_handle, &flags) &&
                        (flags & HANDLE_FLAG_INHERIT);
        FILE *file = NULL;
        if (_wfopen_s(&file, record, L"wb") != 0 || !file) return 15;
        fprintf(file, "{\"ambientHandleInherited\":%s}\n", inherited ? "true" : "false");
        fclose(file);
        return inherited ? 16 : 0;
    }
    int borrowed = 0, private_loadable = 0, relative_read = 0;
    if (private_image) {
        borrowed = dawn_search_borrows_private_image(private_image, &private_loadable);
        if (borrowed != 0 || !private_loadable) return 7;
    }
    if (relative_file) {
        FILE *file = NULL;
        char contents[64] = {0};
        if (_wfopen_s(&file, relative_file, L"rb") != 0 || !file) return 8;
        size_t bytes = fread(contents, 1, sizeof(contents) - 1, file);
        fclose(file);
        relative_read = bytes == strlen("fawn relative fixture") && !strcmp(contents, "fawn relative fixture");
        if (!relative_read) return 9;
    }
    DWORD descendant = 0;
    if (hold) {
        wchar_t path[32767], command[32767];
        DWORD size = GetModuleFileNameW(NULL, path, 32767);
        if (!size || size >= 32767 || swprintf(command, 32767, L"\"%ls\" --probe-descendant", path) < 0) return 3;
        STARTUPINFOW startup = {0};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION child = {0};
        if (!CreateProcessW(path, command, NULL, NULL, FALSE, 0, NULL, NULL, &startup, &child)) return 4;
        descendant = child.dwProcessId;
        CloseHandle(child.hThread);
        CloseHandle(child.hProcess);
    }
    wchar_t directory[32767];
    DWORD length = GetCurrentDirectoryW(32767, directory);
    if (!length || length >= 32767) return 6;
    printf("{\"pid\":%lu,\"descendant\":%lu,\"cwd\":", GetCurrentProcessId(), descendant);
    json_string(directory);
    printf(",\"privateImageLoadable\":%s,\"dawnSearchBorrowedPrivateImage\":%s,\"relativeFileRead\":%s,\"args\":[",
           private_loadable ? "true" : "false", borrowed ? "true" : "false", relative_read ? "true" : "false");
    for (int index = 1; index < argc; ++index) {
        if (index > 1) putchar(',');
        json_string(argv[index]);
    }
    puts("]}");
    fflush(stdout);
    if (pipes) {
        unsigned char bytes[4096];
        _setmode(3, _O_BINARY);
        _setmode(4, _O_BINARY);
        int count = _read(3, bytes, sizeof(bytes));
        if (count <= 0 || _write(4, bytes, (unsigned)count) != count) return 5;
    }
    if (hold) Sleep(INFINITE);
    return exit_code;
}
