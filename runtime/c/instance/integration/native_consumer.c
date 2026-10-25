/* SPDX-License-Identifier: MIT
 * Actual DSO consumer. Metal-unavailable and unconfigured surfaces are CPU only. */
#define _POSIX_C_SOURCE 200809L
#include "webgpu.h"
#include <assert.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static WGPUProcCreateInstance create_instance;
static WGPUProcInstanceAddRef add_ref;
static WGPUProcInstanceRelease release_instance;
static WGPUProcInstanceWaitAny wait_any;
static WGPUProcInstanceProcessEvents process_events;
static WGPUProcInstanceRequestAdapter request_adapter;
static WGPUProcInstanceCreateSurface create_surface;
static WGPUProcSurfaceRelease release_surface;
static WGPUProcGetInstanceFeatures get_features;
static WGPUProcGetInstanceLimits get_limits;
static WGPUProcHasInstanceFeature has_feature;
static WGPUProcSupportedInstanceFeaturesFreeMembers free_features;
static WGPUProcGetProcAddress get_proc;

typedef struct capture {
    WGPUInstance instance;
    unsigned calls;
    int release_caller;
} capture;

static void load(void *library, const char *name, void *out, size_t size)
{
    void *symbol = dlsym(library, name);
    assert(symbol != NULL && size == sizeof(symbol));
    memcpy(out, &symbol, size);
}

static void check_proc(const char *name, const void *direct, size_t size)
{
    WGPUProc explicit_proc = get_proc((WGPUStringView){name, strlen(name)});
    WGPUProc sentinel_proc = get_proc((WGPUStringView){name, WGPU_STRLEN});
    assert(explicit_proc != NULL && explicit_proc == sentinel_proc);
    assert(size == sizeof(explicit_proc));
    assert(memcmp(&explicit_proc, direct, size) == 0);
}

static void callback(WGPURequestAdapterStatus status, WGPUAdapter adapter,
                      WGPUStringView message, void *userdata1, void *userdata2)
{
    capture *seen = userdata1;
    const char expected[] = "metal default device unavailable";
    assert(userdata2 == NULL && status == WGPURequestAdapterStatus_Unavailable && adapter == NULL);
    assert(message.length == sizeof(expected) - 1 && memcmp(message.data, expected, sizeof(expected) - 1) == 0);
    ++seen->calls;
    if (seen->release_caller) release_instance(seen->instance);
}

int main(int argc, char **argv)
{
    void *library;
    WGPUSupportedInstanceFeatures features;
    WGPUInstanceLimits limits;
    uint64_t wrong_marker[512] = {0};
    assert(argc == 2);
    assert(setenv("DOE_BACKEND", "metal", 1) == 0);
    fprintf(stderr, "consumer: load DSO\n");
    library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (library == NULL) { fprintf(stderr, "%s\n", dlerror()); return 1; }
    load(library, "wgpuCreateInstance", &create_instance, sizeof(create_instance));
    load(library, "wgpuInstanceAddRef", &add_ref, sizeof(add_ref));
    load(library, "wgpuInstanceRelease", &release_instance, sizeof(release_instance));
    load(library, "wgpuInstanceWaitAny", &wait_any, sizeof(wait_any));
    load(library, "wgpuInstanceProcessEvents", &process_events, sizeof(process_events));
    load(library, "wgpuInstanceRequestAdapter", &request_adapter, sizeof(request_adapter));
    load(library, "wgpuInstanceCreateSurface", &create_surface, sizeof(create_surface));
    load(library, "wgpuSurfaceRelease", &release_surface, sizeof(release_surface));
    load(library, "wgpuGetInstanceFeatures", &get_features, sizeof(get_features));
    load(library, "wgpuGetInstanceLimits", &get_limits, sizeof(get_limits));
    load(library, "wgpuHasInstanceFeature", &has_feature, sizeof(has_feature));
    load(library, "wgpuSupportedInstanceFeaturesFreeMembers", &free_features, sizeof(free_features));
    load(library, "wgpuGetProcAddress", &get_proc, sizeof(get_proc));

    fprintf(stderr, "consumer: capability proc identity\n");
    check_proc("wgpuGetInstanceFeatures", &get_features, sizeof(get_features));
    check_proc("wgpuGetInstanceLimits", &get_limits, sizeof(get_limits));
    check_proc("wgpuHasInstanceFeature", &has_feature, sizeof(has_feature));
    check_proc("wgpuSupportedInstanceFeaturesFreeMembers", &free_features, sizeof(free_features));
    fprintf(stderr, "consumer: native proc identity\n");
    check_proc("wgpuCreateInstance", &create_instance, sizeof(create_instance));
    check_proc("wgpuInstanceAddRef", &add_ref, sizeof(add_ref));
    check_proc("wgpuInstanceRelease", &release_instance, sizeof(release_instance));
    check_proc("wgpuInstanceWaitAny", &wait_any, sizeof(wait_any));
    check_proc("wgpuInstanceProcessEvents", &process_events, sizeof(process_events));
    check_proc("wgpuInstanceRequestAdapter", &request_adapter, sizeof(request_adapter));
    check_proc("wgpuInstanceCreateSurface", &create_surface, sizeof(create_surface));
    for (unsigned mode = 0; mode < 4; ++mode) {
        fprintf(stderr, "consumer: request mode %u\n", mode);
        capture seen = {.instance = create_instance(NULL), .release_caller = 1};
        WGPURequestAdapterCallbackInfo info = {0};
        WGPUFuture future;
        assert(seen.instance != NULL);
        add_ref(seen.instance);
        release_instance(seen.instance);
        info.mode = (WGPUCallbackMode)mode;
        info.callback = callback;
        info.userdata1 = &seen;
        future = request_adapter(seen.instance, NULL, info);
        assert(future.id != 0);
        if (mode == 1) {
            WGPUFutureWaitInfo waits[2] = {{future, 71}, {future, 73}};
            process_events(seen.instance);
            assert(seen.calls == 0);
            assert(wait_any(seen.instance, 2, waits, 0) == WGPUWaitStatus_Success);
            assert(waits[0].completed == 1 && waits[1].completed == 1);
        } else if (mode == 2) process_events(seen.instance);
        assert(seen.calls == 1);
    }
    // Exercise the actual proc route, not just symbol presence, for surface-held requests.
    WGPUProc proc = get_proc((WGPUStringView){"wgpuCreateInstance", WGPU_STRLEN});
    memcpy(&create_instance, &proc, sizeof(proc));
    proc = get_proc((WGPUStringView){"wgpuInstanceRequestAdapter", WGPU_STRLEN});
    memcpy(&request_adapter, &proc, sizeof(proc));
    proc = get_proc((WGPUStringView){"wgpuInstanceProcessEvents", WGPU_STRLEN});
    memcpy(&process_events, &proc, sizeof(proc));
    proc = get_proc((WGPUStringView){"wgpuInstanceCreateSurface", WGPU_STRLEN});
    memcpy(&create_surface, &proc, sizeof(proc));
    for (unsigned i = 0; i < 32; ++i) {
        fprintf(stderr, "consumer: surface iteration %u\n", i);
        WGPUInstance instance = create_instance(NULL);
        WGPUSurfaceDescriptor descriptor = {0};
        WGPUSurface surface = create_surface(instance, &descriptor);
        capture seen = {.instance = instance};
        WGPURequestAdapterCallbackInfo info = {0};
        assert(surface != NULL);
        release_instance(instance);
        process_events(instance); /* Borrow from the actual unconfigured surface. */
        info.mode = WGPUCallbackMode_AllowProcessEvents;
        info.callback = callback;
        info.userdata1 = &seen;
        assert(request_adapter(instance, NULL, info).id != 0);
        process_events(instance);
        assert(seen.calls == 1);
        release_surface(surface);
    }
    fprintf(stderr, "consumer: null and invalid handles\n");
    assert(wait_any(NULL, 0, NULL, 0) == WGPUWaitStatus_Success);
    assert(wait_any(NULL, 0, NULL, UINT64_MAX) == WGPUWaitStatus_Success);
    assert(wait_any((WGPUInstance)&wrong_marker, 1, NULL, 0) == WGPUWaitStatus_Error);
    add_ref(NULL); release_instance(NULL);
    add_ref((WGPUInstance)&wrong_marker); release_instance((WGPUInstance)&wrong_marker);
    process_events(NULL); process_events((WGPUInstance)&wrong_marker);
    fprintf(stderr, "consumer: capabilities\n");
    memset(&features, 0x71, sizeof(features));
    get_features(&features);
    assert(features.featureCount == 2 && features.features != NULL);
    memset(&limits, 0x71, sizeof(limits));
    assert(get_limits(&limits) == WGPUStatus_Success);
    assert(limits.nextInChain == NULL && limits.timedWaitAnyMaxCount == 64);
    assert(has_feature(WGPUInstanceFeatureName_TimedWaitAny));
    assert(has_feature(WGPUInstanceFeatureName_MultipleDevicesPerAdapter));
    assert(!has_feature((WGPUInstanceFeatureName)UINT32_MAX));
    free_features(features);
    assert(dlclose(library) == 0);
    puts("actual native DSO: instance modes, proc routing, capabilities and unconfigured surface passed; no GPU success");
    return 0;
}
