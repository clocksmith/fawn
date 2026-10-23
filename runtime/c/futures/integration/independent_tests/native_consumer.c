/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe independent evaluation contributors.
 * Native export declarations use the pinned WebGPU header's public types.
 * This CPU consumer does not request an adapter, device or GPU operation. */
#include "webgpu.h"
#include <assert.h>
#include <dlfcn.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

typedef WGPUInstance (*create_fn)(const WGPUInstanceDescriptor *);
typedef void (*ref_fn)(WGPUInstance);
typedef void (*pump_fn)(WGPUInstance);
typedef WGPUWaitStatus (*wait_fn)(WGPUInstance, size_t, WGPUFutureWaitInfo *, uint64_t);

static void load_symbol(void *library, const char *name, void *out, size_t size)
{
    void *symbol = dlsym(library, name);
    assert(symbol != NULL && size == sizeof(symbol));
    memcpy(out, &symbol, size);
}

int main(int argc, char **argv)
{
    create_fn create;
    ref_fn add_ref, release;
    pump_fn pump;
    wait_fn wait;
    void (*arm)(int) = NULL;
    void *library;
    WGPUInstance a, b;
    WGPUFutureWaitInfo infos[3] = {
        {{UINT64_C(1)}, 41}, {{UINT64_C(0x8000000000000000)}, 42},
        {{UINT64_C(0xffffffffffffffff)}, 43}
    };
    assert(argc == 2 || argc == 3);
    library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (library == NULL) { fprintf(stderr, "%s\n", dlerror()); return 2; }
    load_symbol(library, "doeNativeCreateInstance", &create, sizeof(create));
    load_symbol(library, "doeNativeInstanceAddRef", &add_ref, sizeof(add_ref));
    load_symbol(library, "doeNativeInstanceRelease", &release, sizeof(release));
    load_symbol(library, "doeNativeInstanceProcessEvents", &pump, sizeof(pump));
    load_symbol(library, "doeNativeInstanceWaitAny", &wait, sizeof(wait));
    { void *symbol = dlsym(RTLD_DEFAULT, "eval_set_enabled");
      if (symbol != NULL) memcpy(&arm, &symbol, sizeof(arm)); }
    if (arm != NULL) arm(1);
    a = create(NULL);
    if (argc == 3 && strcmp(argv[2], "expect-null") == 0) {
        assert(a == NULL);
        puts("PASS native CreateInstance ordinary setup failure returns NULL");
        if (arm != NULL) arm(0);
        assert(dlclose(library) == 0);
        return 0;
    }
    b = create(NULL);
    assert(a != NULL && b != NULL && a != b);
    assert(wait(NULL, 0, NULL, UINT64_MAX) == WGPUWaitStatus_Success);
    assert(wait(NULL, 1, infos, 0) == WGPUWaitStatus_Error);
    assert(infos[0].completed == 41);
    assert(wait(a, 3, infos, 0) == WGPUWaitStatus_Error);
    assert(infos[0].completed == 0 && infos[1].completed == 42 && infos[2].completed == 43);
    add_ref(a); release(a);
    pump(a); pump(b);
    release(b); release(a);
    if (arm != NULL) arm(0);
        assert(dlclose(library) == 0);
    puts("PASS native exported instance lifecycle, count-zero, unknown-ID prefix flags");
    return 0;
}
