/* SPDX-License-Identifier: MIT */
#include "doe_callbacks.h"
#include <cassert>
#include <cstdint>
#include <cstdio>
static void delivered(void *raw) { ++*static_cast<unsigned *>(raw); }
int main()
{
    unsigned count = 0;
    doe_callback_request request{};
    request.abi_version = DOE_CALLBACKS_ABI_VERSION;
    request.mode = DOE_FUTURE_MODE_ALLOW_SPONTANEOUS;
    request.source_context = &count;
    request.context_size = sizeof(count);
    request.context_alignment = alignof(unsigned);
    request.deliver = delivered;
    std::uint64_t id = 0;
    assert(doe_callback_ready(&request, &id) == DOE_CALLBACK_OK);
    assert(count == 1 && (id >> 63) == 1);
    std::puts("restricted C++ public callback header/static archive passed");
}
