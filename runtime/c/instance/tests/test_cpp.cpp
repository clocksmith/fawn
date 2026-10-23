/* SPDX-License-Identifier: MIT */
#include "doe_instance.h"
#include <cassert>
#include <cstdlib>
#include <type_traits>

static void *allocate(void *, const void *, size_t size, size_t alignment)
{
    void *memory = nullptr;
    return posix_memalign(&memory, alignment, size) == 0 ? memory : nullptr;
}
static void deallocate(void *, const void *, void *memory, size_t, size_t) { std::free(memory); }
int main()
{
    static_assert(std::is_standard_layout<doe_instance_config>::value, "C config");
    doe_instance_config config{};
    config.abi_version = DOE_INSTANCE_ABI_VERSION;
    config.object_allocator = {nullptr, nullptr, allocate, deallocate};
    config.wait_allocator = config.object_allocator;
    doe_instance *instance = nullptr;
    assert(doe_instance_create(&config, &instance) == DOE_INSTANCE_OK);
    const auto queue = doe_instance_borrow_queue(instance);
    queue.retain(queue.userdata);
    queue.release(queue.userdata);
    doe_instance_release(instance);
    assert(doe_instance_wait_any(nullptr, 0, nullptr, UINT64_MAX) == WGPUWaitStatus_Success);
}
