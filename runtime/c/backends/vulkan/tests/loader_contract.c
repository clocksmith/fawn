/* SPDX-License-Identifier: MIT */
#include "../src/internal.h"
#include <assert.h>
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

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

    /* Device selection controls: enumeration order is not identity. */
    doe_vk_device_candidate cpu_first[3] = {{true, {1}}, {false, {2}}, {false, {3}}};
    doe_vk_device_candidate gpu_first[3] = {{false, {3}}, {false, {2}}, {true, {1}}};
    uint32_t index = 99;
    doe_vk_options options = {.selection = DOE_VK_SELECT_CLASS};
    assert(doe_vk_select_device(cpu_first, 3, &options, &index) == DOE_VK_OK && index == 1);
    assert(doe_vk_select_device(gpu_first, 3, &options, &index) == DOE_VK_OK && index == 0);
    options.allow_cpu_adapter = true; /* Fallback request: the CPU device, wherever it is. */
    assert(doe_vk_select_device(cpu_first, 3, &options, &index) == DOE_VK_OK && index == 0);
    assert(doe_vk_select_device(gpu_first, 3, &options, &index) == DOE_VK_OK && index == 2);
    index = 99;
    assert(doe_vk_select_device(gpu_first, 2, &options, &index) == DOE_VK_UNSUPPORTED && index == 99);
    options = (doe_vk_options){.selection = DOE_VK_SELECT_IDENTITY, .device_uuid = {2}};
    assert(doe_vk_select_device(cpu_first, 3, &options, &index) == DOE_VK_OK && index == 1);
    assert(doe_vk_select_device(gpu_first, 3, &options, &index) == DOE_VK_OK && index == 1);
    options.device_uuid[0] = 3; /* Same identity at a different ordinal after reordering. */
    assert(doe_vk_select_device(cpu_first, 3, &options, &index) == DOE_VK_OK && index == 2);
    assert(doe_vk_select_device(gpu_first, 3, &options, &index) == DOE_VK_OK && index == 0);
    options.device_uuid[0] = 4; /* Hardware gone: no substitution. */
    index = 99;
    assert(doe_vk_select_device(cpu_first, 3, &options, &index) == DOE_VK_UNSUPPORTED && index == 99);
    options = (doe_vk_options){.adapter_ordinal = 2};
    assert(doe_vk_select_device(cpu_first, 3, &options, &index) == DOE_VK_OK && index == 2);
    assert(doe_vk_select_device(cpu_first, 2, &options, &index) == DOE_VK_UNSUPPORTED);
    options.selection = (doe_vk_adapter_selection)7;
    assert(doe_vk_select_device(cpu_first, 3, &options, &index) == DOE_VK_INVALID_ARGUMENT);
    puts("PASS real symbol lookup, typed call, precise missing-symbol diagnostic and unchanged destination on failed/invalid lookup");
    puts("PASS device selection: ordinal, CPU/GPU class independent of enumeration order, UUID identity after reordering, absent identity and class rejection without output");
    return 0;
}
