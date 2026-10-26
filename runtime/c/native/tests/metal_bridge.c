/* SPDX-License-Identifier: MIT
 * Host call-ledger composition only. Existing Metal mock boundary supplies no
 * shader interpreter or GPU result. Public handles, owners and queue are real. */
#include "../src/internal.h"
#include "../src/metal_binding_internal.h"
#include "../../backends/metal/tests/mock_native.h"
#include "consumer.h"
#include "metal_graphics_cases.h"
#include "metal_copy_cases.h"
#include "metal_view_cases.h"
#include "metal_f16_cases.h"
#include "sampling_pair_cases.h"
#include "pipeline_cache_cases.h"
#include "doe_native_programs.h"

static mock_native mocks[8];
static doe_mtl_native external_drivers[8];
static doe_mtl_status external_import_ledger(void *, void *, doe_mtl_external_source, void *,
    doe_mtl_native_external *, doe_mtl_error *);
static doe_mtl_context *contexts[8];
static doe_mtl_commands *commands[8];
static unsigned context_count;
static unsigned physical_identity[2];
static bool changed_default, hold;
static doe_mtl_status buffer_failure = DOE_MTL_OK;
static unsigned vulkan_format_queries;

/* A Metal composition must never cross the Vulkan native-format boundary. */
doe_vk_status __wrap_doe_vk_texture_format_caps(doe_vk_context *context,
    doe_vk_texture_format format, doe_vk_format_caps *out)
{
    (void)context;
    (void)format;
    (void)out;
    ++vulkan_format_queries;
    return DOE_VK_NATIVE_FAILURE;
}

doe_mtl_status __real_doe_mtl_commands_wait(doe_mtl_commands *, doe_mtl_error *);
doe_mtl_status __real_doe_mtl_commands_create(doe_mtl_context *, doe_mtl_commands **, doe_mtl_error *);
doe_mtl_status __real_doe_mtl_buffer_create(doe_mtl_context *, uint64_t, doe_mtl_memory,
    doe_mtl_buffer **, doe_mtl_error *);

doe_mtl_status __wrap_doe_mtl_context_create(doe_mtl_context **out, doe_mtl_error *error)
{
    assert(context_count < 8);
    unsigned index = context_count++;
    doe_mtl_native driver = mock_driver(&mocks[index]);
    external_drivers[index] = driver;
    driver.external_texture = external_import_ledger;
    mocks[index].etc2 = mocks[index].astc = mocks[index].astc_sliced_3d = true;
    doe_mtl_status status = doe_mtl_context_open(driver, out, error);
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
    (void)unused;
    if (status != WGPUPopErrorScopeStatus_Success || type != *(WGPUErrorType *)userdata)
        fprintf(stderr,"Metal scope status=%u type=%u expected=%u: %.*s\n",
            (unsigned)status,(unsigned)type,(unsigned)*(WGPUErrorType *)userdata,
            (int)(!message.data ? 0 : message.length == WGPU_STRLEN ? strlen(message.data) : message.length),
            message.data ? message.data : "");
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

static void atomic_pipeline_admission(state *s)
{
    const char *source = "@group(0) @binding(0) var<storage,read_write> data:array<atomic<u32>>;"
        "@compute @workgroup_size(1) fn main(){atomicStore(&data[0],7u);}";
    WGPUShaderSourceWGSL wgsl = {.chain={.sType=WGPUSType_ShaderSourceWGSL},.code={source,WGPU_STRLEN}};
    WGPUShaderModuleDescriptor descriptor = {.nextInChain=&wgsl.chain};
    unsigned created = mocks[2].created, released = mocks[2].released, live = mocks[2].live;
    wgpuDevicePushErrorScope(s->device,WGPUErrorFilter_Validation);
    WGPUShaderModule module = wgpuDeviceCreateShaderModule(s->device,&descriptor);
    assert(module);
    native_shader *payload = doe_resource_shader_native((doe_resource_shader *)module);
    assert(payload->module);
    native_stage checked = {0};
    native_pipeline_error stage_error = {0};
    assert(native_stage_load((native_device *)s->device,module,(WGPUStringView){"main",4},
        DOE_COMPILER_STAGE_COMPUTE,NULL,0,&checked,&stage_error));
    doe_shader_view view = checked.view;
    assert(view.target == DOE_SHADER_METAL && view.text && view.text_size && !view.words && !view.counts.words);
    assert(view.metal_entry_count == 1 && view.metal_binding_count == 1);
    static const char operation[] = "atomic_store_explicit(target";
    bool found = false;
    for (size_t i = 0; i + sizeof(operation) - 1 <= view.text_size; ++i)
        if (!memcmp(view.text + i,operation,sizeof(operation) - 1)) found = true;
    assert(found);
    native_stage_release(&checked);
    WGPUComputePipelineDescriptor compute = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
    compute.compute.module = module;
    compute.compute.entryPoint = (WGPUStringView){"main",4};
    WGPUComputePipeline pipeline = wgpuDeviceCreateComputePipeline(s->device,&compute);
    pop(s,WGPUErrorType_NoError);
    assert(pipeline && !strcmp(mocks[2].entrypoint,"main"));
    native_pipeline *native = doe_resource_pipeline_native((doe_resource_pipeline *)pipeline);
    assert(native->handle.metal && native->metal_compute);
    assert(mocks[2].created == created + 1 && mocks[2].live == live + 1);
    wgpuShaderModuleRelease(module); /* Pipeline keeps its required source owners. */
    wgpuComputePipelineRelease(pipeline);
    assert(mocks[2].released == released + 1 && mocks[2].live == live);

    /* Wrong atomic destination is still a genuine semantic rejection, never a
     * fallback to another target or a call into the native pipeline boundary. */
    wgsl.code = (WGPUStringView){"@group(0) @binding(0) var<storage,read_write> data:array<u32>;"
        "@compute @workgroup_size(1) fn main(){atomicStore(&data[0],7u);}",WGPU_STRLEN};
    wgpuDevicePushErrorScope(s->device,WGPUErrorFilter_Validation);
    module = wgpuDeviceCreateShaderModule(s->device,&descriptor);
    pop(s,WGPUErrorType_Validation);
    assert(module);
    payload = doe_resource_shader_native((doe_resource_shader *)module);
    assert(!payload->module && payload->error.diagnostic.message[0]);
    compute.compute.module = module;
    wgpuDevicePushErrorScope(s->device,WGPUErrorFilter_Validation);
    assert(!wgpuDeviceCreateComputePipeline(s->device,&compute));
    pop(s,WGPUErrorType_Validation);
    assert(mocks[2].created == created + 1 && mocks[2].live == live);
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
    native_stage selected = {0};
    native_pipeline_error stage_error = {0};
    assert(native_stage_load((native_device *)s->device, module, descriptor.compute.entryPoint,
        DOE_COMPILER_STAGE_COMPUTE,NULL,0,&selected,&stage_error));
    const DoeCompilerMetalEntryV12 *selected_entry = NULL;
    for (size_t i = 0; i < selected.view.metal_entry_count; ++i)
        if (selected.view.metal_entries[i].entry_index == (size_t)(selected.entry - selected.view.entries))
            selected_entry = &selected.view.metal_entries[i];
    assert(selected_entry);
    uint32_t output_slot = UINT32_MAX, input_slot = UINT32_MAX, scale_slot = UINT32_MAX;
    for (size_t i = 0; i < selected_entry->binding_count; ++i) {
        const DoeCompilerMetalBindingV12 *binding = &selected.view.metal_bindings[selected_entry->first_binding + i];
        const DoeCompilerResourceV4 *resource = &selected.view.resources[binding->resource_index];
        if (resource->group == 1 && resource->binding == 7) output_slot = binding->slot;
        else if (resource->group == 0 && resource->binding == 4) input_slot = binding->slot;
        else if (resource->group == 0 && resource->binding == 2) scale_slot = binding->slot;
    }
    uint32_t sizes_slot = selected_entry->sizes_slot;
    assert(output_slot < 3 && input_slot < 3 && scale_slot < 3 && sizes_slot < DOE_MTL_BUFFER_SLOTS);
    /* Selected-entry records are independent of their siblings. Feed copied
     * well-typed wrong rows to the shared compute/graphics converter, never
     * mutate an immutable shader allocation or reinterpret legacy ABI10 rows. */
    DoeCompilerMetalBindingV12 rows[3];
    assert(selected_entry->binding_count == 3);
    memcpy(rows, selected.view.metal_bindings + selected_entry->first_binding, sizeof(rows));
    DoeCompilerMetalEntryV12 entry_copy = *selected_entry;
    entry_copy.first_binding = 0;
    native_stage altered = selected;
    altered.view.metal_entries = &entry_copy;
    altered.view.metal_entry_count = 1;
    altered.view.metal_bindings = rows;
    altered.view.metal_binding_count = 3;
    native_metal_stage_map mapping;
    doe_mtl_shader_stage native;
    native_pipeline *native_payload = doe_resource_pipeline_native((doe_resource_pipeline *)pipeline);
    assert(native_metal_stage_map_build(native_payload, &altered, DOE_COMPILER_STAGE_COMPUTE, &mapping, &native));
    DoeCompilerMetalBindingV12 original = rows[1];
    rows[1].entry_index ^= 1;
    assert(!native_metal_stage_map_build(native_payload, &altered, DOE_COMPILER_STAGE_COMPUTE, &mapping, &native));
    rows[1] = original;
    rows[1].table = DOE_COMPILER_TEXT_TABLE_SAMPLER;
    assert(!native_metal_stage_map_build(native_payload, &altered, DOE_COMPILER_STAGE_COMPUTE, &mapping, &native));
    rows[1] = original;
    rows[1].slot = rows[0].slot;
    if (rows[1].size_index != UINT32_MAX) rows[1].size_index = rows[1].slot;
    assert(!native_metal_stage_map_build(native_payload, &altered, DOE_COMPILER_STAGE_COMPUTE, &mapping, &native));
    rows[1] = original;
    entry_copy.first_binding = UINT32_MAX;
    assert(!native_metal_stage_map_build(native_payload, &altered, DOE_COMPILER_STAGE_COMPUTE, &mapping, &native));
    entry_copy.first_binding = 0;
    size_t static_row = 3;
    for (size_t i = 0; i < 3; ++i) {
        if (!(selected.view.resources[rows[i].resource_index].flags & DOE_COMPILER_RESOURCE_RUNTIME_TAIL)) {
            static_row = i;
            continue;
        }
        uint32_t index = rows[i].size_index;
        rows[i].size_index = UINT32_MAX;
        assert(!native_metal_stage_map_build(native_payload, &altered, DOE_COMPILER_STAGE_COMPUTE, &mapping, &native));
        rows[i].size_index = index;
    }
    assert(static_row < 3);
    rows[static_row].size_index = rows[static_row].slot;
    assert(!native_metal_stage_map_build(native_payload, &altered, DOE_COMPILER_STAGE_COMPUTE, &mapping, &native));
    rows[static_row].size_index = UINT32_MAX;
    entry_copy.sizes_slot = DOE_COMPILER_METAL_SIZES_SLOT - 1; /* Unoccupied, but not the ABI slot. */
    assert(!native_metal_stage_map_build(native_payload, &altered, DOE_COMPILER_STAGE_COMPUTE, &mapping, &native));
    entry_copy.sizes_slot = selected_entry->sizes_slot;
    entry_copy.sizes_bytes += sizeof(uint32_t);
    assert(!native_metal_stage_map_build(native_payload, &altered, DOE_COMPILER_STAGE_COMPUTE, &mapping, &native));
    entry_copy.sizes_bytes = selected_entry->sizes_bytes - sizeof(uint32_t);
    assert(!native_metal_stage_map_build(native_payload, &altered, DOE_COMPILER_STAGE_COMPUTE, &mapping, &native));
    entry_copy.sizes_bytes = selected_entry->sizes_bytes;
    for (size_t i = 0; i < 3; ++i) rows[i].size_index = UINT32_MAX;
    entry_copy.sizes_slot = UINT32_MAX;
    entry_copy.sizes_bytes = 0;
    assert(!native_metal_stage_map_build(native_payload, &altered, DOE_COMPILER_STAGE_COMPUTE, &mapping, &native));
    memcpy(rows, selected.view.metal_bindings + selected_entry->first_binding, sizeof(rows));
    entry_copy.sizes_slot = selected_entry->sizes_slot;
    entry_copy.sizes_bytes = selected_entry->sizes_bytes;
    assert(native_metal_stage_map_build(native_payload, &altered, DOE_COMPILER_STAGE_COMPUTE, &mapping, &native));
    native_stage_release(&selected);
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
    assert(mocks[2].dispatches == 1 && mocks[2].sizes_slot == sizes_slot);
    assert(mocks[2].bindings[scale_slot].buffer == scale_identity && mocks[2].bindings[scale_slot].size == 16);
    assert(mocks[2].bindings[input_slot].buffer == input_identity && mocks[2].bindings[input_slot].offset == 256 && mocks[2].bindings[input_slot].size == 32);
    assert(mocks[2].bindings[output_slot].buffer == output_identity && mocks[2].bindings[output_slot].offset == 256 && mocks[2].bindings[output_slot].size == 48);
    assert(!mocks[2].bindings[3].buffer && !mocks[2].bindings[7].buffer);
    assert(mocks[2].groups.x == 3 && mocks[2].groups.y == 2 && mocks[2].groups.z == 1);
    assert(mocks[2].workgroup.x == 4 && mocks[2].workgroup.y == 1 && mocks[2].repeat == 1);
}

typedef struct exact_message {
    const char *text;
    unsigned calls;
} exact_message;

static void require_message(WGPUStringView message, exact_message *expected)
{
    if (message.length != strlen(expected->text) || memcmp(message.data,expected->text,message.length))
        fprintf(stderr,"expected [%s], received [%.*s]\n",expected->text,(int)message.length,message.data);
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

    /* Invalid render construction still delivers validation when callback admission rejects. */
    exact_message render = {"pipeline stage requires a shader from this device", 0};
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


#include "metal_mixed_host.h"

static void queue_upload_admission(state *s)
{
    native_device *device = (native_device *)s->device;
    unsigned baseline = mocks[2].live;
    WGPUTextureDescriptor descriptor = WGPU_TEXTURE_DESCRIPTOR_INIT;
    descriptor.size = (WGPUExtent3D){1,2,1};
    descriptor.format = WGPUTextureFormat_RGBA8Unorm;
    descriptor.usage = WGPUTextureUsage_CopyDst;
    WGPUTexture image = wgpuDeviceCreateTexture(s->device, &descriptor);
    assert(image);
    unsigned live = mocks[2].live;
    /* Isolate the public admission bound in this host-owned fixture. The real
     * mock backend allocation cap remains4096; no public mutation API exists. */
    native_lock(device);
    uint64_t original = device->backend.limits.maxBufferSize;
    device->backend.limits.maxBufferSize = 256;
    native_unlock(device);
    assert(native_allocation_bound(device) == device->backend.value.metal.caps.max_buffer_length);
    WGPUBufferDescriptor public_buffer = {.size=512, .usage=WGPUBufferUsage_CopyDst};
    wgpuDevicePushErrorScope(s->device, WGPUErrorFilter_Validation);
    assert(!wgpuDeviceCreateBuffer(s->device, &public_buffer));
    pop(s, WGPUErrorType_Validation);
    /* A non-attachment texture's private zero staging also uses the actual
     * native allocation cap, independently of this smaller public buffer limit. */
    WGPUTextureDescriptor sampled = descriptor;
    sampled.format = WGPUTextureFormat_RGB9E5Ufloat;
    sampled.usage = WGPUTextureUsage_TextureBinding;
    WGPUTexture sampled_image = wgpuDeviceCreateTexture(s->device, &sampled);
    assert(sampled_image);
    wgpuTextureRelease(sampled_image);
    assert(mocks[2].live == live && !s->errors);
    const unsigned char data[12] = {0xa5,0x5a,0xf0,17,31,63,127,0xc3,192,128,64,8};
    WGPUTexelCopyTextureInfo destination = {.texture=image};
    WGPUTexelCopyBufferLayout layout = {.offset=3,.bytesPerRow=5,.rowsPerImage=2};
    WGPUExtent3D extent = {1,2,1};
    unsigned uploads = mocks[2].uploads;
    wgpuQueueWriteTexture(s->queue, &destination, data, sizeof(data), &layout, &extent);
    assert(mocks[2].uploads == uploads + 1 && mocks[2].live == live && !s->errors);
    assert(mocks[2].image_to.buffer_offset == 0 && mocks[2].image_to.bytes_per_row == 256 &&
        mocks[2].image_to.bytes_per_image == 512 && mocks[2].image_to.region.height == 2);
    buffer_failure = DOE_MTL_OUT_OF_MEMORY;
    wgpuDevicePushErrorScope(s->device, WGPUErrorFilter_OutOfMemory);
    wgpuQueueWriteTexture(s->queue, &destination, data, sizeof(data), &layout, &extent);
    pop(s, WGPUErrorType_OutOfMemory);
    buffer_failure = DOE_MTL_OK;
    assert(mocks[2].uploads == uploads + 1 && mocks[2].live == live);
    mocks[2].fail = MOCK_UPLOAD_IMAGE;
    exact_message expected = {"Metal: injected native failure (native=-71): owned fault boundary", 0};
    WGPUPopErrorScopeCallbackInfo scope = {.mode=WGPUCallbackMode_AllowProcessEvents,
        .callback=exact_scope,.userdata1=&expected};
    wgpuDevicePushErrorScope(s->device, WGPUErrorFilter_Internal);
    wgpuQueueWriteTexture(s->queue, &destination, data, sizeof(data), &layout, &extent);
    mocks[2].fail = MOCK_NONE;
    assert(wgpuDevicePopErrorScope(s->device, scope).id);
    overwrite_stack();
    wgpuInstanceProcessEvents(s->instance);
    assert(expected.calls == 1 && mocks[2].live == live);
    wgpuQueueWriteTexture(s->queue, &destination, data, sizeof(data), &layout, &extent);
    assert(mocks[2].uploads == uploads + 2 && mocks[2].live == live && !s->errors);
    native_lock(device);
    device->backend.limits.maxBufferSize = original;
    native_unlock(device);
    wgpuTextureRelease(image);
    assert(mocks[2].live == baseline);
    /* A valid texture write beyond the actual native allocation cap is OOM,
     * before either CPU staging or a native buffer/command can be published. */
    descriptor.size.height = 17;
    image = wgpuDeviceCreateTexture(s->device, &descriptor);
    assert(image);
    unsigned created = mocks[2].created;
    unsigned char larger[68] = {0};
    destination.texture = image;
    layout = (WGPUTexelCopyBufferLayout){.bytesPerRow=4,.rowsPerImage=17};
    extent.height = 17;
    wgpuDevicePushErrorScope(s->device, WGPUErrorFilter_OutOfMemory);
    wgpuQueueWriteTexture(s->queue, &destination, larger, sizeof(larger), &layout, &extent);
    pop(s, WGPUErrorType_OutOfMemory);
    assert(mocks[2].created == created && !device->pending);
    wgpuTextureRelease(image);
    assert(mocks[2].live == baseline && !s->errors);
}

static void required_metal_limits(state *s)
{
    WGPULimits supported = WGPU_LIMITS_INIT;
    assert(wgpuAdapterGetLimits(s->adapter, &supported) == WGPUStatus_Success);
    WGPULimits required = WGPU_LIMITS_INIT;
    required.maxUniformBuffersPerShaderStage = supported.maxUniformBuffersPerShaderStage + 1;
    WGPUDeviceDescriptor descriptor = WGPU_DEVICE_DESCRIPTOR_INIT;
    descriptor.requiredLimits = &required;
    unsigned rejected = 0, opened = context_count;
    WGPURequestDeviceCallbackInfo rejection = {.mode=WGPUCallbackMode_AllowSpontaneous,
        .callback=failed_device, .userdata1=&rejected};
    assert(wgpuAdapterRequestDevice(s->adapter, &descriptor, rejection).id && rejected == 1);
    assert(context_count == opened); /* Unsupported limits reject before native opening. */
    required = WGPU_LIMITS_INIT;
    required.maxBindGroupsPlusVertexBuffers = 24; /* WebGPU default combined budget. */
    state limited = {.instance=s->instance, .adapter=s->adapter};
    descriptor.uncapturedErrorCallbackInfo = (WGPUUncapturedErrorCallbackInfo){.callback=error_reply, .userdata1=&limited};
    WGPURequestDeviceCallbackInfo requested = {.mode=WGPUCallbackMode_AllowSpontaneous,
        .callback=device_reply, .userdata1=&limited};
    assert(wgpuAdapterRequestDevice(s->adapter, &descriptor, requested).id && limited.device);
    assert(context_count == opened + 1);
    metal_f16_disabled(&limited);
    metal_extended_copy_disabled(&limited);
    metal_swizzle_case(&limited, &mocks[context_count - 1], false);
    WGPULimits resolved = WGPU_LIMITS_INIT;
    assert(wgpuDeviceGetLimits(limited.device, &resolved) == WGPUStatus_Success);
    assert(resolved.maxBindGroupsPlusVertexBuffers == required.maxBindGroupsPlusVertexBuffers);
    assert(resolved.maxUniformBuffersPerShaderStage <= supported.maxUniformBuffersPerShaderStage);
    /* Reported resolution is enforced by actual public slot admission, not a
     * second literal limit table. Exactly the device's uniform count succeeds;
     * the next descriptor slot fails without allocating a native GPU object. */
    WGPUBindGroupLayoutEntry entries[32] = {0};
    size_t allowed = resolved.maxUniformBuffersPerShaderStage;
    assert(allowed && allowed < sizeof(entries) / sizeof(entries[0]));
    for (size_t i = 0; i <= allowed; ++i)
        entries[i] = (WGPUBindGroupLayoutEntry){.binding=(uint32_t)i, .visibility=WGPUShaderStage_Fragment,
            .buffer={.type=WGPUBufferBindingType_Uniform, .minBindingSize=4}};
    WGPUBindGroupLayoutDescriptor layout_descriptor = {.entryCount=allowed, .entries=entries};
    unsigned created = mocks[opened].created;
    WGPUBindGroupLayout layout = wgpuDeviceCreateBindGroupLayout(limited.device, &layout_descriptor);
    assert(layout);
    wgpuBindGroupLayoutRelease(layout);
    ++layout_descriptor.entryCount;
    wgpuDevicePushErrorScope(limited.device, WGPUErrorFilter_Validation);
    assert(!wgpuDeviceCreateBindGroupLayout(limited.device, &layout_descriptor));
    pop(&limited, WGPUErrorType_Validation);
    assert(mocks[opened].created == created && !limited.errors);
    wgpuDeviceRelease(limited.device);
    mock_assert_closed(&mocks[opened]);
    /* Retired test contexts must not steal a reused command address from the
     * active device when the wait wrapper resolves its call ledger. */
    commands[opened] = NULL;
    contexts[opened] = NULL;
}

static void sampled_float_admission(state *s)
{
    unsigned baseline = mocks[2].live;
    assert(!wgpuDeviceHasFeature(s->device, WGPUFeatureName_Float32Filterable));
    const WGPUTextureFormat formats[2] = {WGPUTextureFormat_RGBA8Unorm, WGPUTextureFormat_R32Float};
    WGPUTexture images[2];
    WGPUTextureView views[2];
    for (size_t i = 0; i < 2; ++i) {
        images[i] = texture(s, formats[i], WGPUTextureUsage_TextureBinding, 1);
        views[i] = wgpuTextureCreateView(images[i], NULL);
        assert(views[i]);
    }
    WGPUBindGroupLayoutEntry entry = {.binding=3, .visibility=WGPUShaderStage_Fragment,
        .texture={.sampleType=WGPUTextureSampleType_Float, .viewDimension=WGPUTextureViewDimension_2D}};
    WGPUBindGroupLayoutDescriptor layout_descriptor = {.entryCount=1, .entries=&entry};
    WGPUBindGroupLayout layout = wgpuDeviceCreateBindGroupLayout(s->device, &layout_descriptor);
    assert(layout);
    WGPUBindGroupEntry binding = {.binding=3, .textureView=views[0]};
    WGPUBindGroupDescriptor descriptor = {.layout=layout, .entryCount=1, .entries=&binding};
    WGPUBindGroup group = wgpuDeviceCreateBindGroup(s->device, &descriptor);
    assert(group && vulkan_format_queries == 0);
    const doe_resource_binding *admitted = NULL;
    size_t count = 0;
    admitted = doe_resource_bind_group_bindings((doe_resource_bind_group *)group, &count);
    assert(count == 1 && admitted[0].kind == DOE_BIND_TEXTURE &&
        admitted[0].resource.view == (doe_resource_view *)views[0]);
    wgpuBindGroupRelease(group);
    binding.textureView = views[1];
    wgpuDevicePushErrorScope(s->device, WGPUErrorFilter_Validation);
    assert(!wgpuDeviceCreateBindGroup(s->device, &descriptor));
    pop(s, WGPUErrorType_Validation);
    assert(vulkan_format_queries == 0);
    /* Rejecting Float does not ban an ordinary unfilterable R32 view. */
    wgpuBindGroupLayoutRelease(layout);
    entry.texture.sampleType = WGPUTextureSampleType_UnfilterableFloat;
    layout = wgpuDeviceCreateBindGroupLayout(s->device, &layout_descriptor);
    assert(layout);
    descriptor.layout = layout;
    group = wgpuDeviceCreateBindGroup(s->device, &descriptor);
    assert(group && vulkan_format_queries == 0);
    wgpuBindGroupRelease(group);
    wgpuBindGroupLayoutRelease(layout);
    for (size_t i = 0; i < 2; ++i) {
        wgpuTextureViewRelease(views[i]);
        wgpuTextureRelease(images[i]);
    }
    assert(mocks[2].live == baseline && !s->errors);
}

#include "metal_storage_cases.h"

#include "metal_vertex_cases.h"
#include "metal_vertex_span_cases.h"

static void graphics_ledger(state *s)
{
    (void)metal_graphics_pixels; /* Pixel oracle is exercised only on native hardware. */
    native_device *device = (native_device *)s->device;
    unsigned baseline = mocks[2].live;
    mocks[2].fail = MOCK_RENDER;
    exact_message expected = {"Metal: injected native failure (native=-71): owned fault boundary", 0};
    WGPUPopErrorScopeCallbackInfo scope = {.mode=WGPUCallbackMode_AllowProcessEvents,
        .callback=exact_scope, .userdata1=&expected};
    wgpuDevicePushErrorScope(s->device, WGPUErrorFilter_Internal);
    assert(!metal_image(s));
    mocks[2].fail = MOCK_NONE;
    assert(wgpuDevicePopErrorScope(s->device, scope).id);
    overwrite_stack();
    wgpuInstanceProcessEvents(s->instance);
    assert(expected.calls == 1 && mocks[2].live == baseline);
    /* Initialization unknown cannot publish a public texture. Its private
     * allocation stays alive until the ordinary pending owner proves terminal. */
    native_lock(device);
    hold = true;
    native_unlock(device);
    wgpuDevicePushErrorScope(s->device, WGPUErrorFilter_Internal);
    assert(!metal_image(s));
    pop(s, WGPUErrorType_Internal);
    native_lock(device);
    assert(device->pending && mocks[2].live > baseline);
    hold = false;
    native_queue_settlement settlement = native_settle_pending(device);
    assert(settlement.complete && !device->pending && mocks[2].live == baseline);
    native_unlock(device);
    WGPURenderPipeline pipeline = metal_triangle_pipeline(s);
    WGPUTexture image = metal_image(s);
    WGPUTextureView view = wgpuTextureCreateView(image, NULL);
    assert(pipeline && image && view);
    unsigned renders = mocks[2].renders;
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device, NULL);
    metal_triangle_record(s, pipeline, view, encoder, WGPUStoreOp_Store);
    wgpuRenderPipelineRelease(pipeline);
    wgpuTextureViewRelease(view);
    wgpuTextureRelease(image);
    submit(s, encoder);
    assert(mocks[2].renders == renders + 1 && mocks[2].render_draw_count == 1);
    assert(mocks[2].render_draws[0].values.count == 3);
    assert(mocks[2].live == baseline && !s->errors);
}

#include "metal_indirect_cases.h"
#include "metal_depth_cases.h"
#include "metal_draw_cases.h"

#include "metal_external_cases.h"
#include "metal_import_cases.h"

int main(int argc, char **argv)
{
    assert(argc == 1 || (argc == 2 && (!strcmp(argv[1], "--external-only") || !strcmp(argv[1], "--imports-only"))));
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
    const WGPUFeatureName features[] = {WGPUFeatureName_TextureCompressionBC, WGPUFeatureName_ShaderF16,
        WGPUFeatureName_TextureCompressionETC2, WGPUFeatureName_TextureCompressionASTC,
        WGPUFeatureName_TextureCompressionASTCSliced3D, WGPUFeatureName_TextureComponentSwizzle};
    descriptor.requiredFeatureCount = sizeof(features) / sizeof(features[0]);
    descriptor.requiredFeatures = features;
    descriptor.uncapturedErrorCallbackInfo = (WGPUUncapturedErrorCallbackInfo){.callback=error_reply,.userdata1=&s};
    WGPURequestDeviceCallbackInfo requested = {.mode=WGPUCallbackMode_AllowSpontaneous,.callback=device_reply,.userdata1=&s};
    assert(wgpuAdapterRequestDevice(s.adapter,&descriptor,requested).id && s.device);
    s.queue = wgpuDeviceGetQueue(s.device);
    native_device *device = (native_device *)s.device;
    assert(device->backend.kind == WGPUBackendType_Metal);
    if (argc == 2) {
        if (!strcmp(argv[1], "--imports-only")) metal_shader_import_cases(&s);
        else metal_external_import_cases(&s);
        consumer_finish(&s);
        for (unsigned i = 0; i < context_count; ++i) mock_assert_closed(&mocks[i]);
        return 0;
    }
    compute_binding_ledger(&s);
    atomic_pipeline_admission(&s);
    WGPUBuffer source = buffer(&s,16,WGPUBufferUsage_CopySrc|WGPUBufferUsage_CopyDst);
    WGPUBuffer target = buffer(&s,16,WGPUBufferUsage_MapRead|WGPUBufferUsage_CopyDst);
    uint32_t input[4] = {11,23,37,51};
    wgpuQueueWriteBuffer(s.queue,source,0,input,sizeof(input));
    native_buffer *source_payload = doe_resource_buffer_native((doe_resource_buffer *)source);
    void *source_identity = doe_mtl_buffer_native(source_payload->handle.metal);
    uint32_t host_written[4] = {0};
    assert(native_buffer_read(source_payload,0,host_written,sizeof(host_written)).status == NATIVE_OK);
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
    queue_upload_admission(&s);
    metal_compressed_copy_ledger(&s, &mocks[2]);
    metal_extended_copy_ledger(&s, &mocks[2]);
    metal_f16_consumer(&s, &mocks[2]);
    metal_f16_compact_storage(&s, &mocks[2]);
    metal_swizzle_case(&s, &mocks[2], true);
    exact_pair_layouts(&s);
    pipeline_cache_admission(&s, true);
    required_metal_limits(&s);
    sampled_float_admission(&s);
    metal_storage_cases(&s);
    graphics_ledger(&s);
    metal_vertex_formats(&s, &mocks[2]);
    metal_vertex_spans(&s, &mocks[2]);
    mixed_resource_ledger(&s);
    metal_indirect_ledger(&s, &mocks[2]);
    metal_depth_admission(&s, &mocks[2]);
    metal_draw_guard_ledger(&s, &mocks[2]);
    metal_shader_import_cases(&s);
    metal_external_import_cases(&s);
    public_error_detail(&s); /* Deliberately destroys the device; keep last. */
    consumer_finish(&s);
    for (unsigned i = 0; i < context_count; ++i) mock_assert_closed(&mocks[i]);
    puts("PASS public Metal host composition: exact backend/adapter identity, compiler-owned MSL entry/slots/sizes, shared buffer copy/fill/map, typed failures, early releases and unknown queue retention; call ledger only, not GPU evidence");
}
