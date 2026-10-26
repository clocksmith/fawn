/* SPDX-License-Identifier: MIT */
#ifndef DOE_NATIVE_TEST_METAL_VERTEX_SPAN_CASES_H
#define DOE_NATIVE_TEST_METAL_VERTEX_SPAN_CASES_H

/* Public constructors consume real compiler reflection; the existing Metal
 * native-call ledger supplies no shader interpreter or fetched-pixel oracle. */
static void metal_vertex_span_async(WGPUCreatePipelineAsyncStatus status, WGPURenderPipeline pipeline,
    WGPUStringView message, void *userdata, void *unused)
{
    (void)unused;
    assert(status == WGPUCreatePipelineAsyncStatus_ValidationError && !pipeline);
    assert(message.data && message.length);
    ++*(unsigned *)userdata;
}

static void metal_vertex_span_rejected(state *s, mock_native *mock,
    const WGPURenderPipelineDescriptor *descriptor)
{
    unsigned created = mock->created, live = mock->live;
    wgpuDevicePushErrorScope(s->device, WGPUErrorFilter_Validation);
    assert(!wgpuDeviceCreateRenderPipeline(s->device, descriptor));
    pop(s, WGPUErrorType_Validation);
    assert(mock->created == created && mock->live == live);
    unsigned calls = 0;
    WGPUCreateRenderPipelineAsyncCallbackInfo callback = {.mode=WGPUCallbackMode_AllowSpontaneous,
        .callback=metal_vertex_span_async, .userdata1=&calls};
    assert(wgpuDeviceCreateRenderPipelineAsync(s->device, descriptor, callback).id && calls == 1);
    assert(mock->created == created && mock->live == live && !s->errors);
}

static void metal_vertex_span_accepted(state *s, mock_native *mock,
    const WGPURenderPipelineDescriptor *descriptor, size_t attributes)
{
    unsigned created = mock->created, live = mock->live;
    WGPURenderPipeline pipeline = wgpuDeviceCreateRenderPipeline(s->device, descriptor);
    assert(pipeline && mock->created > created);
    assert(mock->render_pipeline_desc.attribute_count == attributes);
    wgpuRenderPipelineRelease(pipeline);
    assert(mock->live == live && !s->errors);
}

static void metal_vertex_spans(state *s, mock_native *mock)
{
    WGPULimits limits = WGPU_LIMITS_INIT;
    assert(wgpuDeviceGetLimits(s->device, &limits) == WGPUStatus_Success);
    assert(limits.maxVertexAttributes > 1 && limits.maxVertexAttributes <= DOE_MTL_VERTEX_ATTRIBUTES);
    WGPUShaderModule module = metal_vertex_module(s, 0);
    WGPUVertexAttribute attributes[DOE_MTL_VERTEX_ATTRIBUTES];
    for (uint32_t i = 0; i < limits.maxVertexAttributes; ++i)
        attributes[i] = (WGPUVertexAttribute){.format=WGPUVertexFormat_Float32x4, .shaderLocation=i};
    WGPUVertexBufferLayout buffers[2] = {
        {.arrayStride=16, .stepMode=WGPUVertexStepMode_Vertex, .attributeCount=1, .attributes=attributes},
        {.arrayStride=16, .stepMode=WGPUVertexStepMode_Instance, .attributeCount=1, .attributes=attributes}};
    WGPUColorTargetState target = {.format=WGPUTextureFormat_RGBA8Unorm, .writeMask=WGPUColorWriteMask_All};
    WGPUFragmentState fragment = {.module=module, .targetCount=1, .targets=&target};
    WGPURenderPipelineDescriptor descriptor = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    descriptor.vertex = (WGPUVertexState){.module=module, .bufferCount=1, .buffers=buffers};
    descriptor.fragment = &fragment;
    metal_vertex_span_accepted(s, mock, &descriptor, 1);

    descriptor.vertex.buffers = NULL;
    metal_vertex_span_rejected(s, mock, &descriptor);
    descriptor.vertex.buffers = buffers;
    descriptor.vertex.bufferCount = (size_t)limits.maxVertexBuffers + 1;
    metal_vertex_span_rejected(s, mock, &descriptor);
    descriptor.vertex.bufferCount = SIZE_MAX;
    metal_vertex_span_rejected(s, mock, &descriptor);
    descriptor.vertex.bufferCount = 1;
    buffers[0].attributes = NULL;
    metal_vertex_span_rejected(s, mock, &descriptor);
    buffers[0].attributes = attributes;
    buffers[0].attributeCount = SIZE_MAX;
    metal_vertex_span_rejected(s, mock, &descriptor);
    buffers[0].attributeCount = (size_t)limits.maxVertexAttributes + 1;
    metal_vertex_span_rejected(s, mock, &descriptor);

    /* Each span fits separately, but the aggregate exceeds the shared budget.
     * SIZE_MAX after a full first span must reject without sum wraparound. */
    buffers[0].attributeCount = limits.maxVertexAttributes;
    descriptor.vertex.bufferCount = 2;
    metal_vertex_span_rejected(s, mock, &descriptor);
    buffers[1].attributeCount = SIZE_MAX;
    metal_vertex_span_rejected(s, mock, &descriptor);
    buffers[1].attributeCount = 1;
    buffers[0].attributeCount = limits.maxVertexAttributes - 1;
    buffers[1].attributes = &attributes[limits.maxVertexAttributes - 1];
    metal_vertex_span_accepted(s, mock, &descriptor, limits.maxVertexAttributes);

    /* A canonical hole has no attribute storage requirement; the reflected
     * location may be supplied by a later slot. */
    buffers[0] = (WGPUVertexBufferLayout){0};
    buffers[1].attributes = attributes;
    metal_vertex_span_accepted(s, mock, &descriptor, 1);
    wgpuShaderModuleRelease(module);
    WGPURenderPipeline procedural = metal_triangle_pipeline(s);
    assert(procedural); /* A shader without vertex inputs accepts zero/NULL spans. */
    wgpuRenderPipelineRelease(procedural);
    puts("PASS public Metal vertex span preflight: reflected inputs, NULL/count/aggregate/overflow rejection, sync/async Validation without native creation, valid boundary and empty spans; host call ledger only");
}
#endif
