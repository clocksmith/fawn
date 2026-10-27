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

int wmain(int argc, wchar_t **argv)
{
    if (argc == 4 && !wcscmp(argv[1], L"--probe-empty-descriptors"))
        return launch_empty_descriptors(argv[2], argv[3]);
    int hold = 0, pipes = 0, exit_code = 0;
    uintptr_t check_handle = 0;
    const wchar_t *record = NULL;
    for (int index = 1; index < argc; ++index) {
        if (!wcscmp(argv[index], L"--probe-descendant")) { Sleep(INFINITE); return 0; }
        if (!wcsncmp(argv[index], L"--probe-check-handle=", 21))
            check_handle = (uintptr_t)_wcstoui64(argv[index] + 21, NULL, 10);
        if (!wcsncmp(argv[index], L"--probe-record=", 15)) record = argv[index] + 15;
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
    printf("{\"pid\":%lu,\"descendant\":%lu,\"args\":[", GetCurrentProcessId(), descendant);
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
