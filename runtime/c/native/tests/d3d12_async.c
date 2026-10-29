/* SPDX-License-Identifier: MIT
 * Actual private queue gate plus public operations; GPU output is independent. */
#include "../src/surface.h"
#include "../src/d3d12_frontend.h"
#include "../../backends/d3d12/src/internal.h"
#include "../../backends/d3d12/src/windows_internal.h"
#include "consumer.h"

typedef struct gate_case {
    state *state;
    ID3D12Fence *gate;
    WGPUBuffer a, b, ephemeral, readback;
    WGPUCommandBuffer independent, dependent, before_rewrite;
    HANDLE accepted, map_started, map_done, dependent_started, dependent_done, stop;
    atomic_bool watchdog_fired;
    WGPUMapAsyncStatus map_status;
    bool map_future;
} gate_case;

static void watchdog(void *identity)
{
    gate_case *test = identity;
    if (WaitForSingleObject(test->stop, 3000) == WAIT_TIMEOUT) {
        atomic_store(&test->watchdog_fired, true);
        assert(SUCCEEDED(ID3D12Fence_Signal(test->gate, 1)));
    }
}

static void writes_and_submit(void *identity)
{
    gate_case *test = identity;
    uint32_t initial = 10, a = 41, b = 1;
    wgpuQueueWriteBuffer(test->state->queue, test->a, 0, &initial, sizeof(initial));
    wgpuQueueWriteBuffer(test->state->queue, test->b, 0, &b, sizeof(b));
    wgpuQueueWriteBuffer(test->state->queue, test->ephemeral, 0, &a, sizeof(a));
    wgpuBufferRelease(test->ephemeral);
    wgpuQueueSubmit(test->state->queue, 1, &test->before_rewrite);
    wgpuCommandBufferRelease(test->before_rewrite);
    wgpuQueueWriteBuffer(test->state->queue, test->a, 0, &a, sizeof(a));
    wgpuQueueSubmit(test->state->queue, 1, &test->independent);
    wgpuCommandBufferRelease(test->independent);
    assert(SetEvent(test->accepted));
}

static void map_reply(WGPUMapAsyncStatus status, WGPUStringView message, void *identity, void *unused)
{
    (void)message; (void)unused;
    ((gate_case *)identity)->map_status = status;
}

static void map_while_pending(void *identity)
{
    gate_case *test = identity;
    WGPUBufferMapCallbackInfo callback = {.mode=WGPUCallbackMode_AllowSpontaneous,
        .callback=map_reply,.userdata1=test};
    assert(SetEvent(test->map_started));
    test->map_future = wgpuBufferMapAsync(test->readback, WGPUMapMode_Read, 0, 4, callback).id != 0;
    assert(SetEvent(test->map_done));
}

static void dependent_submit(void *identity)
{
    gate_case *test = identity;
    assert(SetEvent(test->dependent_started));
    wgpuQueueSubmit(test->state->queue, 1, &test->dependent);
    wgpuCommandBufferRelease(test->dependent);
    assert(SetEvent(test->dependent_done));
}

static WGPUCommandBuffer dispatch_copy(state *s, WGPUComputePipeline program,
    WGPUBindGroup bindings, WGPUBuffer output, WGPUBuffer readback)
{
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device, NULL);
    WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, NULL);
    wgpuComputePassEncoderSetPipeline(pass, program);
    wgpuComputePassEncoderSetBindGroup(pass, 0, bindings, 0, NULL);
    wgpuComputePassEncoderDispatchWorkgroups(pass, 1, 1, 1);
    wgpuComputePassEncoderEnd(pass);
    wgpuComputePassEncoderRelease(pass);
    wgpuCommandEncoderCopyBufferToBuffer(encoder, output, 0, readback, 0, 4);
    WGPUCommandBuffer result = wgpuCommandEncoderFinish(encoder, NULL);
    assert(result);
    wgpuCommandEncoderRelease(encoder);
    return result;
}

static void expect_value(state *s, WGPUBuffer readback, uint32_t expected)
{
    WGPUBufferMapCallbackInfo callback = {.mode=WGPUCallbackMode_AllowSpontaneous,
        .callback=mapped,.userdata1=s};
    unsigned before = s->mapped;
    assert(wgpuBufferMapAsync(readback, WGPUMapMode_Read, 0, 4, callback).id);
    assert(s->mapped == before + 1);
    const uint32_t *value = wgpuBufferGetConstMappedRange(readback, 0, 4);
    assert(value && *value == expected);
    wgpuBufferUnmap(readback);
}

static void ordered_work(void)
{
    state s = {0};
    consumer_init(&s);
    native_device *device = (native_device *)s.device;
    doe_dx_context *context = device->backend.value.d3d12.context;
    WGPUBuffer a = buffer(&s, 4, WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst);
    WGPUBuffer b = buffer(&s, 4, WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst);
    WGPUBuffer ephemeral = buffer(&s, 4, WGPUBufferUsage_CopyDst);
    WGPUBuffer output = buffer(&s, 4, WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc);
    WGPUBuffer readback = buffer(&s, 4, WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst);
    WGPUBuffer final = buffer(&s, 4, WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst);
    WGPUBuffer intermediate = buffer(&s, 4, WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst);
    WGPUComputePipeline first = pipeline(&s,
        "@group(0) @binding(0) var<storage,read_write> o:array<u32>;"
        "@compute @workgroup_size(1) fn main(){o[0]=7u;}");
    WGPUBindGroupEntry first_entry = {.binding=0,.buffer=output,.size=4};
    WGPUBindGroup first_group = group(&s, first, &first_entry, 1);
    WGPUComputePipeline second = pipeline(&s,
        "@group(0) @binding(0) var<storage,read> a:array<u32>;"
        "@group(0) @binding(1) var<storage,read> b:array<u32>;"
        "@group(0) @binding(2) var<storage,read_write> o:array<u32>;"
        "@compute @workgroup_size(1) fn main(){o[0]=a[0]+b[0];}");
    WGPUBindGroupEntry entries[3] = {{.binding=0,.buffer=a,.size=4},
        {.binding=1,.buffer=b,.size=4},{.binding=2,.buffer=output,.size=4}};
    WGPUBindGroup second_group = group(&s, second, entries, 3);
    gate_case test = {.state=&s,.a=a,.b=b,.ephemeral=ephemeral,.readback=readback,
        .independent=dispatch_copy(&s,first,first_group,output,readback),
        .dependent=dispatch_copy(&s,second,second_group,output,final),
        .before_rewrite=dispatch_copy(&s,second,second_group,output,intermediate),
        .map_status=WGPUMapAsyncStatus_Force32};
    HANDLE *events[] = {&test.accepted,&test.map_started,&test.map_done,
        &test.dependent_started,&test.dependent_done,&test.stop};
    for (size_t i = 0; i < sizeof(events) / sizeof(*events); ++i) {
        *events[i] = CreateEventW(NULL, TRUE, FALSE, NULL);
        assert(*events[i]);
    }
    atomic_init(&test.watchdog_fired, false);
    assert(SUCCEEDED(ID3D12Device_CreateFence(context->native->device, 0,
        D3D12_FENCE_FLAG_NONE, &IID_ID3D12Fence, (void **)&test.gate)));
    assert(SUCCEEDED(ID3D12CommandQueue_Wait(context->native->queue, test.gate, 1)));
    doe_thread guard = {0}, writer = {0}, mapper = {0}, dependent = {0};
    assert(doe_thread_create(&guard, watchdog, &test).status == DOE_HOST_OK);
    assert(doe_thread_create(&writer, writes_and_submit, &test).status == DOE_HOST_OK);
    bool accepted_before_gate = WaitForSingleObject(test.accepted, 2000) == WAIT_OBJECT_0;
    if (!accepted_before_gate) assert(SUCCEEDED(ID3D12Fence_Signal(test.gate, 1)));
    assert(doe_thread_join(&writer).status == DOE_HOST_OK);
    assert(doe_thread_create(&mapper, map_while_pending, &test).status == DOE_HOST_OK);
    assert(WaitForSingleObject(test.map_started, 1000) == WAIT_OBJECT_0);
    bool pending_map = false;
    for (unsigned attempt = 0; attempt < 100 && !pending_map; ++attempt) {
        pending_map = wgpuBufferGetMapState(readback) == WGPUBufferMapState_Pending;
        if (!pending_map) Sleep(1);
    }
    wgpuBufferUnmap(readback);
    native_lock(device);
    native_buffer *payload = doe_resource_buffer_native((doe_resource_buffer *)readback);
    doe_dx_commands *owner = doe_dx_buffer_owner(payload->handle.d3d12->allocation);
    doe_dx_wait_lease lease = {0};
    bool held = owner && doe_dx_commands_hold(owner, &lease, NULL) == DOE_DX_OK;
    native_unlock(device);
    doe_dx_status observed = held ? doe_dx_commands_block(&lease, 1, NULL) : DOE_DX_INVALID_STATE;
    native_lock(device);
    if (held) assert(doe_dx_commands_unhold(&lease, NULL) == DOE_DX_OK);
    native_unlock(device);
    assert(doe_thread_create(&dependent, dependent_submit, &test).status == DOE_HOST_OK);
    assert(WaitForSingleObject(test.dependent_started, 1000) == WAIT_OBJECT_0);
    for (unsigned attempt = 0; attempt < 10; ++attempt) {
        WGPULimits limits = WGPU_LIMITS_INIT;
        assert(wgpuDeviceGetLimits(s.device, &limits) == WGPUStatus_Success);
        Sleep(1);
    }
    bool dependent_accepted = WaitForSingleObject(test.dependent_done, 1000) == WAIT_OBJECT_0;
    assert(SUCCEEDED(ID3D12Fence_Signal(test.gate, 1)));
    assert(SetEvent(test.stop));
    assert(doe_thread_join(&dependent).status == DOE_HOST_OK);
    assert(doe_thread_join(&mapper).status == DOE_HOST_OK);
    assert(doe_thread_join(&guard).status == DOE_HOST_OK);
    /* All gates are open and work is terminal before reporting an assertion. */
    expect_value(&s, intermediate, 11);
    expect_value(&s, readback, 7);
    expect_value(&s, final, 42);
    bool valid = accepted_before_gate && pending_map && held && observed == DOE_DX_PENDING &&
        dependent_accepted && !atomic_load(&test.watchdog_fired) && test.map_future &&
        test.map_status == WGPUMapAsyncStatus_Aborted;
    wgpuBufferRelease(a); wgpuBufferRelease(b); wgpuBufferRelease(output);
    wgpuBufferRelease(readback); wgpuBufferRelease(final); wgpuBufferRelease(intermediate);
    wgpuBindGroupRelease(first_group); wgpuBindGroupRelease(second_group);
    wgpuComputePipelineRelease(first); wgpuComputePipelineRelease(second);
    ID3D12Fence_Release(test.gate);
    for (size_t i = 0; i < sizeof(events) / sizeof(*events); ++i) assert(CloseHandle(*events[i]));
    consumer_finish(&s);
    printf("gated acceptance: writes=%u pendingMap=%u lease=%u waitStatus=%u dependent=%u watchdog=%u mapFuture=%u mapStatus=%u\n",
        (unsigned)accepted_before_gate,(unsigned)pending_map,(unsigned)held,(unsigned)observed,
        (unsigned)dependent_accepted,(unsigned)atomic_load(&test.watchdog_fired),
        (unsigned)test.map_future,(unsigned)test.map_status);
    fflush(stdout);
    assert(valid);
}

static void terminal_loss(void)
{
    state s = {0};
    consumer_init(&s);
    native_device *device = (native_device *)s.device;
    doe_dx_context *context = device->backend.value.d3d12.context;
    WGPUBuffer source = buffer(&s, 4, WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst);
    WGPUBuffer readback = buffer(&s, 4, WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst);
    ID3D12Fence *gate = NULL;
    assert(SUCCEEDED(ID3D12Device_CreateFence(context->native->device, 0,
        D3D12_FENCE_FLAG_NONE, &IID_ID3D12Fence, (void **)&gate)));
    assert(SUCCEEDED(ID3D12CommandQueue_Wait(context->native->queue, gate, 1)));
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s.device, NULL);
    wgpuCommandEncoderCopyBufferToBuffer(encoder, source, 0, readback, 0, 4);
    WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, NULL);
    assert(commands);
    wgpuCommandEncoderRelease(encoder);
    wgpuQueueSubmit(s.queue, 1, &commands);
    wgpuCommandBufferRelease(commands);
    native_lock(device);
    native_buffer *payload = doe_resource_buffer_native((doe_resource_buffer *)readback);
    doe_dx_commands *owner = doe_dx_buffer_owner(payload->handle.d3d12->allocation);
    doe_dx_wait_lease lease = {0};
    assert(owner && doe_dx_commands_hold(owner, &lease, NULL) == DOE_DX_OK);
    native_unlock(device);
    assert(doe_dx_commands_block(&lease, 1, NULL) == DOE_DX_PENDING);
    ID3D12Device5 *extended = NULL;
    assert(SUCCEEDED(ID3D12Device_QueryInterface(context->native->device,
        &IID_ID3D12Device5, (void **)&extended)));
    /* Remove only this test-owned device. Monitored fences become terminal;
     * neither an event nor UINT64_MAX may become successful readback. */
    ID3D12Device5_RemoveDevice(extended);
    assert(doe_dx_commands_block(&lease, 1000, NULL) == DOE_DX_DEVICE_LOST);
    native_lock(device);
    assert(doe_dx_commands_unhold(&lease, NULL) == DOE_DX_OK);
    assert(doe_dx_context_poll(context, 0, NULL) == DOE_DX_DEVICE_LOST);
    native_unlock(device);
    gate_case observed = {.map_status=WGPUMapAsyncStatus_Force32};
    WGPUBufferMapCallbackInfo callback = {.mode=WGPUCallbackMode_AllowSpontaneous,
        .callback=map_reply,.userdata1=&observed};
    assert(wgpuBufferMapAsync(readback, WGPUMapMode_Read, 0, 4, callback).id);
    assert(observed.map_status == WGPUMapAsyncStatus_Aborted);
    native_lock(device);
    native_drain_pending(device);
    assert(!device->pending);
    native_unlock(device);
    ID3D12Device5_Release(extended);
    ID3D12Fence_Release(gate);
    wgpuBufferRelease(source);
    wgpuBufferRelease(readback);
    consumer_finish(&s);
}

int main(void)
{
    ordered_work();
    terminal_loss();
    printf("PASS actual gated D3D12 queue: distinct/reused writes and dependent compute accepted before GPU release, unlocked concurrent map, timeout/cancel/early release, exact11/7/42 readback, own-device terminal loss and teardown %s\n", adapter_class_scope());
    return 0;
}
