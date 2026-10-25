/* SPDX-License-Identifier: MIT
 * Real map-byte and callback-event malloc failures; callback rejection must
 * deliver the produced OOM after rollback, retaining the in-call buffer lease. */
#include "../src/internal.h"
#include "consumer.h"
#include <errno.h>

enum { MAP_BYTES = 64 };
static _Thread_local bool record_allocations, reject_allocations;
static _Thread_local unsigned allocation_count, failure_count;
static _Thread_local size_t allocation_sizes[2];
static native_buffer *tracked_buffer;
static native_backend *tracked_backend;
static atomic_uint buffers_closed, backends_closed;

void *__real_malloc(size_t);
void *__wrap_malloc(size_t size)
{
    if (record_allocations && allocation_count < 2) {
        allocation_sizes[allocation_count++] = size;
        if (reject_allocations) {
            ++failure_count;
            errno = ENOMEM;
            return NULL;
        }
    } else if (record_allocations) {
        /* Additional source-owner allocations invalidate this boundary plan. */
        ++allocation_count;
    }
    return __real_malloc(size);
}

void __real_native_buffer_close(native_buffer *);
void __wrap_native_buffer_close(native_buffer *payload)
{
    bool tracked = payload == tracked_buffer;
    __real_native_buffer_close(payload);
    if (tracked) atomic_fetch_add(&buffers_closed, 1);
}
void __real_native_backend_close(native_backend *);
void __wrap_native_backend_close(native_backend *backend)
{
    bool tracked = backend == tracked_backend;
    __real_native_backend_close(backend);
    if (tracked) atomic_fetch_add(&backends_closed, 1);
}

typedef struct control {
    state s;
    WGPUBuffer buffer;
    unsigned maps, errors;
} control;

typedef struct lock_probe { native_device *device; int result; } lock_probe;
static void *probe_lock(void *userdata)
{
    lock_probe *probe = userdata;
    probe->result = pthread_mutex_trylock(&probe->device->mutex.native);
    if (!probe->result) assert(pthread_mutex_unlock(&probe->device->mutex.native) == 0);
    return NULL;
}
static void out_of_memory(WGPUDevice const *handle, WGPUErrorType type, WGPUStringView message,
    void *userdata, void *unused)
{
    (void)unused;
    control *test = userdata;
    assert(*handle == test->s.device && type == WGPUErrorType_OutOfMemory);
    assert(message.data && message.length && ++test->errors == 1);
    assert(wgpuBufferGetMapState(test->buffer) == WGPUBufferMapState_Unmapped);
    /* A second thread detects an outer recursive lock that same-thread reentry
     * cannot detect. Its nonblocking try never waits for the reporting thread. */
    lock_probe probe = {(native_device *)test->s.device, EBUSY};
    pthread_t thread;
    assert(pthread_create(&thread, NULL, probe_lock, &probe) == 0);
    assert(pthread_join(thread, NULL) == 0 && probe.result == 0);
    wgpuBufferRelease(test->buffer);
    test->buffer = NULL;
    wgpuQueueRelease(test->s.queue);
    test->s.queue = NULL;
    wgpuDeviceRelease(test->s.device);
    test->s.device = NULL;
    assert(atomic_load(&buffers_closed) == 0 && atomic_load(&backends_closed) == 0);
}
static void map_reply(WGPUMapAsyncStatus status, WGPUStringView message, void *userdata, void *unused)
{
    (void)message;
    (void)unused;
    control *test = userdata;
    assert(status == WGPUMapAsyncStatus_Success && ++test->maps == 1);
}

static void map_validation_reply(WGPUPopErrorScopeStatus status, WGPUErrorType type,
    WGPUStringView message, void *userdata, void *unused)
{
    (void)unused;
    assert(status == WGPUPopErrorScopeStatus_Success && type == WGPUErrorType_Validation);
    assert(message.data && message.length);
    ++*(unsigned *)userdata;
}

int main(void)
{
    setbuf(stdout, NULL);
    control test = {0};
    state *s = &test.s;
    s->instance = wgpuCreateInstance(NULL);
    WGPURequestAdapterOptions options = WGPU_REQUEST_ADAPTER_OPTIONS_INIT;
    adapter_class_options(&options);
    WGPURequestAdapterCallbackInfo adapter = {.mode=WGPUCallbackMode_AllowSpontaneous,
        .callback=adapter_reply,.userdata1=s};
    assert(wgpuInstanceRequestAdapter(s->instance, &options, adapter).id && s->adapter);
    adapter_class_require(s->adapter);
    WGPUAdapterInfo info = WGPU_ADAPTER_INFO_INIT;
    assert(wgpuAdapterGetInfo(s->adapter, &info) == WGPUStatus_Success);
    printf("Selected map OOM adapter: %.*s; backend=%u type=%u\n",
        (int)info.device.length, info.device.data, (unsigned)info.backendType, (unsigned)info.adapterType);
    wgpuAdapterInfoFreeMembers(info);
    WGPUDeviceDescriptor descriptor = WGPU_DEVICE_DESCRIPTOR_INIT;
    descriptor.uncapturedErrorCallbackInfo = (WGPUUncapturedErrorCallbackInfo){.callback=out_of_memory,.userdata1=&test};
    WGPURequestDeviceCallbackInfo device = {.mode=WGPUCallbackMode_AllowSpontaneous,.callback=device_reply,.userdata1=s};
    assert(wgpuAdapterRequestDevice(s->adapter, &descriptor, device).id && s->device);
    s->queue = wgpuDeviceGetQueue(s->device);
    test.buffer = buffer(s, MAP_BYTES, WGPUBufferUsage_MapWrite | WGPUBufferUsage_CopySrc);
    tracked_buffer = doe_resource_buffer_native((doe_resource_buffer *)test.buffer);
    tracked_backend = &((native_device *)s->device)->backend;
    WGPUBufferMapCallbackInfo callback = {.mode=WGPUCallbackMode_AllowSpontaneous,.callback=map_reply,.userdata1=&test};

    /* Observe the actual successful owner path rather than duplicate the
     * callback event's private structure size or allocator layout. */
    record_allocations = true;
    WGPUFuture baseline = wgpuBufferMapAsync(test.buffer, WGPUMapMode_Write, 0, MAP_BYTES, callback);
    record_allocations = false;
    assert(baseline.id && test.maps == 1 && allocation_count == 2);
    assert(allocation_sizes[0] == MAP_BYTES && allocation_sizes[1] > MAP_BYTES);
    const size_t callback_bytes = allocation_sizes[1];
    wgpuBufferUnmap(test.buffer);
    assert(wgpuBufferGetMapState(test.buffer) == WGPUBufferMapState_Unmapped);

    /* A validation failure remains observable when admission itself runs out
     * of memory. No mapping token was created, and the first error stays
     * Validation rather than being replaced by callback-allocation OOM. */
    unsigned validations = 0;
    wgpuDevicePushErrorScope(s->device, WGPUErrorFilter_Validation);
    allocation_count = 0;
    record_allocations = true;
    reject_allocations = true;
    WGPUFuture invalid = wgpuBufferMapAsync(test.buffer, WGPUMapMode_Write, 4, 16, callback);
    record_allocations = false;
    reject_allocations = false;
    assert(!invalid.id && failure_count == 1 && allocation_count == 1);
    assert(allocation_sizes[0] == callback_bytes && test.maps == 1 && test.errors == 0);
    WGPUPopErrorScopeCallbackInfo pop = {.mode=WGPUCallbackMode_AllowSpontaneous,
        .callback=map_validation_reply,.userdata1=&validations};
    assert(wgpuDevicePopErrorScope(s->device, pop).id && validations == 1);
    assert(wgpuBufferGetMapState(test.buffer) == WGPUBufferMapState_Unmapped);

    allocation_count = 0;
    failure_count = 0;
    record_allocations = true;
    reject_allocations = true;
    WGPUFuture rejected = wgpuBufferMapAsync(test.buffer, WGPUMapMode_Write, 0, MAP_BYTES, callback);
    record_allocations = false;
    reject_allocations = false;
    assert(!rejected.id && failure_count == 2 && allocation_count == 2);
    assert(allocation_sizes[0] == MAP_BYTES && allocation_sizes[1] == callback_bytes);
    assert(test.maps == 1 && test.errors == 1 && !test.buffer && !s->queue && !s->device);
    assert(atomic_load(&buffers_closed) == 1 && atomic_load(&backends_closed) == 1);
    wgpuAdapterRelease(s->adapter);
    wgpuInstanceRelease(s->instance);
    printf("PASS map OOM: real %u-byte map and %zu-byte callback allocations both fail, future0, "
        "no failure map callback, one unlocked OOM after token rollback, reentrant public releases "
        "and final in-call buffer/device cleanup %s\n", MAP_BYTES, callback_bytes, adapter_class_scope());
    return 0;
}
