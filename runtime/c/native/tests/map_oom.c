/* SPDX-License-Identifier: MIT
 * Real callback-admission and deferred map-byte malloc failures. Both rollback
 * the mapping token, with independent in-call/callback buffer and device leases. */
#include "../src/internal.h"
#include "consumer.h"
#include <errno.h>
enum { MAP_BYTES=64 };
enum { PROGRESS_OFF, PROGRESS_RECORD, PROGRESS_REJECT };
static _Thread_local bool record_allocations,reject_allocations,in_progress;
static _Thread_local unsigned allocation_count,failure_count;
static _Thread_local size_t allocation_sizes[2];
static atomic_int progress_mode;
static atomic_uint progress_allocations,progress_failures;
static _Atomic(native_buffer *) tracked_buffer;
static _Atomic(native_backend *) tracked_backend;
static atomic_uint buffers_closed,backends_closed;
void *__real_malloc(size_t);
void *__wrap_malloc(size_t size)
{
    int mode=atomic_load(&progress_mode);
    if (in_progress && mode!=PROGRESS_OFF) {
        assert(size==MAP_BYTES); atomic_fetch_add(&progress_allocations,1);
        if (mode==PROGRESS_REJECT) {
            atomic_fetch_add(&progress_failures,1); errno=ENOMEM; return NULL;
        }
    } else if (record_allocations) {
        if (allocation_count<2) allocation_sizes[allocation_count]=size;
        ++allocation_count;
        if (reject_allocations) { ++failure_count; errno=ENOMEM; return NULL; }
    }
    return __real_malloc(size);
}
native_pending *__real_native_async_take_ready(native_device *);
native_pending *__wrap_native_async_take_ready(native_device *device)
{
    bool previous=in_progress; in_progress=true;
    native_pending *result=__real_native_async_take_ready(device);
    in_progress=previous; return result;
}
void __real_native_buffer_close(native_buffer *);
void __wrap_native_buffer_close(native_buffer *payload)
{
    bool tracked=payload==atomic_load(&tracked_buffer);
    __real_native_buffer_close(payload);
    if (tracked) atomic_fetch_add(&buffers_closed,1);
}
void __real_native_backend_close(native_backend *);
void __wrap_native_backend_close(native_backend *backend)
{
    bool tracked=backend==atomic_load(&tracked_backend);
    __real_native_backend_close(backend);
    if (tracked) atomic_fetch_add(&backends_closed,1);
}
typedef struct control { state s; WGPUBuffer buffer; unsigned maps,errors; } control;
typedef struct lock_probe { native_device *device; int result; } lock_probe;
static void *probe_lock(void *userdata)
{
    lock_probe *probe=userdata;
    struct timespec now,deadline;
    assert(doe_monotonic_now(&now).status==DOE_HOST_OK && doe_time_add(&now,UINT64_C(1000000000),&deadline).status==DOE_HOST_OK);
    do {
        probe->result=pthread_mutex_trylock(&probe->device->mutex.native);
        if (!probe->result) { assert(pthread_mutex_unlock(&probe->device->mutex.native)==0); return NULL; }
        assert(probe->result==EBUSY && doe_monotonic_now(&now).status==DOE_HOST_OK);
        if (doe_time_reached(&now,&deadline)) break;
        /* Independent worker contention may end; a callback-held outer lock
         * cannot end while its reporting thread is joining this probe. */
        assert(doe_sleep_for(UINT64_C(20000)).status==DOE_HOST_OK);
    } while (true);
    return NULL;
}
static void reentrant_release(control *test)
{
    assert(wgpuBufferGetMapState(test->buffer)==WGPUBufferMapState_Unmapped);
    lock_probe probe={(native_device *)test->s.device,EBUSY}; pthread_t thread;
    assert(pthread_create(&thread,NULL,probe_lock,&probe)==0);
    assert(pthread_join(thread,NULL)==0 && probe.result==0);
    wgpuBufferRelease(test->buffer); test->buffer=NULL;
    wgpuQueueRelease(test->s.queue); test->s.queue=NULL;
    wgpuDeviceRelease(test->s.device); test->s.device=NULL;
    assert(!atomic_load(&buffers_closed) && !atomic_load(&backends_closed));
}
static void out_of_memory(WGPUDevice const *handle,WGPUErrorType type,WGPUStringView message,
    void *userdata,void *unused)
{
    (void)unused; control *test=userdata;
    assert(*handle==test->s.device && type==WGPUErrorType_OutOfMemory);
    assert(message.data && message.length && ++test->errors==1);
    reentrant_release(test);
}
static void map_reply(WGPUMapAsyncStatus status,WGPUStringView message,void *userdata,void *unused)
{
    (void)message; (void)unused; control *test=userdata;
    assert(status==WGPUMapAsyncStatus_Success && ++test->maps==1);
}
static void map_error(WGPUMapAsyncStatus status,WGPUStringView message,void *userdata,void *unused)
{
    (void)unused; control *test=userdata;
    const char expected[]="buffer map failed or aborted";
    assert(status==WGPUMapAsyncStatus_Error && ++test->maps==1 && !test->errors);
    assert(message.length==sizeof(expected)-1 && !memcmp(message.data,expected,sizeof(expected)-1));
    reentrant_release(test);
}
static void validation(WGPUPopErrorScopeStatus status,WGPUErrorType type,WGPUStringView message,
    void *userdata,void *unused)
{
    (void)unused;
    assert(status==WGPUPopErrorScopeStatus_Success && type==WGPUErrorType_Validation && message.length);
    ++*(unsigned *)userdata;
}
static void init(control *test)
{
    atomic_store(&tracked_buffer,NULL); atomic_store(&tracked_backend,NULL);
    atomic_store(&buffers_closed,0); atomic_store(&backends_closed,0);
    state *s=&test->s; s->instance=wgpuCreateInstance(NULL);
    WGPURequestAdapterOptions options=WGPU_REQUEST_ADAPTER_OPTIONS_INIT; adapter_class_options(&options);
    WGPURequestAdapterCallbackInfo adapter={.mode=WGPUCallbackMode_AllowSpontaneous,.callback=adapter_reply,.userdata1=s};
    assert(wgpuInstanceRequestAdapter(s->instance,&options,adapter).id && s->adapter); adapter_class_require(s->adapter);
    WGPUDeviceDescriptor descriptor=WGPU_DEVICE_DESCRIPTOR_INIT;
    descriptor.uncapturedErrorCallbackInfo=(WGPUUncapturedErrorCallbackInfo){.callback=out_of_memory,.userdata1=test};
    WGPURequestDeviceCallbackInfo device={.mode=WGPUCallbackMode_AllowSpontaneous,.callback=device_reply,.userdata1=s};
    assert(wgpuAdapterRequestDevice(s->adapter,&descriptor,device).id && s->device);
    s->queue=wgpuDeviceGetQueue(s->device); test->buffer=buffer(s,MAP_BYTES,WGPUBufferUsage_MapWrite|WGPUBufferUsage_CopySrc);
    atomic_store(&tracked_buffer,doe_resource_buffer_native((doe_resource_buffer *)test->buffer));
    atomic_store(&tracked_backend,&((native_device *)s->device)->backend);
}
static void finish(control *test)
{
    struct timespec now,deadline;
    assert(doe_monotonic_now(&now).status==DOE_HOST_OK && doe_time_add(&now,UINT64_C(5000000000),&deadline).status==DOE_HOST_OK);
    while (!atomic_load(&buffers_closed) || !atomic_load(&backends_closed)) {
        assert(atomic_load(&buffers_closed)<=1 && atomic_load(&backends_closed)<=1);
        assert(doe_monotonic_now(&now).status==DOE_HOST_OK && !doe_time_reached(&now,&deadline));
        assert(doe_sleep_for(UINT64_C(20000)).status==DOE_HOST_OK);
    }
    assert(atomic_load(&buffers_closed)==1 && atomic_load(&backends_closed)==1);
    wgpuAdapterRelease(test->s.adapter); wgpuInstanceRelease(test->s.instance);
}
int main(void)
{
    setbuf(stdout,NULL); control admission={0}; init(&admission); state *s=&admission.s;
    WGPUBufferMapCallbackInfo callback={.mode=WGPUCallbackMode_WaitAnyOnly,.callback=map_reply,.userdata1=&admission};
    record_allocations=true; atomic_store(&progress_mode,PROGRESS_RECORD);
    WGPUFuture baseline=wgpuBufferMapAsync(admission.buffer,WGPUMapMode_Write,0,MAP_BYTES,callback);
    record_allocations=false;
    assert(baseline.id && allocation_count==1 && allocation_sizes[0]>MAP_BYTES);
    const size_t callback_bytes=allocation_sizes[0]; wait_future(s,baseline);
    atomic_store(&progress_mode,PROGRESS_OFF);
    assert(admission.maps==1 && atomic_load(&progress_allocations)==1 && !atomic_load(&progress_failures));
    wgpuBufferUnmap(admission.buffer);
    unsigned validations=0; wgpuDevicePushErrorScope(s->device,WGPUErrorFilter_Validation);
    allocation_count=failure_count=0; record_allocations=reject_allocations=true;
    WGPUFuture invalid=wgpuBufferMapAsync(admission.buffer,WGPUMapMode_Write,4,16,callback);
    record_allocations=reject_allocations=false;
    assert(!invalid.id && failure_count==1 && allocation_count==1 && allocation_sizes[0]==callback_bytes);
    WGPUPopErrorScopeCallbackInfo pop={.mode=WGPUCallbackMode_AllowSpontaneous,.callback=validation,.userdata1=&validations};
    assert(wgpuDevicePopErrorScope(s->device,pop).id && validations==1 && !admission.errors && admission.maps==1);
    allocation_count=failure_count=0; record_allocations=reject_allocations=true;
    WGPUFuture rejected=wgpuBufferMapAsync(admission.buffer,WGPUMapMode_Write,0,MAP_BYTES,callback);
    record_allocations=reject_allocations=false;
    assert(!rejected.id && failure_count==1 && allocation_count==1 && allocation_sizes[0]==callback_bytes);
    assert(admission.maps==1 && admission.errors==1 && !admission.buffer && !s->device); finish(&admission);
    control deferred={0}; init(&deferred); s=&deferred.s;
    callback=(WGPUBufferMapCallbackInfo){.mode=WGPUCallbackMode_WaitAnyOnly,.callback=map_error,.userdata1=&deferred};
    atomic_store(&progress_allocations,0); atomic_store(&progress_failures,0); atomic_store(&progress_mode,PROGRESS_REJECT);
    WGPUFuture failed=wgpuBufferMapAsync(deferred.buffer,WGPUMapMode_Write,0,MAP_BYTES,callback);
    assert(failed.id && !deferred.maps && !deferred.errors);
    wait_future(s,failed); atomic_store(&progress_mode,PROGRESS_OFF);
    assert(atomic_load(&progress_allocations)==1 && atomic_load(&progress_failures)==1);
    assert(deferred.maps==1 && !deferred.errors && !deferred.buffer && !s->device); finish(&deferred);
    printf("PASS map OOM: %zu-byte callback admission failure gives future0/rollback/unlocked OOM; deferred %u-byte allocation failure gives admitted Error callback; distinct real fault counts, validation identity, reentrant releases and final worker/observer cleanup %s\n",callback_bytes,MAP_BYTES,adapter_class_scope());
}
