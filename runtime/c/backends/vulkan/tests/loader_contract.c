/* SPDX-License-Identifier: MIT */
#include "../src/internal.h"
#include <assert.h>
#include <dlfcn.h>
#include <stdio.h>

int main(void) {
    void *library = dlopen(NULL, RTLD_NOW | RTLD_LOCAL);
    assert(library != NULL);
    void *(*allocate)(size_t) = NULL;
    _Static_assert(sizeof(allocate) == sizeof(void *), "POSIX allocation symbol");
    doe_vk_error error = {0};
    assert(doe_vk_load_symbol(library, "malloc", &allocate, sizeof(allocate), &error) == DOE_VK_OK);
    void *allocation = allocate(16);
    assert(allocation != NULL);
    free(allocation);
    void *(*saved)(size_t) = allocate;
    assert(doe_vk_load_symbol(library, "doe_test_missing_vulkan_symbol", &allocate, sizeof(allocate), &error) == DOE_VK_LOADER_UNAVAILABLE);
    assert(allocate == saved && strcmp(error.operation, "doe_test_missing_vulkan_symbol") == 0);
    assert(doe_vk_load_symbol(library, "malloc", &allocate, 1, &error) == DOE_VK_INVALID_ARGUMENT);
    assert(allocate == saved);
    assert(doe_vk_load_symbol(NULL, "malloc", &allocate, sizeof(allocate), &error) == DOE_VK_INVALID_ARGUMENT);
    assert(allocate == saved);
    assert(dlclose(library) == 0);
    puts("PASS real symbol lookup, typed call, precise missing-symbol diagnostic and unchanged destination on failed/invalid lookup");
    return 0;
}
