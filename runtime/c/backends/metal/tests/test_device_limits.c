/* SPDX-License-Identifier: MIT
 * Exact typed device-property calls, not Apple SDK/device qualification. */
#include "../src/objc_abi.h"
#include <assert.h>
#include <stdio.h>

typedef struct limits_fixture {
    doe_mtl_objc runtime;
    unsigned selectors[4];
    void *missing;
    size_t buffer, memory;
    doe_mtl_size dimensions;
    unsigned value_calls;
} limits_fixture;

static doe_objc_bool responds(void *receiver, void *selector, void *method) {
    limits_fixture *f = receiver;
    assert(selector == f->runtime.responds);
    assert(method == f->runtime.max_threads || method == f->runtime.max_buffer_length ||
        method == f->runtime.max_threadgroup_memory);
    return (doe_objc_bool)(method != f->missing);
}

static size_t size_call(void *receiver, void *selector) {
    limits_fixture *f = receiver;
    assert(selector == f->runtime.max_buffer_length ||
        selector == f->runtime.max_threadgroup_memory);
    f->value_calls++;
    return selector == f->runtime.max_buffer_length ? f->buffer : f->memory;
}

static doe_mtl_size dimensions_call(void *receiver, void *selector) {
    limits_fixture *f = receiver;
    assert(selector == f->runtime.max_threads);
    f->value_calls++;
    return f->dimensions;
}

static void dimensions_stret(doe_mtl_size *out, void *receiver, void *selector) {
    *out = dimensions_call(receiver, selector);
}

static void unchanged_failure(limits_fixture *f) {
    doe_mtl_caps result, before;
    memset(&result, 0xa5, sizeof(result));
    memcpy(&before, &result, sizeof(before));
    assert(!doe_mtl_objc_limits(&f->runtime, f, &result));
    assert(memcmp(&result, &before, sizeof(result)) == 0);
}

int main(void) {
    limits_fixture f = {0};
    doe_mtl_caps result = {0};
    f.runtime.responds = &f.selectors[0];
    f.runtime.max_buffer_length = &f.selectors[1];
    f.runtime.max_threads = &f.selectors[2];
    f.runtime.max_threadgroup_memory = &f.selectors[3];
    f.runtime.responds_call = responds;
    f.runtime.size_call = size_call;
    f.runtime.dimensions_call = dimensions_call;
    f.runtime.dimensions_stret_call = dimensions_stret;
    f.buffer = (size_t)UINT32_MAX + 17;
    f.memory = 32768;
    f.dimensions = (doe_mtl_size){1024, 512, 64};
    assert(doe_mtl_objc_limits(&f.runtime, &f, &result));
    assert(result.max_buffer_length == (uint64_t)UINT32_MAX + 17);
    assert(result.max_threadgroup_memory_length == 32768);
    assert(result.max_threads_per_dimension[0] == 1024);
    assert(result.max_threads_per_dimension[1] == 512);
    assert(result.max_threads_per_dimension[2] == 64);
    assert(f.value_calls == 3);
    for (unsigned i = 1; i < 4; ++i) {
        f.missing = &f.selectors[i];
        f.value_calls = 0;
        unchanged_failure(&f);
        assert(f.value_calls == 0);
    }
    f.missing = NULL;
    f.buffer = 0;
    unchanged_failure(&f);
    f.buffer = 4096;
    f.memory = 0;
    unchanged_failure(&f);
    f.memory = 32768;
    f.dimensions.width = 0;
    unchanged_failure(&f);
    f.dimensions = (doe_mtl_size){1, 0, 1};
    unchanged_failure(&f);
    f.dimensions = (doe_mtl_size){1, 1, 0};
    unchanged_failure(&f);
    puts("Metal device limits: typed properties and failure atomicity pass; no SDK/device run");
    return 0;
}
