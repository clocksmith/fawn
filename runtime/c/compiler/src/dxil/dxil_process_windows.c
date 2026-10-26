/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "dxil_process.h"

#if defined(_WIN32)
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

/* CreateProcessW counts its terminator in the 32767 UTF-16 unit limit. */
#define DXIL_WINDOWS_COMMAND_CAP 32767u

typedef struct DxilWindowsPipe {
    HANDLE read_handle;
    HANDLE write_handle;
    char bytes[DXIL_DXC_MAX_OUTPUT_BYTES + 1];
    size_t length;
} DxilWindowsPipe;

static bool append_unit(WCHAR *command, size_t *length, WCHAR unit) {
    if (*length >= DXIL_WINDOWS_COMMAND_CAP - 1u) {
        return false;
    }
    command[*length] = unit;
    *length += 1;
    command[*length] = L'\0';
    return true;
}

/* Microsoft CRT quoting: double backslashes before quotes and the closing
 * quote. No shell participates, so metacharacters stay ordinary arguments. */
static bool append_argument(WCHAR *command, size_t *length,
    const char *argument) {
    WCHAR wide[DXIL_WINDOWS_COMMAND_CAP];
    size_t i = 0;
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, argument, -1,
        wide, (int)DXIL_WINDOWS_COMMAND_CAP) == 0) {
        return false;
    }
    if (!append_unit(command, length, L'"')) {
        return false;
    }
    while (wide[i] != L'\0') {
        size_t slashes = 0;
        size_t copies = 0;
        while (wide[i] == L'\\') {
            slashes++;
            i++;
        }
        copies = slashes;
        if (wide[i] == L'"' || wide[i] == L'\0') {
            copies *= 2u;
        }
        if (wide[i] == L'"') {
            copies++;
        }
        while (copies > 0) {
            if (!append_unit(command, length, L'\\')) {
                return false;
            }
            copies--;
        }
        if (wide[i] == L'\0') {
            break;
        }
        if (!append_unit(command, length, wide[i])) {
            return false;
        }
        i++;
    }
    return append_unit(command, length, L'"');
}

static void close_handle(HANDLE *handle) {
    if (*handle != NULL && *handle != INVALID_HANDLE_VALUE) {
        CloseHandle(*handle);
    }
    *handle = NULL;
}

static bool open_pipe(DxilWindowsPipe *pipe, SECURITY_ATTRIBUTES *security) {
    return CreatePipe(&pipe->read_handle, &pipe->write_handle, security, 0)
        && SetHandleInformation(pipe->read_handle, HANDLE_FLAG_INHERIT, 0);
}

/* Drain both streams incrementally, including bytes beyond the retained cap.
 * Sequential blocking reads deadlock when the other child pipe fills. */
static DWORD drain_pipe(DxilWindowsPipe *pipe, bool *progress) {
    DWORD available = 0;
    DWORD received = 0;
    char bytes[4096];
    size_t keep = 0;
    if (pipe->read_handle == NULL) {
        return ERROR_SUCCESS;
    }
    if (!PeekNamedPipe(pipe->read_handle, NULL, 0, NULL, &available, NULL)) {
        DWORD error = GetLastError();
        close_handle(&pipe->read_handle);
        return error == ERROR_BROKEN_PIPE ? ERROR_SUCCESS : error;
    }
    if (available == 0) {
        return ERROR_SUCCESS;
    }
    if (!ReadFile(pipe->read_handle, bytes,
        available < sizeof(bytes) ? available : (DWORD)sizeof(bytes),
        &received, NULL)) {
        DWORD error = GetLastError();
        close_handle(&pipe->read_handle);
        return error == ERROR_BROKEN_PIPE ? ERROR_SUCCESS : error;
    }
    keep = (size_t)received;
    if (keep > DXIL_DXC_MAX_OUTPUT_BYTES - pipe->length) {
        keep = DXIL_DXC_MAX_OUTPUT_BYTES - pipe->length;
    }
    memcpy(pipe->bytes + pipe->length, bytes, keep);
    pipe->length += keep;
    pipe->bytes[pipe->length] = '\0';
    *progress = *progress || received != 0;
    return ERROR_SUCCESS;
}

static char *trim_detail(DxilWindowsPipe *pipe) {
    char *start = pipe->bytes;
    while (*start != '\0' && strchr(" \t\r\n", *start) != NULL) {
        start++;
    }
    while (pipe->length > (size_t)(start - pipe->bytes)
        && strchr(" \t\r\n", pipe->bytes[pipe->length - 1]) != NULL) {
        pipe->bytes[--pipe->length] = '\0';
    }
    return start;
}

DxilStatus dxil_dxc_run_options(const char *executable, const char *profile,
    const char *entry_name, const char *output_path,
    const char *input_path, DxilDiscovery discovery, bool native16,
    char *errbuf, size_t errcap) {
    const char *argv[10];
    WCHAR command[DXIL_WINDOWS_COMMAND_CAP] = {0};
    size_t command_length = 0;
    size_t i = 0;
    SECURITY_ATTRIBUTES security = {sizeof(security), NULL, TRUE};
    STARTUPINFOEXW startup = {0};
    PROCESS_INFORMATION process = {0};
    DxilWindowsPipe output = {0};
    DxilWindowsPipe errors = {0};
    HANDLE inherited[3] = {NULL, NULL, NULL};
    HANDLE input = NULL;
    SIZE_T attribute_size = 0;
    bool attributes_initialized = false;
    DWORD error = ERROR_SUCCESS;
    DWORD exit_code = 0;
    DxilStatus status = DXIL_SHADER_TOOLCHAIN_UNAVAILABLE;
    char message[2048];
    dxil_dxc_argv_options(executable, profile, entry_name, output_path,
        input_path, native16, argv);
    for (i = 0; argv[i] != NULL; i++) {
        if ((i != 0 && !append_unit(command, &command_length, L' '))
            || !append_argument(command, &command_length, argv[i])) {
            snprintf(message, sizeof(message),
                "DXC command exceeds the Windows limit or contains invalid UTF-8");
            goto cleanup;
        }
    }
    if (!open_pipe(&output, &security) || !open_pipe(&errors, &security)) {
        error = GetLastError();
        goto launch_error;
    }
    input = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
        &security, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (input == INVALID_HANDLE_VALUE) {
        error = GetLastError();
        goto launch_error;
    }
    startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput = input;
    startup.StartupInfo.hStdOutput = output.write_handle;
    startup.StartupInfo.hStdError = errors.write_handle;
    inherited[0] = input;
    inherited[1] = output.write_handle;
    inherited[2] = errors.write_handle;
    /* The Windows process adapter owns this transient OS attribute allocation;
     * no compiler workspace, input or result storage is retained or replaced. */
    InitializeProcThreadAttributeList(NULL, 1, 0, &attribute_size);
    startup.lpAttributeList = HeapAlloc(GetProcessHeap(), 0, attribute_size);
    if (startup.lpAttributeList == NULL) {
        error = ERROR_NOT_ENOUGH_MEMORY;
        goto launch_error;
    }
    if (!InitializeProcThreadAttributeList(startup.lpAttributeList, 1, 0,
        &attribute_size)) {
        error = GetLastError();
        goto launch_error;
    }
    attributes_initialized = true;
    if (!UpdateProcThreadAttribute(startup.lpAttributeList, 0,
        PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited, sizeof(inherited),
        NULL, NULL)
        || !CreateProcessW(NULL, command, NULL, NULL, TRUE,
            EXTENDED_STARTUPINFO_PRESENT | CREATE_NO_WINDOW, NULL, NULL,
            &startup.StartupInfo, &process)) {
        error = GetLastError();
        goto launch_error;
    }
    close_handle(&output.write_handle);
    close_handle(&errors.write_handle);
    close_handle(&input);
    close_handle(&process.hThread);
    while (output.read_handle != NULL || errors.read_handle != NULL) {
        bool progress = false;
        DWORD output_error = drain_pipe(&output, &progress);
        DWORD stderr_error = drain_pipe(&errors, &progress);
        if (error == ERROR_SUCCESS) {
            error = output_error != ERROR_SUCCESS ? output_error : stderr_error;
        }
        if (!progress) {
            Sleep(1);
        }
    }
    if (WaitForSingleObject(process.hProcess, INFINITE) != WAIT_OBJECT_0
        || !GetExitCodeProcess(process.hProcess, &exit_code)) {
        error = GetLastError();
    }
    if (error != ERROR_SUCCESS) {
        snprintf(message, sizeof(message),
            "failed to collect DXC via %s `%s`: Windows error %lu",
            dxil_discovery_label(discovery), executable, (unsigned long)error);
        status = DXIL_INVALID_IR;
    } else if (exit_code != 0) {
        char *detail = trim_detail(&errors);
        if (*detail == '\0') {
            detail = trim_detail(&output);
        }
        snprintf(message, sizeof(message),
            "DXC failed via %s `%s` with exit code %lu for profile `%s` "
            "and entry `%s`%s%.511s", dxil_discovery_label(discovery),
            executable, (unsigned long)exit_code, profile, entry_name,
            *detail == '\0' ? "" : ": ", detail);
        status = DXIL_INVALID_IR;
    } else {
        status = DXIL_OK;
    }
    goto cleanup;

launch_error:
    if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) {
        dxil_dxc_missing_message(discovery, executable, message, sizeof(message));
    } else {
        snprintf(message, sizeof(message),
            "failed to start DXC via %s `%s`: Windows error %lu",
            dxil_discovery_label(discovery), executable, (unsigned long)error);
    }
cleanup:
    close_handle(&output.read_handle);
    close_handle(&output.write_handle);
    close_handle(&errors.read_handle);
    close_handle(&errors.write_handle);
    close_handle(&input);
    close_handle(&process.hThread);
    close_handle(&process.hProcess);
    if (attributes_initialized) {
        DeleteProcThreadAttributeList(startup.lpAttributeList);
    }
    if (startup.lpAttributeList != NULL) {
        HeapFree(GetProcessHeap(), 0, startup.lpAttributeList);
    }
    if (status != DXIL_OK && errbuf != NULL && errcap != 0) {
        snprintf(errbuf, errcap, "%s", message);
    }
    return status;
}
#endif
