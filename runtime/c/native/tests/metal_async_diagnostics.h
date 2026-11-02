/* SPDX-License-Identifier: MIT
 * Coherent deferred diagnostic/error-ownership controls for metal_bridge.c. */
#ifndef DOE_NATIVE_METAL_ASYNC_DIAGNOSTICS_TEST_H
#define DOE_NATIVE_METAL_ASYNC_DIAGNOSTICS_TEST_H
static void require_pending(state *s,WGPUFuture future)
{
    WGPUFutureWaitInfo wait={.future=future};
    assert(future.id && wgpuInstanceWaitAny(s->instance,1,&wait,0)==WGPUWaitStatus_TimedOut && !wait.completed);
}
static void complete_held(state *s)
{
    native_device *device=(native_device *)s->device;
    native_lock(device); hold=false;
    /* The existing native call ledger now explicitly reports terminal status;
     * public registration no longer drives the old blocking wait seam. */
    mock_result(&mocks[2],4,0); native_unlock(device);
}
typedef struct exact_message {
    const char *text;
    unsigned calls;
} exact_message;

static void require_message(WGPUStringView message, exact_message *expected)
{
    if (message.length != strlen(expected->text) || memcmp(message.data,expected->text,message.length))
        fprintf(stderr,"expected [%s], received [%.*s]\n",expected->text,(int)message.length,message.data);
    assert(message.length == strlen(expected->text));
    assert(!memcmp(message.data, expected->text, message.length));
    ++expected->calls;
}

static void exact_scope(WGPUPopErrorScopeStatus status, WGPUErrorType type,
    WGPUStringView message, void *userdata, void *unused)
{
    (void)unused;
    assert(status == WGPUPopErrorScopeStatus_Success && type == WGPUErrorType_Internal);
    require_message(message, userdata);
}

static void exact_pipeline(WGPUCreatePipelineAsyncStatus status, WGPUComputePipeline pipeline,
    WGPUStringView message, void *userdata, void *unused)
{
    (void)unused;
    assert(status == WGPUCreatePipelineAsyncStatus_InternalError && !pipeline);
    require_message(message, userdata);
}

static void exact_validation_scope(WGPUPopErrorScopeStatus status, WGPUErrorType type,
    WGPUStringView message, void *userdata, void *unused)
{
    (void)unused;
    assert(status == WGPUPopErrorScopeStatus_Success && type == WGPUErrorType_Validation);
    require_message(message, userdata);
}

static void exact_queue(WGPUQueueWorkDoneStatus status, WGPUStringView message,
    void *userdata, void *unused)
{
    (void)unused;
    assert(status == WGPUQueueWorkDoneStatus_Error);
    require_message(message, userdata);
}

static void exact_map(WGPUMapAsyncStatus status, WGPUStringView message,
    void *userdata, void *unused)
{
    (void)unused;
    assert(status == WGPUMapAsyncStatus_Error);
    require_message(message, userdata);
}

static void overwrite_stack(void)
{
    volatile unsigned char scratch[4096];
    for (size_t i = 0; i < sizeof(scratch); ++i) scratch[i] = 0xa5;
    (void)scratch[0];
}

static void cache_terminal_failure(state *s)
{
    native_device *device = (native_device *)s->device;
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device, NULL);
    native_lock(device);
    hold = true;
    native_unlock(device);
    wgpuDevicePushErrorScope(s->device, WGPUErrorFilter_Internal);
    submit(s, encoder);
    pop(s, WGPUErrorType_Internal);
    native_lock(device);
    assert(device->pending);
    hold = false;
    strcpy(mocks[2].completion_message, "original terminal diagnostic");
    mock_result(&mocks[2], 5, INT64_C(4294967297));
    /* The production background settlement path runs under this same lock.
     * Call it deterministically rather than race a timer or mutate a lease. */
    native_progress_pending(device);
    assert(!device->pending && device->retired_failure.status == NATIVE_FAILURE);
    strcpy(mocks[2].completion_message, "later text must not escape");
    native_unlock(device);
}

static void public_error_detail(state *s)
{
    native_device *device = (native_device *)s->device;
    const char *code = "@compute @workgroup_size(1) fn main(){}";
    WGPUShaderSourceWGSL source = {.chain = {.sType = WGPUSType_ShaderSourceWGSL},
        .code = {code, WGPU_STRLEN}};
    WGPUShaderModuleDescriptor module_desc = {.nextInChain = &source.chain};
    WGPUShaderModule module = wgpuDeviceCreateShaderModule(s->device, &module_desc);
    assert(module);
    WGPUComputePipelineDescriptor descriptor = {.compute = {.module = module}};
    exact_message pipeline = {"Metal: injected native failure (native=-71): owned fault boundary", 0};
    native_lock(device);
    mocks[2].fail = MOCK_PIPELINE;
    native_unlock(device);
    wgpuDevicePushErrorScope(s->device, WGPUErrorFilter_Internal);
    assert(!wgpuDeviceCreateComputePipeline(s->device, &descriptor));
    WGPUPopErrorScopeCallbackInfo scope = {.mode = WGPUCallbackMode_AllowSpontaneous,
        .callback = exact_scope, .userdata1 = &pipeline};
    assert(wgpuDevicePopErrorScope(s->device, scope).id && pipeline.calls == 1);
    WGPUCreateComputePipelineAsyncCallbackInfo callback = {.mode = (WGPUCallbackMode)UINT32_MAX,
        .callback = exact_pipeline, .userdata1 = &pipeline};
    wgpuDevicePushErrorScope(s->device, WGPUErrorFilter_Internal);
    assert(!wgpuDeviceCreateComputePipelineAsync(s->device, &descriptor, callback).id);
    assert(pipeline.calls == 1);
    assert(wgpuDevicePopErrorScope(s->device, scope).id && pipeline.calls == 2);
    wgpuInstanceProcessEvents(s->instance);
    assert(pipeline.calls == 2); /* Rejected constructor callback did not fire. */
    callback.mode = WGPUCallbackMode_AllowProcessEvents;
    assert(wgpuDeviceCreateComputePipelineAsync(s->device, &descriptor, callback).id);
    assert(pipeline.calls == 2);
    native_lock(device);
    mocks[2].fail = MOCK_NONE;
    native_unlock(device);
    wgpuShaderModuleRelease(module);
    overwrite_stack();
    wgpuInstanceProcessEvents(s->instance);
    assert(pipeline.calls == 3);

    /* Invalid render construction still delivers validation when callback admission rejects. */
    exact_message render = {"pipeline stage requires a shader from this device", 0};
    WGPURenderPipelineDescriptor render_descriptor = {0};
    WGPUCreateRenderPipelineAsyncCallbackInfo render_callback = {.mode = (WGPUCallbackMode)UINT32_MAX};
    WGPUPopErrorScopeCallbackInfo render_scope = {.mode = WGPUCallbackMode_AllowSpontaneous,
        .callback = exact_validation_scope, .userdata1 = &render};
    wgpuDevicePushErrorScope(s->device, WGPUErrorFilter_Validation);
    assert(!wgpuDeviceCreateRenderPipelineAsync(s->device, &render_descriptor, render_callback).id);
    assert(wgpuDevicePopErrorScope(s->device, render_scope).id && render.calls == 1);
    wgpuInstanceProcessEvents(s->instance);
    assert(render.calls == 1);

    exact_message terminal = {
        "Metal: MTLCommandBuffer.error (native=4294967297): original terminal diagnostic", 0};
    WGPUBuffer target = buffer(s, 8, WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead);
    uint64_t value = 42;
    cache_terminal_failure(s);
    wgpuDevicePushErrorScope(s->device, WGPUErrorFilter_Internal);
    wgpuQueueWriteBuffer(s->queue, target, 0, &value, sizeof(value));
    scope.userdata1 = &terminal;
    assert(wgpuDevicePopErrorScope(s->device, scope).id && terminal.calls == 1);
    wgpuQueueWriteBuffer(s->queue, target, 0, &value, sizeof(value));
    assert(!s->errors); /* Cached terminal failure is consumed once. */

    cache_terminal_failure(s);
    WGPUQueueWorkDoneCallbackInfo queue = {.mode = WGPUCallbackMode_AllowProcessEvents,
        .callback = exact_queue, .userdata1 = &terminal};
    WGPUFuture queue_future=wgpuQueueOnSubmittedWorkDone(s->queue,queue);
    assert(queue_future.id && terminal.calls==1);
    overwrite_stack(); wait_future(s,queue_future);
    assert(terminal.calls == 2);

    cache_terminal_failure(s);
    WGPUBufferMapCallbackInfo map = {.mode = WGPUCallbackMode_AllowProcessEvents,
        .callback = exact_map, .userdata1 = &terminal};
    WGPUFuture map_future=wgpuBufferMapAsync(target,WGPUMapMode_Read,0,8,map);
    assert(map_future.id && terminal.calls==2);
    overwrite_stack(); wait_future(s,map_future);
    assert(terminal.calls == 3 && wgpuBufferGetMapState(target) == WGPUBufferMapState_Unmapped);

    cache_terminal_failure(s);
    queue.mode = (WGPUCallbackMode)UINT32_MAX;
    wgpuDevicePushErrorScope(s->device, WGPUErrorFilter_Internal);
    assert(!wgpuQueueOnSubmittedWorkDone(s->queue, queue).id && terminal.calls == 3);
    assert(wgpuDevicePopErrorScope(s->device, scope).id && terminal.calls == 4);
    wgpuInstanceProcessEvents(s->instance);
    assert(terminal.calls == 4); /* Rejected work-done callback did not also fire. */
    wgpuQueueWriteBuffer(s->queue, target, 0, &value, sizeof(value));
    assert(!s->errors);

    cache_terminal_failure(s);
    map.mode = (WGPUCallbackMode)UINT32_MAX;
    wgpuDevicePushErrorScope(s->device, WGPUErrorFilter_Internal);
    assert(!wgpuBufferMapAsync(target, WGPUMapMode_Read, 0, 8, map).id && terminal.calls == 4);
    assert(wgpuDevicePopErrorScope(s->device, scope).id && terminal.calls == 5);
    assert(wgpuBufferGetMapState(target) == WGPUBufferMapState_Unmapped);
    wgpuInstanceProcessEvents(s->instance);
    assert(terminal.calls == 5); /* Rejected map callback did not also fire. */
    wgpuQueueWriteBuffer(s->queue, target, 0, &value, sizeof(value));
    assert(!s->errors);

    wgpuBufferRelease(target);
    cache_terminal_failure(s);
    wgpuDeviceDestroy(s->device);
    queue.mode = WGPUCallbackMode_AllowProcessEvents;
    queue_future=wgpuQueueOnSubmittedWorkDone(s->queue,queue);
    assert(queue_future.id); overwrite_stack(); wait_future(s,queue_future);
    assert(terminal.calls == 6); /* Destroy must not consume an unobserved failure. */
}

#endif
