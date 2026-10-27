/* SPDX-License-Identifier: MIT */
#include "../src/internal.h"
#include "../src/shader_disk_cache.h"
#include "consumer.h"
#include "doe_native_device_policy.h"
#include "doe_native_pipeline_cache.h"

#if defined(DOE_TEST_DISK_WRAPPERS)
static unsigned lookups, stores;
doe_shader *__real_native_shader_disk_lookup(const uint8_t [32], const doe_shader *,
    const DoeCompilerEntrySelectionV5 *, const DoeCompilerOverrideValueV13 *, size_t,
    const DoeCompilerMetalPipelineOptionsV15 *);
void __real_native_shader_disk_store(const uint8_t [32], const doe_shader *,
    const DoeCompilerEntrySelectionV5 *, const DoeCompilerOverrideValueV13 *, size_t,
    const DoeCompilerMetalPipelineOptionsV15 *, const doe_shader *);
doe_shader *__wrap_native_shader_disk_lookup(const uint8_t image[32], const doe_shader *module,
    const DoeCompilerEntrySelectionV5 *selection, const DoeCompilerOverrideValueV13 *values,
    size_t count, const DoeCompilerMetalPipelineOptionsV15 *options)
{
    ++lookups;
    return __real_native_shader_disk_lookup(image,module,selection,values,count,options);
}
void __wrap_native_shader_disk_store(const uint8_t image[32], const doe_shader *module,
    const DoeCompilerEntrySelectionV5 *selection, const DoeCompilerOverrideValueV13 *values,
    size_t count, const DoeCompilerMetalPipelineOptionsV15 *options, const doe_shader *compiled)
{
    ++stores;
    __real_native_shader_disk_store(image,module,selection,values,count,options,compiled);
}
#endif

static void check_stage(state *s, bool disabled)
{
    native_device *device = (native_device *)s->device;
    assert(device->persistent_cache == NATIVE_PERSISTENT_CACHE_FRESH);
    if (disabled) assert(doeNativeDeviceDisablePersistentCache(s->device) == WGPUStatus_Success);
    WGPUComputePipeline first = pipeline(s,"@compute @workgroup_size(1) fn main(){}");
    WGPUComputePipeline second = pipeline(s,"@compute @workgroup_size(1) fn main(){}");
    assert(!s->errors);
    assert(device->persistent_cache == (disabled ?
        NATIVE_PERSISTENT_CACHE_DISABLED : NATIVE_PERSISTENT_CACHE_USED));
    /* Disabled means no loaded-image identity or persistent path was consulted.
     * The ordinary device memory cache still holds both module and target. */
    assert(disabled ? !device->shaders.image_identity_ready : device->shaders.image_identity_ready);
    size_t entries = 0;
    for (size_t i = 0; i < NATIVE_SHADER_CACHE_SLOTS; ++i)
        if (device->shaders.entries[i]) ++entries;
    assert(entries == 2);
    assert(doeNativeDeviceDisablePersistentCache(s->device) ==
        (disabled ? WGPUStatus_Success : WGPUStatus_Error));
    DoeNativePipelineCacheInfo info;
    const uint32_t status = doeNativeDeviceConfigurePipelineCache(s->device,
        DOE_NATIVE_PIPELINE_CACHE_ABI_VERSION,NULL,0,&info,sizeof(info));
    assert(status == (disabled ? DOE_NATIVE_PIPELINE_CACHE_ERROR : DOE_NATIVE_PIPELINE_CACHE_OK));
    wgpuComputePipelineRelease(second);
    wgpuComputePipelineRelease(first);
}

typedef struct cache_race {
    native_device *device;
    WGPUStatus status;
    bool reserved;
} cache_race;
static void disable_thread(void *userdata)
{
    cache_race *race = userdata;
    race->status = doeNativeDeviceDisablePersistentCache((WGPUDevice)race->device);
}
static void reserve_thread(void *userdata)
{
    cache_race *race = userdata;
    native_lock(race->device);
    race->reserved = native_persistent_cache_reserve(&race->device->persistent_cache);
    native_unlock(race->device);
}
static void race_controls(state *s)
{
    /* Every iteration owns a real, fresh device. The cache reservation seam is
     * the same one stage loading executes before dropping its device mutex. */
    for (unsigned i = 0; i < 32; ++i) {
        state selected = {0};
        WGPURequestDeviceCallbackInfo callback = {.mode=WGPUCallbackMode_AllowSpontaneous,
            .callback=device_reply,.userdata1=&selected};
        assert(wgpuAdapterRequestDevice(s->adapter,NULL,callback).id && selected.device);
        cache_race race = {.device=(native_device *)selected.device};
        doe_thread disable = {0}, reserve = {0};
        assert(doe_thread_create(&disable,disable_thread,&race).status == DOE_HOST_OK);
        assert(doe_thread_create(&reserve,reserve_thread,&race).status == DOE_HOST_OK);
        assert(doe_thread_join(&disable).status == DOE_HOST_OK);
        assert(doe_thread_join(&reserve).status == DOE_HOST_OK);
        assert(race.reserved == (race.status == WGPUStatus_Error));
        wgpuDeviceRelease(selected.device);
    }
}
static void lost(WGPUDevice const *device, WGPUDeviceLostReason reason,
    WGPUStringView message, void *userdata, void *unused)
{
    (void)device; (void)message; (void)unused;
    assert(reason == WGPUDeviceLostReason_Destroyed);
    ++*(unsigned *)userdata;
}
static void rejected_policy_cleanup(state *s)
{
    state selected = {0};
    unsigned losses = 0;
    WGPUDeviceDescriptor descriptor = WGPU_DEVICE_DESCRIPTOR_INIT;
    descriptor.deviceLostCallbackInfo = (WGPUDeviceLostCallbackInfo){
        .mode=WGPUCallbackMode_AllowProcessEvents,.callback=lost,.userdata1=&losses};
    WGPURequestDeviceCallbackInfo callback = {.mode=WGPUCallbackMode_AllowSpontaneous,
        .callback=device_reply,.userdata1=&selected};
    assert(wgpuAdapterRequestDevice(s->adapter,&descriptor,callback).id && selected.device);
    DoeNativePipelineCacheInfo info;
    assert(doeNativeDeviceConfigurePipelineCache(selected.device,
        DOE_NATIVE_PIPELINE_CACHE_ABI_VERSION,NULL,0,&info,sizeof(info)) == DOE_NATIVE_PIPELINE_CACHE_OK);
    assert(doeNativeDeviceDisablePersistentCache(selected.device) == WGPUStatus_Error);
    wgpuDeviceDestroy(selected.device);
    assert(doeNativeDeviceDisablePersistentCache(selected.device) == WGPUStatus_Error);
    wgpuDeviceRelease(selected.device);
    assert(losses == 0);
    wgpuInstanceProcessEvents(s->instance);
    assert(losses == 1);
    wgpuInstanceProcessEvents(s->instance);
    assert(losses == 1);
}
int main(void)
{
    assert(doeNativeDeviceDisablePersistentCache(NULL) == WGPUStatus_Error);
    assert(!wgpuGetProcAddress((WGPUStringView){"doeNativeDeviceDisablePersistentCache",WGPU_STRLEN}));
    state enabled = {0}, disabled = {0};
    consumer_init(&disabled);
    check_stage(&disabled,true);
#if defined(DOE_TEST_DISK_WRAPPERS)
    assert(lookups == 0 && stores == 0);
#endif
    consumer_init(&enabled);
    check_stage(&enabled,false);
#if defined(DOE_TEST_DISK_WRAPPERS)
    assert(lookups == 1 && stores == 1); /* Positive control; second stage is a memory hit. */
#endif
    race_controls(&enabled);
    rejected_policy_cleanup(&enabled);
    consumer_finish(&enabled);
    consumer_finish(&disabled);
    printf("PASS private device persistent-cache opt-out, real stage memory reuse, late/configure rejection, 32 races and rejected-device loss cleanup; %s\n",adapter_class_scope());
    return 0;
}
