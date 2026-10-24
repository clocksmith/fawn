/* SPDX-License-Identifier: MIT */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#ifndef _DARWIN_C_SOURCE
#define _DARWIN_C_SOURCE
#endif
#include "provider_pin.h"
#include <errno.h>
#if defined(_WIN32)
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

/* A loader PIN is irrevocable process-code residency, not a resource/device
 * lease. InitOnce publishes one result and no LoadLibrary fallback can turn an
 * unsuccessful pin into admission. The aligned object supplies an image address
 * without an ISO C function/data pointer conversion. */
static INIT_ONCE provider_once = INIT_ONCE_STATIC_INIT;
static HMODULE provider_image;
static bool provider_permanent;
static const WCHAR provider_marker;

static BOOL CALLBACK pin_provider_image(PINIT_ONCE once, PVOID argument, PVOID *context)
{
    (void)once;
    (void)argument;
    (void)context;
    provider_permanent = GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
        GET_MODULE_HANDLE_EX_FLAG_PIN, &provider_marker, &provider_image) != FALSE;
    /* Cache failed admission too; retrying is not another residency policy. */
    return TRUE;
}

bool native_provider_pin(void)
{
    DWORD previous_error = GetLastError();
    int previous_errno = errno;
    BOOL status = InitOnceExecuteOnce(&provider_once, pin_provider_image, NULL, NULL);
    bool pinned = status != FALSE && provider_permanent && provider_image != NULL;
    errno = previous_errno;
    SetLastError(previous_error);
    return pinned;
}
#else
#include <dlfcn.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#elif defined(__linux__)
#include <link.h>
#endif

/* Explicit process-lifetime owner: never dlclose this handle from its worker.
 * Closing the final reference would unmap the very return/cleanup instructions
 * executing that close. pthread_once bounds retention to one reference/image. */
static pthread_once_t provider_once = PTHREAD_ONCE_INIT;
static void *provider_image;
static bool provider_permanent;
static const unsigned char provider_marker;

#if defined(__linux__)
static int main_contains_marker(struct dl_phdr_info *image, size_t size, void *found)
{
    (void)size;
    if (image->dlpi_name && image->dlpi_name[0]) return 0;
    uintptr_t marker = (uintptr_t)&provider_marker;
    for (ElfW(Half) i = 0; i < image->dlpi_phnum; ++i) {
        const ElfW(Phdr) *segment = &image->dlpi_phdr[i];
        if (segment->p_type != PT_LOAD) continue;
        uintptr_t begin = (uintptr_t)image->dlpi_addr + (uintptr_t)segment->p_vaddr;
        if (marker >= begin && marker - begin < segment->p_memsz) {
            *(bool *)found = true;
            return 1;
        }
    }
    return 0;
}
#endif

static void pin_provider_image(void)
{
    Dl_info image;
    if (!dladdr(&provider_marker, &image) || !image.dli_fname) return;
#if defined(__APPLE__)
    provider_permanent = image.dli_fbase == (const void *)_dyld_get_image_header(0);
#elif defined(__linux__)
    (void)dl_iterate_phdr(main_contains_marker, &provider_permanent);
#else
    /* The native build supports Darwin/Linux. Another host must supply a real
     * main-image identity before publishing a background worker. */
    return;
#endif
    if (!provider_permanent) provider_image = dlopen(image.dli_fname, RTLD_NOW | RTLD_LOCAL);
}

bool native_provider_pin(void)
{
    int previous_errno = errno;
    int status = pthread_once(&provider_once, pin_provider_image);
    bool pinned = !status && (provider_permanent || provider_image != NULL);
    errno = previous_errno;
    return pinned;
}

#endif
