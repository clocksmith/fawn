/* SPDX-License-Identifier: MIT
 * Deliberately obscure completion at the real backend-call boundary after an
 * actual software submission. Production code receives typed errors/unknown
 * release, never a test-only state mutation API. */
#include "../src/internal.h"
#include "consumer.h"

static atomic_bool arm, hold, diagnose;
static _Atomic(doe_vk_commands *) obscured;
static _Atomic(doe_vk_status) injected = DOE_VK_NATIVE_FAILURE;
doe_vk_status __real_doe_vk_commands_submit(doe_vk_commands *, doe_vk_error *);
doe_vk_status __real_doe_vk_commands_poll(doe_vk_commands *, doe_vk_error *);
doe_vk_status __real_doe_vk_commands_recover(doe_vk_commands *, doe_vk_error *);
doe_vk_status __real_doe_vk_commands_wait(doe_vk_commands *, uint64_t, doe_vk_error *);
doe_vk_status __real_doe_vk_commands_release(doe_vk_commands *);
static doe_vk_status fault(doe_vk_error *error)
{
    doe_vk_status status=atomic_load(&injected);
    if (error) *error=(doe_vk_error){.status=status,.native_result=status==DOE_VK_OUT_OF_MEMORY ? -72 : -71,
        .operation=status==DOE_VK_OUT_OF_MEMORY ? "injected allocation failure" : "injected native failure"};
    return status;
}
doe_vk_status __wrap_doe_vk_commands_submit(doe_vk_commands *commands, doe_vk_error *error)
{
    if (arm) { obscured = commands; arm = false; }
    return __real_doe_vk_commands_submit(commands,error);
}
/* Blocking settlement waits an accepted submission's own fence. */
doe_vk_status __wrap_doe_vk_commands_wait(doe_vk_commands *commands, uint64_t timeout, doe_vk_error *error)
{
    doe_vk_status actual = __real_doe_vk_commands_wait(commands,timeout,error);
    if (commands == obscured) { assert(actual == DOE_VK_OK); return fault(error); }
    return actual;
}
doe_vk_status __wrap_doe_vk_commands_recover(doe_vk_commands *commands, doe_vk_error *error)
{
    doe_vk_status actual = __real_doe_vk_commands_recover(commands,error);
    if (commands == obscured) { assert(actual == DOE_VK_OK); return fault(error); }
    return actual;
}
/* Submission no longer waits: a poll may see the real work still running.
 * While held, the obscured work reports running; recovery reports injected. */
doe_vk_status __wrap_doe_vk_commands_poll(doe_vk_commands *commands, doe_vk_error *error)
{
    doe_vk_status actual = __real_doe_vk_commands_poll(commands,error);
    if (commands != obscured || actual == DOE_VK_PENDING) return actual;
    assert(actual == DOE_VK_OK);
    return hold && !diagnose ? DOE_VK_PENDING : fault(error);
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
typedef struct completion { const char *message; unsigned calls; } completion;
static void completed(WGPUQueueWorkDoneStatus status,WGPUStringView message,void *data,void *unused)
{
    (void)unused; completion *result=data;
    if (status!=WGPUQueueWorkDoneStatus_Error || message.length!=strlen(result->message) ||
        memcmp(message.data,result->message,message.length))
        fprintf(stderr,"completion status=%u expected=%s actual=%.*s\n",
            (unsigned)status,result->message,(int)message.length,message.data);
    assert(status==WGPUQueueWorkDoneStatus_Error && message.length==strlen(result->message));
    assert(!memcmp(message.data,result->message,message.length) && ++result->calls==1);
}
static WGPUFuture observe_held(state *s,completion *result)
{
    WGPUQueueWorkDoneCallbackInfo info={.mode=WGPUCallbackMode_WaitAnyOnly,.callback=completed,.userdata1=result};
    WGPUFuture future=wgpuQueueOnSubmittedWorkDone(s->queue,info); assert(future.id && !result->calls);
    WGPUFutureWaitInfo wait={.future=future};
    assert(wgpuInstanceWaitAny(s->instance,1,&wait,0)==WGPUWaitStatus_TimedOut && !wait.completed && !result->calls);
    return future;
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
    /* The queue accepted the work: submit reports nothing while it runs. */
    wgpuDevicePushErrorScope(s.device,WGPUErrorFilter_Internal);
    wgpuQueueSubmit(s.queue,1,&commands);
    pop(&s,WGPUErrorType_NoError);
    assert(device->pending && atomic_load(&device->references) > references);
    wgpuCommandBufferRelease(commands);
    wgpuBufferRelease(retained);
    /* A write to a buffer no running work uses neither waits nor fails. */
    uint64_t value = 42;
    wgpuDevicePushErrorScope(s.device,WGPUErrorFilter_Internal);
    wgpuQueueWriteBuffer(s.queue,target,0,&value,sizeof(value));
    pop(&s,WGPUErrorType_NoError);
    assert(device->pending && obscured);
    WGPUTextureDescriptor texture_desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    texture_desc.size = (WGPUExtent3D){1,1,1};
    texture_desc.format = WGPUTextureFormat_RGBA8Unorm;
    texture_desc.usage = WGPUTextureUsage_TextureBinding;
    /* Routine settlement polls: running work neither blocks nor fails
     * texture creation or a later submission; work done waits and reports. */
    wgpuDevicePushErrorScope(s.device,WGPUErrorFilter_Internal);
    WGPUTexture created = wgpuDeviceCreateTexture(s.device,&texture_desc);
    assert(created);
    pop(&s,WGPUErrorType_NoError);
    wgpuTextureRelease(created);
    commands = empty_commands(&s);
    wgpuDevicePushErrorScope(s.device,WGPUErrorFilter_Internal);
    wgpuQueueSubmit(s.queue,1,&commands);
    pop(&s,WGPUErrorType_NoError);
    wgpuCommandBufferRelease(commands);
    /* Establish real GPU completion before selecting diagnostics: a genuine
     * running poll must not bypass the intended fault. The held release keeps
     * the frontend lease unresolved, and the mutex excludes worker observation
     * while each admission snapshots its selected typed diagnostic. */
    native_lock(device);
    assert(obscured && __real_doe_vk_commands_wait(obscured,UINT64_MAX,NULL)==DOE_VK_OK);
    /* Unknown ownership admits obligations; even a known native diagnostic
     * cannot publish completion while the accepted native lease is retained. */
    diagnose=true;
    completion failures[3]={{"Vulkan: injected native failure (native=-71)",0},
        {"Vulkan: injected allocation failure (native=-72)",0},{"Vulkan: injected native failure (native=-71)",0}};
    WGPUFuture futures[3];
    futures[0]=observe_held(&s,&failures[0]);
    injected=DOE_VK_OUT_OF_MEMORY; futures[1]=observe_held(&s,&failures[1]);
    injected=DOE_VK_PENDING; futures[2]=observe_held(&s,&failures[2]);
    assert(device->pending && device->timestamp_normalizer && device->dispatch_validator && device->draw_validator && obscured);
    injected=DOE_VK_NATIVE_FAILURE; hold=false;
    native_unlock(device);
    for (unsigned i=0;i<3;++i) { wait_future(&s,futures[i]); assert(failures[i].calls==1); }
    completion terminal={"queue work failed",0};
    WGPUQueueWorkDoneCallbackInfo done={.mode=WGPUCallbackMode_WaitAnyOnly,.callback=completed,.userdata1=&terminal};
    WGPUFuture terminal_future=wgpuQueueOnSubmittedWorkDone(s.queue,done);
    wait_future(&s,terminal_future); assert(terminal.calls==1);
    assert(!device->pending && !device->timestamp_normalizer && !device->dispatch_validator &&
        !device->draw_validator && !obscured);
    const uint64_t *observed = map(&s,target,8);
    assert(*observed == value); /* The unrelated write ran while other work was unresolved. */
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
    native_lock(device); /* Earlier injected failures leave work done reporting Error. */
    assert(native_settle_pending(device).complete);
    native_unlock(device);
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
    printf("PASS typed recovery: running work leaves submit/texture creation unblocked, unrelated buffer writes neither wait nor fail, native/OOM diagnostic futures and pending obligations admit without premature readiness, zero waits time out while unknown leases remain, exact errors survive deferred completion, terminal cleanup and fresh GPU normalizer/indirect guard recovery %s\n", adapter_class_scope());
}
