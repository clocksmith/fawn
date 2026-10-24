/* SPDX-License-Identifier: MIT
 * Exact Win32 loader boundary fault; no dynamic-library fallback or GPU. */
#ifdef _WIN32
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <assert.h>
#include <errno.h>
#include <stdio.h>

static unsigned calls;
static BOOL WINAPI reject_pin(DWORD flags, LPCWSTR address, HMODULE *image)
{
    assert(flags == (GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN));
    assert(address && image);
    ++calls;
    *image = NULL;
    SetLastError(ERROR_ACCESS_DENIED);
    return FALSE;
}
#define GetModuleHandleExW reject_pin
#include "../src/provider_pin.c"
#undef GetModuleHandleExW

int main(void)
{
    errno = EDOM;
    SetLastError(ERROR_INVALID_DATA);
    assert(!native_provider_pin() && !native_provider_pin());
    DWORD error = GetLastError();
    assert(calls == 1 && errno == EDOM && error == ERROR_INVALID_DATA);
    puts("PASS Win32 provider pin: real InitOnce caches rejected PIN, no fallback, errno/GetLastError preserved");
}
#else
int main(void) { return 77; }
#endif
