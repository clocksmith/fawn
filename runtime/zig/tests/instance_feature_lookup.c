// Exercise the shipped ABI, not the unit suite's wgpuGetProcAddress stub.
// cc -I vendor/webgpu-headers tests/instance_feature_lookup.c -o /tmp/instance-feature-lookup
// /tmp/instance-feature-lookup /absolute/path/libwebgpu_doe_full.dylib
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "webgpu.h"

int main(int argc, char **argv) {
    if (argc != 2) return 2;
    void *library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!library) {
        fprintf(stderr, "%s\n", dlerror());
        return 2;
    }
    WGPUProcGetProcAddress lookup = (WGPUProcGetProcAddress)dlsym(library, "wgpuGetProcAddress");
    WGPUProcHasInstanceFeature direct = (WGPUProcHasInstanceFeature)dlsym(library, "wgpuHasInstanceFeature");
    WGPUProcGetInstanceFeatures enumerate = (WGPUProcGetInstanceFeatures)dlsym(library, "wgpuGetInstanceFeatures");
    if (!lookup || !direct || !enumerate) return 1;
    const char *name = "wgpuHasInstanceFeature";
    const size_t lengths[] = {strlen(name), SIZE_MAX};
    WGPUSupportedInstanceFeatures features = {0};
    enumerate(&features);
    for (size_t i = 0; i < 2; ++i) {
        WGPUStringView view = {.data = name, .length = lengths[i]};
        WGPUProcHasInstanceFeature resolved = (WGPUProcHasInstanceFeature)lookup(view);
        if (resolved != direct) {
            fprintf(stderr, "instance feature query escaped the loaded Doe library\n");
            return 1;
        }
        for (uint32_t feature = 1; feature <= 3; ++feature) {
            WGPUBool advertised = 0;
            for (size_t j = 0; j < features.featureCount; ++j) {
                if (features.features[j] == (WGPUInstanceFeatureName)feature) advertised = 1;
            }
            if (resolved((WGPUInstanceFeatureName)feature) != advertised) return 1;
        }
        if (resolved((WGPUInstanceFeatureName)0x7fffffff) != 0) return 1;
    }
    WGPUProcSupportedInstanceFeaturesFreeMembers release =
        (WGPUProcSupportedInstanceFeaturesFreeMembers)dlsym(library, "wgpuSupportedInstanceFeaturesFreeMembers");
    if (!release) return 1;
    release(features);
    dlclose(library);
    puts("PASS: both name lengths resolve to Doe; capability answers match enumeration");
    return 0;
}
