/* SPDX-License-Identifier: MIT
 * Public pass records and real native/resource/query owners over Metal call
 * ledgers. No shader result, counter value or GPU completion is simulated. */
#include "../src/surface.h"
#include "../src/metal_pass_timestamps.h"
#include "../../backends/metal/tests/mock_native.h"
#include "../../backends/metal/src/query_internal.h"
#include "consumer.h"

typedef struct fixture {
    mock_native mock;
    doe_mtl_native base;
    doe_mtl_context *context;
    atomic_bool hold, unknown;
    char trace[256];
    doe_mtl_native_timestamps samples[32];
    size_t trace_count, sample_count;
} fixture;
static fixture fixtures[2];
static size_t opened;
static unsigned physical_identity;
static _Thread_local size_t allocation_failure;

void *__real_calloc(size_t, size_t);
void *__wrap_calloc(size_t count, size_t size)
{
    if (allocation_failure && !--allocation_failure) return NULL;
    return __real_calloc(count, size);
}
static void trace(fixture *f, char kind, const doe_mtl_native_timestamps *timestamps)
{
    assert(f->trace_count < sizeof(f->trace));
    f->trace[f->trace_count++] = kind;
    if (timestamps) {
        assert(f->sample_count < sizeof(f->samples) / sizeof(f->samples[0]));
        f->samples[f->sample_count++] = *timestamps;
    }
}
static doe_mtl_status ledger_dispatch(void *userdata, void *command, void *pipeline,
    const doe_mtl_native_resources *resources, uint32_t sizes_slot,
    doe_mtl_dimensions groups, doe_mtl_dimensions workgroup, uint32_t repeat,
    const doe_mtl_native_timestamps *timestamps, doe_mtl_error *error)
{
    fixture *f = userdata;
    trace(f, 'D', timestamps);
    return f->base.dispatch(userdata, command, pipeline, resources, sizes_slot,
        groups, workgroup, repeat, timestamps, error);
}
static doe_mtl_status ledger_render(void *userdata, void *command,
    const doe_mtl_native_render_pass *pass, const doe_mtl_native_render_draw *draws,
    size_t count, doe_mtl_error *error)
{
    fixture *f = userdata;
    const doe_mtl_native_timestamps timestamps = {pass->timestamp_buffer,pass->timestamp_begin,pass->timestamp_end};
    trace(f, 'R', pass->timestamp_buffer ? &timestamps : NULL);
    return f->base.render(userdata, command, pass, draws, count, error);
}
static doe_mtl_status ledger_boundary(void *userdata, void *command, void *buffer, uint32_t index,
    bool stage, void *scratch, void **event, uint64_t value, doe_mtl_error *error)
{
    fixture *f = userdata;
    trace(f, 'T', NULL);
    return f->base.timestamp_boundary(userdata, command, buffer, index, stage, scratch, event, value, error);
}
static doe_mtl_status ledger_guard(void *userdata, void *command,
    const doe_mtl_native_guard_copy *copies, size_t copy_count,
    const doe_mtl_native_guard_dispatch *steps, size_t step_count, doe_mtl_error *error)
{
    fixture *f = userdata;
    trace(f, 'G', NULL);
    return f->base.guard_batch(userdata, command, copies, copy_count, steps, step_count, error);
}
doe_mtl_status __wrap_doe_mtl_context_create(doe_mtl_context **out, doe_mtl_error *error)
{
    assert(opened < sizeof(fixtures) / sizeof(fixtures[0]));
    fixture *f = &fixtures[opened++];
    doe_mtl_native native = mock_driver(&f->mock);
    /* Public timestamp admission requires capacity for the advertised maximum
     * query buffer. The generic backend ledger defaults to only 4096 bytes. */
    f->mock.buffer_limit = UINT64_C(1048576);
    f->base = native;
    native.dispatch = ledger_dispatch;
    native.render = ledger_render;
    native.timestamp_boundary = ledger_boundary;
    native.guard_batch = ledger_guard;
    doe_mtl_status status = doe_mtl_context_open(native, out, error);
    if (status == DOE_MTL_OK) f->context = *out;
    return status;
}
void *__wrap_doe_mtl_context_device(const doe_mtl_context *context)
{
    assert(context);
    return &physical_identity;
}
static fixture *owner(doe_mtl_commands *commands)
{
    for (size_t i = 0; i < opened; ++i) if (fixtures[i].context == commands->context) return &fixtures[i];
    abort();
}
doe_mtl_status __real_doe_mtl_commands_poll(doe_mtl_commands *, doe_mtl_error *);
doe_mtl_status __wrap_doe_mtl_commands_poll(doe_mtl_commands *commands, doe_mtl_error *error)
{
    fixture *f = owner(commands);
    mock_command_result(commands->native, atomic_load(&f->unknown) ? 0 : atomic_load(&f->hold) ? 2 : 4, 0);
    return __real_doe_mtl_commands_poll(commands, error);
}
doe_mtl_status __real_doe_mtl_commands_wait(doe_mtl_commands *, doe_mtl_error *);
doe_mtl_status __wrap_doe_mtl_commands_wait(doe_mtl_commands *commands, doe_mtl_error *error)
{
    fixture *f = owner(commands);
    mock_command_result(commands->native, atomic_load(&f->unknown) ? 0 : atomic_load(&f->hold) ? 2 : 4, 0);
    return __real_doe_mtl_commands_wait(commands, error);
}
static WGPUShaderModule shader(state *s, const char *code)
{
    WGPUShaderSourceWGSL source = WGPU_SHADER_SOURCE_WGSL_INIT;
    source.code = (WGPUStringView){code,WGPU_STRLEN};
    WGPUShaderModuleDescriptor descriptor = {.nextInChain=&source.chain};
    WGPUShaderModule module = wgpuDeviceCreateShaderModule(s->device, &descriptor);
    assert(module);
    return module;
}
static WGPUComputePipeline compute_pipeline(state *s)
{
    WGPUShaderModule module = shader(s, "@compute @workgroup_size(1) fn main(){}");
    WGPUComputePipelineDescriptor descriptor = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
    descriptor.compute = (WGPUComputeState){.module=module,.entryPoint={"main",WGPU_STRLEN}};
    WGPUComputePipeline pipeline = wgpuDeviceCreateComputePipeline(s->device, &descriptor);
    wgpuShaderModuleRelease(module);
    assert(pipeline);
    return pipeline;
}
static WGPUQuerySet query_set(state *s)
{
    WGPUQuerySetDescriptor descriptor = {.type=WGPUQueryType_Timestamp,.count=8};
    WGPUQuerySet queries = wgpuDeviceCreateQuerySet(s->device, &descriptor);
    assert(queries);
    return queries;
}
static void compute_pass(WGPUCommandEncoder encoder, WGPUComputePipeline pipeline,
    WGPUQuerySet queries, uint32_t begin, uint32_t end, unsigned dispatches, WGPUBuffer indirect)
{
    WGPUPassTimestampWrites writes = WGPU_PASS_TIMESTAMP_WRITES_INIT;
    writes.querySet = queries; writes.beginningOfPassWriteIndex = begin; writes.endOfPassWriteIndex = end;
    WGPUComputePassDescriptor descriptor = {.timestampWrites=&writes};
    WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, &descriptor);
    assert(pass);
    if (dispatches) wgpuComputePassEncoderSetPipeline(pass, pipeline);
    for (unsigned i = 0; i < dispatches; ++i) {
        if (indirect) wgpuComputePassEncoderDispatchWorkgroupsIndirect(pass, indirect, 0);
        else wgpuComputePassEncoderDispatchWorkgroups(pass, 1, 1, 1);
    }
    wgpuComputePassEncoderEnd(pass);
    wgpuComputePassEncoderRelease(pass);
}
static WGPUCommandBuffer finish(WGPUCommandEncoder encoder)
{
    WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, NULL);
    wgpuCommandEncoderRelease(encoder);
    assert(commands);
    return commands;
}
static void submit_commands(state *s, WGPUCommandBuffer commands)
{
    wgpuQueueSubmit(s->queue, 1, &commands);
    wgpuCommandBufferRelease(commands);
}
static void reset_trace(fixture *f)
{
    f->trace_count = f->sample_count = 0;
}
static void compute_routes(state *s, fixture *f, WGPUComputePipeline pipeline, WGPUQuerySet queries)
{
    const uint32_t sides[][2] = {{0,1},{0,UINT32_MAX},{UINT32_MAX,1}};
    for (size_t i = 0; i < sizeof(sides) / sizeof(sides[0]); ++i) {
        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device, NULL);
        compute_pass(encoder, pipeline, queries, sides[i][0], sides[i][1], 4, NULL);
        WGPUCommandBuffer commands = finish(encoder);
        reset_trace(f);
        unsigned old_boundaries = f->mock.timestamps;
        submit_commands(s, commands);
        assert(f->trace_count == 4 && !memcmp(f->trace,"DDDD",4));
        assert(f->mock.timestamps == old_boundaries && f->sample_count == (i == 0 ? 2 : 1));
        if (i != 2) assert(f->samples[0].begin == 0 && f->samples[0].end == UINT32_MAX);
        if (i != 1) assert(f->samples[f->sample_count - 1].begin == UINT32_MAX &&
            f->samples[f->sample_count - 1].end == 1);
    }
    for (size_t i = 0; i < sizeof(sides) / sizeof(sides[0]); ++i) {
        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device, NULL);
        compute_pass(encoder, pipeline, queries, sides[i][0], sides[i][1], 0, NULL);
        reset_trace(f);
        submit_commands(s, finish(encoder));
        const size_t expected = i == 0 ? 2 : 1;
        assert(f->trace_count == expected && !f->sample_count);
        for (size_t j = 0; j < expected; ++j) assert(f->trace[j] == 'T');
    }
    /* Reusing indices in the same recording preserves the ordered fallback. */
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device, NULL);
    compute_pass(encoder, pipeline, queries, 0, 1, 1, NULL);
    compute_pass(encoder, pipeline, queries, 0, 1, 1, NULL);
    reset_trace(f);
    submit_commands(s, finish(encoder));
    assert(f->trace_count == 4 && !memcmp(f->trace,"DTDT",4));
    assert(f->sample_count == 1);
    /* A prior resolve orders publication before later distinct endpoint writes. */
    WGPUBuffer resolved = buffer(s, 256, WGPUBufferUsage_QueryResolve | WGPUBufferUsage_CopySrc);
    encoder = wgpuDeviceCreateCommandEncoder(s->device, NULL);
    compute_pass(encoder, pipeline, queries, 0, 1, 1, NULL);
    wgpuCommandEncoderResolveQuerySet(encoder, queries, 0, 2, resolved, 0);
    compute_pass(encoder, pipeline, queries, 2, 3, 1, NULL);
    reset_trace(f);
    submit_commands(s, finish(encoder));
    assert(f->trace_count == 5 && !memcmp(f->trace,"DDTDT",5) && f->sample_count == 1);
    wgpuBufferRelease(resolved);
    WGPUBuffer arguments = buffer(s, 12, WGPUBufferUsage_Indirect);
    encoder = wgpuDeviceCreateCommandEncoder(s->device, NULL);
    compute_pass(encoder, pipeline, queries, 0, 1, 1, arguments);
    reset_trace(f);
    unsigned old_indirect = f->mock.indirects;
    submit_commands(s, finish(encoder));
    assert(f->trace_count >= 3 && f->trace[0] == 'T' && f->trace[f->trace_count - 1] == 'T');
    assert(f->mock.indirects == old_indirect + 1 && !f->sample_count);
    wgpuBufferRelease(arguments);
}

static void malformed_spans(void)
{
    doe_resource_command records[3] = {0};
    records[0].kind = records[2].kind = DOE_COMMAND_WRITE_TIMESTAMP;
    records[0].value.timestamp.set = records[2].value.timestamp.set = (doe_resource_query_set *)&physical_identity;
    records[0].value.timestamp.position = DOE_TIMESTAMP_PASS_BEGIN;
    records[2].value.timestamp.position = DOE_TIMESTAMP_PASS_END;
    records[2].value.timestamp.index = 1;
    records[0].value.timestamp.pass_count = records[2].value.timestamp.pass_count = 3;
    size_t cursor = 0;
    native_metal_timestamp_span span;
    assert(native_metal_next_timestamp_span(records,3,&cursor,&span).status == NATIVE_OK && span.body_count == 1);
    records[0].value.timestamp.pass_count = 4; cursor = 0;
    assert(native_metal_next_timestamp_span(records,3,&cursor,&span).status == NATIVE_INVALID_ARGUMENT);
    records[0].value.timestamp.pass_count = 3;
    records[2].value.timestamp.set = NULL; cursor = 0;
    assert(native_metal_next_timestamp_span(records,3,&cursor,&span).status == NATIVE_INVALID_ARGUMENT);
    records[2].value.timestamp.set = records[0].value.timestamp.set;
    records[2].value.timestamp.index = 0; cursor = 0;
    assert(native_metal_next_timestamp_span(records,3,&cursor,&span).status == NATIVE_INVALID_ARGUMENT);
    records[2].value.timestamp.index = 1;
    records[0].value.timestamp.pass_first = 1; cursor = 0;
    assert(native_metal_next_timestamp_span(records,3,&cursor,&span).status == NATIVE_INVALID_ARGUMENT);
    records[0].value.timestamp.pass_first = 0;
    records[2].value.timestamp.pass_count = 2; cursor = 0;
    assert(native_metal_next_timestamp_span(records,3,&cursor,&span).status == NATIVE_INVALID_ARGUMENT);
}

static WGPURenderPipeline render_pipeline(state *s)
{
    WGPUShaderModule module = shader(s,
        "@vertex fn vertex(@builtin(vertex_index)i:u32)->@builtin(position)vec4f{"
        "let p=array(vec2f(-1,-1),vec2f(3,-1),vec2f(-1,3));return vec4f(p[i],0,1);}"
        "@fragment fn color()->@location(0)vec4f{return vec4f(1,0,0,1);}");
    WGPUColorTargetState target = WGPU_COLOR_TARGET_STATE_INIT;
    target.format = WGPUTextureFormat_RGBA8Unorm;
    WGPUFragmentState fragment = {.module=module,.entryPoint={"color",WGPU_STRLEN},
        .targetCount=1,.targets=&target};
    WGPURenderPipelineDescriptor descriptor = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    descriptor.vertex = (WGPUVertexState){.module=module,.entryPoint={"vertex",WGPU_STRLEN}};
    descriptor.fragment = &fragment;
    WGPURenderPipeline pipeline = wgpuDeviceCreateRenderPipeline(s->device,&descriptor);
    wgpuShaderModuleRelease(module);
    assert(pipeline);
    return pipeline;
}
static WGPUCommandBuffer render_commands(state *s, WGPURenderPipeline pipeline,
    WGPUTextureView view, WGPUQuerySet queries, uint32_t begin, uint32_t end,
    unsigned mode, WGPUBuffer arguments)
{
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device,NULL);
    WGPUPassTimestampWrites writes = WGPU_PASS_TIMESTAMP_WRITES_INIT;
    writes.querySet = queries; writes.beginningOfPassWriteIndex = begin; writes.endOfPassWriteIndex = end;
    WGPURenderPassColorAttachment color = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    color.view = view; color.loadOp = WGPULoadOp_Clear; color.storeOp = WGPUStoreOp_Store;
    WGPURenderPassDescriptor descriptor = {.colorAttachmentCount=1,.colorAttachments=&color,.timestampWrites=&writes};
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder,&descriptor);
    assert(pass);
    if (mode != 3) wgpuRenderPassEncoderSetPipeline(pass,pipeline);
    if (mode == 0) wgpuRenderPassEncoderDraw(pass,3,1,0,0);
    else if (mode == 1) {
        wgpuRenderPassEncoderSetIndexBuffer(pass,arguments,WGPUIndexFormat_Uint16,0,6);
        wgpuRenderPassEncoderDrawIndexed(pass,3,1,0,0,0);
    } else if (mode == 2) wgpuRenderPassEncoderDrawIndirect(pass,arguments,0);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
    return finish(encoder);
}
static void render_routes(state *s, fixture *f, WGPUQuerySet queries)
{
    WGPURenderPipeline pipeline = render_pipeline(s);
    WGPUTextureDescriptor descriptor = WGPU_TEXTURE_DESCRIPTOR_INIT;
    descriptor.format = WGPUTextureFormat_RGBA8Unorm;
    descriptor.size = (WGPUExtent3D){2,2,1};
    descriptor.usage = WGPUTextureUsage_RenderAttachment;
    WGPUTexture texture = wgpuDeviceCreateTexture(s->device,&descriptor);
    WGPUTextureView view = wgpuTextureCreateView(texture,NULL);
    assert(texture && view);
    const uint32_t sides[][2] = {{0,1},{0,UINT32_MAX},{UINT32_MAX,1}};
    for (size_t i = 0; i < sizeof(sides) / sizeof(sides[0]); ++i) {
        WGPUCommandBuffer commands = render_commands(s,pipeline,view,queries,sides[i][0],sides[i][1],0,NULL);
        reset_trace(f);
        submit_commands(s,commands);
        assert(f->trace_count == 1 && f->trace[0] == 'R' && f->sample_count == 1);
        assert(f->samples[0].begin == sides[i][0] && f->samples[0].end == sides[i][1]);
    }
    WGPUBuffer arguments = buffer(s,64,WGPUBufferUsage_Index | WGPUBufferUsage_Indirect);
    for (unsigned mode = 1; mode <= 3; ++mode) {
        WGPUCommandBuffer commands = render_commands(s,pipeline,view,queries,0,1,mode,arguments);
        unsigned guards = f->mock.guard_batches;
        reset_trace(f);
        submit_commands(s,commands);
        assert(f->trace_count >= 3 && f->trace[0] == 'T' && f->trace[f->trace_count - 1] == 'T');
        assert(!f->sample_count);
        if (mode != 3) {
            assert(f->mock.guard_batches == guards + 1);
            assert(memchr(f->trace,'G',f->trace_count) && memchr(f->trace,'R',f->trace_count));
        }
    }
    wgpuBufferRelease(arguments);
    wgpuTextureViewRelease(view);
    wgpuTextureRelease(texture);
    wgpuRenderPipelineRelease(pipeline);
}

static void allocation_and_unknown(state *s, fixture *f, WGPUComputePipeline pipeline, WGPUQuerySet queries)
{
    const size_t failures[] = {1,3,5,6,7};
    native_queries *payload = doe_resource_query_set_native((doe_resource_query_set *)queries);
    for (size_t i = 0; i < sizeof(failures) / sizeof(failures[0]); ++i) {
        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device, NULL);
        compute_pass(encoder,pipeline,queries,0,1,1,NULL);
        WGPUCommandBuffer commands = finish(encoder);
        unsigned errors = s->errors;
        reset_trace(f);
        allocation_failure = failures[i];
        wgpuQueueSubmit(s->queue,1,&commands);
        allocation_failure = 0;
        assert(s->errors == errors + 1 && s->last_error == WGPUErrorType_OutOfMemory);
        assert(!f->trace_count && !payload->metal_timestamps->owner);
        wgpuCommandBufferRelease(commands);
    }
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device, NULL);
    compute_pass(encoder,pipeline,queries,0,1,1,NULL);
    atomic_store(&f->unknown,true);
    atomic_store(&f->hold,true);
    submit_commands(s,finish(encoder));
    assert(payload->metal_timestamps->owner && payload->metal_timestamps->references > 1);
    native_device *device = (native_device *)s->device;
    native_lock(device);
    assert(!native_poll_pending(device).complete && payload->metal_timestamps->owner);
    atomic_store(&f->unknown,false);
    atomic_store(&f->hold,false);
    assert(native_poll_pending(device).complete && !payload->metal_timestamps->owner);
    native_unlock(device);
    s->errors = 0; /* Every intentional failure was checked above. */
}

int main(void)
{
    malformed_spans();
    state s = {0};
    s.instance = wgpuCreateInstance(NULL);
    WGPURequestAdapterOptions options = WGPU_REQUEST_ADAPTER_OPTIONS_INIT;
    options.backendType = WGPUBackendType_Metal;
    WGPURequestAdapterCallbackInfo callback = {.mode=WGPUCallbackMode_AllowSpontaneous,.callback=adapter_reply,.userdata1=&s};
    assert(wgpuInstanceRequestAdapter(s.instance,&options,callback).id && s.adapter);
    assert(wgpuAdapterHasFeature(s.adapter,WGPUFeatureName_TimestampQuery));
    const WGPUFeatureName feature = WGPUFeatureName_TimestampQuery;
    WGPUDeviceDescriptor descriptor = WGPU_DEVICE_DESCRIPTOR_INIT;
    descriptor.requiredFeatureCount = 1; descriptor.requiredFeatures = &feature;
    descriptor.uncapturedErrorCallbackInfo = (WGPUUncapturedErrorCallbackInfo){.callback=error_reply,.userdata1=&s};
    s.device = wgpuAdapterCreateDevice(s.adapter,&descriptor);
    assert(s.device && opened == 2);
    s.queue = wgpuDeviceGetQueue(s.device);
    fixture *f = &fixtures[1];
    WGPUComputePipeline pipeline = compute_pipeline(&s);
    WGPUQuerySet queries = query_set(&s);
    compute_routes(&s,f,pipeline,queries);
    render_routes(&s,f,queries);
    allocation_and_unknown(&s,f,pipeline,queries);
    wgpuQuerySetRelease(queries);
    wgpuComputePipelineRelease(pipeline);
    consumer_finish(&s);
    for (size_t i = 0; i < opened; ++i) mock_assert_closed(&fixtures[i].mock);
    puts("PASS public Metal pass timestamp lowering: bounded immutable spans, first/last compute attachments, one-sided/empty/reuse/resolve/indirect fallback, OOM rollback and unknown leases; host ledgers only");
    return 0;
}
