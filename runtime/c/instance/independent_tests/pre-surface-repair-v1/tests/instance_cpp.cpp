/* SPDX-License-Identifier: MIT */
#define _POSIX_C_SOURCE 200809L
#include "doe_instance.h"
#include <cassert>
#include <cstdlib>
#include <cstdio>
#include <type_traits>
#include <cstddef>
static unsigned allocations, frees;
static void *allocate(void *s, const void *v, size_t n, size_t a) {
    assert(!s && !v); void *p = nullptr;
    assert(posix_memalign(&p, a < sizeof(void *) ? sizeof(void *) : a, n) == 0);
    ++allocations; return p;
}
static void deallocate(void *s, const void *v, void *p, size_t n, size_t a) {
    assert(!s && !v && p && n && a); ++frees; free(p);
}
int main() {
    static_assert(std::is_standard_layout<doe_instance_config>::value, "C configuration");
    static_assert(std::is_standard_layout<doe_instance_queue_lease>::value, "C lease");
    static_assert(std::is_same<decltype(&doe_instance_wait_any), WGPUWaitStatus (*)(doe_instance *, size_t, WGPUFutureWaitInfo *, uint64_t)>::value, "wait signature");
    static_assert(sizeof(WGPUBool) == sizeof(uint32_t), "boolean ABI");
    doe_instance_config c{}; c.abi_version = 1;
    c.object_allocator = {nullptr,nullptr,allocate,deallocate}; c.wait_allocator = c.object_allocator;
    doe_instance *p = nullptr; assert(doe_instance_create(&c,&p) == DOE_INSTANCE_OK && p);
    assert(doe_instance_from_handle(p) == p);
    auto l = doe_instance_borrow_queue(p); auto copy = l;
    assert(copy.queue == l.queue && copy.userdata == p); copy.retain(copy.userdata);
    doe_instance_retain(p); doe_instance_undo_retain(p); copy.release(copy.userdata);
    uint64_t id = 0; assert(doe_future_new_id(copy.queue,&id) == DOE_FUTURE_OK);
    WGPUFutureWaitInfo w{{id},99}; assert(doe_instance_wait_any(p,1,&w,0) == WGPUWaitStatus_Success && w.completed == 1);
    doe_instance_process_events(p);
    auto f = doe_instance_features(); assert(f.featureCount == 0 && f.features == nullptr);
    auto limits = doe_instance_limits(); assert(limits.nextInChain == nullptr && limits.timedWaitAnyMaxCount == 0);
    assert(!doe_instance_has_feature(static_cast<WGPUInstanceFeatureName>(0x7fffffff)));
    doe_instance_features_free_members(f); doe_instance_release(p); assert(allocations == frees);
    puts("PASS independent C++17 opaque instance ABI calls and stateless allocator");
}
