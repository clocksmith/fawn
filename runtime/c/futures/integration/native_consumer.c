/* SPDX-License-Identifier: MIT
 * CPU-only actual retained exported instance ABI consumer. No device creation. */
#define _POSIX_C_SOURCE 200809L
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "webgpu.h"

static void fail(const char *message)
{
    fprintf(stderr, "native consumer: %s\n", message);
    exit(2);
}
static void load(void *library, const char *name, void *out, size_t size)
{
    void *symbol = dlsym(library, name);
    if (symbol == NULL || size != sizeof(symbol)) fail(name);
    memcpy(out, &symbol, size);
}
int main(int argc, char **argv)
{
    void *library;
    void *(*create)(void *);
    void (*addref)(void *), (*release)(void *), (*pump)(void *);
    uint32_t (*wait)(void *, size_t, WGPUFutureWaitInfo *, uint64_t);
    WGPUFutureWaitInfo info = {.future = {.id = UINT64_C(0xdeadbeef)}, .completed = 43};
    void *instance;
    if (argc != 3) fail("usage: native_consumer LIBRARY selected|default|setup-failure|fatal-init|fatal-deinit");
    library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (library == NULL) fail(dlerror());
    load(library, "doeNativeCreateInstance", &create, sizeof(create));
    load(library, "doeNativeInstanceAddRef", &addref, sizeof(addref));
    load(library, "doeNativeInstanceRelease", &release, sizeof(release));
    load(library, "doeNativeInstanceWaitAny", &wait, sizeof(wait));
    load(library, "doeNativeInstanceProcessEvents", &pump, sizeof(pump));
    if ((dlsym(library, "doe_future_new_id") != NULL) != (strcmp(argv[2], "default") != 0))
        fail("wrong linked future owner");
    instance = create(NULL);
    if (strcmp(argv[2], "setup-failure") == 0) {
        unsigned (*counts)(void);
        if (instance != NULL) fail("ordinary setup failure returned an instance");
        load(RTLD_DEFAULT, "doe_futures_fault_cleanup_count", &counts, sizeof(counts));
        if (counts() < 2) fail("ordinary setup failure did not clean attributes and mutex");
        instance = create(NULL); /* One injected failure, real fresh allocation. */
    }
    if (instance == NULL) fail("instance creation failed");
    if (strcmp(argv[2], "fatal-init") == 0) fail("exceptional init cleanup did not terminate");
    addref(instance);
    release(instance);
    if (wait(NULL, 0, NULL, 0) != 1) fail("count zero");
    if (wait(NULL, 1, &info, 0) != 3) fail("missing instance");
    if (wait(instance, 1, &info, 0) != 3) fail("unknown future");
    pump(instance);
    release(instance);
    if (strcmp(argv[2], "fatal-deinit") == 0) fail("exceptional final cleanup did not terminate");
    if (dlclose(library) != 0) fail("dlclose");
    puts("actual native instance ABI consumer passed (no GPU)");
    return 0;
}
