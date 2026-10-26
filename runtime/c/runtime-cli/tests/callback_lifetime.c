/* SPDX-License-Identifier: MIT
 * Public-provider callback controls exercise CLI ownership without GPU work. */
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
static atomic_uint live_allocations;
static int allocation_failure = -1;
static void *tracked_calloc(size_t count, size_t size)
{
    if (allocation_failure == 0) return NULL;
    if (allocation_failure > 0) --allocation_failure;
    void *result = calloc(count, size);
    if (result) ++live_allocations;
    return result;
}
static void tracked_free(void *value)
{
    if (value) { assert(atomic_fetch_sub(&live_allocations, 1) > 0); free(value); }
}
#define calloc tracked_calloc
#define free tracked_free
#define DOE_RUNTIME_DAWN 1
#include "../src/execute.c"
#undef calloc
#undef free

static WGPURequestAdapterCallbackInfo adapter_pending;
static WGPURequestDeviceCallbackInfo device_pending;
static WGPUUncapturedErrorCallbackInfo error_callback;
static WGPUPopErrorScopeCallbackInfo scope_pending[3];
static unsigned adapter_requests, device_requests, scope_requests, instance_refs;
static atomic_uint adapter_releases, device_releases;
static bool wait_timeout, immediate_adapter, immediate_device, immediate_scope;
static bool inline_adapter, inline_device, inline_scope, complete_then_error;
static bool zero_adapter, zero_device, zero_scope, loss_unresolved, scope_error;
static bool device_wait_error;
static uint64_t now;
static int adapter_token, device_token, instance_token;
#define ADAPTER ((WGPUAdapter)&adapter_token)
#define DEVICE ((WGPUDevice)&device_token)

uint64_t doe_host_monotonic_ns(void) { now += WAIT_TIMEOUT_NS; return now; }
void doe_host_pause_ns(uint64_t ns) { (void)ns; }
static void deliver_adapter(void)
{
    WGPURequestAdapterCallbackInfo info = adapter_pending; adapter_pending.callback = NULL;
    if (info.callback) info.callback(WGPURequestAdapterStatus_Success, ADAPTER,
        (WGPUStringView){"adapter reply",13}, info.userdata1, info.userdata2);
}
static void deliver_device(void)
{
    WGPURequestDeviceCallbackInfo info = device_pending; device_pending.callback = NULL;
    if (info.callback) info.callback(WGPURequestDeviceStatus_Success, DEVICE,
        (WGPUStringView){"device reply",12}, info.userdata1, info.userdata2);
}
static void deliver_scope(size_t i)
{
    WGPUPopErrorScopeCallbackInfo info = scope_pending[i]; scope_pending[i].callback = NULL;
    if (info.callback) info.callback(WGPUPopErrorScopeStatus_Success,
        scope_error ? WGPUErrorType_Validation : WGPUErrorType_NoError,
        scope_error ? (WGPUStringView){"scope error",11} : (WGPUStringView){0}, info.userdata1, info.userdata2);
}
WGPUFuture wgpuInstanceRequestAdapter(WGPUInstance instance, const WGPURequestAdapterOptions *options,
    WGPURequestAdapterCallbackInfo info)
{
    (void)instance; (void)options; ++adapter_requests;
    if (zero_adapter) {
        if (inline_adapter) info.callback(WGPURequestAdapterStatus_Success, ADAPTER,
            (WGPUStringView){0}, info.userdata1, info.userdata2);
        return (WGPUFuture){0};
    }
    adapter_pending = info;
    if (inline_adapter) deliver_adapter();
    return (WGPUFuture){1};
}
WGPUFuture wgpuAdapterRequestDevice(WGPUAdapter adapter, const WGPUDeviceDescriptor *descriptor,
    WGPURequestDeviceCallbackInfo info)
{
    assert(adapter == ADAPTER); ++device_requests;
    if (zero_device) {
        if (inline_device) {
            error_callback = descriptor->uncapturedErrorCallbackInfo;
            info.callback(WGPURequestDeviceStatus_Success, DEVICE, (WGPUStringView){0}, info.userdata1, info.userdata2);
        }
        return (WGPUFuture){0};
    }
    error_callback = descriptor->uncapturedErrorCallbackInfo; device_pending = info;
    if (inline_device) deliver_device();
    return (WGPUFuture){2};
}
WGPUFuture wgpuDevicePopErrorScope(WGPUDevice device, WGPUPopErrorScopeCallbackInfo info)
{
    (void)device; assert(scope_requests < 3); size_t i = scope_requests++;
    if (zero_scope) {
        if (inline_scope) info.callback(WGPUPopErrorScopeStatus_Success, WGPUErrorType_NoError,
            (WGPUStringView){0}, info.userdata1, info.userdata2);
        return (WGPUFuture){0};
    }
    scope_pending[i] = info;
    if (inline_scope) deliver_scope(i);
    return (WGPUFuture){3};
}
void wgpuDevicePushErrorScope(WGPUDevice device, WGPUErrorFilter filter) { (void)device; (void)filter; }
void wgpuDeviceDestroy(WGPUDevice device)
{
    assert(device == DEVICE);
    if (error_callback.callback) error_callback.callback(&device, WGPUErrorType_Internal,
        (WGPUStringView){"during destruction",18}, error_callback.userdata1, error_callback.userdata2);
}
WGPUFuture wgpuDeviceGetLostFuture(WGPUDevice device) { assert(device == DEVICE); return (WGPUFuture){4}; }
WGPUWaitStatus wgpuInstanceWaitAny(WGPUInstance instance, size_t count, WGPUFutureWaitInfo *infos, uint64_t ns)
{
    (void)instance; (void)ns; assert(instance_refs > 0 && count == 1);
    uint64_t id = infos->future.id;
    if (id == 4) {
        if (loss_unresolved) return WGPUWaitStatus_Error;
        error_callback = (WGPUUncapturedErrorCallbackInfo){0};
        infos->completed = WGPU_TRUE; return WGPUWaitStatus_Success;
    }
    bool complete = id == 1 ? immediate_adapter || inline_adapter :
        id == 2 ? immediate_device || inline_device : immediate_scope || inline_scope;
    if (complete) {
        if (id == 1) deliver_adapter(); else if (id == 2) deliver_device(); else deliver_scope(scope_requests - 1);
        infos->completed = WGPU_TRUE;
        return complete_then_error || (id == 2 && device_wait_error) ? WGPUWaitStatus_Error : WGPUWaitStatus_Success;
    }
    return wait_timeout ? WGPUWaitStatus_TimedOut : WGPUWaitStatus_Error;
}
void wgpuAdapterRelease(WGPUAdapter adapter) { assert(adapter == ADAPTER); ++adapter_releases; }
void wgpuDeviceRelease(WGPUDevice device) { assert(device == DEVICE); ++device_releases; }
WGPUInstance wgpuCreateInstance(const WGPUInstanceDescriptor *desc)
{ (void)desc; ++instance_refs; return (WGPUInstance)&instance_token; }
void wgpuInstanceAddRef(WGPUInstance instance) { assert(instance == (WGPUInstance)&instance_token); ++instance_refs; }
void wgpuInstanceRelease(WGPUInstance instance)
{
    assert(instance == (WGPUInstance)&instance_token && instance_refs > 0);
    if (--instance_refs) return;
    deliver_adapter();
    for (size_t i = 0; i < scope_requests; i++) deliver_scope(i);
}
WGPUStatus wgpuAdapterGetInfo(WGPUAdapter adapter, WGPUAdapterInfo *info) { (void)adapter; (void)info; return WGPUStatus_Error; }
void wgpuAdapterInfoFreeMembers(WGPUAdapterInfo info) { (void)info; }
WGPUBool wgpuAdapterHasFeature(WGPUAdapter adapter, WGPUFeatureName feature) { (void)adapter; (void)feature; return WGPU_FALSE; }
WGPUStatus wgpuAdapterGetLimits(WGPUAdapter adapter, WGPULimits *limits) { (void)adapter; (void)limits; return WGPUStatus_Error; }
WGPUQueue wgpuDeviceGetQueue(WGPUDevice device) { assert(device == DEVICE); return NULL; }
void wgpuQueueRelease(WGPUQueue queue) { assert(!queue); }
void wgpuBufferRelease(WGPUBuffer buffer) { assert(!buffer); }
void wgpuComputePipelineRelease(WGPUComputePipeline pipeline) { assert(!pipeline); }
void wgpuBindGroupLayoutRelease(WGPUBindGroupLayout layout) { assert(!layout); }
void runtime_release_render(doe_runtime *runtime) { (void)runtime; }
void runtime_release_surfaces(doe_runtime *runtime) { (void)runtime; }
void runtime_release_diagnostics(doe_runtime *runtime) { (void)runtime; }
void runtime_release_textures(doe_runtime *runtime) { (void)runtime; }
void runtime_kernel_sources_clear(doe_runtime *runtime) { (void)runtime; }
void runtime_pipeline_cache_open(doe_runtime *runtime) { (void)runtime; }
bool doe_runtime_load_device_policy(const char *name, DoeNativeDevicePolicy *policy, char *message,
    size_t capacity, char hash[65], WGPUBackendType *backend)
{ (void)name; (void)policy; (void)message; (void)capacity; (void)hash; (void)backend; return false; }

static void reset(void)
{
    assert(!live_allocations && !instance_refs);
    adapter_pending = (WGPURequestAdapterCallbackInfo){0}; device_pending = (WGPURequestDeviceCallbackInfo){0};
    error_callback = (WGPUUncapturedErrorCallbackInfo){0}; memset(scope_pending, 0, sizeof(scope_pending));
    adapter_requests = device_requests = scope_requests = adapter_releases = device_releases = 0;
    now = 0; allocation_failure = -1;
    wait_timeout = immediate_adapter = immediate_device = immediate_scope = complete_then_error = false;
    inline_adapter = inline_device = inline_scope = zero_adapter = zero_device = zero_scope = false;
    loss_unresolved = scope_error = device_wait_error = false;
}
static doe_runtime *fixture(void)
{
    doe_runtime *runtime = tracked_calloc(1, sizeof(*runtime)); assert(runtime);
    atomic_init(&runtime->host_references, 1); atomic_flag_clear(&runtime->error_guard);
    runtime->instance = wgpuCreateInstance(NULL); return runtime;
}
static void *concurrent_adapter(void *payload)
{
    adapter_reply(WGPURequestAdapterStatus_Success, ADAPTER, (WGPUStringView){0}, payload, NULL);
    return NULL;
}
static void *concurrent_error(void *payload)
{
    for (unsigned i = 0; i < 1000; i++)
        uncaptured_error(NULL, WGPUErrorType_Internal, (WGPUStringView){"first error",11}, payload, NULL);
    return NULL;
}
int main(void)
{
    char message[256]; doe_runtime_options options = {.adapter=DOE_ADAPTER_AUTO};
    for (unsigned timeout = 0; timeout < 2; timeout++) {
        reset(); wait_timeout = timeout != 0;
        assert(!doe_runtime_open(&options, message, sizeof(message)));
        assert(adapter_requests == 1 && adapter_releases == 1 && !device_requests);
        reset(); wait_timeout = timeout != 0; immediate_adapter = true;
        assert(!doe_runtime_open(&options, message, sizeof(message)));
        assert(adapter_requests == 1 && device_requests == 1 && adapter_releases == 1);
        assert(!device_releases && instance_refs == 1);
        deliver_device(); assert(device_releases == 1);
        reset(); wait_timeout = timeout != 0;
        doe_runtime *runtime = fixture(); doe_trace_execution execution = {.ok=true};
        assert(!runtime_pop_scopes(runtime, &execution) && scope_requests == 3);
        assert(!strcmp(execution.status_code, "error_scope_unresolved"));
        assert(!runtime_pop_scopes(runtime, &execution) && scope_requests == 3);
        for (size_t i = 0; i < 3; i++) deliver_scope(i);
        doe_runtime_close(runtime);
    }
    for (unsigned inlined = 0; inlined < 2; inlined++) {
        reset(); immediate_adapter = immediate_device = immediate_scope = !inlined;
        inline_adapter = inline_device = inline_scope = inlined != 0;
        doe_runtime *runtime = doe_runtime_open(&options, message, sizeof(message)); assert(runtime);
        doe_trace_execution execution = {.ok=true};
        assert(runtime_pop_scopes(runtime, &execution)); scope_requests = 0;
        scope_error = true; execution = (doe_trace_execution){.ok=false};
        execution.status_code = "original_failure";
        assert(!runtime_pop_scopes(runtime, &execution));
        assert(!strcmp(execution.status_code, "original_failure"));
        doe_runtime_close(runtime); assert(adapter_releases == 1 && device_releases == 1);
    }
    reset(); zero_adapter = true;
    assert(!doe_runtime_open(&options, message, sizeof(message)) && adapter_requests == 1);
    reset(); immediate_adapter = true; zero_device = true;
    assert(!doe_runtime_open(&options, message, sizeof(message)) && device_requests == 1);
    reset(); zero_scope = true;
    doe_runtime *runtime = fixture(); doe_trace_execution execution = {.ok=true};
    assert(!runtime_pop_scopes(runtime, &execution)); doe_runtime_close(runtime);
    reset(); immediate_adapter = true; complete_then_error = true;
    assert(!doe_runtime_open(&options, message, sizeof(message)) && adapter_releases == 1);
    reset(); immediate_adapter = immediate_device = device_wait_error = true;
    assert(!doe_runtime_open(&options, message, sizeof(message)) && device_releases == 1);
    reset(); zero_adapter = inline_adapter = true;
    assert(!doe_runtime_open(&options, message, sizeof(message)) && adapter_releases == 1);
    reset(); immediate_adapter = zero_device = inline_device = true;
    assert(!doe_runtime_open(&options, message, sizeof(message)) && device_releases == 1);
    reset(); zero_scope = inline_scope = true; runtime = fixture(); execution = (doe_trace_execution){.ok=true};
    assert(!runtime_pop_scopes(runtime, &execution)); assert(doe_runtime_close(runtime));
    reset(); runtime = fixture(); allocation_failure = 0; execution = (doe_trace_execution){.ok=true};
    assert(!runtime_pop_scopes(runtime, &execution) && !scope_requests);
    assert(!strcmp(execution.status_code, "out_of_memory")); assert(doe_runtime_close(runtime));
    reset(); immediate_adapter = immediate_device = true;
    runtime = doe_runtime_open(&options, message, sizeof(message)); assert(runtime);
    loss_unresolved = true; assert(!doe_runtime_close(runtime));
    assert(live_allocations == 1 && !device_releases && instance_refs == 1);
    assert(runtime->closing && !runtime->uncaptured);
    loss_unresolved = false; assert(doe_runtime_close(runtime));
    for (int fail = 0; fail < 3; fail++) {
        reset(); immediate_adapter = immediate_device = true; allocation_failure = fail;
        assert(!doe_runtime_open(&options, message, sizeof(message)));
    }
    reset(); runtime = fixture(); pthread_t a, b;
    assert(!pthread_create(&a, NULL, concurrent_error, runtime));
    assert(!pthread_create(&b, NULL, concurrent_error, runtime));
    assert(!pthread_join(a, NULL) && !pthread_join(b, NULL));
    assert(runtime->uncaptured && !strcmp(runtime->uncaptured_message, "first error"));
    doe_runtime_close(runtime);
    for (unsigned i = 0; i < 128; i++) {
        reset(); runtime_reply *reply = reply_create(NULL); assert(reply);
        assert(!pthread_create(&a, NULL, concurrent_adapter, reply));
        unsigned previous = atomic_fetch_or_explicit(&reply->state, REPLY_ABANDONED, memory_order_acq_rel);
        if (previous & REPLY_DONE) discard_adapter_reply(reply);
        reply_release(reply); assert(!pthread_join(a, NULL));
        assert(adapter_releases == 1);
    }
    reset(); puts("CLI callback host controls passed: inline/late/zero/OOM, RMW races, quiescence and exact cleanup");
    return 0;
}
