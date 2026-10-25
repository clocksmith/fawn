/* SPDX-License-Identifier: MIT
 * Public two-device isolation and finite WaitAnyOnly delivery control. */
#include "adapter_class.h"
#include <stdint.h>
#include <string.h>

typedef struct device_state {
    WGPUDevice device;
    WGPUQueue queue;
    WGPUBuffer readback;
    unsigned devices, maps, done, errors;
    uint32_t expected[16];
} device_state;

static void adapter_reply(WGPURequestAdapterStatus status, WGPUAdapter adapter,
    WGPUStringView message, void *userdata, void *unused)
{
    (void)unused;
    if (status != WGPURequestAdapterStatus_Success)
        fprintf(stderr, "adapter request: %.*s\n", (int)message.length, message.data);
    assert(status == WGPURequestAdapterStatus_Success && adapter);
    *(WGPUAdapter *)userdata = adapter;
}

static void device_reply(WGPURequestDeviceStatus status, WGPUDevice device,
    WGPUStringView message, void *userdata, void *unused)
{
    (void)unused;
    if (status != WGPURequestDeviceStatus_Success)
        fprintf(stderr, "device request: %.*s\n", (int)message.length, message.data);
    assert(status == WGPURequestDeviceStatus_Success && device);
    device_state *state = userdata;
    state->device = device;
    ++state->devices;
}

static void error_reply(WGPUDevice const *device, WGPUErrorType type,
    WGPUStringView message, void *userdata, void *unused)
{
    (void)device;
    (void)type;
    (void)unused;
    ++((device_state *)userdata)->errors;
    fprintf(stderr, "unexpected device error: %.*s\n", (int)message.length, message.data);
}

static void map_reply(WGPUMapAsyncStatus status, WGPUStringView message,
    void *userdata, void *unused)
{
    (void)unused;
    if (status != WGPUMapAsyncStatus_Success)
        fprintf(stderr, "map: %.*s\n", (int)message.length, message.data);
    assert(status == WGPUMapAsyncStatus_Success);
    ++((device_state *)userdata)->maps;
}

static void done_reply(WGPUQueueWorkDoneStatus status, WGPUStringView message,
    void *userdata, void *unused)
{
    (void)unused;
    if (status != WGPUQueueWorkDoneStatus_Success)
        fprintf(stderr, "work done: %.*s\n", (int)message.length, message.data);
    assert(status == WGPUQueueWorkDoneStatus_Success);
    ++((device_state *)userdata)->done;
}

static void wait_ready(WGPUInstance instance, WGPUFuture future)
{
    assert(future.id);
    WGPUFutureWaitInfo info = {.future = future};
    assert(wgpuInstanceWaitAny(instance, 1, &info, UINT64_C(5000000000)) ==
        WGPUWaitStatus_Success);
    assert(info.completed);
}

static void record_copy(device_state *state, uint32_t seed)
{
    for (uint32_t i = 0; i < 16; ++i) state->expected[i] = seed + i * 37u;
    WGPUBufferDescriptor source_desc = {.size = sizeof(state->expected),
        .usage = WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst};
    WGPUBuffer source = wgpuDeviceCreateBuffer(state->device, &source_desc);
    WGPUBufferDescriptor read_desc = {.size = sizeof(state->expected),
        .usage = WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst};
    state->readback = wgpuDeviceCreateBuffer(state->device, &read_desc);
    assert(source && state->readback);
    wgpuQueueWriteBuffer(state->queue, source, 0, state->expected, sizeof(state->expected));
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(state->device, NULL);
    assert(encoder);
    wgpuCommandEncoderCopyBufferToBuffer(encoder, source, 0, state->readback, 0,
        sizeof(state->expected));
    WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, NULL);
    assert(commands);
    wgpuCommandEncoderRelease(encoder);
    wgpuQueueSubmit(state->queue, 1, &commands);
    wgpuCommandBufferRelease(commands);
    wgpuBufferRelease(source);
    assert(!state->errors);
}

static void verify_copy(WGPUInstance instance, device_state *state)
{
    unsigned before_done = state->done;
    WGPUQueueWorkDoneCallbackInfo done = {.mode = WGPUCallbackMode_WaitAnyOnly,
        .callback = done_reply, .userdata1 = state};
    WGPUFuture future = wgpuQueueOnSubmittedWorkDone(state->queue, done);
    assert(state->done == before_done);
    wait_ready(instance, future);
    assert(state->done == before_done + 1);
    unsigned before_maps = state->maps;
    WGPUBufferMapCallbackInfo map = {.mode = WGPUCallbackMode_WaitAnyOnly,
        .callback = map_reply, .userdata1 = state};
    future = wgpuBufferMapAsync(state->readback, WGPUMapMode_Read, 0,
        sizeof(state->expected), map);
    assert(state->maps == before_maps);
    wait_ready(instance, future);
    assert(state->maps == before_maps + 1);
    const void *mapped = wgpuBufferGetConstMappedRange(state->readback, 0,
        sizeof(state->expected));
    assert(mapped && !memcmp(mapped, state->expected, sizeof(state->expected)));
    wgpuBufferUnmap(state->readback);
    wgpuBufferRelease(state->readback);
    state->readback = NULL;
    assert(!state->errors);
}

int main(void)
{
    const WGPUInstanceFeatureName required[] = {
        WGPUInstanceFeatureName_TimedWaitAny,
        WGPUInstanceFeatureName_MultipleDevicesPerAdapter
    };
    WGPUSupportedInstanceFeatures supported = WGPU_SUPPORTED_INSTANCE_FEATURES_INIT;
    wgpuGetInstanceFeatures(&supported);
    for (size_t i = 0; i < 2; ++i) {
        assert(wgpuHasInstanceFeature(required[i]));
        bool found = false;
        for (size_t j = 0; j < supported.featureCount; ++j)
            found |= supported.features[j] == required[i];
        assert(found);
    }
    wgpuSupportedInstanceFeaturesFreeMembers(supported);
    WGPUInstanceLimits limits = WGPU_INSTANCE_LIMITS_INIT;
    assert(wgpuGetInstanceLimits(&limits) == WGPUStatus_Success);
    assert(limits.timedWaitAnyMaxCount >= 2);
    limits.timedWaitAnyMaxCount = 2;
    WGPUInstanceDescriptor descriptor = WGPU_INSTANCE_DESCRIPTOR_INIT;
    descriptor.requiredFeatureCount = 2;
    descriptor.requiredFeatures = required;
    descriptor.requiredLimits = &limits;
    WGPUInstance instance = wgpuCreateInstance(&descriptor);
    assert(instance);
    WGPUAdapter adapter = NULL;
    WGPURequestAdapterOptions options = WGPU_REQUEST_ADAPTER_OPTIONS_INIT;
    adapter_class_options(&options);
    WGPURequestAdapterCallbackInfo adapter_info = {.mode = WGPUCallbackMode_WaitAnyOnly,
        .callback = adapter_reply, .userdata1 = &adapter};
    WGPUFuture future = wgpuInstanceRequestAdapter(instance, &options, adapter_info);
    assert(!adapter);
    wait_ready(instance, future);
    adapter_class_require(adapter);
    device_state states[2] = {{0}, {0}};
    WGPUFuture devices[2];
    for (size_t i = 0; i < 2; ++i) {
        WGPUDeviceDescriptor descriptor = WGPU_DEVICE_DESCRIPTOR_INIT;
        descriptor.uncapturedErrorCallbackInfo = (WGPUUncapturedErrorCallbackInfo){
            .callback = error_reply, .userdata1 = &states[i]};
        WGPURequestDeviceCallbackInfo callback = {.mode = WGPUCallbackMode_WaitAnyOnly,
            .callback = device_reply, .userdata1 = &states[i]};
        devices[i] = wgpuAdapterRequestDevice(adapter, &descriptor, callback);
        assert(devices[i].id && !states[i].devices);
    }
    for (size_t i = 0; i < 2; ++i) {
        wait_ready(instance, devices[i]);
        assert(states[i].devices == 1);
        states[i].queue = wgpuDeviceGetQueue(states[i].device);
        assert(states[i].queue);
    }
    assert(states[0].device != states[1].device);
    record_copy(&states[0], 17);
    record_copy(&states[1], 9001);
    verify_copy(instance, &states[1]);
    verify_copy(instance, &states[0]);
    wgpuQueueRelease(states[0].queue);
    wgpuDeviceRelease(states[0].device);
    record_copy(&states[1], 42001);
    verify_copy(instance, &states[1]);
    wgpuQueueRelease(states[1].queue);
    wgpuDeviceRelease(states[1].device);
    wgpuAdapterRelease(adapter);
    wgpuInstanceRelease(instance);
    assert(!states[0].errors && !states[1].errors);
    printf("PASS same-adapter two-device copies, isolation, surviving-device reuse and finite WaitAnyOnly delivery %s\n",
        adapter_class_scope());
    return 0;
}
