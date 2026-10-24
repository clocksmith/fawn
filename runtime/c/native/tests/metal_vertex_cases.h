/* SPDX-License-Identifier: MIT */
#ifndef DOE_NATIVE_TEST_METAL_VERTEX_CASES_H
#define DOE_NATIVE_TEST_METAL_VERTEX_CASES_H

/* Included after the host bridge's scoped-error/stack-overwrite helpers. These
 * controls inspect real owner/native arguments, never invented fetched pixels. */
typedef struct metal_vertex_case {
    WGPUVertexFormat format;
    uint32_t native_format, bytes, scalar;
} metal_vertex_case;

static WGPUShaderModule metal_vertex_module(state *s, uint32_t scalar)
{
    const char *code = scalar == 0 ?
        "@vertex fn vs(@location(0) a:vec4f)->@builtin(position) vec4f{return a;}"
        "@fragment fn fs()->@location(0) vec4f{return vec4f(1.0);}" : scalar == 1 ?
        "@vertex fn vs(@location(0) a:vec4u)->@builtin(position) vec4f{return vec4f(f32(a.x),0.0,0.0,1.0);}"
        "@fragment fn fs()->@location(0) vec4f{return vec4f(1.0);}" :
        "@vertex fn vs(@location(0) a:vec4i)->@builtin(position) vec4f{return vec4f(f32(a.x),0.0,0.0,1.0);}"
        "@fragment fn fs()->@location(0) vec4f{return vec4f(1.0);}";
    WGPUShaderSourceWGSL source = WGPU_SHADER_SOURCE_WGSL_INIT;
    source.code = (WGPUStringView){code, WGPU_STRLEN};
    WGPUShaderModuleDescriptor descriptor = {.nextInChain=&source.chain};
    WGPUShaderModule result = wgpuDeviceCreateShaderModule(s->device, &descriptor);
    assert(result);
    return result;
}

static WGPURenderPipeline metal_vertex_pipeline(state *s, WGPUShaderModule module,
    WGPUVertexFormat format, uint64_t offset)
{
    WGPUVertexAttribute attribute = {.format=format, .offset=offset, .shaderLocation=0};
    WGPUVertexBufferLayout buffer = {.arrayStride=32, .stepMode=WGPUVertexStepMode_Instance,
        .attributeCount=1, .attributes=&attribute};
    WGPUColorTargetState target = {.format=WGPUTextureFormat_RGBA8Unorm, .writeMask=WGPUColorWriteMask_All};
    WGPUFragmentState fragment = {.module=module, .targetCount=1, .targets=&target};
    WGPURenderPipelineDescriptor descriptor = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    descriptor.vertex = (WGPUVertexState){.module=module, .bufferCount=1, .buffers=&buffer};
    descriptor.fragment = &fragment;
    return wgpuDeviceCreateRenderPipeline(s->device, &descriptor);
}

static void metal_vertex_rejected(state *s, mock_native *mock, WGPUShaderModule module,
    WGPUVertexFormat format, uint64_t offset)
{
    unsigned created = mock->created, live = mock->live;
    wgpuDevicePushErrorScope(s->device, WGPUErrorFilter_Validation);
    assert(!metal_vertex_pipeline(s, module, format, offset));
    pop(s, WGPUErrorType_Validation);
    assert(mock->created == created && mock->live == live);
}

static void metal_vertex_formats(state *s, mock_native *mock)
{
    static const metal_vertex_case cases[] = {
        {WGPUVertexFormat_Uint8, DOE_MTL_VERTEX_UCHAR, 1, 1},
        {WGPUVertexFormat_Uint8x2, DOE_MTL_VERTEX_UCHAR2, 2, 1},
        {WGPUVertexFormat_Uint8x4, DOE_MTL_VERTEX_UCHAR4, 4, 1},
        {WGPUVertexFormat_Sint8, DOE_MTL_VERTEX_CHAR, 1, 2},
        {WGPUVertexFormat_Sint8x2, DOE_MTL_VERTEX_CHAR2, 2, 2},
        {WGPUVertexFormat_Sint8x4, DOE_MTL_VERTEX_CHAR4, 4, 2},
        {WGPUVertexFormat_Unorm8, DOE_MTL_VERTEX_UCHAR_NORMALIZED, 1, 0},
        {WGPUVertexFormat_Unorm8x2, DOE_MTL_VERTEX_UCHAR2_NORMALIZED, 2, 0},
        {WGPUVertexFormat_Unorm8x4, DOE_MTL_VERTEX_UCHAR4_NORMALIZED, 4, 0},
        {WGPUVertexFormat_Snorm8, DOE_MTL_VERTEX_CHAR_NORMALIZED, 1, 0},
        {WGPUVertexFormat_Snorm8x2, DOE_MTL_VERTEX_CHAR2_NORMALIZED, 2, 0},
        {WGPUVertexFormat_Snorm8x4, DOE_MTL_VERTEX_CHAR4_NORMALIZED, 4, 0},
        {WGPUVertexFormat_Uint16, DOE_MTL_VERTEX_USHORT, 2, 1},
        {WGPUVertexFormat_Uint16x2, DOE_MTL_VERTEX_USHORT2, 4, 1},
        {WGPUVertexFormat_Uint16x4, DOE_MTL_VERTEX_USHORT4, 8, 1},
        {WGPUVertexFormat_Sint16, DOE_MTL_VERTEX_SHORT, 2, 2},
        {WGPUVertexFormat_Sint16x2, DOE_MTL_VERTEX_SHORT2, 4, 2},
        {WGPUVertexFormat_Sint16x4, DOE_MTL_VERTEX_SHORT4, 8, 2},
        {WGPUVertexFormat_Unorm16, DOE_MTL_VERTEX_USHORT_NORMALIZED, 2, 0},
        {WGPUVertexFormat_Unorm16x2, DOE_MTL_VERTEX_USHORT2_NORMALIZED, 4, 0},
        {WGPUVertexFormat_Unorm16x4, DOE_MTL_VERTEX_USHORT4_NORMALIZED, 8, 0},
        {WGPUVertexFormat_Snorm16, DOE_MTL_VERTEX_SHORT_NORMALIZED, 2, 0},
        {WGPUVertexFormat_Snorm16x2, DOE_MTL_VERTEX_SHORT2_NORMALIZED, 4, 0},
        {WGPUVertexFormat_Snorm16x4, DOE_MTL_VERTEX_SHORT4_NORMALIZED, 8, 0},
        {WGPUVertexFormat_Float16, DOE_MTL_VERTEX_HALF, 2, 0},
        {WGPUVertexFormat_Float16x2, DOE_MTL_VERTEX_HALF2, 4, 0},
        {WGPUVertexFormat_Float16x4, DOE_MTL_VERTEX_HALF4, 8, 0},
        {WGPUVertexFormat_Float32, DOE_MTL_VERTEX_FLOAT, 4, 0},
        {WGPUVertexFormat_Float32x2, DOE_MTL_VERTEX_FLOAT2, 8, 0},
        {WGPUVertexFormat_Float32x3, DOE_MTL_VERTEX_FLOAT3, 12, 0},
        {WGPUVertexFormat_Float32x4, DOE_MTL_VERTEX_FLOAT4, 16, 0},
        {WGPUVertexFormat_Uint32, DOE_MTL_VERTEX_UINT, 4, 1},
        {WGPUVertexFormat_Uint32x2, DOE_MTL_VERTEX_UINT2, 8, 1},
        {WGPUVertexFormat_Uint32x3, DOE_MTL_VERTEX_UINT3, 12, 1},
        {WGPUVertexFormat_Uint32x4, DOE_MTL_VERTEX_UINT4, 16, 1},
        {WGPUVertexFormat_Sint32, DOE_MTL_VERTEX_INT, 4, 2},
        {WGPUVertexFormat_Sint32x2, DOE_MTL_VERTEX_INT2, 8, 2},
        {WGPUVertexFormat_Sint32x3, DOE_MTL_VERTEX_INT3, 12, 2},
        {WGPUVertexFormat_Sint32x4, DOE_MTL_VERTEX_INT4, 16, 2},
        {WGPUVertexFormat_Unorm10_10_10_2, DOE_MTL_VERTEX_UINT1010102_NORMALIZED, 4, 0},
        {WGPUVertexFormat_Unorm8x4BGRA, DOE_MTL_VERTEX_UCHAR4_NORMALIZED_BGRA, 4, 0}
    };
    WGPUShaderModule modules[3] = {metal_vertex_module(s, 0), metal_vertex_module(s, 1), metal_vertex_module(s, 2)};
    unsigned baseline = mock->live;
    for (size_t i = 0; i < sizeof(cases) / sizeof(*cases); ++i) {
        const metal_vertex_case *test = &cases[i];
        WGPURenderPipeline result = metal_vertex_pipeline(s, modules[test->scalar], test->format, 8);
        assert(result);
        overwrite_stack();
        const doe_mtl_render_pipeline_desc *native = &mock->render_pipeline_desc;
        assert(native->attribute_count == 1 && native->layout_count == 1);
        assert(native->attributes == mock->render_attributes && native->layouts == mock->render_layouts);
        assert(native->attributes[0].format == test->native_format && native->attributes[0].offset == 8);
        assert(native->attributes[0].location == 0 && native->attributes[0].slot == native->layouts[0].slot);
        assert(native->layouts[0].stride == 32 && native->layouts[0].step == 2);
        const doe_resource_render_pipeline_desc *canonical =
            doe_resource_render_pipeline_description((doe_resource_render_pipeline *)result);
        assert(canonical->vertex_count == 1 && canonical->vertices[0].stride == 32);
        assert(canonical->vertices[0].last_attribute_end == 8 + test->bytes);
        wgpuRenderPipelineRelease(result);
        assert(mock->live == baseline);
    }
    WGPURenderPipeline odd = metal_vertex_pipeline(s, modules[0], WGPUVertexFormat_Unorm8, 3);
    assert(odd && mock->render_attributes[0].offset == 3);
    wgpuRenderPipelineRelease(odd);
    metal_vertex_rejected(s, mock, modules[1], WGPUVertexFormat_Uint8x2, 1);
    metal_vertex_rejected(s, mock, modules[0], WGPUVertexFormat_Unorm8x4, 1);
    metal_vertex_rejected(s, mock, modules[1], WGPUVertexFormat_Uint16, 1);
    metal_vertex_rejected(s, mock, modules[0], WGPUVertexFormat_Float16x4, 2);
    metal_vertex_rejected(s, mock, modules[0], WGPUVertexFormat_Uint8x4, 0);
    metal_vertex_rejected(s, mock, modules[1], WGPUVertexFormat_Unorm16x2, 0);
    metal_vertex_rejected(s, mock, modules[0], (WGPUVertexFormat)0x2a, 0);
    mock->fail = MOCK_RENDER_PIPELINE;
    wgpuDevicePushErrorScope(s->device, WGPUErrorFilter_Internal);
    assert(!metal_vertex_pipeline(s, modules[0], WGPUVertexFormat_Unorm8x4BGRA, 8));
    pop(s, WGPUErrorType_Internal);
    mock->fail = MOCK_NONE;
    assert(mock->live == baseline);
    for (size_t i = 0; i < 3; ++i) wgpuShaderModuleRelease(modules[i]);
    puts("PASS public Metal vertex formats: 41 SDK identities, owned descriptor snapshots, scalar/offset rejection and native failure rollback; host call ledger only");
}
#endif
