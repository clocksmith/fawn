/* SPDX-License-Identifier: MIT */
#include "consumer.h"
#include "doe_native_device_policy.h"
static void refused(WGPURequestDeviceStatus status, WGPUDevice device, WGPUStringView message,
    void *data, void *unused) {
    (void)message; (void)unused;
    assert(status == WGPURequestDeviceStatus_Error && !device);
    ++*(unsigned *)data;
}
int main(void) {
    state s = {0}; consumer_init(&s);
    assert(doeNativeQueueSyncInfo(s.queue) & DOE_NATIVE_QUEUE_SYNC_FENCE_POOL);
    for (unsigned mode = 0; mode < 2; ++mode) {
        state selected = {.instance=s.instance,.adapter=s.adapter};
        DoeNativeDevicePolicy policy = {.queue_selection=DOE_NATIVE_QUEUE_PREFER_GRAPHICS_COMPUTE,.synchronization=mode,.upload_policy=DOE_NATIVE_UPLOAD_STAGED_COPY};
        WGPUDeviceDescriptor descriptor = WGPU_DEVICE_DESCRIPTOR_INIT;
        descriptor.uncapturedErrorCallbackInfo = (WGPUUncapturedErrorCallbackInfo){.callback=error_reply,.userdata1=&selected};
        WGPURequestDeviceCallbackInfo callback = {.mode=WGPUCallbackMode_AllowSpontaneous,.callback=device_reply,.userdata1=&selected};
        assert(doeNativeAdapterRequestDeviceWithPolicy(s.adapter, &descriptor, callback,
            DOE_NATIVE_DEVICE_POLICY_ABI_VERSION, &policy, sizeof(policy)).id && selected.device);
        selected.queue = wgpuDeviceGetQueue(selected.device);
        uint32_t sync = doeNativeQueueSyncInfo(selected.queue);
        assert(sync & DOE_NATIVE_QUEUE_SYNC_BACKEND_VULKAN);
        assert(sync & (mode ? DOE_NATIVE_QUEUE_SYNC_TIMELINE_SEMAPHORE : DOE_NATIVE_QUEUE_SYNC_FENCE_POOL));
        WGPUBuffer destination = buffer(&selected, 16, WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead);
        for (unsigned iteration = 0; iteration < 4; ++iteration) {
            WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(selected.device, NULL);
            wgpuCommandEncoderClearBuffer(encoder, destination, 0, 16);
            submit(&selected, encoder); work_done(&selected);
        }
        uint32_t input[4] = {1,2,3,4};
        wgpuQueueWriteBuffer(selected.queue, destination, 0, input, sizeof(input));
        work_done(&selected);
        WGPUBufferMapCallbackInfo mapped_info = {.mode=WGPUCallbackMode_AllowSpontaneous,.callback=mapped,.userdata1=&selected};
        assert(wgpuBufferMapAsync(destination, WGPUMapMode_Read, 0, 16, mapped_info).id && selected.mapped == 1);
        assert(!memcmp(wgpuBufferGetConstMappedRange(destination,0,16), input, sizeof(input)));
        wgpuBufferUnmap(destination);
        assert(!selected.errors);
        wgpuBufferRelease(destination); wgpuQueueRelease(selected.queue); wgpuDeviceRelease(selected.device);
        assert(doeNativeQueueSyncInfo(s.queue) & DOE_NATIVE_QUEUE_SYNC_FENCE_POOL); /* Adapter/default device unchanged. */
    }
    unsigned failures = 0;
    WGPURequestDeviceCallbackInfo callback = {.mode=WGPUCallbackMode_AllowSpontaneous,.callback=refused,.userdata1=&failures};
    DoeNativeDevicePolicy invalid = {.upload_policy=99};
    assert(doeNativeAdapterRequestDeviceWithPolicy(s.adapter, NULL, callback, 1, &invalid, sizeof(invalid)).id);
    assert(doeNativeAdapterRequestDeviceWithPolicy(s.adapter, NULL, callback, 2, &invalid, sizeof(invalid)).id);
    assert(failures == 2);
    consumer_finish(&s);
    puts("PASS per-device Vulkan fence/timeline policy, same adapter default unchanged, repeated submissions and unsupported/version callbacks; software execution only");
}
