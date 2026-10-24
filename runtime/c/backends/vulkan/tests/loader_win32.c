/* SPDX-License-Identifier: MIT
 * Actual Win32 symbol loader calls and Linux-WSI rejection; no Vulkan/GPU load. */
#include "../src/internal.h"
#include "../src/surface_internal.h"
#include <assert.h>
#include <stdio.h>
#include <windows.h>

#if !defined(_WIN32) || !defined(_WIN64)
#error "This control requires an actual Win64 target"
#endif

int main(void)
{
    HMODULE library = LoadLibraryExW(L"kernel32.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    assert(library != NULL);
    DWORD (WINAPI *process_id)(void) = NULL;
    _Static_assert(sizeof(process_id) == sizeof(FARPROC), "Win64 typed procedure storage");
    doe_vk_error error = {0};
    assert(doe_vk_load_symbol(library, "GetCurrentProcessId", &process_id, sizeof(process_id), &error) == DOE_VK_OK);
    assert(process_id != NULL && process_id() == GetCurrentProcessId());
    DWORD (WINAPI *saved)(void) = process_id;
    assert(doe_vk_load_symbol(library, "doe_missing_test_symbol", &process_id, sizeof(process_id), &error) ==
        DOE_VK_LOADER_UNAVAILABLE);
    assert(process_id == saved && strcmp(error.operation, "doe_missing_test_symbol") == 0);
    assert(error.native_result == ERROR_PROC_NOT_FOUND);
    assert(doe_vk_load_symbol(library, "GetCurrentProcessId", &process_id, 1, &error) == DOE_VK_INVALID_ARGUMENT);
    assert(process_id == saved);
    assert(doe_vk_load_symbol(NULL, "GetCurrentProcessId", &process_id, sizeof(process_id), &error) == DOE_VK_INVALID_ARGUMENT);
    assert(process_id == saved);
    assert(FreeLibrary(library));

    /* A null loader makes accidental native extension lookup fail visibly.
     * Linux WSI must reject before touching either loader or output storage. */
    doe_vk_context context = {0};
    doe_vk_instance_extensions selection, before;
    memset(&selection, 0x5a, sizeof(selection));
    memcpy(&before, &selection, sizeof(before));
    for (uint32_t platform = 1; platform <= 7; ++platform) {
        assert(doe_vk_surface_instance_extensions(&context, platform, &selection, &error) == DOE_VK_UNSUPPORTED);
        assert(!memcmp(&selection, &before, sizeof(selection)));
    }
    assert(doe_vk_surface_instance_extensions(&context, 0, &selection, &error) == DOE_VK_OK);
    assert(selection.count == 0);
    puts("Win64 loader: actual typed procedure call, untouched failed lookups and Linux WSI rejection passed; no Vulkan device or GPU run");
    return 0;
}
