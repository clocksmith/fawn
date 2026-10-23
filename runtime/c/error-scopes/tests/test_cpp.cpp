/* SPDX-License-Identifier: MIT */
#include "doe_error_scopes.h"
#include <cassert>
#include <cstdlib>
#include <type_traits>

static_assert(std::is_standard_layout<doe_error_scope_result>::value, "C result layout");
static_assert(sizeof(uint32_t) == 4, "callback scalar width");

static void capture(uint32_t type, WGPUStringView message, void *first, void *second)
{
    assert(type == UINT32_MAX);
    assert(message.length == 0 && message.data != nullptr);
    assert(first != second);
    *static_cast<bool *>(first) = true;
}

int main()
{
    doe_error_scope_requirements requirements{};
    assert(doe_error_scope_storage_requirements(1, &requirements).status == DOE_ERROR_SCOPE_OK);
    void *storage = std::malloc(requirements.size);
    assert(storage != nullptr);
    doe_error_scope_stack *stack = nullptr;
    assert(doe_error_scope_init(1, storage, requirements.size, &stack).status == DOE_ERROR_SCOPE_OK);
    bool called = false;
    assert(doe_error_scope_set_handler(stack, capture, &called, nullptr).status == DOE_ERROR_SCOPE_OK);
    assert(doe_error_scope_deliver(stack, UINT32_MAX, nullptr, 0).status == DOE_ERROR_SCOPE_OK);
    assert(called);
    assert(doe_error_scope_deinit(stack).status == DOE_ERROR_SCOPE_OK);
    std::free(storage);
}
