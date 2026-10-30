/* SPDX-License-Identifier: MIT
 * Actual public compiler metadata and copied-row rejection; no SDK/device. */
#include "../src/d3d12_binding_internal.h"
#include "doe_compiler_hlsl.h"
#include <assert.h>
#include <stdalign.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void *allocate(void *user,size_t bytes,size_t alignment)
{
    (void)user;
    assert(alignment <= alignof(max_align_t));
    return malloc(bytes);
}
static void deallocate(void *user,void *memory,size_t bytes,size_t alignment)
{
    (void)user; (void)bytes; (void)alignment;
    free(memory);
}
static void reject(const native_pipeline *pipeline,const native_stage *stage)
{
    native_d3d12_layout layout,prior;
    native_d3d12_compute map,previous;
    memset(&layout,0x5a,sizeof(layout)); prior=layout;
    memset(&map,0xa5,sizeof(map)); previous=map;
    assert(!native_d3d12_stage_bindings(pipeline,stage,&layout,&map));
    assert(!memcmp(&layout,&prior,sizeof(layout)) && !memcmp(&map,&previous,sizeof(map)));
}
static void samplers(void)
{
    WGPUSamplerDescriptor input = WGPU_SAMPLER_DESCRIPTOR_INIT;
    input.addressModeU=WGPUAddressMode_ClampToEdge;
    input.addressModeV=WGPUAddressMode_Repeat;
    input.addressModeW=WGPUAddressMode_MirrorRepeat;
    input.minFilter=WGPUFilterMode_Nearest; input.magFilter=WGPUFilterMode_Linear;
    input.mipmapFilter=WGPUMipmapFilterMode_Linear;
    input.lodMinClamp=2; input.lodMaxClamp=7; input.maxAnisotropy=1;
    doe_dx_sampler_desc desc;
    assert(native_d3d12_sampler_descriptor(&input,&desc));
    assert(desc.min_filter==DOE_DX_NEAREST && desc.mag_filter==DOE_DX_LINEAR && desc.mip_filter==DOE_DX_LINEAR &&
        desc.address_u==DOE_DX_CLAMP && desc.address_v==DOE_DX_REPEAT && desc.address_w==DOE_DX_MIRROR &&
        desc.min_lod==2 && desc.max_lod==7 && desc.max_anisotropy==1 && desc.compare==DOE_DX_SAMPLER_NO_COMPARE);
    const WGPUCompareFunction comparisons[] = {WGPUCompareFunction_Never,WGPUCompareFunction_Less,
        WGPUCompareFunction_Equal,WGPUCompareFunction_LessEqual,WGPUCompareFunction_Greater,
        WGPUCompareFunction_NotEqual,WGPUCompareFunction_GreaterEqual,WGPUCompareFunction_Always};
    const doe_dx_sampler_compare expected[] = {DOE_DX_SAMPLER_NEVER,DOE_DX_SAMPLER_LESS,DOE_DX_SAMPLER_EQUAL,
        DOE_DX_SAMPLER_LESS_EQUAL,DOE_DX_SAMPLER_GREATER,DOE_DX_SAMPLER_NOT_EQUAL,
        DOE_DX_SAMPLER_GREATER_EQUAL,DOE_DX_SAMPLER_ALWAYS};
    for (size_t i=0;i<sizeof(comparisons)/sizeof(*comparisons);++i) {
        input.compare=comparisons[i];
        assert(native_d3d12_sampler_descriptor(&input,&desc) && desc.compare==expected[i]);
    }
    doe_dx_sampler_desc saved=desc;
    input.addressModeV=WGPUAddressMode_Undefined;
    assert(!native_d3d12_sampler_descriptor(&input,&desc) && !memcmp(&saved,&desc,sizeof(desc)));
}
static void raw_half_metadata(const doe_shader_options *options)
{
    const char *source="enable f16; @group(0) @binding(0) var<uniform> input:vec4u;"
        "@group(1) @binding(3) var<storage,read_write> output:array<f16>;"
        "@compute @workgroup_size(1) fn main(){output[0]=f16(input.x);}";
    doe_shader *shader=NULL; doe_shader_error error={0};
    doe_shader_status status=doe_shader_create(options,source,strlen(source),&shader,&error);
    if (status!=DOE_SHADER_OK) fprintf(stderr,"raw compile %d: %s\n",(int)status,error.diagnostic.message);
    assert(status==DOE_SHADER_OK);
    doe_shader_view view; assert(doe_shader_borrow(shader,&view)==DOE_SHADER_OK);
    native_pipeline pipeline={.binding_count=2,.bindings={
        {.group=0,.binding=0,.kind=DOE_BIND_BUFFER,.buffer_type=WGPUBufferBindingType_Uniform},
        {.group=1,.binding=3,.kind=DOE_BIND_BUFFER,.buffer_type=WGPUBufferBindingType_Storage}}};
    native_stage stage={.view=view,.entry=&view.entries[0]};
    native_d3d12_layout layout; native_d3d12_compute map;
    assert(native_d3d12_stage_bindings(&pipeline,&stage,&layout,&map));
    assert(map.buffer_count==2 && map.logical_size_count==view.counts.resources && map.logical_size_count==2);
    assert(layout.buffers[0].view==DOE_DX_UNIFORM && layout.buffers[1].view==DOE_DX_RAW && !layout.buffers[1].stride);
    assert(map.logical_size_indices[1]==1 && (map.entry_flags & DOE_COMPILER_HLSL_ENTRY_NATIVE16) &&
        (map.entry_flags & DOE_COMPILER_HLSL_ENTRY_RAW_STORAGE));
    assert(strstr(view.text,"Store<float16_t>") && strstr(view.text,"register(b2, space7)"));
    doe_dx_caps caps={.shader_model=0x62,.native_16bit_shader_ops=true};
    assert(native_d3d12_capabilities(&map,&caps));
    caps.native_16bit_shader_ops=false; assert(!native_d3d12_capabilities(&map,&caps));
    caps.native_16bit_shader_ops=true; caps.shader_model=0x60; assert(!native_d3d12_capabilities(&map,&caps));
    DoeCompilerHlslEntryV11 row=view.hlsl_entries[0]; row.flags&=~DOE_COMPILER_HLSL_ENTRY_RAW_STORAGE;
    stage.view.hlsl_entries=&row; reject(&pipeline,&stage);
    doe_shader_release(shader);
}
/* Structured storage that reads binding sizes takes one table row per canonical
 * resource; uniform rows stay unsized in the backend. Copied rows isolate the flag. */
static void structured_size_metadata(const doe_shader_options *options)
{
    const char *source="@group(0) @binding(0) var<uniform> limit:vec4u;"
        "@group(0) @binding(2) var<storage,read_write> output:array<u32>;"
        "@compute @workgroup_size(1) fn main(@builtin(global_invocation_id) id:vec3u)"
        "{output[id.x]=limit.x+arrayLength(&output);}";
    doe_shader *shader=NULL; doe_shader_error error={0};
    assert(doe_shader_create(options,source,strlen(source),&shader,&error)==DOE_SHADER_OK);
    doe_shader_view view; assert(doe_shader_borrow(shader,&view)==DOE_SHADER_OK);
    native_pipeline pipeline={.binding_count=2,.bindings={
        {.group=0,.binding=0,.kind=DOE_BIND_BUFFER,.buffer_type=WGPUBufferBindingType_Uniform},
        {.group=0,.binding=2,.kind=DOE_BIND_BUFFER,.buffer_type=WGPUBufferBindingType_Storage}}};
    native_stage stage={.view=view,.entry=&view.entries[0]};
    native_d3d12_layout layout; native_d3d12_compute map;
    DoeCompilerHlslEntryV11 row=view.hlsl_entries[0];
    stage.view.hlsl_entries=&row;
    row.flags&=~DOE_COMPILER_HLSL_ENTRY_STORAGE_SIZES;
    assert(native_d3d12_stage_bindings(&pipeline,&stage,&layout,&map) && !map.logical_size_count);
    row.flags|=DOE_COMPILER_HLSL_ENTRY_STORAGE_SIZES;
    assert(native_d3d12_stage_bindings(&pipeline,&stage,&layout,&map));
    assert(map.logical_size_count==view.counts.resources && map.logical_size_count==2);
    assert(layout.buffers[1].view==DOE_DX_STRUCTURED && layout.buffers[1].stride==4 && map.logical_size_indices[1]==1);
    assert(!(map.entry_flags & DOE_COMPILER_HLSL_ENTRY_RAW_STORAGE));
    doe_shader_release(shader);
}
static void external_metadata(const doe_shader_options *options)
{
    const char *source = "@group(2) @binding(3) var frame:texture_external;"
        "@group(0) @binding(1) var<storage,read_write> output:array<vec4f>;"
        "@compute @workgroup_size(1) fn main(){output[0]=textureLoad(frame,vec2i(0));}";
    doe_shader *shader = NULL;
    doe_shader_error error = {0};
    assert(doe_shader_create(options,source,strlen(source),&shader,&error) == DOE_SHADER_OK);
    doe_shader_view view;
    assert(doe_shader_borrow(shader,&view) == DOE_SHADER_OK);
    native_pipeline pipeline = {.binding_count=2,.bindings={
        {.group=2,.binding=3,.kind=DOE_BIND_EXTERNAL_TEXTURE},
        {.group=0,.binding=1,.kind=DOE_BIND_BUFFER,.buffer_type=WGPUBufferBindingType_Storage}}};
    native_stage stage = {.view=view,.entry=&view.entries[0]};
    native_d3d12_layout layout;
    native_d3d12_compute map;
    assert(native_d3d12_stage_bindings(&pipeline,&stage,&layout,&map));
    assert(map.texture_count == 1 && layout.contracts[0].sample_class == DOE_DX_SAMPLE_FLOAT &&
        layout.contracts[0].dimension == DOE_COMPILER_VIEW_2D && map.texture_indices[0] == 0 &&
        layout.textures[0].shader_register == 3 && layout.textures[0].register_space == 2);
    pipeline.bindings[0].kind = DOE_BIND_TEXTURE;
    reject(&pipeline,&stage);
    pipeline.bindings[0].kind = DOE_BIND_EXTERNAL_TEXTURE;
    assert(view.handle_count == 1);
    DoeCompilerHandleV6 handle = view.handles[0];
    stage.view.handles = &handle;
    handle.flags |= DOE_COMPILER_HANDLE_MULTISAMPLED;
    reject(&pipeline,&stage);
    handle = view.handles[0];
    handle.view_dimension = DOE_COMPILER_VIEW_CUBE;
    reject(&pipeline,&stage);
    doe_shader_release(shader);
}

int main(int argc,char **argv)
{
    const char *source =
        "@group(0) @binding(2) var image:texture_2d<f32>;"
        "@group(0) @binding(3) var filtering:sampler;"
        "@group(1) @binding(7) var input_image:texture_storage_2d<r32uint,read>;"
        "@group(2) @binding(9) var output_image:texture_storage_2d<r32uint,write>;"
        "@group(3) @binding(1) var<storage,read_write> output_values:array<u32>;"
        "@compute @workgroup_size(1) fn main(@builtin(num_workgroups) groups:vec3u){"
        "let value=textureSampleLevel(image,filtering,vec2f(0.5),0.0);"
        "let read_value=textureLoad(input_image,vec2i(0));"
        "textureStore(output_image,vec2i(0),read_value);output_values[0]=u32(value.x)+groups.x;}";
    const doe_shader_options options = {.target=DOE_SHADER_HLSL,
        .allocator={NULL,allocate,deallocate},.capacities={.typed={
            .source_bytes=8192,.tokens=2048,.statements=128,.expressions=2048,
            .types=128,.members=128,.resources=16,.access_steps=512,.constants=256,
            .constant_edges=512,.ir_instructions=4096,.ir_operands=4096,
            .target_declarations=2048,.traversal_items=97,.words=32768,
            .expression_depth=32,.control_depth=16,.type_syntax_depth=32},
            .entries=4,.entry_inputs=16,.entry_references=32},
        .selection={.mode=DOE_COMPILER_SELECT_NONE},.compiler_identity="stage-control",.compiler_identity_size=13};
    doe_shader *shader=NULL;
    doe_shader_error error={0};
    doe_shader_status status=doe_shader_create(&options,source,strlen(source),&shader,&error);
    if(status!=DOE_SHADER_OK) fprintf(stderr,"compile %d: %s\n",(int)status,error.diagnostic.message);
    assert(status==DOE_SHADER_OK);
    doe_shader_view view;
    assert(doe_shader_borrow(shader,&view)==DOE_SHADER_OK);
    if (argc > 1) {
        FILE *file=fopen(argv[1],"wb");
        assert(file && fwrite(view.text,1,view.text_size,file)==view.text_size && fclose(file)==0);
    }
    native_pipeline pipeline={.binding_count=5,.bindings={
        {.group=0,.binding=2,.kind=DOE_BIND_TEXTURE}, {.group=0,.binding=3,.kind=DOE_BIND_SAMPLER},
        {.group=1,.binding=7,.kind=DOE_BIND_STORAGE_TEXTURE}, {.group=2,.binding=9,.kind=DOE_BIND_STORAGE_TEXTURE},
        {.group=3,.binding=1,.kind=DOE_BIND_BUFFER,.buffer_type=WGPUBufferBindingType_Storage}}};
    native_stage stage={.view=view,.entry=&view.entries[0]};
    native_d3d12_layout layout;
    native_d3d12_compute map;
    assert(native_d3d12_stage_bindings(&pipeline,&stage,&layout,&map));
    assert(map.texture_count==3 && map.sampler_count==1 && map.buffer_count==2 && map.dispatch_index==1);
    assert(layout.contracts[0].sample_class==DOE_DX_SAMPLE_FLOAT && !layout.contracts[0].storage);
    assert(layout.contracts[1].storage && layout.textures[1].access==DOE_DX_READ_ONLY &&
        layout.textures[1].shader_register==7 && layout.textures[1].register_space==1);
    assert(layout.contracts[2].storage && layout.textures[2].access==DOE_DX_READ_WRITE);
    assert(layout.samplers[0].shader_register==3 && !layout.samplers[0].comparison);
    assert(map.texture_indices[1]==2 && map.buffer_indices[0]==4 && map.sampler_indices[0]==1);
    assert(layout.buffers[1].view==DOE_DX_UNIFORM && layout.buffers[1].register_space==7);
    DoeCompilerHlslBindingV11 rows[5];
    DoeCompilerHandleV6 handles[4];
    assert(view.hlsl_binding_count==5 && view.handle_count==4);
    memcpy(rows,view.hlsl_bindings,sizeof(rows)); memcpy(handles,view.handles,sizeof(handles));
    stage.view.hlsl_bindings=rows;
    size_t readonly_row=SIZE_MAX;
    for(size_t i=0;i<5;++i) if(view.resources[rows[i].resource_index].group==1) readonly_row=i;
    assert(readonly_row!=SIZE_MAX);
    rows[readonly_row].register_kind=DOE_COMPILER_HLSL_REGISTER_UAV; reject(&pipeline,&stage);
    memcpy(rows,view.hlsl_bindings,sizeof(rows)); rows[readonly_row].stride=4; reject(&pipeline,&stage);
    stage.view=view; stage.view.handles=handles;
    size_t readonly_handle=SIZE_MAX;
    for(size_t i=0;i<4;++i) if(view.resources[handles[i].resource_index].group==1) readonly_handle=i;
    assert(readonly_handle!=SIZE_MAX);
    /* Historical 2D-only admission rejected 3D here. The real immutable-view
     * owner now supports it; preserve rejection with a genuinely invalid enum. */
    handles[readonly_handle].view_dimension=DOE_COMPILER_VIEW_3D;
    assert(native_d3d12_stage_bindings(&pipeline,&stage,&layout,&map));
    handles[readonly_handle].view_dimension=UINT32_MAX; reject(&pipeline,&stage);
    memcpy(handles,view.handles,sizeof(handles)); handles[readonly_handle].sample_scalar=DOE_COMPILER_LEAF_F32;
    reject(&pipeline,&stage);
    stage.view=view;
    DoeCompilerHlslEntryV11 entry=view.hlsl_entries[0]; stage.view.hlsl_entries=&entry;
    entry.dispatch_bytes=12; reject(&pipeline,&stage);
    stage.view=view;
    DoeCompilerEntryV5 selected=*stage.entry; selected.first_resource_reference=UINT32_MAX;
    stage.view.entries=&selected; stage.entry=&selected; reject(&pipeline,&stage);
    stage.view=view; stage.entry=&selected; reject(&pipeline,&stage);
    stage.entry=&view.entries[0];
    pipeline.bindings[4].buffer_type=WGPUBufferBindingType_ReadOnlyStorage; reject(&pipeline,&stage);
    raw_half_metadata(&options);
    structured_size_metadata(&options);
    external_metadata(&options);
    samplers();
    doe_shader_release(shader);
    puts("PASS D3D12 selected compiler binding/sampler conversion: actual HLSL, read-storage SRV, dynamic samplers, row/span rejection and untouched outputs; no SDK/GPU");
    return 0;
}
