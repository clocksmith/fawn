// The Dawn extension is unadvertised: its ABI must reject in Doe, without
// passing a Doe adapter into Dawn or modifying caller-owned output chains.
// cc -I vendor/webgpu-headers tests/format_capability_lookup.c -o /tmp/format-capability-lookup
// /tmp/format-capability-lookup /absolute/path/libwebgpu_doe_full.dylib
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "webgpu.h"

typedef struct { void *nextInChain; } DawnFormatCapabilities;
typedef WGPUStatus (*GetFormatCapabilities)(WGPUAdapter, WGPUTextureFormat, DawnFormatCapabilities *);
enum { DAWN_FORMAT_CAPABILITIES = 0x00050017, DAWN_DRM_FORMAT_CAPABILITIES = 0x00050018 };

// These extension outputs share the chain/count/owned-array ABI. Their element
// layout differs; free-members must also accept the default empty output.
typedef struct { WGPUChainedStruct chain; size_t count; const void *members; } CapabilityArray;
typedef void (*FreeCapabilityArray)(CapabilityArray);

static int check_free_members(void *library, WGPUProcGetProcAddress lookup) {
    const char *names[] = {
        "wgpuAdapterPropertiesMemoryHeapsFreeMembers",
        "wgpuAdapterPropertiesSubgroupMatrixConfigsFreeMembers",
        "wgpuDawnDrmFormatCapabilitiesFreeMembers",
    };
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        FreeCapabilityArray direct = (FreeCapabilityArray)dlsym(library, names[i]);
        if (!direct) return 0;
        const size_t lengths[] = {strlen(names[i]), SIZE_MAX};
        for (size_t j = 0; j < 2; ++j) {
            FreeCapabilityArray resolved = (FreeCapabilityArray)lookup((WGPUStringView){names[i], lengths[j]});
            if (resolved != direct) {
                fprintf(stderr, "%s escaped the loaded Doe library\n", names[i]);
                return 0;
            }
            resolved((CapabilityArray){0});
            void *members = calloc(1, 64);
            if (!members) return 0;
            resolved((CapabilityArray){.count = 1, .members = members});
        }
    }
    return 1;
}

static void adapter_ready(WGPURequestAdapterStatus status, WGPUAdapter adapter,
                          WGPUStringView message, void *userdata, void *unused) {
    (void)message;
    (void)unused;
    if (status == WGPURequestAdapterStatus_Success) *(WGPUAdapter *)userdata = adapter;
}

int main(int argc, char **argv) {
    if (argc != 2) return 2;
    void *library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!library) { fprintf(stderr, "%s\n", dlerror()); return 2; }
    WGPUProcGetProcAddress lookup = (WGPUProcGetProcAddress)dlsym(library, "wgpuGetProcAddress");
    GetFormatCapabilities direct = (GetFormatCapabilities)dlsym(library, "wgpuAdapterGetFormatCapabilities");
    if (!lookup || !direct) return 1;
    const char *name = "wgpuAdapterGetFormatCapabilities";
    const size_t lengths[] = {strlen(name), SIZE_MAX};
    for (size_t i = 0; i < 2; ++i) {
        GetFormatCapabilities resolved = (GetFormatCapabilities)lookup((WGPUStringView){name, lengths[i]});
        if (resolved != direct) {
            fprintf(stderr, "format capability query escaped the loaded Doe library\n");
            return 1;
        }
    }
    if (!check_free_members(library, lookup)) return 1;
    WGPUProcCreateInstance create = (WGPUProcCreateInstance)dlsym(library, "wgpuCreateInstance");
    WGPUProcInstanceRequestAdapter request = (WGPUProcInstanceRequestAdapter)dlsym(library, "wgpuInstanceRequestAdapter");
    WGPUProcInstanceProcessEvents process = (WGPUProcInstanceProcessEvents)dlsym(library, "wgpuInstanceProcessEvents");
    WGPUProcAdapterHasFeature has = (WGPUProcAdapterHasFeature)dlsym(library, "wgpuAdapterHasFeature");
    WGPUProcAdapterRelease release_adapter = (WGPUProcAdapterRelease)dlsym(library, "wgpuAdapterRelease");
    WGPUProcInstanceRelease release_instance = (WGPUProcInstanceRelease)dlsym(library, "wgpuInstanceRelease");
    if (!create || !request || !process || !has || !release_adapter || !release_instance) return 1;
    WGPUInstance instance = create(NULL);
    if (!instance) return 1;
    WGPUAdapter adapter = NULL;
    WGPURequestAdapterCallbackInfo callback = WGPU_REQUEST_ADAPTER_CALLBACK_INFO_INIT;
    callback.mode = WGPUCallbackMode_AllowSpontaneous;
    callback.callback = adapter_ready;
    callback.userdata1 = &adapter;
    request(instance, NULL, callback);
    process(instance);
    int passed = adapter != NULL;
    if (adapter) {
        passed &= !has(adapter, (WGPUFeatureName)DAWN_FORMAT_CAPABILITIES);
        passed &= !has(adapter, (WGPUFeatureName)DAWN_DRM_FORMAT_CAPABILITIES);
        unsigned char chain[64];
        memset(chain, 0xa5, sizeof(chain));
        DawnFormatCapabilities capabilities = {chain};
        passed &= direct(adapter, WGPUTextureFormat_RGBA8Unorm, &capabilities) == WGPUStatus_Error;
        passed &= capabilities.nextInChain == chain;
        for (size_t i = 0; i < sizeof(chain); ++i) passed &= chain[i] == 0xa5;
        capabilities.nextInChain = NULL;
        passed &= direct(adapter, WGPUTextureFormat_RGBA8Unorm, &capabilities) == WGPUStatus_Error;
        passed &= direct(NULL, WGPUTextureFormat_Undefined, &capabilities) == WGPUStatus_Error;
        release_adapter(adapter);
    }
    release_instance(instance);
    dlclose(library);
    if (!passed) return 1;
    puts("PASS: lookup stays in Doe; unadvertised format extensions reject without writing output");
    return 0;
}
