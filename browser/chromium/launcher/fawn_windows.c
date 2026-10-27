// SPDX-License-Identifier: MIT
/* Relocatable C-default product entrypoint. Chrome remains the observed child image. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#define COMMAND_CAPACITY 32767
#define MAX_INHERITED_HANDLES 256

typedef struct {
    wchar_t text[COMMAND_CAPACITY];
    size_t used;
} Command;

typedef struct {
    STARTUPINFOEXW startup;
    HANDLE handles[MAX_INHERITED_HANDLES];
    size_t count;
    unsigned char *crt;
    int attributes_ready;
} Inheritance;

static int append(Command *command, wchar_t value)
{
    if (command->used + 1 >= COMMAND_CAPACITY) return 0;
    command->text[command->used++] = value;
    command->text[command->used] = L'\0';
    return 1;
}

/* Quote every argument with the documented Windows CRT backslash/quote rule. */
static int argument(Command *command, const wchar_t *text)
{
    if (command->used && !append(command, L' ')) return 0;
    if (!append(command, L'"')) return 0;
    for (;;) {
        size_t slashes = 0;
        while (*text == L'\\') { ++slashes; ++text; }
        size_t copies = slashes;
        if (*text == L'"' || *text == L'\0') copies *= 2;
        while (copies--) if (!append(command, L'\\')) return 0;
        if (*text == L'\0') break;
        if (*text == L'"' && !append(command, L'\\')) return 0;
        if (!append(command, *text++)) return 0;
    }
    return append(command, L'"');
}

static int sibling(wchar_t *target, const wchar_t *root, const wchar_t *name)
{
    return swprintf(target, COMMAND_CAPACITY, L"%ls%ls", root, name) > 0;
}

static int file_exists(const wchar_t *file, DWORD *error)
{
    DWORD attributes = GetFileAttributesW(file);
    if (attributes == INVALID_FILE_ATTRIBUTES) { *error = GetLastError(); return 0; }
    if (attributes & FILE_ATTRIBUTE_DIRECTORY) { *error = ERROR_DIRECTORY; return 0; }
    return 1;
}

static HANDLE duplicate(Inheritance *inheritance, HANDLE original, DWORD *error)
{
    if (original == NULL || original == INVALID_HANDLE_VALUE) return original;
    if (inheritance->count == MAX_INHERITED_HANDLES) { *error = ERROR_TOO_MANY_OPEN_FILES; return INVALID_HANDLE_VALUE; }
    HANDLE copy = NULL;
    if (!DuplicateHandle(GetCurrentProcess(), original, GetCurrentProcess(), &copy,
                         0, TRUE, DUPLICATE_SAME_ACCESS)) { *error = GetLastError(); return INVALID_HANDLE_VALUE; }
    inheritance->handles[inheritance->count++] = copy;
    return copy;
}

static void release_inheritance(Inheritance *inheritance)
{
    if (inheritance->startup.lpAttributeList) {
        if (inheritance->attributes_ready)
            DeleteProcThreadAttributeList(inheritance->startup.lpAttributeList);
        HeapFree(GetProcessHeap(), 0, inheritance->startup.lpAttributeList);
    }
    for (size_t index = 0; index < inheritance->count; ++index)
        CloseHandle(inheritance->handles[index]);
    if (inheritance->crt) HeapFree(GetProcessHeap(), 0, inheritance->crt);
}

/* Node/libuv passes CDP descriptors 3/4 in the CRT reserved table, not stdin/out.
 * Duplicate only explicit startup descriptors and restrict CreateProcess's handle list. */
static int prepare_inheritance(Inheritance *inheritance, DWORD *error)
{
    STARTUPINFOW parent = {0};
    parent.cb = sizeof(parent);
    GetStartupInfoW(&parent);
    inheritance->startup.StartupInfo.cb = sizeof(STARTUPINFOEXW);
    inheritance->startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    HANDLE *standards[] = {&inheritance->startup.StartupInfo.hStdInput,
                          &inheritance->startup.StartupInfo.hStdOutput,
                          &inheritance->startup.StartupInfo.hStdError};
    DWORD identifiers[] = {STD_INPUT_HANDLE, STD_OUTPUT_HANDLE, STD_ERROR_HANDLE};
    for (size_t index = 0; index < 3; ++index) {
        HANDLE original = GetStdHandle(identifiers[index]);
        *standards[index] = duplicate(inheritance, original, error);
        if (original != NULL && original != INVALID_HANDLE_VALUE &&
            *standards[index] == INVALID_HANDLE_VALUE) return 0;
    }
    if (parent.cbReserved2) {
        if (!parent.lpReserved2 || parent.cbReserved2 < sizeof(int)) { *error = ERROR_INVALID_DATA; return 0; }
        int count = 0;
        memcpy(&count, parent.lpReserved2, sizeof(count));
        if (count < 0 || count > MAX_INHERITED_HANDLES - 3) { *error = ERROR_INVALID_DATA; return 0; }
        size_t required = sizeof(int) + (size_t)count * (sizeof(unsigned char) + sizeof(intptr_t));
        if (required > parent.cbReserved2) { *error = ERROR_INVALID_DATA; return 0; }
        inheritance->crt = HeapAlloc(GetProcessHeap(), 0, parent.cbReserved2);
        if (!inheritance->crt) { *error = ERROR_NOT_ENOUGH_MEMORY; return 0; }
        memcpy(inheritance->crt, parent.lpReserved2, parent.cbReserved2);
        unsigned char *descriptors = inheritance->crt + sizeof(int) + count;
        for (int index = 0; index < count; ++index) {
            intptr_t raw = 0;
            memcpy(&raw, descriptors + (size_t)index * sizeof(raw), sizeof(raw));
            if (raw == -1 || raw == -2) continue;
            HANDLE copy = duplicate(inheritance, (HANDLE)raw, error);
            if (copy == INVALID_HANDLE_VALUE) return 0;
            raw = (intptr_t)copy;
            memcpy(descriptors + (size_t)index * sizeof(raw), &raw, sizeof(raw));
        }
        inheritance->startup.StartupInfo.cbReserved2 = parent.cbReserved2;
        inheritance->startup.StartupInfo.lpReserved2 = inheritance->crt;
    }
    if (!inheritance->count) return 1;
    SIZE_T bytes = 0;
    InitializeProcThreadAttributeList(NULL, 1, 0, &bytes);
    inheritance->startup.lpAttributeList = HeapAlloc(GetProcessHeap(), 0, bytes);
    if (!inheritance->startup.lpAttributeList) { *error = ERROR_NOT_ENOUGH_MEMORY; return 0; }
    if (!InitializeProcThreadAttributeList(inheritance->startup.lpAttributeList, 1, 0, &bytes)) {
        *error = GetLastError();
        return 0;
    }
    inheritance->attributes_ready = 1;
    if (!UpdateProcThreadAttribute(inheritance->startup.lpAttributeList, 0,
        PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inheritance->handles,
        inheritance->count * sizeof(HANDLE), NULL, NULL)) {
        *error = GetLastError();
        return 0;
    }
    return 1;
}

static void report_failure(const wchar_t *reason, const wchar_t *path, DWORD error)
{
    wchar_t message[2048];
    int written = swprintf(message, 2048, L"Fawn: %ls. Windows error %lu.%ls%ls",
                           reason, error, path ? L"\n" : L"", path ? path : L"");
    if (written < 0) wcscpy_s(message, 2048, L"Fawn could not start its private browser/runtime.");
    HANDLE diagnostic = GetStdHandle(STD_ERROR_HANDLE);
    if (diagnostic && diagnostic != INVALID_HANDLE_VALUE) fwprintf(stderr, L"%ls\n", message);
    else MessageBoxW(NULL, message, L"Fawn could not start", MB_OK | MB_ICONERROR);
}

/* Match the pinned Chromium Windows CommandLine grammar for protected keys:
 * base/command_line.cc and base/strings/whitespace_constants.h. */
typedef struct {
    const wchar_t *key;
    size_t key_length;
    const wchar_t *value;
    size_t value_length;
    int terminator;
} LauncherSwitch;

static int chromium_space(wchar_t value)
{
    return (value >= 0x09 && value <= 0x0D) || value == 0x20 || value == 0x85 ||
           value == 0xA0 || value == 0x1680 || (value >= 0x2000 && value <= 0x200A) ||
           value == 0x2028 || value == 0x2029 || value == 0x202F ||
           value == 0x205F || value == 0x3000;
}

static LauncherSwitch parse_switch(const wchar_t *argument)
{
    LauncherSwitch option = {0};
    size_t length = wcslen(argument);
    while (length && chromium_space(*argument)) { ++argument; --length; }
    while (length && chromium_space(argument[length - 1])) --length;
    size_t prefix = 0;
    if (length >= 2 && argument[0] == L'-' && argument[1] == L'-') prefix = 2;
    else if (length && (argument[0] == L'-' || argument[0] == L'/')) prefix = 1;
    if (length == 2 && prefix == 2) { option.terminator = 1; return option; }
    if (!prefix || length == prefix) return option;
    option.key = argument + prefix;
    option.key_length = length - prefix;
    for (size_t index = 0; index < option.key_length; ++index) {
        if (option.key[index] != L'=') continue;
        option.value = option.key + index + 1;
        option.value_length = option.key_length - index - 1;
        option.key_length = index;
        break;
    }
    return option;
}

static int switch_key(LauncherSwitch option, const wchar_t *key)
{
    if (!option.key || option.key_length != wcslen(key)) return 0;
    for (size_t index = 0; index < option.key_length; ++index) {
        wchar_t value = option.key[index];
        if (value >= L'A' && value <= L'Z') value += L'a' - L'A';
        if (value != key[index]) return 0;
    }
    return 1;
}

static int switch_value(LauncherSwitch option, const wchar_t *value)
{
    return option.value && option.value_length == wcslen(value) &&
           wmemcmp(option.value, value, option.value_length) == 0;
}

static int launch(int count, wchar_t **args)
{
    int status = 1;
    const wchar_t *failure = L"Launcher allocation failed";
    const wchar_t *failure_path = NULL;
    DWORD error = ERROR_NOT_ENOUGH_MEMORY;
    wchar_t *root = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                              COMMAND_CAPACITY * sizeof(wchar_t));
    wchar_t *path = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                              COMMAND_CAPACITY * sizeof(wchar_t));
    Command *command = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(Command));
    Inheritance inheritance = {0};
    HANDLE job = NULL;
    PROCESS_INFORMATION child = {0};
    int dawn = 0, has_runtime = 0, has_profile = 0;
    if (!root || !path || !command) goto cleanup;
    failure = L"Cannot resolve launcher directory";
    DWORD length = GetModuleFileNameW(NULL, root, COMMAND_CAPACITY);
    if (!length || length >= COMMAND_CAPACITY) { error = GetLastError(); goto cleanup; }
    wchar_t *slash = wcsrchr(root, L'\\');
    if (!slash) { error = ERROR_BAD_PATHNAME; goto cleanup; }
    slash[1] = L'\0';
    failure = L"Private chrome.exe missing, or its path exceeds the command limit";
    failure_path = path;
    if (!sibling(path, root, L"chrome.exe") || !file_exists(path, &error) || !argument(command, path)) goto cleanup;
    failure_path = NULL;
    failure = L"Conflicting or unsupported runtime/profile argument";
    error = ERROR_INVALID_PARAMETER;
    for (int index = 1; index < count; ++index) {
        LauncherSwitch option = parse_switch(args[index]);
        if (option.terminator) break;
        if (switch_key(option, L"single-argument")) {
            failure = L"Windows --single-argument cannot cross this launcher; pass literal URLs after -- instead";
            goto cleanup;
        }
        if (switch_key(option, L"remote-debugging-io-pipes")) {
            failure = L"Numeric IO pipe handles cannot cross this launcher; use CRT descriptors 3/4 with --remote-debugging-pipe";
            goto cleanup;
        }
        if (switch_key(option, L"use-webgpu-runtime")) {
            if (has_runtime++) goto cleanup;
            if (switch_value(option, L"dawn")) dawn = 1;
            else if (!switch_value(option, L"doe")) goto cleanup;
        } else if (switch_key(option, L"doe-webgpu-library-path") ||
                   switch_key(option, L"no-sandbox")) goto cleanup;
        if (switch_key(option, L"user-data-dir") && (!option.value_length || has_profile++))
            goto cleanup;
    }
    failure = L"Command line exceeds the Windows limit";
    error = ERROR_BUFFER_OVERFLOW;
    if (!dawn) {
        const wchar_t *files[] = {L"lib\\doe\\webgpu_doe_c.dll", L"lib\\doe\\doe_compiler.dll", L"lib\\doe\\dxcompiler.dll", L"lib\\doe\\dxil.dll"};
        for (size_t index = 0; index < 4; ++index) {
            if (!sibling(path, root, files[index]) || !file_exists(path, &error)) {
                failure = L"Required private C runtime dependency missing or invalid";
                failure_path = path;
                goto cleanup;
            }
        }
        if (!argument(command, L"--use-webgpu-runtime=doe")) goto cleanup;
        if (swprintf(path, COMMAND_CAPACITY, L"--doe-webgpu-library-path=%lslib\\doe\\webgpu_doe_c.dll", root) <= 0 ||
            !argument(command, path)) goto cleanup;
    } else if (!argument(command, L"--use-webgpu-runtime=dawn")) goto cleanup;
    if (!has_profile) {
        DWORD size = GetEnvironmentVariableW(L"LOCALAPPDATA", path, COMMAND_CAPACITY);
        if (!size || size >= COMMAND_CAPACITY) {
            failure = L"LOCALAPPDATA is unavailable or too long for the default profile";
            error = GetLastError();
            goto cleanup;
        }
        wchar_t *profile = HeapAlloc(GetProcessHeap(), 0, COMMAND_CAPACITY * sizeof(wchar_t));
        if (!profile) {
            failure = L"Default profile allocation failed";
            error = ERROR_NOT_ENOUGH_MEMORY;
            goto cleanup;
        }
        int written = swprintf(profile, COMMAND_CAPACITY, L"--user-data-dir=%ls\\Fawn\\%ls", path, dawn ? L"Dawn User Data" : L"Doe User Data");
        int added = written > 0 && argument(command, profile);
        HeapFree(GetProcessHeap(), 0, profile);
        if (!added) goto cleanup;
    }
    int literal_arguments = 0;
    for (int index = 1; index < count; ++index) {
        LauncherSwitch option = parse_switch(args[index]);
        if (option.terminator) literal_arguments = 1;
        if (!literal_arguments && switch_key(option, L"use-webgpu-runtime")) continue;
        if (!argument(command, args[index])) goto cleanup;
    }
    failure = L"Cannot transfer inherited standard/CDP pipe descriptors";
    if (!prepare_inheritance(&inheritance, &error)) goto cleanup;
    failure = L"Cannot create owned browser Job";
    job = CreateJobObjectW(NULL, NULL);
    if (!job) { error = GetLastError(); goto cleanup; }
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits = {0};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
        error = GetLastError();
        goto cleanup;
    }
    if (!sibling(path, root, L"chrome.exe")) goto cleanup;
    failure = L"Cannot create private browser process";
    if (!CreateProcessW(path, command->text, NULL, NULL, inheritance.count != 0,
        CREATE_SUSPENDED | EXTENDED_STARTUPINFO_PRESENT, NULL, NULL,
        &inheritance.startup.StartupInfo, &child)) { error = GetLastError(); goto cleanup; }
    if (!AssignProcessToJobObject(job, child.hProcess) || ResumeThread(child.hThread) == (DWORD)-1) {
        failure = L"Cannot assign/resume the owned browser process";
        error = GetLastError();
        TerminateProcess(child.hProcess, 1);
        WaitForSingleObject(child.hProcess, INFINITE);
        goto cleanup;
    }
    if (WaitForSingleObject(child.hProcess, INFINITE) != WAIT_OBJECT_0) {
        failure = L"Cannot wait for owned browser completion";
        error = GetLastError();
        goto cleanup;
    }
    DWORD exit_code = 1;
    if (!GetExitCodeProcess(child.hProcess, &exit_code)) {
        failure = L"Cannot read browser exit status";
        error = GetLastError();
        goto cleanup;
    }
    failure = NULL;
    status = (int)exit_code;
cleanup:
    if (child.hThread) CloseHandle(child.hThread);
    if (child.hProcess) CloseHandle(child.hProcess);
    if (job) CloseHandle(job);
    release_inheritance(&inheritance);
    if (failure) report_failure(failure, failure_path, error);
    if (command) HeapFree(GetProcessHeap(), 0, command);
    if (path) HeapFree(GetProcessHeap(), 0, path);
    if (root) HeapFree(GetProcessHeap(), 0, root);
    return status;
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous, PWSTR command, int show)
{
    (void)instance; (void)previous; (void)command; (void)show;
    int count = 0;
    wchar_t **args = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!args) return 1;
    int status = launch(count, args);
    LocalFree(args);
    return status;
}
