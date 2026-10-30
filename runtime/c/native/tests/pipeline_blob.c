/* SPDX-License-Identifier: MIT
 * Embedder pipeline blob store on a real Vulkan device: miss then store, memory
 * reuse without a driver load or store, a driver-accepted hit on a fresh device, a corrupt value
 * treated as a miss, unlocked callbacks and identical dispatch output. */
#include "consumer.h"
#include "doe_native_pipeline_cache.h"
#include <stdlib.h>

static const char *const kernel =
    "@group(0) @binding(0) var<storage, read_write> out : array<u32>;\n"
    "@compute @workgroup_size(64) fn main(@builtin(global_invocation_id) id : vec3u) {\n"
    "  out[id.x] = id.x * 3u + 1u;\n}\n";
typedef struct store {
    WGPUDevice device; /* Callbacks query this device to prove no Doe lock is held. */
    uint8_t key[32];
    uint8_t *value;
    size_t size;
    unsigned loads, hits, stores;
} store;
static void blob_load(void *userdata, const void *key, size_t key_size,
    DoeNativePipelineBlobReceive receive, void *receiver)
{
    store *s = userdata;
    DoeNativePipelineCacheInfo info;
    assert(doeNativeDevicePipelineCacheInfo(s->device, DOE_NATIVE_PIPELINE_CACHE_ABI_VERSION,
        &info, sizeof(info)) == DOE_NATIVE_PIPELINE_CACHE_OK && info.enabled);
    assert(key_size == 32);
    ++s->loads;
    if (s->value && !memcmp(s->key, key, 32)) {
        ++s->hits;
        receive(receiver, s->value, s->size);
    }
}
static void blob_store(void *userdata, const void *key, size_t key_size, const void *value, size_t size)
{
    store *s = userdata;
    DoeNativePipelineCacheInfo info;
    assert(doeNativeDevicePipelineCacheInfo(s->device, DOE_NATIVE_PIPELINE_CACHE_ABI_VERSION,
        &info, sizeof(info)) == DOE_NATIVE_PIPELINE_CACHE_OK);
    assert(key_size == 32 && size > 96);
    free(s->value);
    s->value = malloc(size);
    assert(s->value);
    memcpy(s->key, key, 32);
    memcpy(s->value, value, size);
    s->size = size;
    ++s->stores;
}
static void configure(state *device, store *blobs)
{
    blobs->device = device->device;
    DoeNativePipelineBlobStore value = {.userdata=blobs,.load=blob_load,.store=blob_store};
    DoeNativePipelineCacheInfo info;
    assert(doeNativeDeviceConfigurePipelineBlobStore(device->device, DOE_NATIVE_PIPELINE_CACHE_ABI_VERSION,
        &value, sizeof(value), &info, sizeof(info)) == DOE_NATIVE_PIPELINE_CACHE_OK && info.enabled);
}
static void run(state *s)
{
    WGPUComputePipeline compute = pipeline(s, kernel);
    WGPUBuffer output = buffer(s, 1024, WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc);
    WGPUBuffer readback = buffer(s, 1024, WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst);
    WGPUBindGroupEntry entry = {.binding=0,.buffer=output,.size=1024};
    WGPUBindGroup bindings = group(s, compute, &entry, 1);
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device, NULL);
    dispatch(encoder, compute, bindings);
    wgpuCommandEncoderCopyBufferToBuffer(encoder, output, 0, readback, 0, 1024);
    submit(s, encoder);
    work_done(s);
    const uint32_t *words = map(s, readback, 1024);
    for (uint32_t i = 0; i < 256; ++i) assert(words[i] == i * 3u + 1u);
    wgpuBufferUnmap(readback);
    wgpuBindGroupRelease(bindings);
    wgpuBufferRelease(readback);
    wgpuBufferRelease(output);
    wgpuComputePipelineRelease(compute);
}
static DoeNativePipelineCacheInfo info_of(state *s)
{
    DoeNativePipelineCacheInfo info;
    assert(doeNativeDevicePipelineCacheInfo(s->device, DOE_NATIVE_PIPELINE_CACHE_ABI_VERSION,
        &info, sizeof(info)) == DOE_NATIVE_PIPELINE_CACHE_OK);
    return info;
}
int main(void)
{
    store blobs = {0};
    state first = {0}, second = {0}, third = {0};
    consumer_init(&first);
    DoeNativePipelineCacheInfo info;
    DoeNativePipelineBlobStore missing = {.userdata=&blobs,.load=blob_load};
    assert(doeNativeDeviceConfigurePipelineBlobStore(first.device, DOE_NATIVE_PIPELINE_CACHE_ABI_VERSION,
        &missing, sizeof(missing), &info, sizeof(info)) == DOE_NATIVE_PIPELINE_CACHE_INVALID_ARGUMENT);
    assert(doeNativeDeviceConfigurePipelineBlobStore(first.device, DOE_NATIVE_PIPELINE_CACHE_ABI_VERSION,
        &missing, sizeof(missing) - 1, &info, sizeof(info)) == DOE_NATIVE_PIPELINE_CACHE_INVALID_ARGUMENT);
    configure(&first, &blobs);
    DoeNativePipelineBlobStore again = {.userdata=&blobs,.load=blob_load,.store=blob_store};
    assert(doeNativeDeviceConfigurePipelineBlobStore(first.device, DOE_NATIVE_PIPELINE_CACHE_ABI_VERSION,
        &again, sizeof(again), &info, sizeof(info)) == DOE_NATIVE_PIPELINE_CACHE_ERROR && info.message[0]);
    assert(doeNativeDeviceConfigurePipelineCache(first.device, DOE_NATIVE_PIPELINE_CACHE_ABI_VERSION,
        "unused", 6, &info, sizeof(info)) != DOE_NATIVE_PIPELINE_CACHE_OK);
    run(&first);
    assert(blobs.loads == 1 && blobs.hits == 0 && blobs.stores == 1);
    run(&first); /* Identical native pipeline reuse: a lookup may precede it, but no driver load or store. */
    assert(blobs.loads == 2 && blobs.hits == 1 && blobs.stores == 1);
    info = info_of(&first);
    assert(info.load_attempts == 1 && info.load_successes == 0 && info.serializations == 1);

    consumer_init(&second);
    configure(&second, &blobs);
    run(&second);
    info = info_of(&second);
    assert(blobs.loads == 3 && blobs.hits == 2 && blobs.stores == 1);
    assert(info.load_attempts == 1 && info.load_successes == 1 && info.serializations == 0);

    blobs.value[blobs.size - 1] ^= 0x5a; /* The payload digest no longer matches. */
    consumer_init(&third);
    configure(&third, &blobs);
    run(&third);
    info = info_of(&third);
    assert(blobs.loads == 4 && blobs.hits == 3 && blobs.stores == 2);
    assert(info.load_attempts == 1 && info.load_successes == 0 && info.serializations == 1);

    consumer_finish(&third);
    consumer_finish(&second);
    consumer_finish(&first);
    free(blobs.value);
    printf("PASS embedder pipeline blobs: miss/store, memory reuse, driver-accepted hit, corrupt miss, "
        "unlocked callbacks and exact output; %s\n", adapter_class_scope());
    return 0;
}
