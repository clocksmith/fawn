/* SPDX-License-Identifier: MIT
 * Public lifecycle calls plus read-only private label-owner inspection. */
#include "../src/internal.h"
#include "adapter_class.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct capture {
    WGPUInstance instance;
    WGPUAdapter adapter;
    WGPUDevice device;
    WGPUDeviceLostReason reason;
    WGPUDevice lost_device;
    WGPUComputePipeline pipeline;
    WGPUCreatePipelineAsyncStatus pipeline_status;
    unsigned lost, pipeline_calls, compilation_calls, uncaptured;
    size_t messages;
    bool expect_device_failure;
    pthread_mutex_t race_mutex;
    pthread_cond_t race_condition;
    bool race_active, race_entered, race_resume;
} capture;
static void adapter_reply(WGPURequestAdapterStatus status, WGPUAdapter adapter,
    WGPUStringView message, void *userdata1, void *userdata2)
{
    (void)message;
    (void)userdata2;
    assert(status == WGPURequestAdapterStatus_Success);
    ((capture *)userdata1)->adapter = adapter;
}
static void device_reply(WGPURequestDeviceStatus status, WGPUDevice device,
    WGPUStringView message, void *userdata1, void *userdata2)
{
    (void)message;
    (void)userdata2;
    capture *value = userdata1;
    assert(status == (value->expect_device_failure ? WGPURequestDeviceStatus_Error : WGPURequestDeviceStatus_Success));
    value->device = device;
}
static void loss_reply(WGPUDevice const *device, WGPUDeviceLostReason reason,
    WGPUStringView message, void *userdata1, void *userdata2)
{
    (void)message;
    (void)userdata2;
    capture *value = userdata1;
    assert(device);
    ++value->lost;
    value->lost_device = *device;
    value->reason = reason;
    if (value->race_active) {
        assert(*device);
        assert(!pthread_mutex_lock(&value->race_mutex));
        value->race_entered = true;
        assert(!pthread_cond_signal(&value->race_condition));
        while (!value->race_resume)
            assert(!pthread_cond_wait(&value->race_condition, &value->race_mutex));
        assert(!pthread_mutex_unlock(&value->race_mutex));
        assert(atomic_load(&((native_device *)*device)->external_references) == 0);
        wgpuDeviceAddRef(*device);
        assert(atomic_load(&((native_device *)*device)->external_references) == 1);
        value->device = *device;
    }
    if (*device) assert(wgpuDeviceGetLostFuture(*device).id);
    wgpuInstanceProcessEvents(value->instance);
}
static void pipeline_reply(WGPUCreatePipelineAsyncStatus status, WGPUComputePipeline pipeline,
    WGPUStringView message, void *userdata1, void *userdata2)
{
    (void)message;
    (void)userdata2;
    capture *value = userdata1;
    ++value->pipeline_calls;
    value->pipeline_status = status;
    value->pipeline = pipeline;
}
static void compilation_reply(WGPUCompilationInfoRequestStatus status,
    WGPUCompilationInfo const *info, void *userdata1, void *userdata2)
{
    (void)userdata2;
    capture *value = userdata1;
    assert(status == WGPUCompilationInfoRequestStatus_Success && info);
    ++value->compilation_calls;
    value->messages = info->messageCount;
    if (info->messageCount) {
        assert(info->messageCount == 1);
        assert(info->messages[0].type == WGPUCompilationMessageType_Error);
        assert(info->messages[0].message.length && info->messages[0].lineNum == 1);
    }
}
static void uncaptured(WGPUDevice const *device, WGPUErrorType type, WGPUStringView message,
    void *userdata1, void *userdata2)
{
    (void)device;
    (void)type;
    (void)message;
    (void)userdata2;
    ++((capture *)userdata1)->uncaptured;
}
static void wait_ready(WGPUInstance instance, WGPUFuture future)
{
    assert(future.id);
    WGPUFutureWaitInfo info = {.future=future};
    assert(wgpuInstanceWaitAny(instance, 1, &info, 0) == WGPUWaitStatus_Success && info.completed);
}
static void request_device(capture *value, WGPUCallbackMode mode)
{
    WGPUDeviceDescriptor descriptor = WGPU_DEVICE_DESCRIPTOR_INIT;
    descriptor.label = (WGPUStringView){"device",6};
    descriptor.defaultQueue.label = (WGPUStringView){"queue",5};
    descriptor.deviceLostCallbackInfo = (WGPUDeviceLostCallbackInfo){.mode=mode,.callback=loss_reply,.userdata1=value};
    descriptor.uncapturedErrorCallbackInfo = (WGPUUncapturedErrorCallbackInfo){.callback=uncaptured,.userdata1=value};
    WGPURequestDeviceCallbackInfo callback = {.mode=WGPUCallbackMode_AllowSpontaneous,.callback=device_reply,.userdata1=value};
    assert(wgpuAdapterRequestDevice(value->adapter, &descriptor, callback).id && value->device);
}
static WGPUShaderModule make_shader(WGPUDevice device, const char *text)
{
    WGPUShaderSourceWGSL source = WGPU_SHADER_SOURCE_WGSL_INIT;
    source.code = (WGPUStringView){text,WGPU_STRLEN};
    WGPUShaderModuleDescriptor descriptor = {.nextInChain=&source.chain,.label={"shader",6}};
    return wgpuDeviceCreateShaderModule(device, &descriptor);
}
static void *release_device_thread(void *argument)
{
    wgpuDeviceRelease((WGPUDevice)argument);
    return NULL;
}
static void *destroy_device_thread(void *argument)
{
    wgpuDeviceDestroy((WGPUDevice)argument);
    return NULL;
}
int main(void)
{
    capture value = {0};
    value.instance = wgpuCreateInstance(NULL);
    assert(value.instance);
    WGPURequestAdapterOptions options = WGPU_REQUEST_ADAPTER_OPTIONS_INIT;
    adapter_class_options(&options);
    WGPURequestAdapterCallbackInfo adapter = {.mode=WGPUCallbackMode_AllowSpontaneous,.callback=adapter_reply,.userdata1=&value};
    assert(wgpuInstanceRequestAdapter(value.instance, &options, adapter).id && value.adapter);
    adapter_class_require(value.adapter);
    /* With lavapipe as the only Vulkan device, a default request (no
     * forceFallbackAdapter) returns that CPU adapter instead of nothing. */
    const char *icd = getenv("VK_ICD_FILENAMES");
    if (!adapter_class_physical() && icd && strstr(icd, "lvp_icd")) {
        capture plain = {0};
        WGPURequestAdapterOptions defaults = WGPU_REQUEST_ADAPTER_OPTIONS_INIT;
        WGPURequestAdapterCallbackInfo reply = {.mode=WGPUCallbackMode_AllowSpontaneous,.callback=adapter_reply,.userdata1=&plain};
        assert(wgpuInstanceRequestAdapter(value.instance, &defaults, reply).id && plain.adapter);
        assert(adapter_class_is_cpu(plain.adapter));
        wgpuAdapterRelease(plain.adapter);
    }
    const WGPUCallbackMode modes[] = {WGPUCallbackMode_WaitAnyOnly,WGPUCallbackMode_AllowProcessEvents,WGPUCallbackMode_AllowSpontaneous};
    for (size_t i=0;i<3;++i) {
        value.lost=0;
        request_device(&value,modes[i]);
        WGPUFuture lost=wgpuDeviceGetLostFuture(value.device);
        assert(lost.id && wgpuDeviceGetLostFuture(value.device).id==lost.id);
        WGPUFutureWaitInfo pending={.future=lost};
        assert(wgpuInstanceWaitAny(value.instance,1,&pending,0)==WGPUWaitStatus_TimedOut && !pending.completed);
        wgpuDeviceDestroy(value.device);
        wgpuDeviceDestroy(value.device);
        if (modes[i]!=WGPUCallbackMode_AllowSpontaneous) assert(!value.lost);
        wait_ready(value.instance,lost);
        assert(value.lost==1 && value.reason==WGPUDeviceLostReason_Destroyed && value.lost_device==value.device);
        wgpuDeviceRelease(value.device);
    }
    value.lost=0;
    request_device(&value,WGPUCallbackMode_AllowProcessEvents);
    WGPUFuture lost=wgpuDeviceGetLostFuture(value.device);
    WGPUQueue queue=wgpuDeviceGetQueue(value.device);
    wgpuDeviceAddRef(value.device);
    wgpuDeviceRelease(value.device);
    assert(!value.lost);
    wgpuDeviceRelease(value.device);
    assert(!value.lost);
    wait_ready(value.instance,lost);
    assert(value.lost==1 && !value.lost_device && value.reason==WGPUDeviceLostReason_Destroyed);
    wgpuQueueRelease(queue);

    value.lost=0;
    request_device(&value,WGPUCallbackMode_AllowProcessEvents);
    lost=wgpuDeviceGetLostFuture(value.device);
    pthread_t releases[8];
    for (size_t i=1;i<8;++i) wgpuDeviceAddRef(value.device);
    for (size_t i=0;i<8;++i) assert(!pthread_create(&releases[i],NULL,release_device_thread,value.device));
    for (size_t i=0;i<8;++i) assert(!pthread_join(releases[i],NULL));
    wait_ready(value.instance,lost);
    assert(value.lost==1 && !value.lost_device);

    value.lost = 0;
    value.race_active = true;
    assert(!pthread_mutex_init(&value.race_mutex, NULL));
    assert(!pthread_cond_init(&value.race_condition, NULL));
    request_device(&value, WGPUCallbackMode_AllowSpontaneous);
    WGPUDevice borrowed = value.device;
    pthread_t destroy;
    assert(!pthread_create(&destroy, NULL, destroy_device_thread, borrowed));
    assert(!pthread_mutex_lock(&value.race_mutex));
    while (!value.race_entered)
        assert(!pthread_cond_wait(&value.race_condition, &value.race_mutex));
    wgpuDeviceRelease(borrowed);
    value.race_resume = true;
    assert(!pthread_cond_signal(&value.race_condition));
    assert(!pthread_mutex_unlock(&value.race_mutex));
    assert(!pthread_join(destroy, NULL));
    assert(value.lost == 1 && value.device == borrowed);
    assert(!native_device_alive((native_device *)value.device));
    wgpuDeviceRelease(value.device);
    value.race_active = false;
    assert(!pthread_cond_destroy(&value.race_condition));
    assert(!pthread_mutex_destroy(&value.race_mutex));

    value.lost=0;
    request_device(&value,WGPUCallbackMode_WaitAnyOnly);
    native_device *device=(native_device *)value.device;
    doe_label_view label;
    assert(doe_label_store_get(device->labels,device,&label)==DOE_LABEL_OK && label.length==6);
    assert(doe_label_store_get(device->labels,&device->queue_label_key,&label)==DOE_LABEL_OK && label.length==5);
    WGPUBufferDescriptor buffer_desc={.size=16,.usage=WGPUBufferUsage_CopyDst,.label={"buffer",6}};
    WGPUBuffer buffer=wgpuDeviceCreateBuffer(value.device,&buffer_desc);
    assert(buffer);
    char bytes[3]={'a',0,'b'};
    wgpuBufferSetLabel(buffer,(WGPUStringView){bytes,3});
    bytes[0]='z';
    assert(doe_label_store_get(device->labels,buffer,&label)==DOE_LABEL_OK && label.length==3 && label.data[0]=='a');
    wgpuBufferRelease(buffer);
    assert(doe_label_store_get(device->labels,buffer,&label)==DOE_LABEL_OK && !label.length);

    WGPUShaderModule shader=make_shader(value.device,"@compute @workgroup_size(1) fn main(){}");
    assert(shader);
    WGPUCompilationInfoCallbackInfo compilation={.mode=WGPUCallbackMode_WaitAnyOnly,.callback=compilation_reply,.userdata1=&value};
    WGPUFuture compiled=wgpuShaderModuleGetCompilationInfo(shader,compilation);
    WGPUComputePipelineDescriptor pipeline=WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
    pipeline.compute.module=shader;
    WGPUCreateComputePipelineAsyncCallbackInfo callback={.mode=WGPUCallbackMode_WaitAnyOnly,.callback=pipeline_reply,.userdata1=&value};
    WGPUFuture created=wgpuDeviceCreateComputePipelineAsync(value.device,&pipeline,callback);
    assert(created.id && !value.pipeline_calls);
    wgpuShaderModuleRelease(shader);
    wait_ready(value.instance,compiled);
    assert(value.compilation_calls==1 && !value.messages);
    wait_ready(value.instance,created);
    assert(value.pipeline_calls==1 && value.pipeline_status==WGPUCreatePipelineAsyncStatus_Success && value.pipeline);
    wgpuComputePipelineRelease(value.pipeline);
    unsigned errors=value.uncaptured;
    shader=make_shader(value.device,"@compute @workgroup_size(1) fn broken(){missing=1u;}");
    assert(shader && value.uncaptured==errors+1);
    pipeline.compute.module=shader;
    created=wgpuDeviceCreateComputePipelineAsync(value.device,&pipeline,callback);
    assert(value.uncaptured==errors+1);
    compiled=wgpuShaderModuleGetCompilationInfo(shader,compilation);
    wgpuShaderModuleRelease(shader);
    wait_ready(value.instance,created);
    assert(value.pipeline_calls==2 && value.pipeline_status==WGPUCreatePipelineAsyncStatus_ValidationError && !value.pipeline);
    wait_ready(value.instance,compiled);
    assert(value.compilation_calls==2 && value.messages==1);
    lost=wgpuDeviceGetLostFuture(value.device);
    wgpuDeviceRelease(value.device);
    wait_ready(value.instance,lost);

    value.lost=0;
    value.expect_device_failure=true;
    /* A required limit better than the adapter's fails creation. */
    WGPULimits adapter_limits=WGPU_LIMITS_INIT;
    assert(wgpuAdapterGetLimits(value.adapter,&adapter_limits)==WGPUStatus_Success);
    WGPULimits unsupported=WGPU_LIMITS_INIT;
    unsupported.maxBindGroups=adapter_limits.maxBindGroups+1;
    WGPUDeviceDescriptor failure=WGPU_DEVICE_DESCRIPTOR_INIT;
    failure.requiredLimits=&unsupported;
    failure.deviceLostCallbackInfo=(WGPUDeviceLostCallbackInfo){.mode=WGPUCallbackMode_AllowProcessEvents,.callback=loss_reply,.userdata1=&value};
    WGPURequestDeviceCallbackInfo failed={.mode=WGPUCallbackMode_AllowSpontaneous,.callback=device_reply,.userdata1=&value};
    assert(wgpuAdapterRequestDevice(value.adapter,&failure,failed).id && !value.device);
    wgpuInstanceProcessEvents(value.instance);
    assert(value.lost==1 && value.reason==WGPUDeviceLostReason_FailedCreation && !value.lost_device);
    wgpuAdapterRelease(value.adapter);
    wgpuInstanceRelease(value.instance);
    printf("PASS device loss modes/future/last-external-release/failed-creation, owned labels, async pipeline and real compilation diagnostics %s\n", adapter_class_scope());
}
