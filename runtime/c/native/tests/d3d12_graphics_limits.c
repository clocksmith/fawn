/* SPDX-License-Identifier: MIT
 * Compose D3D12's reported limits with actual shared render-layout admission.
 * Pure host control: native tokens exercise ownership, never a GPU or SDK. */
#include "../src/d3d12_frontend.h"
#include "../../backends/d3d12/include/doe_d3d12_render.h"
#include "../../backends/d3d12/include/doe_d3d12_texture.h"
#include "../../resources/tests/fixture.h"

static void admission(fixture *f,const doe_resource_config *c,const WGPUTextureFormat *formats,
    uint32_t count,doe_resource_status expected)
{
    doe_resource_pipeline_layout *layout=NULL;
    assert(doe_resource_pipeline_layout_create(c,NULL,0,0,&layout)==DOE_RESOURCE_OK);
    doe_resource_render_pipeline_desc desc={.samples=1,.color_count=count};
    memcpy(desc.color_formats,formats,count*sizeof(*formats));
    native_token token={0};
    doe_resource_render_pipeline *pipeline=NULL;
    assert(doe_resource_render_pipeline_create(c,layout,&desc,native(f,&token),&pipeline)==expected);
    if (expected==DOE_RESOURCE_OK) {
        assert(pipeline);
        doe_resource_render_pipeline_release(pipeline);
        assert(token.releases==1);
    } else assert(!pipeline && !token.releases);
    doe_resource_pipeline_layout_release(layout);
    empty(f);
}

static void *shader_alloc(void *user,size_t size,size_t alignment)
{
    (void)user; (void)alignment;
    return malloc(size);
}
static void shader_free(void *user,void *memory,size_t size,size_t alignment)
{
    (void)user; (void)size; (void)alignment;
    free(memory);
}
static void fragment_storage(void)
{
    static const char source[] =
        "@group(0)@binding(0)var<storage,read_write> output:array<u32>;"
        "@fragment fn fs()->@location(0)vec4f{output[0]=7u;return vec4f(1);}";
    doe_shader *shader = NULL;
    doe_shader_error error = {0};
    doe_shader_options options = {.target=DOE_SHADER_HLSL,
        .allocator={NULL,shader_alloc,shader_free},
        .capacities={.typed={.source_bytes=4096,.tokens=1024,.statements=256,.expressions=1024,
            .types=128,.members=128,.resources=16,.access_steps=1024,.constants=256,.constant_edges=1024,
            .ir_instructions=4096,.ir_operands=4096,.target_declarations=4096,.traversal_items=193,
            .words=16384,.expression_depth=32,.control_depth=32,.type_syntax_depth=32},
            .entries=8,.entry_inputs=32,.entry_references=128},
        .selection={.mode=DOE_COMPILER_SELECT_NONE},.interface_attributes=128,.stage_interfaces=128,
        .sampling_pairs=128,.compiler_identity="fragment-storage-control",.compiler_identity_size=24};
    doe_shader *module = NULL;
    assert(doe_shader_create_module(&options,1024,source,sizeof(source)-1,&module,&error) == DOE_SHADER_OK);
    DoeCompilerEntrySelectionV5 selected = {.mode=DOE_COMPILER_SELECT_NAME,.name="fs",.name_length=2};
    assert(doe_shader_specialize(module,&selected,NULL,0,&shader,&error) == DOE_SHADER_OK);
    doe_shader_release(module);
    native_stage stage = {.visibility=WGPUShaderStage_Fragment};
    assert(doe_shader_borrow(shader, &stage.view) == DOE_SHADER_OK);
    assert(stage.view.counts.entries == 1);
    stage.entry = stage.view.entries;
    fixture f = fresh();
    native_device device = {.resources=config(&f), .backend={.kind=WGPUBackendType_D3D12}};
    device.backend.value.d3d12.caps.feature_level = 0xb000;
    device.backend.value.d3d12.caps.shader_model = 0x60;
    device.backend.limits = (WGPULimits){.maxBindGroups=4,.maxBindingsPerBindGroup=32,
        .maxStorageBuffersPerShaderStage=8,.maxStorageTexturesPerShaderStage=8};
    doe_resource_pipeline_layout *layout = NULL;
    assert(native_automatic_layout(&device, &stage, 1, &layout) == DOE_RESOURCE_OK);
    native_pipeline payload = {.device=&device};
    assert(native_pipeline_bindings(&payload, &stage, 1, layout));
    assert(payload.binding_count == 1 && payload.bindings[0].buffer_type == WGPUBufferBindingType_Storage);
    device.backend.value.d3d12.caps.shader_model = 0x51;
    payload = (native_pipeline){.device=&device};
    assert(!native_pipeline_bindings(&payload, &stage, 1, layout));
    device.backend.value.d3d12.caps.shader_model = 0x60;
    stage.visibility = WGPUShaderStage_Vertex;
    payload = (native_pipeline){.device=&device};
    assert(!native_pipeline_bindings(&payload, &stage, 1, layout));
    doe_resource_pipeline_layout_release(layout);
    doe_shader_release(shader);
    empty(&f);
}

static void native_import_contract(void)
{
    fixture f = fresh();
    native_device device = {.resources=config(&f),.backend={.kind=WGPUBackendType_Metal}};
    device.backend.limits=(WGPULimits){.maxBindGroups=4,.maxBindingsPerBindGroup=32,
        .maxUniformBuffersPerShaderStage=1,.maxStorageBuffersPerShaderStage=1};
    const doe_resource_layout_entry entry={.binding=7,.visibility=WGPUShaderStage_Compute,
        .kind=DOE_BIND_BUFFER,.buffer_type=WGPUBufferBindingType_Uniform,.minimum_size=16};
    doe_resource_layout *group=NULL;
    assert(doe_resource_layout_create(&device.resources,&entry,1,false,&group)==DOE_RESOURCE_OK);
    doe_resource_pipeline_layout *layout=NULL;
    assert(doe_resource_pipeline_layout_create(&device.resources,&group,1,0,&layout)==DOE_RESOURCE_OK);
    native_shader_import import={.target=DOE_SHADER_METAL};
    native_stage stage={.imported=&import,.visibility=WGPUShaderStage_Compute};
    native_pipeline payload={.device=&device};
    assert(native_pipeline_bindings(&payload,&stage,1,layout));
    assert(payload.binding_count==1 && payload.bindings[0].binding==7 && payload.minimum_sizes[0]==16);
    assert(!stage.view.translation_complete && !stage.view.sampling_pairs_complete);
    import.target=DOE_SHADER_HLSL;
    payload=(native_pipeline){.device=&device};
    assert(!native_pipeline_bindings(&payload,&stage,1,layout));
    import.target=DOE_SHADER_METAL;
    device.backend.limits.maxUniformBuffersPerShaderStage=0;
    payload=(native_pipeline){.device=&device};
    assert(!native_pipeline_bindings(&payload,&stage,1,layout));
    doe_resource_pipeline_layout_release(layout);
    doe_resource_layout_release(group);
    empty(&f);
}

int main(void)
{
    fragment_storage();
    native_import_contract();
    doe_dx_caps caps={.feature_level=0xb000,.limits={.max_buffer_size=UINT64_C(268435456),
        .uniform_offset_alignment=256,.max_descriptors=64}};
    WGPULimits limits=WGPU_LIMITS_INIT;
    assert(native_d3d12_limits(&caps,&limits)==WGPUStatus_Success);
    assert(limits.maxTextureDimension1D==DOE_DX_MAX_TEXTURE_DIMENSION_1D);
    assert(limits.maxTextureDimension3D==DOE_DX_MAX_TEXTURE_DIMENSION_3D);
    assert(limits.maxTextureArrayLayers==DOE_DX_MAX_TEXTURE_ARRAY_LAYERS);
    assert(limits.maxColorAttachments==DOE_DX_MAX_COLOR_TARGETS);
    assert(limits.maxColorAttachmentBytesPerSample==32);
    assert(limits.maxImmediateSize==DOE_RESOURCE_MAX_IMMEDIATE_BYTES);
    WGPULimits unchanged=limits;
    caps.feature_level=0;
    assert(native_d3d12_limits(&caps,&limits)==WGPUStatus_Error);
    assert(!memcmp(&limits,&unchanged,sizeof(limits)));
    fixture f=fresh();
    doe_resource_config c=config(&f);
    c.max_color_attachments=limits.maxColorAttachments;
    c.max_color_attachment_bytes_per_sample=limits.maxColorAttachmentBytesPerSample;
    const WGPUTextureFormat formats[]={WGPUTextureFormat_RGBA8Unorm,WGPUTextureFormat_BGRA8Unorm};
    for (size_t i=0;i<sizeof(formats)/sizeof(*formats);++i)
        admission(&f,&c,&formats[i],1,DOE_RESOURCE_OK);
    const WGPUTextureFormat large[]={WGPUTextureFormat_RGBA32Uint,WGPUTextureFormat_RGBA32Uint,
        WGPUTextureFormat_RGBA32Uint};
    admission(&f,&c,large,2,DOE_RESOURCE_OK);
    admission(&f,&c,large,3,DOE_RESOURCE_RANGE);
    const WGPUTextureFormat slots[DOE_DX_MAX_COLOR_TARGETS]={[DOE_DX_MAX_COLOR_TARGETS-1]=WGPUTextureFormat_RGBA8Unorm};
    admission(&f,&c,slots,DOE_DX_MAX_COLOR_TARGETS,DOE_RESOURCE_OK);
    c.max_color_attachments=DOE_DX_MAX_COLOR_TARGETS-1;
    admission(&f,&c,slots,DOE_DX_MAX_COLOR_TARGETS,DOE_RESOURCE_RANGE);
    c.max_color_attachments=limits.maxColorAttachments;
    /* Reproduce the former mistake without changing the canonical format cost:
     * four storage bytes do not satisfy UNORM's eight render-budget bytes. */
    c.max_color_attachment_bytes_per_sample=4;
    for (size_t i=0;i<sizeof(formats)/sizeof(*formats);++i)
        admission(&f,&c,&formats[i],1,DOE_RESOURCE_RANGE);
    puts("PASS D3D12 limits compose with shared RGBA/BGRA render admission; MRT slot/byte budgets, queried-feature-level rejection, exact cleanup; host only");
    return 0;
}
