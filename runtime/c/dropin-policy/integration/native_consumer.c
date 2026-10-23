/* SPDX-License-Identifier: MIT */
/* Actual dlopen consumer, not an exported GetProcAddress replacement. */
#define _POSIX_C_SOURCE 200809L
#include "webgpu.h"
#include <assert.h>
#include <dlfcn.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static WGPUProcGetProcAddress lookup;
static uint32_t (*last_error)(void);

static WGPUProc exported_proc(void *library, const char *name) {
    void *address = dlsym(library, name);
    WGPUProc proc = NULL;
    assert(address != NULL && sizeof(proc) == sizeof(address));
    memcpy(&proc, &address, sizeof(proc));
    return proc;
}

static void identity(void *library, const char *name, const char *target) {
    WGPUProc direct = exported_proc(library, target);
    const size_t lengths[] = {strlen(name), WGPU_STRLEN};
    for (size_t i = 0; i < sizeof(lengths) / sizeof(lengths[0]); ++i) {
        fprintf(stderr, "native policy consumer: identity %s length=%zu\n", name, lengths[i]);
        WGPUProc resolved = lookup((WGPUStringView){name, lengths[i]});
        assert(resolved == direct && last_error() == 0);
    }
}

static void missing(WGPUStringView view, uint32_t expected_error) {
    assert(lookup(view) == NULL);
    assert(last_error() == expected_error);
}

static void *write_error(void *unused) {
    (void)unused;
    missing((WGPUStringView){NULL, SIZE_MAX}, 3);
    return NULL;
}

int main(int argc, char **argv) {
    if (argc != 2) return 2;
    fprintf(stderr, "native policy consumer: dlopen %s\n", argv[1]);
    void *library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (library == NULL) { fprintf(stderr, "%s\n", dlerror()); return 1; }
    WGPUProc generic = exported_proc(library, "wgpuGetProcAddress");
    assert(sizeof(lookup) == sizeof(generic));
    memcpy(&lookup, &generic, sizeof(lookup));
    generic = exported_proc(library, "doeWgpuDropinLastErrorCode");
    assert(sizeof(last_error) == sizeof(generic));
    memcpy(&last_error, &generic, sizeof(last_error));

    identity(library, "wgpuDeviceCreateBuffer", "doeNativeDeviceCreateBuffer");
    identity(library, "wgpuGetProcAddress", "wgpuGetProcAddress");
    identity(library, "wgpuHasInstanceFeature", "wgpuHasInstanceFeature");
    const char slice[] = "wgpuHasInstanceFeature trailing bytes";
    assert(lookup((WGPUStringView){slice, strlen("wgpuHasInstanceFeature")}) ==
        exported_proc(library, "wgpuHasInstanceFeature"));
    missing((WGPUStringView){NULL, 1}, 3);
    missing((WGPUStringView){"", 0}, 3);
    missing((WGPUStringView){"", WGPU_STRLEN}, 2);
    missing((WGPUStringView){"wgpuDefinitelyMissing", WGPU_STRLEN}, 2);
    missing((WGPUStringView){"WGPUHasInstanceFeature", WGPU_STRLEN}, 2);
    const char embedded[] = "wgpuHasInstanceFeature\0suffix";
    missing((WGPUStringView){embedded, sizeof(embedded) - 1}, 2);
    char long_name[258];
    memset(long_name, 'q', sizeof(long_name));
    for (size_t length = 255; length <= 257; ++length) {
        long_name[length] = '\0';
        missing((WGPUStringView){long_name, length}, 2);
        missing((WGPUStringView){long_name, WGPU_STRLEN}, 2);
        long_name[length] = 'q';
    }
    identity(library, "wgpuHasInstanceFeature", "wgpuHasInstanceFeature");
    pthread_t thread;
    assert(pthread_create(&thread, NULL, write_error, NULL) == 0);
    assert(pthread_join(thread, NULL) == 0);
    assert(last_error() == 3); /* Existing process-wide state, not TLS. */
    assert(dlclose(library) == 0);
    puts("native policy consumer: identities, views, missing-name and cross-thread error checks passed");
    return 0;
}
