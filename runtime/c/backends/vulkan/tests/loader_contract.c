/* SPDX-License-Identifier: MIT */
#include "adapter_identity_fixture.h"
#include <assert.h>
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

static void selection(const doe_vk_options *options, const uint32_t enumeration[3],
                      uint32_t count, doe_vk_status expected, uint32_t index) {
    memcpy(order, enumeration, sizeof(order));
    available = count;
    doe_vk_context selected = context();
    selected.caps.adapter_ordinal = 99;
    doe_vk_error error;
    assert(doe_vk_choose_device(&selected, options, &error) == expected);
    assert(selected.caps.adapter_ordinal == index);
    if (expected == DOE_VK_OK) assert(selected.physical == handle(enumeration[index]));
    else assert(selected.physical == NULL);
}

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

    /* Incoming class/order regressions use the canonical native-procedure owner,
     * not a retained competing candidate-array selector. */
    const uint32_t cpu_first[3] = {2, 0, 1}, gpu_first[3] = {1, 0, 2};
    doe_vk_options options = {.adapter_selection = DOE_VK_SELECT_HARDWARE};
    selection(&options, cpu_first, 3, DOE_VK_OK, 1);
    selection(&options, gpu_first, 3, DOE_VK_OK, 0);
    selection(&options, cpu_first, 1, DOE_VK_UNSUPPORTED, 99);
    options.adapter_selection = DOE_VK_SELECT_SOFTWARE;
    options.allow_cpu_adapter = true;
    selection(&options, cpu_first, 3, DOE_VK_OK, 0);
    selection(&options, gpu_first, 3, DOE_VK_OK, 2);
    selection(&options, gpu_first, 2, DOE_VK_UNSUPPORTED, 99);
    options = (doe_vk_options){.adapter_selection = DOE_VK_SELECT_IDENTITY};
    memset(options.required_identity.driver_uuid, 0x99, DOE_VK_UUID_SIZE);
    memset(options.required_identity.device_uuid, 1, DOE_VK_UUID_SIZE);
    selection(&options, cpu_first, 3, DOE_VK_OK, 1);
    selection(&options, gpu_first, 3, DOE_VK_OK, 1);
    memset(options.required_identity.device_uuid, 2, DOE_VK_UUID_SIZE);
    selection(&options, cpu_first, 3, DOE_VK_OK, 2);
    selection(&options, gpu_first, 3, DOE_VK_OK, 0);
    memset(options.required_identity.device_uuid, 4, DOE_VK_UUID_SIZE);
    selection(&options, cpu_first, 3, DOE_VK_UNSUPPORTED, 99);
    options = (doe_vk_options){.adapter_ordinal = 2};
    selection(&options, cpu_first, 3, DOE_VK_OK, 2);
    selection(&options, cpu_first, 2, DOE_VK_UNSUPPORTED, 99);
    options.adapter_selection = (doe_vk_adapter_selection)7;
    selection(&options, cpu_first, 3, DOE_VK_INVALID_ARGUMENT, 99);
    puts("PASS real symbol lookup, typed call, precise missing-symbol diagnostic and unchanged destination on failed/invalid lookup");
    puts("PASS device selection: ordinal, CPU/GPU class independent of enumeration order, UUID identity after reordering, absent identity and class rejection without output");
    return 0;
}
