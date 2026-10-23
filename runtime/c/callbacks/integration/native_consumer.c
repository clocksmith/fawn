/* SPDX-License-Identifier: MIT
 * Real exported native APIs. Metal-unavailable ownership, never GPU success. */
#define _POSIX_C_SOURCE 200809L
#include "webgpu.h"
#include <assert.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct api {
    WGPUProcCreateInstance create;
    WGPUProcInstanceRelease release;
    WGPUProcInstanceProcessEvents pump;
    WGPUProcInstanceWaitAny wait;
    WGPUProcInstanceRequestAdapter adapter;
    WGPUProcAdapterRequestDevice device;
    WGPUProcBufferMapAsync map;
    WGPUProcDevicePopErrorScope scope;
    WGPUProcShaderModuleGetCompilationInfo compilation;
};
static struct api api;
static void load(void *library, const char *name, void *out, size_t size)
{
    void *symbol = dlsym(library, name);
    if (symbol == NULL || size != sizeof(symbol)) {
        fprintf(stderr, "missing native symbol: %s\n", name); exit(2);
    }
    memcpy(out, &symbol, size);
}
static int message_is(WGPUStringView view, const char *expected)
{
    return view.length == strlen(expected) && memcmp(view.data, expected, view.length) == 0;
}
struct capture { WGPUInstance instance; unsigned calls; int release_caller; };
static void adapter_reply(WGPURequestAdapterStatus status, WGPUAdapter adapter,
                          WGPUStringView message, void *raw, void *unused)
{
    (void)unused;
    struct capture *capture = raw;
    assert((unsigned)status == 3 && adapter == NULL);
    assert(message_is(message, "metal default device unavailable"));
    capture->calls++;
    if (capture->release_caller) api.release(capture->instance);
}
static void device_reply(WGPURequestDeviceStatus status, WGPUDevice device,
                         WGPUStringView message, void *raw, void *unused)
{
    (void)unused;
    assert((unsigned)status == 3 && device == NULL);
    assert(message_is(message, "invalid adapter handle"));
    ++*(unsigned *)raw;
}
static void map_reply(WGPUMapAsyncStatus status, WGPUStringView message,
                      void *raw, void *unused)
{
    (void)unused;
    assert((unsigned)status == 3 && message_is(message, "invalid buffer mapping"));
    ++*(unsigned *)raw;
}
static void scope_reply(WGPUPopErrorScopeStatus status, WGPUErrorType error,
                        WGPUStringView message, void *raw, void *unused)
{
    (void)unused;
    assert((unsigned)status == 3 && (unsigned)error == 1);
    assert(message_is(message, "no error scope to pop"));
    ++*(unsigned *)raw;
}
static void compilation_reply(WGPUCompilationInfoRequestStatus status,
                              const WGPUCompilationInfo *info, void *raw, void *unused)
{
    (void)unused;
    assert((unsigned)status == 2 && info == NULL);
    ++*(unsigned *)raw;
}
static void owned_modes(void)
{
    for (unsigned mode = 0; mode != 4; mode++) {
        struct capture capture = {.instance = api.create(NULL), .release_caller = 1};
        assert(capture.instance != NULL);
        WGPURequestAdapterCallbackInfo callback = {0};
        callback.mode = (WGPUCallbackMode)mode;
        callback.callback = adapter_reply;
        callback.userdata1 = &capture;
        WGPUFuture future = api.adapter(capture.instance, NULL, callback);
        assert(future.id != 0 && (future.id >> 63) == 0);
        if (mode == 1) {
            assert(capture.calls == 0);
            api.pump(capture.instance);
            assert(capture.calls == 0);
            WGPUFutureWaitInfo infos[2] = {{.future = future}, {.future = future}};
            assert((unsigned)api.wait(capture.instance, 2, infos, 0) == 1);
            assert(infos[0].completed && infos[1].completed);
        } else if (mode == 2) {
            assert(capture.calls == 0);
            api.pump(capture.instance);
        }
        assert(capture.calls == 1);
    }
}
static void ownerless_modes(void)
{
    for (unsigned mode = 0; mode <= 3; mode += 3) {
        unsigned calls = 0;
        WGPURequestDeviceCallbackInfo device = {0};
        device.mode = (WGPUCallbackMode)mode; device.callback = device_reply; device.userdata1 = &calls;
        WGPUBufferMapCallbackInfo map = {0};
        map.mode = (WGPUCallbackMode)mode; map.callback = map_reply; map.userdata1 = &calls;
        WGPUPopErrorScopeCallbackInfo scope = {0};
        scope.mode = (WGPUCallbackMode)mode; scope.callback = scope_reply; scope.userdata1 = &calls;
        WGPUCompilationInfoCallbackInfo compilation = {0};
        compilation.mode = (WGPUCallbackMode)mode; compilation.callback = compilation_reply; compilation.userdata1 = &calls;
        assert((api.device(NULL, NULL, device).id >> 63) == 1);
        assert((api.map(NULL, WGPUMapMode_Read, 0, 4, map).id >> 63) == 1);
        assert((api.scope(NULL, scope).id >> 63) == 1);
        assert((api.compilation(NULL, compilation).id >> 63) == 1);
        assert(calls == 4);
    }
}
int main(int argc, char **argv)
{
    if (argc != 3) { fputs("usage: native_consumer LIBRARY selected|default\n", stderr); return 2; }
    assert(setenv("DOE_BACKEND", "metal", 1) == 0);
    void *library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (library == NULL) { fputs(dlerror(), stderr); return 2; }
    load(library, "wgpuCreateInstance", &api.create, sizeof(api.create));
    load(library, "wgpuInstanceRelease", &api.release, sizeof(api.release));
    load(library, "wgpuInstanceProcessEvents", &api.pump, sizeof(api.pump));
    load(library, "wgpuInstanceWaitAny", &api.wait, sizeof(api.wait));
    load(library, "wgpuInstanceRequestAdapter", &api.adapter, sizeof(api.adapter));
    load(library, "wgpuAdapterRequestDevice", &api.device, sizeof(api.device));
    load(library, "wgpuBufferMapAsync", &api.map, sizeof(api.map));
    load(library, "wgpuDevicePopErrorScope", &api.scope, sizeof(api.scope));
    load(library, "wgpuShaderModuleGetCompilationInfo", &api.compilation, sizeof(api.compilation));
    assert((dlsym(library, "doe_callback_publish") != NULL) == (strcmp(argv[2], "selected") == 0));
    owned_modes(); ownerless_modes();
    assert(dlclose(library) == 0);
    puts("actual native callback consumer: owned Metal-unavailable modes and ownerless error APIs passed; no GPU success");
    return 0;
}
