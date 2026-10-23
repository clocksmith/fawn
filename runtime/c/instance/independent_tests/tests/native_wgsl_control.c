/* Post-handoff realization of presealed P06: unchanged WGSL control exports. */
#include <assert.h>
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <webgpu.h>

static void load(void *library, const char *name, void *out, size_t size) {
    void *symbol = dlsym(library, name);
    assert(symbol && size == sizeof(symbol));
    memcpy(out, &symbol, size);
}

int main(int argc, char **argv) {
    assert(argc == 2);
    void *library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    assert(library);
    WGPUProcCreateInstance create;
    WGPUProcInstanceRelease release;
    WGPUProcGetProcAddress get_proc;
    load(library, "wgpuCreateInstance", &create, sizeof(create));
    load(library, "wgpuInstanceRelease", &release, sizeof(release));
    load(library, "wgpuGetProcAddress", &get_proc, sizeof(get_proc));
    puts("[");
    for (int mode = 0; mode < 3; ++mode) {
        WGPUProcInstanceGetWGSLLanguageFeatures get;
        WGPUProcInstanceHasWGSLLanguageFeature has;
        WGPUProcSupportedWGSLLanguageFeaturesFreeMembers free_members;
        const char *names[] = {"wgpuInstanceGetWGSLLanguageFeatures",
            "wgpuInstanceHasWGSLLanguageFeature",
            "wgpuSupportedWGSLLanguageFeaturesFreeMembers"};
        void *outputs[] = {&get, &has, &free_members};
        size_t sizes[] = {sizeof(get), sizeof(has), sizeof(free_members)};
        for (size_t i = 0; i < 3; ++i) {
            if (!mode) load(library, names[i], outputs[i], sizes[i]);
            else {
                WGPUStringView name = {names[i], mode == 1 ? strlen(names[i]) : SIZE_MAX};
                WGPUProc proc = get_proc(name);
                assert(proc && sizeof(proc) == sizes[i]);
                memcpy(outputs[i], &proc, sizes[i]);
            }
        }
        WGPUInstance instance = create(NULL);
        assert(instance);
        WGPUSupportedWGSLLanguageFeatures features;
        memset(&features, 0x5a, sizeof(features));
        get(instance, &features);
        assert(features.featureCount <= 1024);
        assert(!features.featureCount || features.features);
        printf("{\"count\":%zu,\"features\":[", features.featureCount);
        for (size_t i = 0; i < features.featureCount; ++i)
            printf("%s%u", i ? "," : "", (unsigned)features.features[i]);
        printf("],\"has\":[");
        for (unsigned i = 0; i <= 12; ++i)
            printf("%s%u", i ? "," : "", (unsigned)has(instance, (WGPUWGSLLanguageFeatureName)i));
        printf(",%u]}", (unsigned)has(instance, (WGPUWGSLLanguageFeatureName)0x7fffffff));
        free_members(features);
        release(instance);
        puts(mode == 2 ? "" : ",");
    }
    puts("]");
    assert(dlclose(library) == 0);
    return 0;
}
