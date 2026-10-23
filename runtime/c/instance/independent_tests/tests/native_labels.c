/* SPDX-License-Identifier: MIT
 * Post-handoff mechanical realization of sealed N05 actual label cleanup. */
#include "webgpu.h"
#include <assert.h>
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef void (*SetLabel)(void *, const char *, size_t);
typedef const char *(*GetLabel)(void *, size_t *);
static void *library;
static void symbol(const char *name, void *out, size_t size) {
    void *value = dlsym(library, name);
    assert(value && size == sizeof(value));
    memcpy(out, &value, size);
}
int main(int argc, char **argv) {
    assert(argc == 2);
    library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    assert(library);
    WGPUProcCreateInstance create;
    WGPUProcInstanceAddRef retain;
    WGPUProcInstanceRelease release;
    WGPUProcInstanceCreateSurface surface;
    WGPUProcSurfaceRelease surface_release;
    SetLabel set;
    GetLabel get;
    symbol("wgpuCreateInstance", &create, sizeof(create));
    symbol("wgpuInstanceAddRef", &retain, sizeof(retain));
    symbol("wgpuInstanceRelease", &release, sizeof(release));
    symbol("wgpuInstanceCreateSurface", &surface, sizeof(surface));
    symbol("wgpuSurfaceRelease", &surface_release, sizeof(surface_release));
    symbol("doeNativeObjectSetLabel", &set, sizeof(set));
    symbol("doeNativeObjectGetLabel", &get, sizeof(get));
    for (unsigned i = 0; i < 64; ++i) {
        WGPUInstance instance = create(NULL);
        assert(instance);
        uintptr_t key = (uintptr_t)instance;
        const char expected[7] = {'s', 'e', 'a', '\0', 'l', 'e', 'd'};
        char source[7]; memcpy(source, expected, sizeof(source));
        set(instance, source, sizeof(source));
        memset(source, 'x', sizeof(source));
        size_t n = 91; const char *label = get(instance, &n);
        assert(n == sizeof(expected) && label && !memcmp(label, expected, n));
        retain(instance); release(instance);
        assert(get(instance, &n) && n == sizeof(expected));
        WGPUSurface s = NULL;
        if (i & 1) {
            WGPUSurfaceDescriptor descriptor = {0};
            s = surface(instance, &descriptor); assert(s);
        }
        release(instance);
        if (s) {
            assert(get((void *)key, &n) && n == sizeof(expected));
            surface_release(s);
        }
        /* This is an integer-key label-store query, not dereferencing or
         * invoking an instance operation through a freed object. */
        n = 93; assert(get((void *)key, &n) == NULL && n == 0);
    }
    assert(dlclose(library) == 0);
    puts("PASS native copied labels survive caller/surface ownership and disappear at final release");
}
