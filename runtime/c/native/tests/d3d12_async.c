/* SPDX-License-Identifier: MIT
 * Actual private queue gate plus public operations; GPU output is independent. */
#include "../src/surface.h"
#include "../src/d3d12_frontend.h"
#include "../../backends/d3d12/src/internal.h"
#include "../../backends/d3d12/src/windows_internal.h"
#include "consumer.h"
#include <d3d12sdklayers.h>

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
    native_lock(device);
    doe_dx_buffer *staging = context->upload_staging;
    assert(staging && staging->references == 1 && !staging->owner && !staging->pending);
    native_unlock(device);
    uint32_t later = 99;
    wgpuQueueWriteBuffer(s.queue,a,0,&later,sizeof(later));
    WGPUCommandBuffer reused = dispatch_copy(&s,second,second_group,output,final);
    wgpuQueueSubmit(s.queue,1,&reused);
    wgpuCommandBufferRelease(reused);
    expect_value(&s,final,100);
    native_lock(device);
    assert(context->upload_staging == staging && staging->references == 1 &&
        !staging->owner && !staging->pending);
    native_unlock(device);
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
    printf("gated acceptance: exact11/7/42 then idle-staging reuse100; writes=%u pendingMap=%u lease=%u waitStatus=%u dependent=%u watchdog=%u mapFuture=%u mapStatus=%u\n",
        (unsigned)accepted_before_gate,(unsigned)pending_map,(unsigned)held,(unsigned)observed,
        (unsigned)dependent_accepted,(unsigned)atomic_load(&test.watchdog_fired),
        (unsigned)test.map_future,(unsigned)test.map_status);
    fflush(stdout);
    assert(valid);
}

typedef struct initialization_case {
    gate_case gate;
    WGPUBuffer full,partial,padding;
    bool cpu_zeroes;
} initialization_case;

static void initialize_while_closed(void *identity)
{
    initialization_case *test=identity;
    state *s=test->gate.state;
    native_device *device=(native_device *)s->device;
    WGPUBuffer source=buffer(s,260,WGPUBufferUsage_CopySrc|WGPUBufferUsage_CopyDst);
    test->full=buffer(s,260,WGPUBufferUsage_MapRead|WGPUBufferUsage_CopyDst);
    test->partial=buffer(s,260,WGPUBufferUsage_MapRead|WGPUBufferUsage_CopyDst);
    const unsigned char *zero=map(s,test->partial,260);
    test->cpu_zeroes=true;
    for(size_t i=0;i<260;++i) if(zero[i]) test->cpu_zeroes=false;
    wgpuBufferUnmap(test->partial);
    const uint32_t update[2]={0x12345678u,0x9abcdef0u};
    wgpuQueueWriteBuffer(s->queue,source,60,update,sizeof(update));
    WGPUCommandEncoder encoder=wgpuDeviceCreateCommandEncoder(s->device,NULL);
    wgpuCommandEncoderCopyBufferToBuffer(encoder,source,0,test->full,0,260);
    wgpuCommandEncoderCopyBufferToBuffer(encoder,source,60,test->partial,60,sizeof(update));
    WGPUCommandBuffer commands=wgpuCommandEncoderFinish(encoder,NULL);
    assert(commands); wgpuCommandEncoderRelease(encoder);
    wgpuQueueSubmit(s->queue,1,&commands);
    wgpuCommandBufferRelease(commands);
    wgpuBufferRelease(source); /* Both accepted initialization and copies retain it. */

    WGPUBuffer uniform=buffer(s,13,WGPUBufferUsage_Uniform|WGPUBufferUsage_CopySrc);
    test->padding=buffer(s,256,WGPUBufferUsage_MapRead|WGPUBufferUsage_CopyDst);
    /* Backend-private padding oracle: the public logical source is only13
     * bytes, while its initialized physical CBV allocation is256 bytes. */
    native_lock(device);
    native_buffer *from=doe_resource_buffer_native((doe_resource_buffer *)uniform);
    native_buffer *to=doe_resource_buffer_native((doe_resource_buffer *)test->padding);
    assert(from->handle.d3d12->allocation_size == 256);
    native_command_handle native={0};
    /* Internal recordings, like every frontend one, first submit deferred fills. */
    assert(native_d3d12_materialize_zero(device).status == NATIVE_OK);
    assert(native_command_create(&device->backend,&native).status == NATIVE_OK);
    assert(doe_dx_commands_copy(native.value.d3d12.commands,from->handle.d3d12->allocation,
        0,to->handle.d3d12->allocation,0,256,NULL) == DOE_DX_OK);
    assert(native_submit_backend_commands_async(device,native).status == NATIVE_OK);
    native_unlock(device);
    wgpuBufferRelease(uniform);
    assert(SetEvent(test->gate.accepted));
}

static void initialization_ordering(bool debug)
{
    state s={0}; consumer_init(&s);
    native_device *device=(native_device *)s.device;
    doe_dx_context *context=device->backend.value.d3d12.context;
    ID3D12InfoQueue *messages=NULL;
    if (debug) assert(SUCCEEDED(ID3D12Device_QueryInterface(context->native->device,
        &IID_ID3D12InfoQueue,(void **)&messages)));
    initialization_case test={.gate={.state=&s}};
    test.gate.accepted=CreateEventW(NULL,TRUE,FALSE,NULL);
    test.gate.stop=CreateEventW(NULL,TRUE,FALSE,NULL);
    assert(test.gate.accepted && test.gate.stop);
    atomic_init(&test.gate.watchdog_fired,false);
    assert(SUCCEEDED(ID3D12Device_CreateFence(context->native->device,0,
        D3D12_FENCE_FLAG_NONE,&IID_ID3D12Fence,(void **)&test.gate.gate)));
    assert(SUCCEEDED(ID3D12CommandQueue_Wait(context->native->queue,test.gate.gate,1)));
    doe_thread guard={0}, creator={0};
    assert(doe_thread_create(&guard,watchdog,&test.gate).status == DOE_HOST_OK);
    assert(doe_thread_create(&creator,initialize_while_closed,&test).status == DOE_HOST_OK);
    const bool accepted=WaitForSingleObject(test.gate.accepted,2000) == WAIT_OBJECT_0;
    assert(SUCCEEDED(ID3D12Fence_Signal(test.gate.gate,1)));
    assert(SetEvent(test.gate.stop));
    assert(doe_thread_join(&creator).status == DOE_HOST_OK);
    assert(doe_thread_join(&guard).status == DOE_HOST_OK);
    unsigned char expected[260]={0};
    const uint32_t update[2]={0x12345678u,0x9abcdef0u};
    memcpy(expected+60,update,sizeof(update));
    assert(!memcmp(map(&s,test.full,sizeof(expected)),expected,sizeof(expected)));
    wgpuBufferUnmap(test.full);
    assert(!memcmp(map(&s,test.partial,sizeof(expected)),expected,sizeof(expected)));
    wgpuBufferUnmap(test.partial);
    const unsigned char *padding=map(&s,test.padding,256);
    for(size_t i=0;i<256;++i) assert(padding[i] == 0);
    wgpuBufferUnmap(test.padding);
    wgpuBufferRelease(test.full); wgpuBufferRelease(test.partial); wgpuBufferRelease(test.padding);
    ID3D12Fence_Release(test.gate.gate);
    assert(CloseHandle(test.gate.accepted) && CloseHandle(test.gate.stop));
    if (messages) {
        bool clean=true;
        unsigned state_advisories=0,range_advisories=0;
        const UINT64 count=ID3D12InfoQueue_GetNumStoredMessagesAllowedByRetrievalFilter(messages);
        for (UINT64 i=0;i<count;++i) {
            SIZE_T size=0;
            assert(SUCCEEDED(ID3D12InfoQueue_GetMessage(messages,i,NULL,&size)));
            D3D12_MESSAGE *message=malloc(size);
            assert(message && SUCCEEDED(ID3D12InfoQueue_GetMessage(messages,i,message,&size)));
            if (message->Severity <= D3D12_MESSAGE_SEVERITY_WARNING) {
                fprintf(stderr,"D3D12 debug message %u: %s\n",(unsigned)message->ID,message->pDescription);
                /* The SDK explicitly permits CPU writes to READBACK. Keep the
                 * truthful written range for tooling; do not hide those writes
                 * merely to avoid this low-utility advisory. A committed DEFAULT
                 * initial state is also an advisory (buffers implicitly start
                 * COMMON); these <=1 MiB buffers are placed and must emit none. */
                if (message->Severity == D3D12_MESSAGE_SEVERITY_WARNING &&
                    message->ID == D3D12_MESSAGE_ID_UNMAP_RANGE_NOT_EMPTY) range_advisories++;
                else if (message->Severity == D3D12_MESSAGE_SEVERITY_WARNING &&
                    message->ID == D3D12_MESSAGE_ID_CREATERESOURCE_STATE_IGNORED) state_advisories++;
                else clean=false;
            }
            free(message);
        }
        ID3D12InfoQueue_Release(messages);
        /* These READBACK buffers take never-placed slots of a new heap, which
         * D3D12 zeroed at creation: no CPU zero write, so no range advisory. */
        assert(clean && state_advisories == 0 && range_advisories == 0);
        puts("D3D12 initialization debug layer: no errors/corruption/unexpected warnings; "
            "placed buffers emit no state advisory; D3D12-zeroed READBACK placements need no CPU write");
    }
    consumer_finish(&s);
    printf("gated initialization: accepted=%u CPUzero=%u watchdog=%u; complete260-byte/partial-cacheline/256-byte-CBV-padding oracles\n",
        (unsigned)accepted,(unsigned)test.cpu_zeroes,(unsigned)atomic_load(&test.gate.watchdog_fired));
    fflush(stdout);
    assert(accepted && test.cpu_zeroes && !atomic_load(&test.gate.watchdog_fired));
}

static void terminal_loss(void)
{
    state s = {0};
    consumer_init(&s);
    native_device *device = (native_device *)s.device;
    doe_dx_context *context = device->backend.value.d3d12.context;
    ID3D12Fence *gate = NULL;
    assert(SUCCEEDED(ID3D12Device_CreateFence(context->native->device, 0,
        D3D12_FENCE_FLAG_NONE, &IID_ID3D12Fence, (void **)&gate)));
    assert(SUCCEEDED(ID3D12CommandQueue_Wait(context->native->queue, gate, 1)));
    /* Construction itself is now accepted behind the closed GPU gate. Loss
     * below must retire that initialization lease, not publish success. */
    WGPUBuffer source = buffer(&s, 4, WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst);
    WGPUBuffer readback = buffer(&s, 4, WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst);
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s.device, NULL);
    wgpuCommandEncoderCopyBufferToBuffer(encoder, source, 0, readback, 0, 4);
    WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, NULL);
    assert(commands);
    wgpuCommandEncoderRelease(encoder);
    wgpuQueueSubmit(s.queue, 1, &commands);
    wgpuCommandBufferRelease(commands);
    wgpuBufferRelease(source);
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
    wgpuBufferRelease(readback);
    consumer_finish(&s);
}

/* A later record's backend poll retires every command its fence covers, so an
 * earlier record can be terminal yet still linked once a poll walk returns
 * incomplete. The pending owner must observe that completion, not report the
 * still-linked record as a conflict (WorkDone then published no future). The
 * device mutex is held throughout, so the progress worker cannot settle it. */
static void stale_terminal_owner(void)
{
    state s = {0};
    consumer_init(&s);
    native_device *device = (native_device *)s.device;
    doe_dx_context *context = device->backend.value.d3d12.context;
    WGPUBuffer target = buffer(&s, 4, WGPUBufferUsage_CopyDst | WGPUBufferUsage_CopySrc);
    uint32_t value = 1;
    wgpuQueueWriteBuffer(s.queue, target, 0, &value, sizeof(value));
    native_lock(device);
    native_drain_pending(device);
    assert(!device->pending);
    ID3D12Fence *gate = NULL;
    assert(SUCCEEDED(ID3D12Device_CreateFence(context->native->device, 0,
        D3D12_FENCE_FLAG_NONE, &IID_ID3D12Fence, (void **)&gate)));
    /* Queue writes record into one open command; each flush submits one record. */
    assert(SUCCEEDED(ID3D12CommandQueue_Wait(context->native->queue, gate, 1)));
    wgpuQueueWriteBuffer(s.queue, target, 0, &value, sizeof(value));
    assert(!device->pending && native_d3d12_flush_writes(device).status == NATIVE_OK);
    assert(SUCCEEDED(ID3D12CommandQueue_Wait(context->native->queue, gate, 2)));
    wgpuQueueWriteBuffer(s.queue, target, 0, &value, sizeof(value));
    assert(native_d3d12_flush_writes(device).status == NATIVE_OK);
    native_pending *first = device->pending;
    assert(first && first->next && !first->next->next);
    doe_dx_commands *earlier = first->native.value.d3d12.commands;
    const uint64_t earlier_fence = earlier->fence;
    assert(earlier_fence && first->next->native.value.d3d12.commands->fence > earlier_fence);
    assert(SUCCEEDED(ID3D12Fence_Signal(gate, 1)));
    uint64_t completed = 0;
    for (unsigned attempt = 0; attempt < 5000 && completed < earlier_fence; ++attempt) {
        assert(doe_dx_native_completed(context->native, &completed, NULL) == DOE_DX_OK);
        if (completed < earlier_fence) Sleep(1);
    }
    assert(completed >= earlier_fence);
    /* The backend poll a later record would issue: earlier retires in the
     * backend while its frontend record stays linked behind running work. */
    assert(doe_dx_context_poll(context, 0, NULL) == DOE_DX_PENDING);
    assert(device->pending == first && !native_command_in_flight(&first->native));
    struct timespec now, deadline;
    assert(doe_monotonic_now(&now).status == DOE_HOST_OK &&
        doe_time_add(&now, UINT64_C(5000000000), &deadline).status == DOE_HOST_OK);
    native_failure waited = native_d3d12_wait_owner(device, earlier, &deadline);
    const bool observed = device->pending && device->pending != first &&
        native_command_in_flight(&device->pending->native) && !device->pending->next;
    assert(SUCCEEDED(ID3D12Fence_Signal(gate, 2)));
    native_unlock(device);
    printf("stale terminal owner: wait=%d observed=%u\n", (int)waited.status, (unsigned)observed);
    fflush(stdout);
    assert(waited.status == NATIVE_OK && observed);
    WGPUBuffer readback = buffer(&s, 4, WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst);
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s.device, NULL);
    wgpuCommandEncoderCopyBufferToBuffer(encoder, target, 0, readback, 0, 4);
    WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, NULL);
    assert(commands);
    wgpuCommandEncoderRelease(encoder);
    wgpuQueueSubmit(s.queue, 1, &commands);
    wgpuCommandBufferRelease(commands);
    expect_value(&s, readback, 1);
    ID3D12Fence_Release(gate);
    wgpuBufferRelease(readback);
    wgpuBufferRelease(target);
    consumer_finish(&s);
}

int main(int argc,char **argv)
{
    if (argc == 2 && !strcmp(argv[1],"--initialization-debug")) {
        ID3D12Debug *debug=NULL;
        assert(SUCCEEDED(D3D12GetDebugInterface(&IID_ID3D12Debug,(void **)&debug)));
        ID3D12Debug_EnableDebugLayer(debug);
        ID3D12Debug_Release(debug);
        initialization_ordering(true);
        return 0;
    }
    assert(argc == 1);
    ordered_work();
    stale_terminal_owner();
    initialization_ordering(false);
    terminal_loss();
    printf("PASS actual gated D3D12 queue: distinct/reused writes and dependent compute accepted before GPU release, unlocked concurrent map, timeout/cancel/early release, exact11/7/42 readback, own-device terminal loss and teardown %s\n", adapter_class_scope());
    return 0;
}
