/* SPDX-License-Identifier: MIT
 * Real frontend/pending/backend owners, native CPU call ledger only. */
#include "../src/surface.h"
#include "../../backends/metal/src/internal.h"
#include "../../backends/metal/tests/mock_native.h"
#include "consumer.h"

static mock_native mocks[2];
static doe_mtl_native originals[2];
static unsigned opened, physical_identity;
static atomic_bool automatic_terminal, complete_conflict;
static atomic_bool hold_worker, worker_entered, release_worker, second_submitted;
static atomic_uint closed;
static doe_thread_id caller_thread;
static atomic_uint race_action;
static WGPUBuffer raced_buffer;
static state *raced_state;

native_failure __real_native_record_resource_commands(native_command_handle *, doe_resource_commands *);
native_failure __wrap_native_record_resource_commands(native_command_handle *native, doe_resource_commands *resources)
{
    native_failure failure = __real_native_record_resource_commands(native,resources);
    if (failure.status == NATIVE_BUSY && atomic_exchange(&complete_conflict,false))
        atomic_store(&automatic_terminal,true);
    return failure;
}

static void block_hook(mock_native *mock, void *command, uint64_t timeout)
{
    (void)mock;
    if (atomic_exchange(&hold_worker,false)) {
        atomic_store(&worker_entered,true);
        while (!atomic_load(&release_worker)) assert(doe_sleep_for(UINT64_C(1000000)).status == DOE_HOST_OK);
    }
    if (doe_thread_equal(doe_thread_current(),caller_thread)) {
        unsigned action = atomic_exchange(&race_action,0);
        if (action) {
            mock_command_result(command,4,0);
            if (action == 1) {
                WGPUBufferMapCallbackInfo callback = {.mode=WGPUCallbackMode_AllowSpontaneous,
                    .callback=mapped,.userdata1=raced_state};
                assert(wgpuBufferMapAsync(raced_buffer,WGPUMapMode_Read,0,16,callback).id);
            } else wgpuBufferDestroy(raced_buffer);
        }
    }
    if (atomic_load(&automatic_terminal)) mock_command_result(command,4,0);
    else if (timeout) assert(doe_sleep_for(UINT64_C(1000000)).status == DOE_HOST_OK);
}
typedef struct submit_call { WGPUQueue queue; WGPUCommandBuffer command; } submit_call;
static void submit_second(void *userdata)
{
    submit_call *call = userdata;
    wgpuQueueSubmit(call->queue,1,&call->command);
    atomic_store(&second_submitted,true);
}
static bool await_flag(atomic_bool *flag)
{
    struct timespec now, deadline;
    assert(doe_monotonic_now(&now).status == DOE_HOST_OK);
    assert(doe_time_add(&now,UINT64_C(1000000000),&deadline).status == DOE_HOST_OK);
    while (!atomic_load(flag)) {
        assert(doe_monotonic_now(&now).status == DOE_HOST_OK);
        if (doe_time_reached(&now,&deadline)) return false;
        assert(doe_sleep_for(UINT64_C(1000000)).status == DOE_HOST_OK);
    }
    return true;
}
static size_t observe(void *userdata, void *command, bool wait, doe_mtl_error *error)
{
    mock_native *mock = userdata;
    unsigned index = mock == &mocks[0] ? 0u : 1u;
    if (wait && atomic_load(&automatic_terminal)) mock_command_result(command,4,0);
    return originals[index].observe(userdata,command,wait,error);
}
static void close_driver(void *userdata)
{
    unsigned index = userdata == &mocks[0] ? 0u : 1u;
    originals[index].close(userdata);
    atomic_fetch_add(&closed,1);
}
doe_mtl_status __wrap_doe_mtl_context_create(doe_mtl_context **out, doe_mtl_error *error)
{
    assert(opened < 2);
    unsigned index = opened++;
    doe_mtl_native driver = mock_driver(&mocks[index]);
    originals[index] = driver;
    driver.observe = observe;
    driver.close = close_driver;
    mocks[index].block_hook = block_hook;
    return doe_mtl_context_open(driver,out,error);
}
void *__wrap_doe_mtl_context_device(const doe_mtl_context *context)
{
    (void)context;
    return &physical_identity;
}
static WGPUCommandBuffer copy(state *s, WGPUBuffer source, WGPUBuffer target, WGPUBuffer scratch)
{
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device,NULL);
    if (scratch) wgpuCommandEncoderClearBuffer(encoder,scratch,0,16);
    wgpuCommandEncoderCopyBufferToBuffer(encoder,source,0,target,0,16);
    WGPUCommandBuffer command = wgpuCommandEncoderFinish(encoder,NULL);
    wgpuCommandEncoderRelease(encoder);
    assert(command);
    return command;
}
static size_t pending_count(native_device *device)
{
    size_t count = 0;
    for (native_pending *pending = device->pending; pending; pending = pending->next) ++count;
    return count;
}
static void queue_reply(WGPUQueueWorkDoneStatus status, WGPUStringView message, void *one, void *two)
{
    (void)status; (void)message; (void)two;
    ++*(unsigned *)one;
}
static void unlocked_race(state *s, bool write, bool destroy)
{
    native_device *device = (native_device *)s->device;
    atomic_store(&automatic_terminal,false);
    WGPUBuffer source = buffer(s,16,WGPUBufferUsage_CopySrc|WGPUBufferUsage_CopyDst);
    WGPUBuffer target = buffer(s,16,WGPUBufferUsage_CopyDst|WGPUBufferUsage_MapRead);
    WGPUCommandBuffer preceding = copy(s,source,target,NULL);
    wgpuQueueSubmit(s->queue,1,&preceding);
    wgpuCommandBufferRelease(preceding);
    native_lock(device);
    unsigned committed = mocks[1].commits;
    native_unlock(device);
    unsigned errors = s->errors, maps = s->mapped;
    raced_buffer = target;
    raced_state = s;
    atomic_store(&race_action,destroy ? 2u : 1u);
    if (write) {
        const uint32_t input[4] = {11,22,33,44};
        wgpuQueueWriteBuffer(s->queue,target,0,input,sizeof(input));
    } else {
        WGPUCommandBuffer retry = copy(s,source,target,NULL);
        wgpuQueueSubmit(s->queue,1,&retry);
        wgpuCommandBufferRelease(retry);
    }
    assert(!atomic_load(&race_action) && s->errors == errors + 1 && s->last_error == WGPUErrorType_Validation);
    native_lock(device);
    assert(mocks[1].commits == committed && !device->pending);
    native_unlock(device);
    if (!destroy) {
        assert(s->mapped == maps + 1 && wgpuBufferGetMapState(target) == WGPUBufferMapState_Mapped);
        wgpuBufferUnmap(target);
    }
    wgpuBufferRelease(source); wgpuBufferRelease(target);
}
int main(void)
{
    caller_thread = doe_thread_current();
    state s = {0};
    s.instance = wgpuCreateInstance(NULL);
    WGPURequestAdapterOptions options = WGPU_REQUEST_ADAPTER_OPTIONS_INIT;
    options.backendType = WGPUBackendType_Metal;
    WGPURequestAdapterCallbackInfo adapter = {.mode=WGPUCallbackMode_AllowSpontaneous,
        .callback=adapter_reply,.userdata1=&s};
    assert(wgpuInstanceRequestAdapter(s.instance,&options,adapter).id && s.adapter);
    WGPUDeviceDescriptor desc = WGPU_DEVICE_DESCRIPTOR_INIT;
    desc.uncapturedErrorCallbackInfo = (WGPUUncapturedErrorCallbackInfo){.callback=error_reply,.userdata1=&s};
    WGPURequestDeviceCallbackInfo requested = {.mode=WGPUCallbackMode_AllowSpontaneous,
        .callback=device_reply,.userdata1=&s};
    assert(wgpuAdapterRequestDevice(s.adapter,&desc,requested).id && s.device);
    s.queue = wgpuDeviceGetQueue(s.device);
    native_device *device = (native_device *)s.device;
    WGPUBuffer source_a = buffer(&s,16,WGPUBufferUsage_CopySrc|WGPUBufferUsage_CopyDst);
    WGPUBuffer target_a = buffer(&s,16,WGPUBufferUsage_CopyDst|WGPUBufferUsage_MapRead);
    WGPUBuffer source_b = buffer(&s,16,WGPUBufferUsage_CopySrc|WGPUBufferUsage_CopyDst);
    WGPUBuffer target_b = buffer(&s,16,WGPUBufferUsage_CopyDst|WGPUBufferUsage_MapRead);
    WGPUBuffer scratch = buffer(&s,16,WGPUBufferUsage_CopyDst);
    WGPUCommandBuffer a = copy(&s,source_a,target_a,NULL), b = copy(&s,source_b,target_b,NULL);
    atomic_store(&hold_worker,true);
    wgpuQueueSubmit(s.queue,1,&a);
    assert(!s.errors); /* Accepted pending work is not an execution error. */
    assert(await_flag(&worker_entered));
    submit_call call = {s.queue,b};
    doe_thread thread = {0};
    assert(doe_thread_create(&thread,submit_second,&call).status == DOE_HOST_OK);
    bool submitted_unlocked = await_flag(&second_submitted);
    atomic_store(&release_worker,true);
    assert(doe_thread_join(&thread).status == DOE_HOST_OK);
    assert(submitted_unlocked); /* No dependency on polling/timing luck. */
    native_lock(device);
    assert(pending_count(device) == 2 && mocks[1].commits == 2 && mocks[1].waits == 0 && !s.errors);
    native_unlock(device);
    wgpuCommandBufferRelease(a); wgpuCommandBufferRelease(b);

    /* A partially recorded clear must be abandoned before the shared-resource
     * predecessor settles. Exactly one additional native commit is allowed. */
    WGPUCommandBuffer shared = copy(&s,source_a,target_a,scratch);
    atomic_store(&complete_conflict,true);
    wgpuQueueSubmit(s.queue,1,&shared);
    native_lock(device);
    assert(mocks[1].commits == 3 && mocks[1].fills >= 2 && !s.errors);
    native_unlock(device);
    wgpuCommandBufferRelease(shared);
    unsigned callbacks = 0;
    WGPUQueueWorkDoneCallbackInfo done = {.mode=WGPUCallbackMode_AllowSpontaneous,
        .callback=queue_reply,.userdata1=&callbacks};
    assert(wgpuQueueOnSubmittedWorkDone(s.queue,done).id && callbacks == 1);
    unlocked_race(&s,false,false);
    unlocked_race(&s,false,true);
    unlocked_race(&s,true,false);
    unlocked_race(&s,true,true);

    atomic_store(&automatic_terminal,false);
    native_lock(device);
    mocks[1].unknown_commit = true;
    native_unlock(device);
    WGPUCommandBuffer unknown = copy(&s,source_a,target_a,NULL);
    wgpuQueueSubmit(s.queue,1,&unknown);
    wgpuCommandBufferRelease(unknown);
    assert(!wgpuQueueOnSubmittedWorkDone(s.queue,done).id && callbacks == 1);
    native_lock(device);
    assert(pending_count(device) == 1);
    mock_command_result(device->pending->native.value.metal->native,5,-771);
    native_queue_settlement failure = native_poll_pending(device);
    assert(failure.failure.status == NATIVE_FAILURE && failure.failure.native_code == -771);
    assert(!device->pending);
    mocks[1].unknown_commit = false;
    native_unlock(device);

    /* Fresh disjoint resources survive all public handles going away. The
     * pending worker/native wait lease alone may retire them when terminal. */
    WGPUBuffer last_source = buffer(&s,16,WGPUBufferUsage_CopySrc|WGPUBufferUsage_CopyDst);
    WGPUBuffer last_target = buffer(&s,16,WGPUBufferUsage_CopyDst);
    WGPUCommandBuffer last = copy(&s,last_source,last_target,NULL);
    wgpuQueueSubmit(s.queue,1,&last);
    wgpuCommandBufferRelease(last);
    wgpuBufferRelease(last_source); wgpuBufferRelease(last_target);
    wgpuBufferRelease(source_a); wgpuBufferRelease(target_a);
    wgpuBufferRelease(source_b); wgpuBufferRelease(target_b); wgpuBufferRelease(scratch);
    wgpuDeviceDestroy(s.device); /* Loss never implies GPU completion. */
    wgpuQueueRelease(s.queue); wgpuDeviceRelease(s.device); wgpuAdapterRelease(s.adapter);
    wgpuInstanceRelease(s.instance);
    assert(atomic_load(&closed) == 0);
    /* The fake GPU owns completion, independently of which waiter happens to
     * run. Its status is held pending until this explicit controller signal;
     * that pending record still pins device storage while we take its mutex. */
    native_lock(device);
    assert(pending_count(device) == 1);
    mock_command_result(device->pending->native.value.metal->native,4,0);
    native_unlock(device);
    struct timespec now, deadline;
    assert(doe_monotonic_now(&now).status == DOE_HOST_OK);
    assert(doe_time_add(&now,UINT64_C(5000000000),&deadline).status == DOE_HOST_OK);
    while (atomic_load(&closed) != 2) {
        assert(doe_monotonic_now(&now).status == DOE_HOST_OK && !doe_time_reached(&now,&deadline));
        assert(doe_sleep_for(UINT64_C(1000000)).status == DOE_HOST_OK);
    }
    puts("PASS Metal asynchronous disjoint/shared submit, rollback, unknown/failure, early public drops (CPU call ledger)");
    return 0;
}
