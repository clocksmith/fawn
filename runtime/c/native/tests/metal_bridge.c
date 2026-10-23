/* SPDX-License-Identifier: MIT
 * Host call-ledger composition only. Existing Metal mock boundary supplies no
 * shader interpreter or GPU result. Public handles, owners and queue are real. */
#include "../src/internal.h"
#include "../../backends/metal/tests/mock_native.h"
#include "consumer.h"
#include "doe_native_programs.h"

static mock_native mocks[8];
static doe_mtl_context *contexts[8];
static doe_mtl_commands *commands[8];
static unsigned context_count;
static unsigned physical_identity[2];
static bool changed_default, hold;
static doe_mtl_status buffer_failure = DOE_MTL_OK;

doe_mtl_status __real_doe_mtl_commands_wait(doe_mtl_commands *, doe_mtl_error *);
doe_mtl_status __real_doe_mtl_commands_create(doe_mtl_context *, doe_mtl_commands **, doe_mtl_error *);
doe_mtl_status __real_doe_mtl_buffer_create(doe_mtl_context *, uint64_t, doe_mtl_memory,
    doe_mtl_buffer **, doe_mtl_error *);

doe_mtl_status __wrap_doe_mtl_context_create(doe_mtl_context **out, doe_mtl_error *error)
{
    assert(context_count < 8);
    unsigned index = context_count++;
    doe_mtl_status status = doe_mtl_context_open(mock_driver(&mocks[index]), out, error);
    if (status == DOE_MTL_OK) contexts[index] = *out;
    return status;
}
void *__wrap_doe_mtl_context_device(const doe_mtl_context *context)
{
    for (unsigned i = 0; i < context_count; ++i)
        if (contexts[i] == context) return &physical_identity[changed_default && i != 0];
    assert(false);
    return NULL;
}
doe_mtl_status __wrap_doe_mtl_commands_create(doe_mtl_context *context,
    doe_mtl_commands **out, doe_mtl_error *error)
{
    doe_mtl_status status = __real_doe_mtl_commands_create(context,out,error);
    if (status == DOE_MTL_OK) {
        for (unsigned i = context_count; i != 0; --i) {
            if (contexts[i - 1] != context) continue;
            commands[i - 1] = *out;
            break;
        }
    }
    return status;
}
doe_mtl_status __wrap_doe_mtl_commands_wait(doe_mtl_commands *command, doe_mtl_error *error)
{
    for (unsigned i = context_count; i != 0; --i) {
        if (commands[i - 1] != command) continue;
        mock_result(&mocks[i - 1],hold ? 0 : 4,0);
        break;
    }
    return __real_doe_mtl_commands_wait(command,error);
}
doe_mtl_status __wrap_doe_mtl_buffer_create(doe_mtl_context *context, uint64_t size,
    doe_mtl_memory memory, doe_mtl_buffer **out, doe_mtl_error *error)
{
    if (buffer_failure != DOE_MTL_OK) return buffer_failure;
    return __real_doe_mtl_buffer_create(context,size,memory,out,error);
}

static void captured(WGPUPopErrorScopeStatus status, WGPUErrorType type,
    WGPUStringView message, void *userdata, void *unused)
{
    (void)message;
    (void)unused;
    assert(status == WGPUPopErrorScopeStatus_Success && type == *(WGPUErrorType *)userdata);
    *(WGPUErrorType *)userdata = WGPUErrorType_NoError;
}
static void pop(state *s, WGPUErrorType type)
{
    WGPUPopErrorScopeCallbackInfo callback = {.mode=WGPUCallbackMode_AllowSpontaneous,
        .callback=captured,.userdata1=&type};
    assert(wgpuDevicePopErrorScope(s->device,callback).id && type == WGPUErrorType_NoError);
}
static void failed_device(WGPURequestDeviceStatus status, WGPUDevice device,
    WGPUStringView message, void *userdata, void *unused)
{
    (void)message;
    (void)unused;
    assert(status == WGPURequestDeviceStatus_Error && !device);
    ++*(unsigned *)userdata;
}
static void done(WGPUQueueWorkDoneStatus status, WGPUStringView message, void *userdata, void *unused)
{
    (void)message;
    (void)unused;
    assert(status == WGPUQueueWorkDoneStatus_Error);
    ++*(unsigned *)userdata;
}

static void pipeline_ready(WGPUCreatePipelineAsyncStatus status, WGPUComputePipeline pipeline,
    WGPUStringView message, void *userdata, void *unused)
{
    (void)message;
    (void)unused;
    assert(status == WGPUCreatePipelineAsyncStatus_Success && pipeline);
    *(WGPUComputePipeline *)userdata = pipeline;
}

static void reject_spirv_fallback(state *s)
{
    const char *source = "@group(0) @binding(0) var<storage,read_write> data:array<atomic<u32>>;"
        "@compute @workgroup_size(1) fn main(){atomicStore(&data[0],7u);}";
    WGPUShaderSourceWGSL wgsl = {.chain={.sType=WGPUSType_ShaderSourceWGSL},.code={source,WGPU_STRLEN}};
    WGPUShaderModuleDescriptor descriptor = {.nextInChain=&wgsl.chain};
    wgpuDevicePushErrorScope(s->device,WGPUErrorFilter_Validation);
    WGPUShaderModule module = wgpuDeviceCreateShaderModule(s->device,&descriptor);
    pop(s,WGPUErrorType_Validation);
    assert(module);
    native_shader *payload = doe_resource_shader_native((doe_resource_shader *)module);
    assert(!payload->compiled && payload->error.diagnostic.message[0]);
    wgpuShaderModuleRelease(module);
}

static void compute_binding_ledger(state *s)
{
    const char *source =
        "@group(1) @binding(7) var<storage,read_write> output:array<u32>;"
        "@group(0) @binding(4) var<storage,read> input:array<u32>;"
        "@group(0) @binding(2) var<uniform> scale:vec4u;"
        "@compute @workgroup_size(1) fn first(){output[0]=9u;}"
        "@compute @workgroup_size(4) fn selected(@builtin(global_invocation_id) id:vec3u){"
        "output[id.x]=input[id.x]*scale.x;}";
    WGPUShaderSourceWGSL wgsl = {.chain={.sType=WGPUSType_ShaderSourceWGSL},.code={source,WGPU_STRLEN}};
    WGPUShaderModuleDescriptor module_desc = {.nextInChain=&wgsl.chain};
    WGPUShaderModule module = wgpuDeviceCreateShaderModule(s->device,&module_desc);
    assert(module);
    WGPUBindGroupLayoutEntry entries0[2] = {
        {.binding=2,.visibility=WGPUShaderStage_Compute,.buffer={.type=WGPUBufferBindingType_Uniform}},
        {.binding=4,.visibility=WGPUShaderStage_Compute,.buffer={.type=WGPUBufferBindingType_ReadOnlyStorage}}};
    WGPUBindGroupLayoutEntry entry1 = {.binding=7,.visibility=WGPUShaderStage_Compute,
        .buffer={.type=WGPUBufferBindingType_Storage,.hasDynamicOffset=WGPU_TRUE}};
    WGPUBindGroupLayoutDescriptor desc0 = {.entryCount=2,.entries=entries0};
    WGPUBindGroupLayoutDescriptor desc1 = {.entryCount=1,.entries=&entry1};
    WGPUBindGroupLayout layouts[2] = {wgpuDeviceCreateBindGroupLayout(s->device,&desc0),
        wgpuDeviceCreateBindGroupLayout(s->device,&desc1)};
    assert(layouts[0] && layouts[1]);
    WGPUPipelineLayoutDescriptor layout_desc = {.bindGroupLayoutCount=2,.bindGroupLayouts=layouts};
    WGPUPipelineLayout layout = wgpuDeviceCreatePipelineLayout(s->device,&layout_desc);
    WGPUComputePipelineDescriptor descriptor = {.layout=layout,
        .compute={.module=module,.entryPoint={"selected",8}}};
    mocks[2].pipeline_max_threads = 1;
    wgpuDevicePushErrorScope(s->device,WGPUErrorFilter_Validation);
    assert(!wgpuDeviceCreateComputePipeline(s->device,&descriptor));
    pop(s,WGPUErrorType_Validation);
    mocks[2].pipeline_max_threads = 256;
    WGPUComputePipeline pipeline = NULL;
    WGPUCreateComputePipelineAsyncCallbackInfo ready = {.mode=WGPUCallbackMode_AllowSpontaneous,
        .callback=pipeline_ready,.userdata1=&pipeline};
    assert(wgpuDeviceCreateComputePipelineAsync(s->device,&descriptor,ready).id);
    assert(pipeline && !strcmp(mocks[2].entrypoint,"selected"));
    WGPUBuffer input = buffer(s,512,WGPUBufferUsage_Storage);
    WGPUBuffer output = buffer(s,512,WGPUBufferUsage_Storage);
    WGPUBuffer scale = buffer(s,256,WGPUBufferUsage_Uniform);
    native_buffer *input_payload = doe_resource_buffer_native((doe_resource_buffer *)input);
    native_buffer *output_payload = doe_resource_buffer_native((doe_resource_buffer *)output);
    native_buffer *scale_payload = doe_resource_buffer_native((doe_resource_buffer *)scale);
    void *input_identity = doe_mtl_buffer_native(input_payload->handle.metal);
    void *output_identity = doe_mtl_buffer_native(output_payload->handle.metal);
    void *scale_identity = doe_mtl_buffer_native(scale_payload->handle.metal);
    WGPUBindGroupEntry bindings0[2] = {{.binding=2,.buffer=scale,.size=16},
        {.binding=4,.buffer=input,.offset=256,.size=32}};
    WGPUBindGroupEntry binding1 = {.binding=7,.buffer=output,.size=48};
    WGPUBindGroupDescriptor group_desc0 = {.layout=layouts[0],.entryCount=2,.entries=bindings0};
    WGPUBindGroupDescriptor group_desc1 = {.layout=layouts[1],.entryCount=1,.entries=&binding1};
    WGPUBindGroup group0 = wgpuDeviceCreateBindGroup(s->device,&group_desc0);
    WGPUBindGroup group1 = wgpuDeviceCreateBindGroup(s->device,&group_desc1);
    assert(group0 && group1);
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device,NULL);
    WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder,NULL);
    wgpuComputePassEncoderSetPipeline(pass,pipeline);
    wgpuComputePassEncoderSetBindGroup(pass,0,group0,0,NULL);
    uint32_t offset = 256;
    wgpuComputePassEncoderSetBindGroup(pass,1,group1,1,&offset);
    wgpuComputePassEncoderDispatchWorkgroups(pass,3,2,1);
    wgpuComputePassEncoderEnd(pass);
    wgpuComputePassEncoderRelease(pass);
    wgpuShaderModuleRelease(module);
    wgpuComputePipelineRelease(pipeline);
    wgpuPipelineLayoutRelease(layout);
    wgpuBindGroupLayoutRelease(layouts[0]);
    wgpuBindGroupLayoutRelease(layouts[1]);
    wgpuBindGroupRelease(group0);
    wgpuBindGroupRelease(group1);
    wgpuBufferRelease(input);
    wgpuBufferRelease(output);
    wgpuBufferRelease(scale);
    submit(s,encoder);
    assert(mocks[2].dispatches == 1 && mocks[2].sizes_slot == 30);
    assert(mocks[2].bindings[2].buffer == scale_identity && mocks[2].bindings[2].size == 16);
    assert(mocks[2].bindings[4].buffer == input_identity && mocks[2].bindings[4].offset == 256 && mocks[2].bindings[4].size == 32);
    assert(mocks[2].bindings[23].buffer == output_identity && mocks[2].bindings[23].offset == 256 && mocks[2].bindings[23].size == 48);
    assert(!mocks[2].bindings[0].buffer && !mocks[2].bindings[7].buffer);
    assert(mocks[2].groups.x == 3 && mocks[2].groups.y == 2 && mocks[2].groups.z == 1);
    assert(mocks[2].workgroup.x == 4 && mocks[2].workgroup.y == 1 && mocks[2].repeat == 1);
}

typedef struct exact_message {
    const char *text;
    unsigned calls;
} exact_message;

static void require_message(WGPUStringView message, exact_message *expected)
{
    assert(message.length == strlen(expected->text));
    assert(!memcmp(message.data, expected->text, message.length));
    ++expected->calls;
}

static void exact_scope(WGPUPopErrorScopeStatus status, WGPUErrorType type,
    WGPUStringView message, void *userdata, void *unused)
{
    (void)unused;
    assert(status == WGPUPopErrorScopeStatus_Success && type == WGPUErrorType_Internal);
    require_message(message, userdata);
}

static void exact_pipeline(WGPUCreatePipelineAsyncStatus status, WGPUComputePipeline pipeline,
    WGPUStringView message, void *userdata, void *unused)
{
    (void)unused;
    assert(status == WGPUCreatePipelineAsyncStatus_InternalError && !pipeline);
    require_message(message, userdata);
}

static void exact_validation_scope(WGPUPopErrorScopeStatus status, WGPUErrorType type,
    WGPUStringView message, void *userdata, void *unused)
{
    (void)unused;
    assert(status == WGPUPopErrorScopeStatus_Success && type == WGPUErrorType_Validation);
    require_message(message, userdata);
}

static void exact_queue(WGPUQueueWorkDoneStatus status, WGPUStringView message,
    void *userdata, void *unused)
{
    (void)unused;
    assert(status == WGPUQueueWorkDoneStatus_Error);
    require_message(message, userdata);
}

static void exact_map(WGPUMapAsyncStatus status, WGPUStringView message,
    void *userdata, void *unused)
{
    (void)unused;
    assert(status == WGPUMapAsyncStatus_Error);
    require_message(message, userdata);
}

static void overwrite_stack(void)
{
    volatile unsigned char scratch[4096];
    for (size_t i = 0; i < sizeof(scratch); ++i) scratch[i] = 0xa5;
    (void)scratch[0];
}

static void cache_terminal_failure(state *s)
{
    native_device *device = (native_device *)s->device;
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device, NULL);
    native_lock(device);
    hold = true;
    native_unlock(device);
    wgpuDevicePushErrorScope(s->device, WGPUErrorFilter_Internal);
    submit(s, encoder);
    pop(s, WGPUErrorType_Internal);
    native_lock(device);
    assert(device->pending);
    hold = false;
    strcpy(mocks[2].completion_message, "original terminal diagnostic");
    mock_result(&mocks[2], 5, INT64_C(4294967297));
    /* The production background settlement path runs under this same lock.
     * Call it deterministically rather than race a timer or mutate a lease. */
    native_progress_pending(device);
    assert(!device->pending && device->retired_failure.status == NATIVE_FAILURE);
    strcpy(mocks[2].completion_message, "later text must not escape");
    native_unlock(device);
}

static void public_error_detail(state *s)
{
    native_device *device = (native_device *)s->device;
    const char *code = "@compute @workgroup_size(1) fn main(){}";
    WGPUShaderSourceWGSL source = {.chain = {.sType = WGPUSType_ShaderSourceWGSL},
        .code = {code, WGPU_STRLEN}};
    WGPUShaderModuleDescriptor module_desc = {.nextInChain = &source.chain};
    WGPUShaderModule module = wgpuDeviceCreateShaderModule(s->device, &module_desc);
    assert(module);
    WGPUComputePipelineDescriptor descriptor = {.compute = {.module = module}};
    exact_message pipeline = {"Metal: injected native failure (native=-71): owned fault boundary", 0};
    native_lock(device);
    mocks[2].fail = MOCK_PIPELINE;
    native_unlock(device);
    wgpuDevicePushErrorScope(s->device, WGPUErrorFilter_Internal);
    assert(!wgpuDeviceCreateComputePipeline(s->device, &descriptor));
    WGPUPopErrorScopeCallbackInfo scope = {.mode = WGPUCallbackMode_AllowSpontaneous,
        .callback = exact_scope, .userdata1 = &pipeline};
    assert(wgpuDevicePopErrorScope(s->device, scope).id && pipeline.calls == 1);
    WGPUCreateComputePipelineAsyncCallbackInfo callback = {.mode = (WGPUCallbackMode)UINT32_MAX,
        .callback = exact_pipeline, .userdata1 = &pipeline};
    wgpuDevicePushErrorScope(s->device, WGPUErrorFilter_Internal);
    assert(!wgpuDeviceCreateComputePipelineAsync(s->device, &descriptor, callback).id);
    assert(pipeline.calls == 1);
    assert(wgpuDevicePopErrorScope(s->device, scope).id && pipeline.calls == 2);
    wgpuInstanceProcessEvents(s->instance);
    assert(pipeline.calls == 2); /* Rejected constructor callback did not fire. */
    callback.mode = WGPUCallbackMode_AllowProcessEvents;
    assert(wgpuDeviceCreateComputePipelineAsync(s->device, &descriptor, callback).id);
    assert(pipeline.calls == 2);
    native_lock(device);
    mocks[2].fail = MOCK_NONE;
    native_unlock(device);
    wgpuShaderModuleRelease(module);
    overwrite_stack();
    wgpuInstanceProcessEvents(s->instance);
    assert(pipeline.calls == 3);

    /* Metal render composition is unsupported here. Its existing validation
     * error must still reach the scope when async callback admission rejects. */
    exact_message render = {"render descriptor, stage interface or capability unsupported", 0};
    WGPURenderPipelineDescriptor render_descriptor = {0};
    WGPUCreateRenderPipelineAsyncCallbackInfo render_callback = {.mode = (WGPUCallbackMode)UINT32_MAX};
    WGPUPopErrorScopeCallbackInfo render_scope = {.mode = WGPUCallbackMode_AllowSpontaneous,
        .callback = exact_validation_scope, .userdata1 = &render};
    wgpuDevicePushErrorScope(s->device, WGPUErrorFilter_Validation);
    assert(!wgpuDeviceCreateRenderPipelineAsync(s->device, &render_descriptor, render_callback).id);
    assert(wgpuDevicePopErrorScope(s->device, render_scope).id && render.calls == 1);
    wgpuInstanceProcessEvents(s->instance);
    assert(render.calls == 1);

    exact_message terminal = {
        "Metal: MTLCommandBuffer.error (native=4294967297): original terminal diagnostic", 0};
    WGPUBuffer target = buffer(s, 8, WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead);
    uint64_t value = 42;
    cache_terminal_failure(s);
    wgpuDevicePushErrorScope(s->device, WGPUErrorFilter_Internal);
    wgpuQueueWriteBuffer(s->queue, target, 0, &value, sizeof(value));
    scope.userdata1 = &terminal;
    assert(wgpuDevicePopErrorScope(s->device, scope).id && terminal.calls == 1);
    wgpuQueueWriteBuffer(s->queue, target, 0, &value, sizeof(value));
    assert(!s->errors); /* Cached terminal failure is consumed once. */

    cache_terminal_failure(s);
    WGPUQueueWorkDoneCallbackInfo queue = {.mode = WGPUCallbackMode_AllowProcessEvents,
        .callback = exact_queue, .userdata1 = &terminal};
    assert(wgpuQueueOnSubmittedWorkDone(s->queue, queue).id);
    assert(terminal.calls == 1);
    overwrite_stack();
    wgpuInstanceProcessEvents(s->instance);
    assert(terminal.calls == 2);

    cache_terminal_failure(s);
    WGPUBufferMapCallbackInfo map = {.mode = WGPUCallbackMode_AllowProcessEvents,
        .callback = exact_map, .userdata1 = &terminal};
    assert(wgpuBufferMapAsync(target, WGPUMapMode_Read, 0, 8, map).id);
    assert(terminal.calls == 2);
    overwrite_stack();
    wgpuInstanceProcessEvents(s->instance);
    assert(terminal.calls == 3 && wgpuBufferGetMapState(target) == WGPUBufferMapState_Unmapped);

    cache_terminal_failure(s);
    queue.mode = (WGPUCallbackMode)UINT32_MAX;
    wgpuDevicePushErrorScope(s->device, WGPUErrorFilter_Internal);
    assert(!wgpuQueueOnSubmittedWorkDone(s->queue, queue).id && terminal.calls == 3);
    assert(wgpuDevicePopErrorScope(s->device, scope).id && terminal.calls == 4);
    wgpuInstanceProcessEvents(s->instance);
    assert(terminal.calls == 4); /* Rejected work-done callback did not also fire. */
    wgpuQueueWriteBuffer(s->queue, target, 0, &value, sizeof(value));
    assert(!s->errors);

    cache_terminal_failure(s);
    map.mode = (WGPUCallbackMode)UINT32_MAX;
    wgpuDevicePushErrorScope(s->device, WGPUErrorFilter_Internal);
    assert(!wgpuBufferMapAsync(target, WGPUMapMode_Read, 0, 8, map).id && terminal.calls == 4);
    assert(wgpuDevicePopErrorScope(s->device, scope).id && terminal.calls == 5);
    assert(wgpuBufferGetMapState(target) == WGPUBufferMapState_Unmapped);
    wgpuInstanceProcessEvents(s->instance);
    assert(terminal.calls == 5); /* Rejected map callback did not also fire. */
    wgpuQueueWriteBuffer(s->queue, target, 0, &value, sizeof(value));
    assert(!s->errors);

    wgpuBufferRelease(target);
    cache_terminal_failure(s);
    wgpuDeviceDestroy(s->device);
    queue.mode = WGPUCallbackMode_AllowProcessEvents;
    assert(wgpuQueueOnSubmittedWorkDone(s->queue, queue).id);
    overwrite_stack();
    wgpuInstanceProcessEvents(s->instance);
    assert(terminal.calls == 6); /* Destroy must not consume an unobserved failure. */
}

static void host_replay_program(state *s)
{
    native_device *device = (native_device *)s->device;
    const unsigned baseline = mocks[2].live;
    WGPUComputePipeline compute = pipeline(s,
        "@group(0) @binding(0) var<storage,read_write> data:array<u32>;"
        "@compute @workgroup_size(1) fn main(){data[0]=data[0]+3u;}");
    WGPUBuffer data = buffer(s, 16, WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst);
    native_buffer *payload = doe_resource_buffer_native((doe_resource_buffer *)data);
    void *identity = doe_mtl_buffer_native(payload->handle.metal);
    WGPUBindGroupEntry entry = {.binding = 0, .buffer = data, .size = 16};
    WGPUBindGroup bindings = group(s, compute, &entry, 1);
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device, NULL);
    wgpuCommandEncoderClearBuffer(encoder, data, 0, 16);
    dispatch(encoder, compute, bindings);
    WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, NULL);
    wgpuCommandEncoderRelease(encoder);
    assert(commands);
    wgpuDevicePushErrorScope(s->device, WGPUErrorFilter_Validation);
    assert(!doeNativeComputeProgramPrepare(s->queue, commands)); /* GPU recording remains Vulkan-only. */
    pop(s, WGPUErrorType_Validation);
    void *program = doeNativeComputeProgramPrepareHostReplay(s->queue, commands);
    assert(program);
    wgpuCommandBufferRelease(commands);
    wgpuBindGroupRelease(bindings);
    wgpuComputePipelineRelease(compute);
    wgpuBufferRelease(data); /* The program's resource claim owns the complete packet. */
    const unsigned live = mocks[2].live, commits = mocks[2].commits;
    const mock_operation failures[2] = {MOCK_COMMAND, MOCK_FILL};
    exact_message failure = {"Metal: injected native failure (native=-71): owned fault boundary", 0};
    WGPUPopErrorScopeCallbackInfo scope = {.mode = WGPUCallbackMode_AllowSpontaneous,
        .callback = exact_scope, .userdata1 = &failure};
    for (size_t i = 0; i < 2; ++i) {
        native_lock(device);
        mocks[2].fail = failures[i];
        native_unlock(device);
        wgpuDevicePushErrorScope(s->device, WGPUErrorFilter_Internal);
        assert(!doeNativeComputeProgramSubmit(program));
        assert(wgpuDevicePopErrorScope(s->device, scope).id && failure.calls == i + 1);
        assert(mocks[2].live == live && mocks[2].commits == commits && !device->pending);
    }
    native_lock(device);
    mocks[2].fail = MOCK_NONE;
    native_unlock(device);
    const unsigned dispatches = mocks[2].dispatches, fills = mocks[2].fills;
    assert(doeNativeComputeProgramSubmit(program) && doeNativeComputeProgramSubmit(program));
    assert(mocks[2].dispatches == dispatches + 2 && mocks[2].fills == fills + 2);
    assert(mocks[2].commits == commits + 2 && mocks[2].bindings[0].buffer == identity);
    assert(mocks[2].bindings[0].size == 16 && !device->pending);
    native_lock(device);
    hold = true;
    native_unlock(device);
    wgpuDevicePushErrorScope(s->device, WGPUErrorFilter_Internal);
    assert(!doeNativeComputeProgramSubmit(program));
    pop(s, WGPUErrorType_Internal);
    assert(device->pending);
    const unsigned released = mocks[2].released;
    doeNativeComputeProgramRelease(program);
    assert(device->pending && mocks[2].released == released); /* Replay holds its own packets/native command. */
    native_lock(device);
    hold = false;
    native_unlock(device);
    unsigned completed = 0;
    WGPUQueueWorkDoneCallbackInfo done_info = {.mode = WGPUCallbackMode_AllowSpontaneous,
        .callback = done, .userdata1 = &completed};
    assert(wgpuQueueOnSubmittedWorkDone(s->queue, done_info).id && completed == 1 && !device->pending);
    assert(mocks[2].live == baseline && !s->errors);
}

int main(void)
{
    state s = {0};
    s.instance = wgpuCreateInstance(NULL);
    WGPURequestAdapterOptions options = WGPU_REQUEST_ADAPTER_OPTIONS_INIT;
    options.backendType = WGPUBackendType_Metal;
    WGPURequestAdapterCallbackInfo adapter = {.mode=WGPUCallbackMode_AllowSpontaneous,.callback=adapter_reply,.userdata1=&s};
    assert(wgpuInstanceRequestAdapter(s.instance,&options,adapter).id && s.adapter);
    WGPUAdapterInfo info = WGPU_ADAPTER_INFO_INIT;
    assert(wgpuAdapterGetInfo(s.adapter,&info) == WGPUStatus_Success && info.backendType == WGPUBackendType_Metal);
    assert(info.adapterType == WGPUAdapterType_Unknown);
    wgpuAdapterInfoFreeMembers(info);
    WGPULimits limits = WGPU_LIMITS_INIT;
    assert(wgpuAdapterGetLimits(s.adapter,&limits) == WGPUStatus_Success && limits.maxBufferSize == 4096);
    assert(!wgpuAdapterHasFeature(s.adapter,WGPUFeatureName_TimestampQuery));
    unsigned rejected = 0;
    changed_default = true;
    WGPURequestDeviceCallbackInfo rejection = {.mode=WGPUCallbackMode_AllowSpontaneous,.callback=failed_device,.userdata1=&rejected};
    assert(wgpuAdapterRequestDevice(s.adapter,NULL,rejection).id && rejected == 1);
    mock_assert_closed(&mocks[1]);
    changed_default = false;
    WGPUDeviceDescriptor descriptor = WGPU_DEVICE_DESCRIPTOR_INIT;
    descriptor.uncapturedErrorCallbackInfo = (WGPUUncapturedErrorCallbackInfo){.callback=error_reply,.userdata1=&s};
    WGPURequestDeviceCallbackInfo requested = {.mode=WGPUCallbackMode_AllowSpontaneous,.callback=device_reply,.userdata1=&s};
    assert(wgpuAdapterRequestDevice(s.adapter,&descriptor,requested).id && s.device);
    s.queue = wgpuDeviceGetQueue(s.device);
    native_device *device = (native_device *)s.device;
    assert(device->backend.kind == WGPUBackendType_Metal);
    compute_binding_ledger(&s);
    reject_spirv_fallback(&s);
    WGPUBuffer source = buffer(&s,16,WGPUBufferUsage_CopySrc|WGPUBufferUsage_CopyDst);
    WGPUBuffer target = buffer(&s,16,WGPUBufferUsage_MapRead|WGPUBufferUsage_CopyDst);
    uint32_t input[4] = {11,23,37,51};
    wgpuQueueWriteBuffer(s.queue,source,0,input,sizeof(input));
    native_buffer *source_payload = doe_resource_buffer_native((doe_resource_buffer *)source);
    void *source_identity = doe_mtl_buffer_native(source_payload->handle.metal);
    uint32_t host_written[4] = {0};
    assert(native_buffer_read(source_payload,0,host_written,sizeof(host_written)) == NATIVE_OK);
    assert(memcmp(host_written,input,sizeof(input)) == 0);
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s.device,NULL);
    wgpuCommandEncoderCopyBufferToBuffer(encoder,source,0,target,0,16);
    wgpuCommandEncoderClearBuffer(encoder,target,8,4);
    wgpuBufferRelease(source);
    submit(&s,encoder);
    WGPUBufferMapCallbackInfo map = {.mode=WGPUCallbackMode_AllowSpontaneous,.callback=mapped,.userdata1=&s};
    assert(wgpuBufferMapAsync(target,WGPUMapMode_Read,0,16,map).id && s.mapped == 1);
    uint32_t actual[4] = {0};
    assert(wgpuBufferReadMappedRange(target,0,actual,sizeof(actual)) == WGPUStatus_Success);
    /* The retained mock deliberately records copy/fill calls without executing
     * them. Verify translation identity, not an invented GPU readback oracle. */
    assert(mocks[2].last_source == source_identity && mocks[2].last_source_offset == 0);
    assert(mocks[2].last_destination_offset == 8 && mocks[2].last_length == 4 && mocks[2].last_fill == 0);
    assert(actual[0] == 0 && actual[1] == 0 && actual[2] == 0 && actual[3] == 0);
    wgpuBufferUnmap(target);
    WGPUBufferDescriptor bad = {.size=4,.usage=WGPUBufferUsage_CopyDst};
    buffer_failure = DOE_MTL_OUT_OF_MEMORY;
    wgpuDevicePushErrorScope(s.device,WGPUErrorFilter_OutOfMemory);
    assert(!wgpuDeviceCreateBuffer(s.device,&bad));
    pop(&s,WGPUErrorType_OutOfMemory);
    buffer_failure = DOE_MTL_NATIVE_FAILURE;
    wgpuDevicePushErrorScope(s.device,WGPUErrorFilter_Internal);
    assert(!wgpuDeviceCreateBuffer(s.device,&bad));
    pop(&s,WGPUErrorType_Internal);
    buffer_failure = DOE_MTL_OK;
    encoder = wgpuDeviceCreateCommandEncoder(s.device,NULL);
    wgpuCommandEncoderClearBuffer(encoder,target,0,16);
    hold = true;
    wgpuDevicePushErrorScope(s.device,WGPUErrorFilter_Internal);
    submit(&s,encoder);
    pop(&s,WGPUErrorType_Internal);
    assert(device->pending);
    wgpuDevicePushErrorScope(s.device,WGPUErrorFilter_Internal);
    wgpuQueueWriteBuffer(s.queue,target,0,input,sizeof(input));
    pop(&s,WGPUErrorType_Internal);
    unsigned completed = 0;
    WGPUQueueWorkDoneCallbackInfo completion = {.mode=WGPUCallbackMode_AllowSpontaneous,.callback=done,.userdata1=&completed};
    wgpuDevicePushErrorScope(s.device,WGPUErrorFilter_Internal);
    assert(!wgpuQueueOnSubmittedWorkDone(s.queue,completion).id && !completed);
    pop(&s,WGPUErrorType_Internal);
    wgpuBufferRelease(target);
    assert(device->pending);
    hold = false;
    assert(wgpuQueueOnSubmittedWorkDone(s.queue,completion).id && completed == 1 && !device->pending);
    assert(mocks[2].copies == 1 && mocks[2].fills == 2);
    host_replay_program(&s);
    public_error_detail(&s);
    consumer_finish(&s);
    for (unsigned i = 0; i < context_count; ++i) mock_assert_closed(&mocks[i]);
    puts("PASS public Metal host composition: exact backend/adapter identity, compiler-owned MSL entry/slots/sizes, shared buffer copy/fill/map, typed failures, early releases and unknown queue retention; call ledger only, not GPU evidence");
}
