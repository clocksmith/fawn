/* SPDX-License-Identifier: MIT
 * Shared-memory public ABI and real owners over private Metal call ledgers.
 * No pixel values, native event completion or GPU execution are simulated. */
#include "../src/internal.h"
#include "doe_native_shared_memory.h"
#include "../../backends/metal/tests/mock_native.h"
#include "../../backends/metal/src/internal.h"
#include "doe_metal_shared.h"
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
    bool hold, fail_signal, fail_import, fail_surface, invalid_surface_format;
    bool timeline_enabled;
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
    assert(command && is_event(f,handle));
    if (f->timeline_enabled && handle == &f->events[0]) assert(value > 0);
    else assert(value == 1 && ((shared_event *)handle)->references == 2);
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
static doe_mtl_status event_wait(void *userdata, void *command, void *handle, uint64_t value, doe_mtl_error *error)
{
    fixture *f = driver_fixture(userdata);
    assert(is_event(f,handle));
    ++f->producer_waits;
    f->waited_value = value;
    assert(command && !((shared_event *)handle)->value);
    assert(((shared_event *)handle)->references);
    return doe_mtl_fail(DOE_MTL_OK, "producer wait call ledger", error);
}
static uint64_t event_value(void *userdata, void *handle)
{
    fixture *f = driver_fixture(userdata);
    assert(is_event(f, handle));
    return ((shared_event *)handle)->value;
}
static doe_mtl_status surface_open(void *userdata, void *surface, void **out,
    uint32_t *width, uint32_t *height, doe_mtl_format *format, doe_mtl_error *error)
{
    fixture *f = driver_fixture(userdata);
    assert(surface == f);
    if (f->fail_surface) return doe_mtl_fail(DOE_MTL_UNSUPPORTED,"multiplane surface ledger",error);
    ++f->surface_references;
    *out = surface;
    *width = 4;
    *height = 2;
    *format = f->invalid_surface_format ? DOE_MTL_R8_UNORM : DOE_MTL_BGRA8_UNORM;
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
    f->timeline_enabled = fixture_count == 3;
    if (f->timeline_enabled) native.timeline_value = event_value;
    native.release = release;
    native.shared_surface = surface_open;
    native.shared_surface_release = surface_release;
    native.shared_texture = texture_import;
    native.shared_event_retain = event_retain;
    native.commands_wait_event = event_wait;
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
    fixture *f = NULL;
    for (size_t i = 0; i < fixture_count; ++i)
        if (fixtures[i].context == commands->context) f = &fixtures[i];
    assert(f);
    mock_command_result(commands->native,f->hold ? 2 : 4,0);
    return __real_doe_mtl_commands_wait(commands,error);
}
doe_mtl_status __real_doe_mtl_commands_poll(doe_mtl_commands *, doe_mtl_error *);
doe_mtl_status __wrap_doe_mtl_commands_poll(doe_mtl_commands *commands, doe_mtl_error *error)
{
    fixture *f = NULL;
    for (size_t i = 0; i < fixture_count; ++i)
        if (fixtures[i].context == commands->context) f = &fixtures[i];
    assert(f);
    mock_command_result(commands->native,f->hold ? 2 : 4,0);
    return __real_doe_mtl_commands_poll(commands,error);
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
    WGPUTextureFormat srgb_view = WGPUTextureFormat_BGRA8UnormSrgb;
    WGPUTextureDescriptor desc = {.dimension=WGPUTextureDimension_2D,.format=format,
        .size={4,2,1},.mipLevelCount=1,.sampleCount=1,.usage=WGPUTextureUsage_CopySrc |
        WGPUTextureUsage_CopyDst | WGPUTextureUsage_TextureBinding | WGPUTextureUsage_RenderAttachment,
        .viewFormatCount=1,.viewFormats=&srgb_view};
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
static void producer_concurrency(state *s, fixture *f, WGPUSharedTextureMemory memory, WGPUTexture texture)
{
    WGPUSharedFence fence = import_fence(s,f);
    assert(fence);
    shared_event *native = export_event(fence);
    uint64_t value = 73;
    WGPUTexture waiting_texture = make_texture(memory,WGPUTextureFormat_BGRA8Unorm);
    assert(waiting_texture);
    WGPUSharedTextureMemoryBeginAccessDescriptor descriptor = {
        .fenceCount=1,.fences=&fence,.signaledValueCount=1,.signaledValues=&value};
    native_device *device = (native_device *)s->device;
    native_lock(device);
    f->hold = true;
    native_unlock(device);
    assert(wgpuSharedTextureMemoryBeginAccess(memory,waiting_texture,&descriptor) == WGPUStatus_Success);
    assert(f->waited_value == 73 && !native->value);
    assert(wgpuSharedTextureMemoryBeginAccess(memory,texture,NULL) == WGPUStatus_Error);
    assert(wgpuSharedTextureMemoryEndAccess(memory,texture,NULL) == WGPUStatus_Error);
    wgpuSharedFenceRelease(fence);
    assert(native->references == 1); /* Accepted wait retains the last native event lease. */
    assert(wgpuSharedTextureMemoryEndAccess(memory,waiting_texture,NULL) == WGPUStatus_Success);
    wgpuTextureRelease(waiting_texture);
    native_lock(device);
    assert(device->pending);
    f->hold = false;
    native_queue_settlement settled = native_poll_pending(device);
    assert(settled.complete && !native->references);
    native_unlock(device);
}

static void unsupported_buffer_contract(void)
{
    WGPUSharedBufferMemoryProperties properties = {.size=99,.usage=99};
    WGPUSharedBufferMemoryEndAccessState end = {.initialized=WGPU_TRUE,.fenceCount=99,
        .signaledValueCount=123};
    assert(!wgpuDeviceImportSharedBufferMemory(NULL,NULL));
    assert(!wgpuSharedBufferMemoryCreateBuffer(NULL,NULL));
    assert(wgpuSharedBufferMemoryBeginAccess(NULL,NULL,NULL) == WGPUStatus_Error);
    assert(wgpuSharedBufferMemoryGetProperties(NULL,&properties) == WGPUStatus_Error && !properties.size && !properties.usage);
    assert(wgpuSharedBufferMemoryEndAccess(NULL,NULL,&end) == WGPUStatus_Error && !end.initialized && !end.fenceCount && !end.signaledValueCount);
    assert(wgpuSharedBufferMemoryIsDeviceLost(NULL));
    wgpuSharedBufferMemoryEndAccessStateFreeMembers(end);
}

static void fence_span_contract(state *s, fixture *f,
    WGPUSharedTextureMemory memory, WGPUTexture texture)
{
    WGPUSharedFence fence = import_fence(s, f);
    assert(fence);
    shared_event *event = export_event(fence);
    unsigned references = event->references;
    uint64_t value = 37;
    WGPUSharedTextureMemoryBeginAccessDescriptor descriptor = {
        .fenceCount = 1, .fences = &fence, .signaledValues = &value};
    assert(wgpuSharedTextureMemoryBeginAccess(memory, texture, &descriptor) == WGPUStatus_Error);
    descriptor.fenceCount = 0;
    descriptor.signaledValueCount = 1;
    assert(wgpuSharedTextureMemoryBeginAccess(memory, texture, &descriptor) == WGPUStatus_Error);
    descriptor.fenceCount = 1;
    descriptor.signaledValueCount = 2;
    assert(wgpuSharedTextureMemoryBeginAccess(memory, texture, &descriptor) == WGPUStatus_Error);
    descriptor.signaledValueCount = 1;
    descriptor.signaledValues = NULL;
    assert(wgpuSharedTextureMemoryBeginAccess(memory, texture, &descriptor) == WGPUStatus_Error);
    descriptor.signaledValues = &value;
    descriptor.fences = NULL;
    assert(wgpuSharedTextureMemoryBeginAccess(memory, texture, &descriptor) == WGPUStatus_Error);
    descriptor.fences = &fence;
    descriptor.fenceCount = SIZE_MAX;
    descriptor.signaledValueCount = SIZE_MAX;
    assert(wgpuSharedTextureMemoryBeginAccess(memory, texture, &descriptor) == WGPUStatus_Error);
    assert(event->references == references);
    /* Rejected spans must not reserve access or consume the producer's fence. */
    assert(wgpuSharedTextureMemoryBeginAccess(memory, texture, NULL) == WGPUStatus_Success);
    assert(wgpuSharedTextureMemoryEndAccess(memory, texture, NULL) == WGPUStatus_Success);
    wgpuSharedFenceRelease(fence);

    /* The two output arrays have independent ownership/counts. LSan catches a
     * values allocation skipped merely because no fences were returned. */
    uint64_t *owned_values = calloc(1, sizeof(*owned_values));
    assert(owned_values);
    WGPUSharedTextureMemoryEndAccessState only_values = {
        .signaledValueCount = 1, .signaledValues = owned_values};
    wgpuSharedTextureMemoryEndAccessStateFreeMembers(only_values);
}
/* An access with no texture work must forward its accepted producer wait,
 * including across an intervening access that also records no work. */
static void zero_work_timeline_forwarding(state *parent)
{
    state s = {.instance=parent->instance,.adapter=parent->adapter};
    wgpuInstanceAddRef(s.instance);
    wgpuAdapterAddRef(s.adapter);
    WGPUDeviceDescriptor descriptor = WGPU_DEVICE_DESCRIPTOR_INIT;
    descriptor.uncapturedErrorCallbackInfo = (WGPUUncapturedErrorCallbackInfo){.callback=error_capture,.userdata1=&s};
    s.device = wgpuAdapterCreateDevice(s.adapter, &descriptor);
    assert(s.device && fixture_count == 3);
    s.queue = wgpuDeviceGetQueue(s.device);
    fixture *f = &fixtures[2];
    doe_mtl_caps caps;
    assert(doe_mtl_context_caps(f->context, &caps) == DOE_MTL_OK);
    assert(caps.timeline_enabled);
    WGPUSharedTextureMemory memory = import_memory(&s, f);
    WGPUTexture texture = make_texture(memory, WGPUTextureFormat_BGRA8Unorm);
    WGPUSharedFence producer = import_fence(&s, f);
    shared_event *event = export_event(producer);
    uint64_t value = 73;
    WGPUSharedTextureMemoryBeginAccessDescriptor begin = {.initialized=WGPU_TRUE,
        .fenceCount=1,.fences=&producer,.signaledValueCount=1,.signaledValues=&value};
    native_device *device = (native_device *)s.device;
    native_lock(device);
    f->hold = true;
    native_unlock(device);
    assert(wgpuSharedTextureMemoryBeginAccess(memory,texture,&begin) == WGPUStatus_Success);
    assert(!event->value && f->producer_waits == 1);
    assert(wgpuSharedTextureMemoryEndAccess(memory,texture,NULL) == WGPUStatus_Success);
    assert(wgpuSharedTextureMemoryBeginAccess(memory,texture,NULL) == WGPUStatus_Success);
    WGPUSharedTextureMemoryEndAccessState end = {0};
    assert(wgpuSharedTextureMemoryEndAccess(memory,texture,&end) == WGPUStatus_Success);
    assert(end.fenceCount == 1 && end.signaledValues[0] > 0);
    shared_event *timeline = export_event(end.fences[0]);
    assert(!timeline->value && timeline == &f->events[0]);
    wgpuSharedFenceRelease(producer);
    assert(event->references == 1); /* Pending ordinary command owns the native event. */
    native_lock(device);
    assert(device->pending);
    timeline->value = end.signaledValues[0];
    f->hold = false;
    assert(native_poll_pending(device).complete && !event->references);
    native_unlock(device);
    wgpuSharedTextureMemoryEndAccessStateFreeMembers(end);
    wgpuTextureRelease(texture);
    wgpuSharedTextureMemoryRelease(memory);
    consumer_finish(&s);
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
    f->invalid_surface_format = true;
    unsigned metadata_errors = s.errors;
    assert(!import_memory(&s,f) && s.errors == metadata_errors + 1);
    assert(!f->surface_references); /* Malformed native success rolls back its lease. */
    f->invalid_surface_format = false;
    WGPUSharedTextureMemory memory = import_memory(&s,f);
    WGPUSharedTextureMemory other = import_memory(&s,f);
    assert(memory && other && f->surface_references == 2);
    WGPUSharedTextureMemoryProperties properties = {0};
    assert(wgpuSharedTextureMemoryGetProperties(memory,&properties) == WGPUStatus_Success);
    assert(properties.format == WGPUTextureFormat_BGRA8Unorm && properties.size.width == 4 && properties.size.height == 2);
    WGPUTexture texture = make_texture(memory,WGPUTextureFormat_BGRA8Unorm);
    assert(texture && f->mock.texture_desc.format == DOE_MTL_BGRA8_UNORM && !f->mock.uploads && !f->mock.renders);
    assert(wgpuSharedTextureMemoryGetProperties(memory,&properties) == WGPUStatus_Success &&
        properties.format == WGPUTextureFormat_BGRA8Unorm);
    WGPUTextureViewDescriptor srgb_descriptor = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    srgb_descriptor.format = WGPUTextureFormat_BGRA8UnormSrgb;
    WGPUTextureView srgb_view = wgpuTextureCreateView(texture,&srgb_descriptor);
    assert(srgb_view && f->mock.view_desc.format == DOE_MTL_BGRA8_SRGB);
    wgpuTextureViewRelease(srgb_view);
    unsigned errors = s.errors;
    assert(!make_texture(memory,WGPUTextureFormat_RGBA8Unorm) && s.errors == errors + 1);
    errors = s.errors;
    assert(!make_texture(memory,WGPUTextureFormat_BGRA8UnormSrgb) && s.errors == errors + 1);
    assert(wgpuSharedTextureMemoryGetProperties(memory,&properties) == WGPUStatus_Success &&
        properties.format == WGPUTextureFormat_BGRA8Unorm);
    assert(wgpuSharedTextureMemoryBeginAccess(other,texture,NULL) == WGPUStatus_Error);
    WGPUTexture second = make_texture(memory,WGPUTextureFormat_BGRA8Unorm);
    fence_span_contract(&s, f, memory, texture);
    assert(second && wgpuSharedTextureMemoryBeginAccess(memory,texture,NULL) == WGPUStatus_Success);
    assert(wgpuSharedTextureMemoryEndAccess(memory,second,NULL) == WGPUStatus_Error);
    WGPUSharedTextureMemoryEndAccessState end = {0};
    assert(wgpuSharedTextureMemoryEndAccess(memory,texture,&end) == WGPUStatus_Success);
    assert(end.initialized && end.fenceCount == 1 && end.signaledValueCount == 1 && end.signaledValues[0] == 1);
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
    assert(wgpuSharedTextureMemoryEndAccess(memory,texture,&end) == WGPUStatus_Success && end.fenceCount == 1);
    wgpuSharedTextureMemoryEndAccessStateFreeMembers(end);
    native_lock(device);
    assert(device->pending && ((shared_event *)f->last_signal)->references == 1);
    f->hold = false;
    native_queue_settlement settled = native_settle_pending(device);
    assert(settled.complete && settled.failure.status == NATIVE_OK && !device->pending);
    assert(!((shared_event *)f->last_signal)->references);
    native_unlock(device);
    WGPUTexture active = make_texture(memory,WGPUTextureFormat_BGRA8Unorm);
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
    zero_work_timeline_forwarding(&s);
    consumer_finish(&s);
    for (unsigned i = 0; i < fixture_count; ++i) {
        for (unsigned j = 0; j < fixtures[i].event_count; ++j) assert(!fixtures[i].events[j].references);
        mock_assert_closed(&fixtures[i].mock);
    }
    puts("PASS shared IOSurface/event host controls: native identity, channel order, exact texture provenance, queued producer dependencies, transitive zero-work timeline, fresh completion signals, first-error rollback and unknown-work leases; no SDK/GPU run");
    return 0;
}
