/* SPDX-License-Identifier: MIT
 * Public callback readiness and native leases over CPU Metal call ledgers.
 * Known host bytes are checked; mock commands do not claim GPU execution. */
#include "../src/pending.h"
#include "../../backends/metal/src/internal.h"
#include "../../backends/metal/tests/mock_native.h"
#include "consumer.h"
#include <errno.h>

static mock_native mocks[12];
static doe_mtl_native drivers[12];
static unsigned opened, identity;
static atomic_uint closed, worker_reads;
static _Thread_local unsigned lock_depth, wakes, lends;
static _Thread_local bool registering, reject_callback_allocation;
static WGPUQueue later_queue;
static WGPUCommandBuffer later_command;

void __real_native_lock(native_device *);
void __wrap_native_lock(native_device *device)
{
    __real_native_lock(device);
    ++lock_depth;
}
void __real_native_unlock(native_device *);
void __wrap_native_unlock(native_device *device)
{
    assert(lock_depth);
    --lock_depth;
    __real_native_unlock(device);
}
void __real_native_pending_progress_wake(native_device *);
void __wrap_native_pending_progress_wake(native_device *device)
{
    if (registering) ++wakes;
    __real_native_pending_progress_wake(device);
}
native_failure __real_native_buffer_host_range(native_buffer *, uint64_t, size_t, const unsigned char **);
native_failure __wrap_native_buffer_host_range(native_buffer *buffer, uint64_t offset,
    size_t size, const unsigned char **out)
{
    if (registering) ++lends;
    return __real_native_buffer_host_range(buffer,offset,size,out);
}
native_failure __real_native_buffer_read(native_buffer *, uint64_t, void *, size_t);
native_failure __wrap_native_buffer_read(native_buffer *buffer, uint64_t offset, void *bytes, size_t size)
{
    assert(!registering); /* Registration must never do copy-backed readback. */
    atomic_fetch_add(&worker_reads,1);
    return __real_native_buffer_read(buffer,offset,bytes,size);
}
void *__real_malloc(size_t);
void *__wrap_malloc(size_t size)
{
    if (reject_callback_allocation) {
        reject_callback_allocation=false;
        errno=ENOMEM;
        return NULL;
    }
    return __real_malloc(size);
}
WGPUFuture __real_native_callback_publish_admit(doe_instance *, WGPUCallbackMode,
    void *, size_t, size_t, doe_callback_copy_context, doe_callback_deliver_context,
    doe_callback_publisher, void *, doe_callback_status *);
WGPUFuture __wrap_native_callback_publish_admit(doe_instance *instance, WGPUCallbackMode mode,
    void *context, size_t size, size_t alignment, doe_callback_copy_context copy,
    doe_callback_deliver_context deliver, doe_callback_publisher publisher, void *userdata,
    doe_callback_status *status)
{
    if (later_command) {
        WGPUCommandBuffer command=later_command;
        later_command=NULL;
        wgpuQueueSubmit(later_queue,1,&command);
    }
    return __real_native_callback_publish_admit(instance,mode,context,size,alignment,
        copy,deliver,publisher,userdata,status);
}
static void close_driver(void *userdata)
{
    for (unsigned i=0;i<opened;++i) if (userdata==&mocks[i]) {
        drivers[i].close(userdata);
        atomic_fetch_add(&closed,1);
        return;
    }
    assert(false);
}
doe_mtl_status __wrap_doe_mtl_context_create(doe_mtl_context **out, doe_mtl_error *error)
{
    assert(opened<12);
    unsigned index=opened++;
    doe_mtl_native driver=mock_driver(&mocks[index]);
    drivers[index]=driver;
    driver.close=close_driver;
    return doe_mtl_context_open(driver,out,error);
}
void *__wrap_doe_mtl_context_device(const doe_mtl_context *context)
{
    assert(context);
    return &identity;
}
static void error_reply_unlocked(WGPUDevice const *device, WGPUErrorType type,
    WGPUStringView message, void *userdata, void *unused)
{
    assert(!lock_depth);
    error_reply(device,type,message,userdata,unused);
}
static void init(state *s)
{
    s->instance=wgpuCreateInstance(NULL);
    WGPURequestAdapterOptions options=WGPU_REQUEST_ADAPTER_OPTIONS_INIT;
    options.backendType=WGPUBackendType_Metal;
    WGPURequestAdapterCallbackInfo adapter={.mode=WGPUCallbackMode_AllowSpontaneous,
        .callback=adapter_reply,.userdata1=s};
    assert(wgpuInstanceRequestAdapter(s->instance,&options,adapter).id && s->adapter);
    WGPUDeviceDescriptor descriptor=WGPU_DEVICE_DESCRIPTOR_INIT;
    descriptor.uncapturedErrorCallbackInfo=(WGPUUncapturedErrorCallbackInfo){
        .callback=error_reply_unlocked,.userdata1=s};
    s->device=wgpuAdapterCreateDevice(s->adapter,&descriptor);
    assert(s->device);
    s->queue=wgpuDeviceGetQueue(s->device);
}
typedef struct observed {
    unsigned calls, callback_wakes;
    WGPUMapAsyncStatus map_status;
    WGPUQueueWorkDoneStatus queue_status;
    const char *message;
    state *release;
    WGPUBuffer buffer;
} observed;
static void release_call_handles(observed *result)
{
    if (!result->release) return;
    if (result->buffer) {
        wgpuBufferUnmap(result->buffer);
        wgpuBufferDestroy(result->buffer);
        wgpuBufferRelease(result->buffer);
        result->buffer=NULL;
    }
    wgpuQueueRelease(result->release->queue);
    result->release->queue=NULL;
    wgpuDeviceRelease(result->release->device);
    result->release->device=NULL;
}
static void map_reply_unlocked(WGPUMapAsyncStatus status, WGPUStringView message,
    void *userdata, void *unused)
{
    (void)message; (void)unused;
    observed *result=userdata;
    assert(!lock_depth && status==result->map_status && ++result->calls==1);
    result->callback_wakes=wakes;
    release_call_handles(result);
}
static void queue_reply_unlocked(WGPUQueueWorkDoneStatus status, WGPUStringView message,
    void *userdata, void *unused)
{
    (void)unused;
    observed *result=userdata;
    assert(!lock_depth && status==result->queue_status && ++result->calls==1);
    result->callback_wakes=wakes;
    if (result->message)
        assert(message.length==strlen(result->message) && !memcmp(message.data,result->message,message.length));
    release_call_handles(result);
}
static WGPUFuture map_ready(state *s, WGPUBuffer buffer, WGPUMapMode mode,
    WGPUCallbackMode callback_mode, observed *result)
{
    (void)s;
    wakes=lends=0;
    registering=true;
    WGPUFuture future=wgpuBufferMapAsync(buffer,mode,0,16,(WGPUBufferMapCallbackInfo){
        .mode=callback_mode,.callback=map_reply_unlocked,.userdata1=result});
    registering=false;
    return future;
}
static WGPUFuture queue_ready(state *s, WGPUCallbackMode mode, observed *result)
{
    wakes=lends=0;
    registering=true;
    WGPUFuture future=wgpuQueueOnSubmittedWorkDone(s->queue,(WGPUQueueWorkDoneCallbackInfo){
        .mode=mode,.callback=queue_reply_unlocked,.userdata1=result});
    registering=false;
    return future;
}
static void ready_zero_wait(state *s, WGPUFuture future)
{
    WGPUFutureWaitInfo wait={.future=future};
    assert(future.id && wgpuInstanceWaitAny(s->instance,1,&wait,0)==WGPUWaitStatus_Success && wait.completed);
}
static void not_ready(state *s, WGPUFuture future)
{
    WGPUFutureWaitInfo wait={.future=future};
    assert(future.id && wgpuInstanceWaitAny(s->instance,1,&wait,0)==WGPUWaitStatus_TimedOut && !wait.completed);
}
static void modes(state *s)
{
    const WGPUCallbackMode modes[]={WGPUCallbackMode_WaitAnyOnly,WGPUCallbackMode_AllowProcessEvents,
        WGPUCallbackMode_AllowSpontaneous};
    const uint32_t bytes[]={31,41,59,26};
    WGPUBuffer value=buffer(s,sizeof(bytes),WGPUBufferUsage_MapRead|WGPUBufferUsage_CopyDst);
    wgpuQueueWriteBuffer(s->queue,value,0,bytes,sizeof(bytes));
    for (size_t i=0;i<3;++i) {
        observed map_result={.map_status=WGPUMapAsyncStatus_Success};
        WGPUFuture map=map_ready(s,value,WGPUMapMode_Read,modes[i],&map_result);
        assert(map.id && !wakes && lends==1);
        assert(map_result.calls==(i==2 ? 1u : 0u));
        wgpuInstanceProcessEvents(s->instance);
        assert(map_result.calls==(i==0 ? 0u : 1u));
        ready_zero_wait(s,map);
        assert(map_result.calls==1 && !memcmp(wgpuBufferGetConstMappedRange(value,0,16),bytes,16));
        wgpuBufferUnmap(value);
        observed queue_result={.queue_status=WGPUQueueWorkDoneStatus_Success};
        WGPUFuture queue=queue_ready(s,modes[i],&queue_result);
        assert(queue.id && !wakes && !lends && queue_result.calls==(i==2 ? 1u : 0u));
        wgpuInstanceProcessEvents(s->instance);
        assert(queue_result.calls==(i==0 ? 0u : 1u));
        ready_zero_wait(s,queue);
        assert(queue_result.calls==1);
    }
    observed canceled={.map_status=WGPUMapAsyncStatus_Aborted};
    WGPUFuture old=map_ready(s,value,WGPUMapMode_Read,WGPUCallbackMode_WaitAnyOnly,&canceled);
    assert(old.id && !wakes && lends==1 && !canceled.calls);
    wgpuBufferUnmap(value);
    observed current={.map_status=WGPUMapAsyncStatus_Success};
    WGPUFuture fresh=map_ready(s,value,WGPUMapMode_Read,WGPUCallbackMode_WaitAnyOnly,&current);
    ready_zero_wait(s,fresh);
    ready_zero_wait(s,old);
    assert(current.calls==1 && canceled.calls==1 && wgpuBufferGetMapState(value)==WGPUBufferMapState_Mapped);
    assert(!memcmp(wgpuBufferGetConstMappedRange(value,0,16),bytes,16));
    wgpuBufferUnmap(value);
    observed refused={.map_status=WGPUMapAsyncStatus_Success};
    unsigned errors=s->errors;
    reject_callback_allocation=true;
    assert(!map_ready(s,value,WGPUMapMode_Read,WGPUCallbackMode_WaitAnyOnly,&refused).id);
    assert(!reject_callback_allocation && s->errors==errors+1 && s->last_error==WGPUErrorType_OutOfMemory);
    assert(!refused.calls && !lends && wgpuBufferGetMapState(value)==WGPUBufferMapState_Unmapped);
    s->errors=errors;
    wgpuBufferRelease(value);
}
static size_t count(native_device *device, native_pending_kind kind)
{
    size_t result=0;
    for (native_pending *p=device->pending;p;p=p->next) if (p->kind==kind) ++result;
    return result;
}
static WGPUCommandBuffer copy(state *s, WGPUBuffer source, WGPUBuffer target)
{
    WGPUCommandEncoder encoder=wgpuDeviceCreateCommandEncoder(s->device,NULL);
    wgpuCommandEncoderCopyBufferToBuffer(encoder,source,0,target,0,16);
    WGPUCommandBuffer command=wgpuCommandEncoderFinish(encoder,NULL);
    wgpuCommandEncoderRelease(encoder);
    assert(command);
    return command;
}
static void own_record_only(state *s)
{
    native_device *device=(native_device *)s->device;
    WGPUBuffer shadow=buffer(s,16,WGPUBufferUsage_MapWrite|WGPUBufferUsage_CopySrc);
    WGPUBuffer target=buffer(s,16,WGPUBufferUsage_MapRead|WGPUBufferUsage_CopyDst);
    WGPUBuffer source=buffer(s,16,WGPUBufferUsage_CopySrc);
    WGPUCommandBuffer command=copy(s,source,target);
    observed map_result={.map_status=WGPUMapAsyncStatus_Success};
    observed queue_result={.queue_status=WGPUQueueWorkDoneStatus_Success};
    unsigned reads=atomic_load(&worker_reads);
    native_lock(device); /* Excludes worker, not proof that GPU work is pending. */
    WGPUFuture map=map_ready(s,shadow,WGPUMapMode_Write,WGPUCallbackMode_WaitAnyOnly,&map_result);
    assert(map.id && wakes==1 && !lends && count(device,NATIVE_PENDING_ASYNC)==1);
    not_ready(s,map); /* This nonlendable mock buffer still requires the excluded worker's CPU copy. */
    assert(!map_result.calls && wgpuBufferGetMapState(shadow)==WGPUBufferMapState_Pending);
    later_queue=s->queue;
    later_command=command; /* Accepted after the queue callback's captured serial. */
    WGPUFuture queue=queue_ready(s,WGPUCallbackMode_WaitAnyOnly,&queue_result);
    assert(queue.id && !later_command && !wakes && count(device,NATIVE_PENDING_ASYNC)==1);
    assert(count(device,NATIVE_PENDING_COMMANDS)==1 && !map_result.calls && !queue_result.calls);
    assert(atomic_load(&worker_reads)==reads); /* Fast selection did not prepare the other map. */
    native_unlock(device);
    ready_zero_wait(s,queue); /* Later running work cannot delay the captured empty queue. */
    wait_future(s,map);
    assert(map_result.calls==1 && atomic_load(&worker_reads)==reads+1);
    wgpuBufferUnmap(shadow);
    native_lock(device);
    mock_command_result(device->pending->native.value.metal->native,4,0);
    native_progress_pending(device);
    assert(!count(device,NATIVE_PENDING_COMMANDS));
    native_unlock(device);
    wgpuCommandBufferRelease(command);
    wgpuBufferRelease(source); wgpuBufferRelease(target); wgpuBufferRelease(shadow);
}
static void pending_and_unknown(state *s, bool unknown)
{
    native_device *device=(native_device *)s->device;
    WGPUBuffer source=buffer(s,16,WGPUBufferUsage_CopySrc);
    WGPUBuffer target=buffer(s,16,WGPUBufferUsage_MapRead|WGPUBufferUsage_CopyDst);
    WGPUCommandBuffer command=copy(s,source,target);
    observed map_result={.map_status=unknown ? WGPUMapAsyncStatus_Error : WGPUMapAsyncStatus_Success};
    observed queue_result={.queue_status=unknown ? WGPUQueueWorkDoneStatus_Error : WGPUQueueWorkDoneStatus_Success,
        .message=unknown ? "Metal: MTLCommandBuffer.error (native=-771): first terminal fault" : NULL};
    native_lock(device);
    wgpuQueueSubmit(s->queue,1,&command);
    void *native=device->pending->native.value.metal->native;
    WGPUFuture map=map_ready(s,target,WGPUMapMode_Read,WGPUCallbackMode_WaitAnyOnly,&map_result);
    assert(map.id && wakes==1 && !lends);
    WGPUFuture queue=queue_ready(s,WGPUCallbackMode_WaitAnyOnly,&queue_result);
    assert(queue.id && wakes==1 && count(device,NATIVE_PENDING_ASYNC)==2);
    if (unknown) {
        mock_command_result(native,0,0);
        native_progress_pending(device);
    }
    assert(mock_command_status(native)==(unknown ? 0u : 2u));
    not_ready(s,map); not_ready(s,queue);
    assert(count(device,NATIVE_PENDING_COMMANDS)==1 && !map_result.calls && !queue_result.calls);
    assert(doe_mtl_buffer_owner(((native_buffer *)doe_resource_buffer_native((doe_resource_buffer *)target))->handle.metal));
    unsigned index=opened-1;
    assert(!mocks[index].waits && !atomic_load(&mocks[index].blocks));
    if (unknown) strcpy(mocks[index].completion_message,"first terminal fault");
    mock_command_result(native,unknown ? 5u : 4u,unknown ? -771 : 0);
    native_unlock(device);
    wait_future(s,map); wait_future(s,queue);
    assert(map_result.calls==1 && queue_result.calls==1);
    if (!unknown) wgpuBufferUnmap(target);
    wgpuCommandBufferRelease(command);
    wgpuBufferRelease(source); wgpuBufferRelease(target);
}
static void known_error(void)
{
    state s={0}; init(&s);
    native_device *device=(native_device *)s.device;
    WGPUBuffer source=buffer(&s,16,WGPUBufferUsage_CopySrc);
    WGPUBuffer target=buffer(&s,16,WGPUBufferUsage_CopyDst);
    WGPUCommandBuffer command=copy(&s,source,target);
    native_lock(device);
    wgpuQueueSubmit(s.queue,1,&command);
    strcpy(mocks[opened-1].completion_message,"first terminal fault");
    mock_command_result(device->pending->native.value.metal->native,5,-771);
    native_unlock(device);
    observed result={.queue_status=WGPUQueueWorkDoneStatus_Error,
        .message="Metal: MTLCommandBuffer.error (native=-771): first terminal fault"};
    WGPUFuture future=queue_ready(&s,WGPUCallbackMode_WaitAnyOnly,&result);
    assert(future.id && !wakes);
    ready_zero_wait(&s,future);
    assert(result.calls==1);
    wgpuCommandBufferRelease(command);
    wgpuBufferRelease(source); wgpuBufferRelease(target);
    consumer_finish(&s);
}
static void reentrant_ready(bool map)
{
    state s={0}; init(&s);
    observed result={.map_status=WGPUMapAsyncStatus_Success,
        .queue_status=WGPUQueueWorkDoneStatus_Success,.release=&s};
    if (map) result.buffer=buffer(&s,16,WGPUBufferUsage_MapRead|WGPUBufferUsage_CopyDst);
    WGPUFuture future=map ? map_ready(&s,result.buffer,WGPUMapMode_Read,WGPUCallbackMode_AllowSpontaneous,&result) :
        queue_ready(&s,WGPUCallbackMode_AllowSpontaneous,&result);
    assert(future.id && result.calls==1 && !s.device && !s.queue && !result.buffer && !result.callback_wakes);
    wgpuAdapterRelease(s.adapter); wgpuInstanceRelease(s.instance);
}
int main(void)
{
    state s={0}; init(&s);
    modes(&s);
    own_record_only(&s);
    pending_and_unknown(&s,false);
    consumer_finish(&s);
    state uncertain={0}; init(&uncertain);
    pending_and_unknown(&uncertain,true);
    consumer_finish(&uncertain);
    known_error();
    reentrant_ready(true); reentrant_ready(false);
    assert(opened==10 && atomic_load(&closed)==opened && !lock_depth);
    for (unsigned i=0;i<opened;++i) mock_assert_closed(&mocks[i]);
    puts("PASS admitted async ready paths: three modes, direct host bytes, zero-hop own-record selection, deferred copies, pending/unknown leases, captured serial, cancellation, OOM, first error and unlocked reentrant final releases; CPU ledger only");
}
