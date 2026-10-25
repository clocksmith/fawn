/* SPDX-License-Identifier: MIT
 * Shared-memory public ABI and real owners over private Metal call ledgers.
 * No pixel values, native event completion or GPU execution are simulated. */
#include "../src/internal.h"
#include "doe_native_shared_memory.h"
#include "../../backends/metal/tests/mock_native.h"
#include "consumer.h"
#include <stdatomic.h>

typedef struct shared_event { unsigned references; uint64_t value; } shared_event;
typedef struct fixture {
    mock_native mock;
    doe_mtl_native base;
    doe_mtl_context *context;
    shared_event events[12];
    unsigned event_count, event_releases, surface_references, signals, producer_waits;
    uint64_t waited_value;
    bool hold, fail_signal, fail_import, fail_surface;
    atomic_bool wait_entered, wait_release;
    bool blocking_wait;
    void *last_signal;
} fixture;
static fixture fixtures[3];
static unsigned fixture_count;
static unsigned physical_identity;
static char last_message[NATIVE_FAILURE_MESSAGE_CAPACITY];
static fixture *driver_fixture(void *userdata) { return userdata; }
static bool is_event(fixture *f, void *handle)
{
    for (unsigned i = 0; i < f->event_count; ++i) if (handle == &f->events[i]) return true;
    return false;
}
static void release(void *userdata, void *handle)
{
    fixture *f = driver_fixture(userdata);
    if (is_event(f, handle)) {
        shared_event *event = handle;
        assert(event->references);
        --event->references;
        ++f->event_releases;
    } else f->base.release(userdata, handle);
}
static doe_mtl_status event_create(void *userdata, void *device, void **out, doe_mtl_error *error)
{
    fixture *f = driver_fixture(userdata);
    assert(device && f->event_count < 12);
    shared_event *event = &f->events[f->event_count++];
    *event = (shared_event){.references=1};
    *out = event;
    return doe_mtl_fail(DOE_MTL_OK, "shared-event allocation ledger", error);
}
static doe_mtl_status event_retain(void *userdata, void *handle, void **out, doe_mtl_error *error)
{
    fixture *f = driver_fixture(userdata);
    assert(is_event(f,handle));
    shared_event *event = handle;
    assert(event->references);
    ++event->references;
    *out = handle;
    return doe_mtl_fail(DOE_MTL_OK, "shared-event retain ledger", error);
}
static doe_mtl_status signal_event(void *userdata, void *command, void *handle,
    uint64_t value, doe_mtl_error *error)
{
    fixture *f = driver_fixture(userdata);
    assert(command && is_event(f,handle) && value == 1);
    assert(((shared_event *)handle)->references == 2);
    f->last_signal = handle;
    ++f->signals;
    if (f->fail_signal) {
        doe_mtl_status status = doe_mtl_fail(DOE_MTL_NATIVE_FAILURE, "retained first signal failure", error);
        error->native_code = -91;
        memcpy(error->message, "signal detail", sizeof("signal detail"));
        return status;
    }
    return doe_mtl_fail(DOE_MTL_OK, "shared signal ledger", error);
}
static doe_mtl_status event_wait(void *userdata, void *handle, uint64_t value, doe_mtl_error *error)
{
    fixture *f = driver_fixture(userdata);
    assert(is_event(f,handle));
    ++f->producer_waits;
    f->waited_value = value;
    atomic_store_explicit(&f->wait_entered, true, memory_order_release);
    while (f->blocking_wait && !atomic_load_explicit(&f->wait_release, memory_order_acquire)) {
        struct timespec delay = {0,1000000};
        assert(nanosleep(&delay,NULL) == 0);
    }
    assert(((shared_event *)handle)->references);
    return doe_mtl_fail(DOE_MTL_OK, "producer wait call ledger", error);
}
static doe_mtl_status surface_open(void *userdata, void *surface, void **out,
    uint32_t *width, uint32_t *height, doe_mtl_error *error)
{
    fixture *f = driver_fixture(userdata);
    assert(surface == f);
    if (f->fail_surface) return doe_mtl_fail(DOE_MTL_UNSUPPORTED,"multiplane surface ledger",error);
    ++f->surface_references;
    *out = surface;
    *width = 4;
    *height = 2;
    return doe_mtl_fail(DOE_MTL_OK, "surface retain ledger", error);
}
static void surface_release(void *userdata, void *surface)
{
    fixture *f = driver_fixture(userdata);
    assert(surface == f && f->surface_references);
    --f->surface_references;
}
static doe_mtl_status texture_import(void *userdata, void *device, void *surface,
    const doe_mtl_texture_desc *desc, void **out, doe_mtl_error *error)
{
    fixture *f = driver_fixture(userdata);
    assert(surface == f && f->surface_references && desc->width == 4 && desc->height == 2);
    if (f->fail_import) return doe_mtl_fail(DOE_MTL_NATIVE_FAILURE,"native IOSurface import failure",error);
    return f->base.texture(userdata,device,desc,out,error);
}
doe_mtl_status __wrap_doe_mtl_context_create(doe_mtl_context **out, doe_mtl_error *error)
{
    assert(fixture_count < 3);
    fixture *f = &fixtures[fixture_count++];
    doe_mtl_native native = mock_driver(&f->mock);
    f->base = native;
    atomic_init(&f->wait_entered,false);
    atomic_init(&f->wait_release,false);
    native.release = release;
    native.shared_surface = surface_open;
    native.shared_surface_release = surface_release;
    native.shared_texture = texture_import;
    native.shared_event_retain = event_retain;
    native.shared_event_wait = event_wait;
    native.timeline_create = event_create;
    native.timeline_signal = signal_event;
    doe_mtl_status status = doe_mtl_context_open(native,out,error);
    if (status == DOE_MTL_OK) f->context = *out;
    return status;
}
void *__wrap_doe_mtl_context_device(const doe_mtl_context *context)
{
    assert(context);
    return &physical_identity;
}
doe_mtl_status __real_doe_mtl_commands_wait(doe_mtl_commands *, doe_mtl_error *);
doe_mtl_status __wrap_doe_mtl_commands_wait(doe_mtl_commands *commands, doe_mtl_error *error)
{
    fixture *f = &fixtures[1];
    mock_result(&f->mock,f->hold ? 0 : 4,0);
    return __real_doe_mtl_commands_wait(commands,error);
}
static void error_capture(WGPUDevice const *device, WGPUErrorType type, WGPUStringView message,
    void *userdata, void *unused)
{
    (void)device;
    (void)unused;
    state *s = userdata;
    ++s->errors;
    s->last_error = type;
    size_t length = message.length < sizeof(last_message) - 1 ? message.length : sizeof(last_message) - 1;
    memcpy(last_message,message.data,length);
    last_message[length] = '\0';
}
static WGPUSharedTextureMemory import_memory(state *s, fixture *f)
{
    WGPUSharedTextureMemoryIOSurfaceDescriptor surface = {
        .chain={.sType=(WGPUSType)DOE_SHARED_STYPE_IOSURFACE},.ioSurface=f};
    WGPUSharedTextureMemoryDescriptor desc = {.nextInChain=&surface.chain,.label={"surface",7}};
    return wgpuDeviceImportSharedTextureMemory(s->device,&desc);
}
static WGPUTexture make_texture(WGPUSharedTextureMemory memory, WGPUTextureFormat format)
{
    WGPUTextureDescriptor desc = {.dimension=WGPUTextureDimension_2D,.format=format,
        .size={4,2,1},.mipLevelCount=1,.sampleCount=1,.usage=WGPUTextureUsage_CopySrc |
        WGPUTextureUsage_CopyDst | WGPUTextureUsage_TextureBinding | WGPUTextureUsage_RenderAttachment};
    return wgpuSharedTextureMemoryCreateTexture(memory,&desc);
}
static WGPUSharedFence import_fence(state *s, fixture *f)
{
    void *event = NULL;
    assert(event_create(f,(void *)s->device,&event,NULL) == DOE_MTL_OK);
    WGPUSharedFenceMTLSharedEventDescriptor native = {
        .chain={.sType=(WGPUSType)DOE_SHARED_STYPE_MTL_EVENT_DESCRIPTOR},.sharedEvent=event};
    WGPUSharedFenceDescriptor desc = {.nextInChain=&native.chain,.label={"producer",8}};
    WGPUSharedFence fence = wgpuDeviceImportSharedFence(s->device,&desc);
    release(f,event); /* Public fence must retain the caller's original identity. */
    return fence;
}
static void *export_event(WGPUSharedFence fence)
{
    WGPUSharedFenceMTLSharedEventExportInfo native = {.chain={.sType=(WGPUSType)DOE_SHARED_STYPE_MTL_EVENT_EXPORT}};
    WGPUSharedFenceExportInfo info = {.nextInChain=&native.chain};
    wgpuSharedFenceExportInfo(fence,&info);
    assert(info.type == DOE_SHARED_FENCE_MTL_EVENT);
    return native.sharedEvent;
}
typedef struct begin_call {
    WGPUSharedTextureMemory memory;
    WGPUTexture texture;
    WGPUSharedTextureMemoryBeginAccessDescriptor descriptor;
    WGPUStatus status;
} begin_call;
static void begin_thread(void *raw)
{
    begin_call *call = raw;
    call->status = wgpuSharedTextureMemoryBeginAccess(call->memory,call->texture,&call->descriptor);
}
static void producer_concurrency(state *s, fixture *f, WGPUSharedTextureMemory memory, WGPUTexture texture)
{
    WGPUSharedFence fence = import_fence(s,f);
    assert(fence);
    shared_event *native = export_event(fence);
    uint64_t value = 73;
    WGPUTexture waiting_texture = make_texture(memory,WGPUTextureFormat_BGRA8UnormSrgb);
    assert(waiting_texture);
    begin_call call = {.memory=memory,.texture=waiting_texture,
        .descriptor={.fenceCount=1,.fences=&fence,.signaledValues=&value}};
    doe_thread thread = {0};
    f->blocking_wait = true;
    assert(doe_thread_create(&thread,begin_thread,&call).status == DOE_HOST_OK);
    for (unsigned i = 0; !atomic_load_explicit(&f->wait_entered,memory_order_acquire); ++i) {
        assert(i < 5000);
        struct timespec delay = {0,1000000};
        assert(nanosleep(&delay,NULL) == 0);
    }
    /* Acquiring this lock while the producer wait is blocked proves Begin did
     * not hold it; another Begin cannot claim the not-yet-published access. */
    native_lock((native_device *)s->device);
    assert(native->references == 1);
    native_unlock((native_device *)s->device);
    assert(wgpuSharedTextureMemoryBeginAccess(memory,texture,NULL) == WGPUStatus_Error);
    assert(wgpuSharedTextureMemoryEndAccess(memory,texture,NULL) == WGPUStatus_Error);
    /* Failed unrelated texture creation must not clear the reserved Begin state. */
    f->fail_import = true;
    unsigned errors = s->errors;
    assert(!make_texture(memory,WGPUTextureFormat_BGRA8UnormSrgb) && s->errors == errors + 1);
    f->fail_import = false;
    assert(wgpuSharedTextureMemoryBeginAccess(memory,texture,NULL) == WGPUStatus_Error);
    unsigned released = f->mock.releases_by_kind[MOCK_KIND_TEXTURE];
    wgpuTextureRelease(waiting_texture);
    assert(f->mock.releases_by_kind[MOCK_KIND_TEXTURE] == released);
    wgpuSharedFenceRelease(fence);
    assert(native->references == 1); /* In-flight Begin owns the last fence lease. */
    atomic_store_explicit(&f->wait_release,true,memory_order_release);
    assert(doe_thread_join(&thread).status == DOE_HOST_OK && call.status == WGPUStatus_Success);
    assert(f->waited_value == 73 && !native->references);
    f->blocking_wait = false;
    assert(f->mock.releases_by_kind[MOCK_KIND_TEXTURE] == released + 1);
    /* Begin's call lease was last: finalization cleared the weak active identity. */
    assert(wgpuSharedTextureMemoryBeginAccess(memory,texture,NULL) == WGPUStatus_Success);
    assert(wgpuSharedTextureMemoryEndAccess(memory,texture,NULL) == WGPUStatus_Success);
}
static void unsupported_buffer_contract(void)
{
    WGPUSharedBufferMemoryProperties properties = {.size=99,.usage=99};
    WGPUSharedBufferMemoryEndAccessState end = {.initialized=WGPU_TRUE,.fenceCount=99};
    assert(!wgpuDeviceImportSharedBufferMemory(NULL,NULL));
    assert(!wgpuSharedBufferMemoryCreateBuffer(NULL,NULL));
    assert(wgpuSharedBufferMemoryBeginAccess(NULL,NULL,NULL) == WGPUStatus_Error);
    assert(wgpuSharedBufferMemoryGetProperties(NULL,&properties) == WGPUStatus_Error && !properties.size && !properties.usage);
    assert(wgpuSharedBufferMemoryEndAccess(NULL,NULL,&end) == WGPUStatus_Error && !end.initialized && !end.fenceCount);
    assert(wgpuSharedBufferMemoryIsDeviceLost(NULL));
    wgpuSharedBufferMemoryEndAccessStateFreeMembers(end);
}
int main(void)
{
    state s = {0};
    s.instance = wgpuCreateInstance(NULL);
    WGPURequestAdapterOptions options = WGPU_REQUEST_ADAPTER_OPTIONS_INIT;
    options.backendType = WGPUBackendType_Metal;
    WGPURequestAdapterCallbackInfo adapter = {.mode=WGPUCallbackMode_AllowSpontaneous,.callback=adapter_reply,.userdata1=&s};
    assert(wgpuInstanceRequestAdapter(s.instance,&options,adapter).id && s.adapter);
    WGPUDeviceDescriptor desc = WGPU_DEVICE_DESCRIPTOR_INIT;
    desc.uncapturedErrorCallbackInfo = (WGPUUncapturedErrorCallbackInfo){.callback=error_capture,.userdata1=&s};
    s.device = wgpuAdapterCreateDevice(s.adapter,&desc);
    assert(s.device);
    s.queue = wgpuDeviceGetQueue(s.device);
    fixture *f = &fixtures[1];
    unsupported_buffer_contract();
    WGPUSharedTextureMemory memory = import_memory(&s,f);
    WGPUSharedTextureMemory other = import_memory(&s,f);
    assert(memory && other && f->surface_references == 2);
    WGPUSharedTextureMemoryProperties properties = {0};
    assert(wgpuSharedTextureMemoryGetProperties(memory,&properties) == WGPUStatus_Success);
    assert(properties.format == WGPUTextureFormat_Undefined && properties.size.width == 4 && properties.size.height == 2);
    WGPUTexture texture = make_texture(memory,WGPUTextureFormat_BGRA8UnormSrgb);
    assert(texture && f->mock.texture_desc.format == DOE_MTL_BGRA8_SRGB && !f->mock.uploads && !f->mock.renders);
    assert(wgpuSharedTextureMemoryGetProperties(memory,&properties) == WGPUStatus_Success &&
        properties.format == WGPUTextureFormat_BGRA8UnormSrgb);
    unsigned errors = s.errors;
    assert(!make_texture(memory,WGPUTextureFormat_RGBA8Unorm) && s.errors == errors + 1);
    assert(wgpuSharedTextureMemoryBeginAccess(other,texture,NULL) == WGPUStatus_Error);
    WGPUTexture second = make_texture(memory,WGPUTextureFormat_BGRA8UnormSrgb);
    assert(second && wgpuSharedTextureMemoryBeginAccess(memory,texture,NULL) == WGPUStatus_Success);
    assert(wgpuSharedTextureMemoryEndAccess(memory,second,NULL) == WGPUStatus_Error);
    WGPUSharedTextureMemoryEndAccessState end = {0};
    assert(wgpuSharedTextureMemoryEndAccess(memory,texture,&end) == WGPUStatus_Success);
    assert(end.initialized && end.fenceCount == 1 && end.signaledValues[0] == 1);
    void *first_event = export_event(end.fences[0]);
    assert(first_event == f->last_signal && ((shared_event *)first_event)->references == 1);
    wgpuSharedTextureMemoryEndAccessStateFreeMembers(end);
    assert(!((shared_event *)first_event)->references);
    assert(wgpuSharedTextureMemoryBeginAccess(memory,texture,NULL) == WGPUStatus_Success);
    assert(wgpuSharedTextureMemoryEndAccess(memory,texture,&end) == WGPUStatus_Success);
    assert(export_event(end.fences[0]) != first_event); /* No pre-access completion reuse. */
    wgpuSharedTextureMemoryEndAccessStateFreeMembers(end);
    producer_concurrency(&s,f,memory,texture);
    assert(wgpuSharedTextureMemoryBeginAccess(memory,texture,NULL) == WGPUStatus_Success);
    f->fail_signal = true;
    end = (WGPUSharedTextureMemoryEndAccessState){.initialized=7,.fenceCount=123};
    errors = s.errors;
    assert(wgpuSharedTextureMemoryEndAccess(memory,texture,&end) == WGPUStatus_Error);
    assert(end.initialized == 7 && end.fenceCount == 123 && s.errors == errors + 1);
    assert(strstr(last_message,"retained first signal failure") && strstr(last_message,"-91") && strstr(last_message,"signal detail"));
    assert(!((shared_event *)f->last_signal)->references);
    f->fail_signal = false;
    assert(wgpuSharedTextureMemoryEndAccess(memory,texture,&end) == WGPUStatus_Success);
    wgpuSharedTextureMemoryEndAccessStateFreeMembers(end);
    assert(wgpuSharedTextureMemoryBeginAccess(memory,texture,NULL) == WGPUStatus_Success);
    native_device *device = (native_device *)s.device;
    native_lock(device);
    f->hold = true;
    native_unlock(device);
    end = (WGPUSharedTextureMemoryEndAccessState){.initialized=7,.fenceCount=123};
    assert(wgpuSharedTextureMemoryEndAccess(memory,texture,&end) == WGPUStatus_Error && end.fenceCount == 123);
    native_lock(device);
    assert(device->pending && ((shared_event *)f->last_signal)->references == 1);
    f->hold = false;
    native_queue_settlement settled = native_settle_pending(device);
    assert(settled.complete && settled.failure.status == NATIVE_OK && !device->pending);
    assert(!((shared_event *)f->last_signal)->references);
    native_unlock(device);
    assert(wgpuSharedTextureMemoryEndAccess(memory,texture,NULL) == WGPUStatus_Success);
    WGPUTexture active = make_texture(memory,WGPUTextureFormat_BGRA8UnormSrgb);
    assert(active && wgpuSharedTextureMemoryBeginAccess(memory,active,NULL) == WGPUStatus_Success);
    wgpuTextureRelease(active);
    assert(wgpuSharedTextureMemoryBeginAccess(memory,texture,NULL) == WGPUStatus_Success);
    assert(wgpuSharedTextureMemoryEndAccess(memory,texture,NULL) == WGPUStatus_Success);
    wgpuSharedTextureMemoryRelease(memory);
    wgpuSharedTextureMemoryRelease(other);
    assert(f->surface_references == 1); /* Both textures retain the same memory owner. */
    wgpuTextureRelease(texture);
    assert(f->surface_references == 1);
    wgpuTextureRelease(second);
    assert(!f->surface_references);
    s.errors = 0; /* Every expected failure was asserted at its operation above. */
    consumer_finish(&s);
    for (unsigned i = 0; i < fixture_count; ++i) {
        for (unsigned j = 0; j < fixtures[i].event_count; ++j) assert(!fixtures[i].events[j].references);
        mock_assert_closed(&fixtures[i].mock);
    }
    puts("PASS shared IOSurface/event host controls: native identity, channel order, exact texture provenance, producer-lock exclusion, fresh completion signals, first-error rollback and unknown-work leases; no SDK/GPU run");
    return 0;
}
