/* SPDX-License-Identifier: MIT
 * Deliberately obscure completion at the real backend-call boundary after an
 * actual software submission. Production code receives typed errors/unknown
 * release, never a test-only state mutation API. */
#include "../src/internal.h"
#include "consumer.h"

static atomic_bool arm, hold;
static _Atomic(doe_vk_commands *) obscured;
static _Atomic(doe_vk_status) injected = DOE_VK_NATIVE_FAILURE;
doe_vk_status __real_doe_vk_commands_submit(doe_vk_commands *, doe_vk_error *);
doe_vk_status __real_doe_vk_commands_poll(doe_vk_commands *, doe_vk_error *);
doe_vk_status __real_doe_vk_commands_recover(doe_vk_commands *, doe_vk_error *);
doe_vk_status __real_doe_vk_commands_release(doe_vk_commands *);
doe_vk_status __wrap_doe_vk_commands_submit(doe_vk_commands *commands, doe_vk_error *error)
{
    if (arm) { obscured = commands; arm = false; }
    return __real_doe_vk_commands_submit(commands,error);
}
doe_vk_status __wrap_doe_vk_commands_recover(doe_vk_commands *commands, doe_vk_error *error)
{
    doe_vk_status actual = __real_doe_vk_commands_recover(commands,error);
    if (commands == obscured) { assert(actual == DOE_VK_OK); return injected; }
    return actual;
}
doe_vk_status __wrap_doe_vk_commands_poll(doe_vk_commands *commands, doe_vk_error *error)
{
    doe_vk_status actual = __real_doe_vk_commands_poll(commands,error);
    if (commands == obscured) { assert(actual == DOE_VK_OK); return injected; }
    return actual;
}
doe_vk_status __wrap_doe_vk_commands_release(doe_vk_commands *commands)
{
    if (commands && commands == obscured && hold) return DOE_VK_BUSY;
    doe_vk_status status = __real_doe_vk_commands_release(commands);
    if (commands && commands == obscured && status == DOE_VK_OK) obscured = NULL;
    return status;
}
static void error_type(WGPUPopErrorScopeStatus status, WGPUErrorType type,
    WGPUStringView message, void *userdata, void *unused)
{
    (void)message; (void)unused;
    assert(status == WGPUPopErrorScopeStatus_Success && type == *(WGPUErrorType *)userdata);
    *(WGPUErrorType *)userdata = WGPUErrorType_NoError;
}
static void pop(state *s, WGPUErrorType expected)
{
    WGPUPopErrorScopeCallbackInfo callback = {.mode=WGPUCallbackMode_AllowSpontaneous,
        .callback=error_type,.userdata1=&expected};
    assert(wgpuDevicePopErrorScope(s->device,callback).id && expected == WGPUErrorType_NoError);
}
static WGPUCommandBuffer empty_commands(state *s)
{
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device,NULL);
    WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder,NULL);
    wgpuCommandEncoderRelease(encoder);
    assert(commands);
    return commands;
}
int main(void)
{
    state s = {0};
    s.instance = wgpuCreateInstance(NULL);
    WGPURequestAdapterOptions options = WGPU_REQUEST_ADAPTER_OPTIONS_INIT;
    adapter_class_options(&options);
    WGPURequestAdapterCallbackInfo adapter = {.mode=WGPUCallbackMode_AllowSpontaneous,.callback=adapter_reply,.userdata1=&s};
    assert(wgpuInstanceRequestAdapter(s.instance,&options,adapter).id && s.adapter);
    adapter_class_require(s.adapter);
    WGPUFeatureName feature = WGPUFeatureName_TimestampQuery;
    WGPUDeviceDescriptor descriptor = WGPU_DEVICE_DESCRIPTOR_INIT;
    descriptor.requiredFeatureCount = 1;
    descriptor.requiredFeatures = &feature;
    descriptor.uncapturedErrorCallbackInfo = (WGPUUncapturedErrorCallbackInfo){.callback=error_reply,.userdata1=&s};
    WGPURequestDeviceCallbackInfo requested = {.mode=WGPUCallbackMode_AllowSpontaneous,.callback=device_reply,.userdata1=&s};
    assert(wgpuAdapterRequestDevice(s.adapter,&descriptor,requested).id && s.device);
    s.queue = wgpuDeviceGetQueue(s.device);
    native_device *device = (native_device *)s.device;
    assert(device->timestamp_normalizer);
    assert(native_dispatch_guard_initialize(device) == DOE_VK_OK);
    assert(native_draw_guard_initialize(device) == DOE_VK_OK);
    WGPUBuffer target = buffer(&s,8,WGPUBufferUsage_CopyDst|WGPUBufferUsage_MapRead);
    WGPUBuffer retained = buffer(&s,8,WGPUBufferUsage_CopyDst);
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s.device,NULL);
    wgpuCommandEncoderClearBuffer(encoder,retained,0,8);
    WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder,NULL);
    wgpuCommandEncoderRelease(encoder);
    unsigned references = atomic_load(&device->references);
    arm = hold = true;
    wgpuDevicePushErrorScope(s.device,WGPUErrorFilter_Internal);
    wgpuQueueSubmit(s.queue,1,&commands);
    pop(&s,WGPUErrorType_Internal);
    assert(device->pending && atomic_load(&device->references) > references);
    wgpuCommandBufferRelease(commands);
    wgpuBufferRelease(retained);
    uint64_t value = 42;
    wgpuDevicePushErrorScope(s.device,WGPUErrorFilter_Internal);
    wgpuQueueWriteBuffer(s.queue,target,0,&value,sizeof(value));
    pop(&s,WGPUErrorType_Internal);
    WGPUTextureDescriptor texture_desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    texture_desc.size = (WGPUExtent3D){1,1,1};
    texture_desc.format = WGPUTextureFormat_RGBA8Unorm;
    texture_desc.usage = WGPUTextureUsage_TextureBinding;
    wgpuDevicePushErrorScope(s.device,WGPUErrorFilter_Internal);
    assert(!wgpuDeviceCreateTexture(s.device,&texture_desc));
    pop(&s,WGPUErrorType_Internal);
    commands = empty_commands(&s);
    wgpuDevicePushErrorScope(s.device,WGPUErrorFilter_Internal);
    wgpuQueueSubmit(s.queue,1,&commands);
    pop(&s,WGPUErrorType_Internal);
    wgpuCommandBufferRelease(commands);
    wgpuDevicePushErrorScope(s.device,WGPUErrorFilter_Internal);
    assert(!wgpuQueueOnSubmittedWorkDone(s.queue,(WGPUQueueWorkDoneCallbackInfo){.mode=WGPUCallbackMode_AllowSpontaneous}).id);
    pop(&s,WGPUErrorType_Internal);
    injected = DOE_VK_OUT_OF_MEMORY;
    wgpuDevicePushErrorScope(s.device,WGPUErrorFilter_OutOfMemory);
    wgpuQueueWriteBuffer(s.queue,target,0,&value,sizeof(value));
    pop(&s,WGPUErrorType_OutOfMemory);
    injected = DOE_VK_PENDING;
    wgpuDevicePushErrorScope(s.device,WGPUErrorFilter_Internal);
    wgpuQueueWriteBuffer(s.queue,target,0,&value,sizeof(value));
    pop(&s,WGPUErrorType_Internal);
    assert(device->pending && device->timestamp_normalizer && device->dispatch_validator && device->draw_validator);
    injected = DOE_VK_NATIVE_FAILURE;
    hold = false;
    wgpuDevicePushErrorScope(s.device,WGPUErrorFilter_Internal);
    wgpuQueueWriteBuffer(s.queue,target,0,&value,sizeof(value));
    pop(&s,WGPUErrorType_Internal);
    assert(!device->pending && !device->timestamp_normalizer && !device->dispatch_validator &&
        !device->draw_validator && !obscured);
    const uint64_t *observed = map(&s,target,8);
    assert(*observed == 0); /* Failed writes were never performed. */
    wgpuBufferUnmap(target);
    wgpuQueueWriteBuffer(s.queue,target,0,&value,sizeof(value));
    observed = map(&s,target,8);
    assert(*observed == value);
    wgpuBufferUnmap(target);
    WGPUQuerySetDescriptor query_desc = {.type=WGPUQueryType_Timestamp,.count=1};
    WGPUQuerySet set = wgpuDeviceCreateQuerySet(s.device,&query_desc);
    WGPUBuffer resolved = buffer(&s,8,WGPUBufferUsage_QueryResolve);
    encoder = wgpuDeviceCreateCommandEncoder(s.device,NULL);
    wgpuCommandEncoderWriteTimestamp(encoder,set,0);
    wgpuCommandEncoderResolveQuerySet(encoder,set,0,1,resolved,0);
    submit(&s,encoder);
    assert(device->timestamp_normalizer && !device->pending && !s.errors);
    assert(native_dispatch_guard_initialize(device) == DOE_VK_OK);
    assert(native_draw_guard_initialize(device) == DOE_VK_OK);
    wgpuQuerySetRelease(set);
    wgpuBufferRelease(resolved);
    wgpuBufferRelease(target);
    wgpuQueueRelease(s.queue);
    wgpuDeviceRelease(s.device);
    wgpuAdapterRelease(s.adapter);
    wgpuInstanceRelease(s.instance);
    printf("PASS typed recovery: native/OOM/pending errors survive submit/write/texture/workdone, unknown leases retained, terminal cleanup and fresh GPU normalizer/indirect guard recovery %s\n", adapter_class_scope());
}
