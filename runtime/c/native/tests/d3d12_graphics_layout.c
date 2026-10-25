/* SPDX-License-Identifier: MIT
 * Real public compiler VS/PS metadata, no SDK/device/GPU. */
#include "../src/d3d12_graphics_internal.h"
#include <assert.h>
#include <stdalign.h>
#include <stdio.h>
#include <stdlib.h>

static void *allocate(void *user,size_t bytes,size_t alignment)
{
    (void)user;
    assert(alignment<=alignof(max_align_t));
    return malloc(bytes);
}
static void deallocate(void *user,void *memory,size_t bytes,size_t alignment)
{
    (void)user; (void)bytes; (void)alignment;
    free(memory);
}
static doe_shader *compile(const char *source)
{
    const doe_shader_options options={.target=DOE_SHADER_HLSL,.allocator={NULL,allocate,deallocate},
        .capacities={.typed={.source_bytes=8192,.tokens=2048,.statements=256,.expressions=2048,
            .types=128,.members=128,.resources=16,.access_steps=1024,.constants=256,.constant_edges=512,
            .ir_instructions=8192,.ir_operands=4096,.target_declarations=4096,.traversal_items=193,.words=65536,
            .expression_depth=32,.control_depth=32,.type_syntax_depth=32},.entries=4,.entry_inputs=32,.entry_references=64},
        .selection={.mode=DOE_COMPILER_SELECT_NONE},.interface_attributes=64,.stage_interfaces=64,
        .compiler_identity="graphics-layout-control",.compiler_identity_size=23};
    doe_shader *shader=NULL;
    doe_shader_error error={0};
    doe_shader_status status=doe_shader_create(&options,source,strlen(source),&shader,&error);
    if (status!=DOE_SHADER_OK) fprintf(stderr,"graphics compile %u: %s\n",(unsigned)status,error.diagnostic.message);
    assert(status==DOE_SHADER_OK);
    return shader;
}
static void projection(const native_stage *fragment,const native_stage *vertex)
{
    uint32_t mask=UINT32_MAX;
    assert(native_d3d12_interstage_mask(fragment,&mask) && mask==1);
    DoeCompilerStageInterfaceV7 rows[2];
    bool found=false;
    for (size_t i=0;i<fragment->view.interface_count;++i) {
        const DoeCompilerStageInterfaceV7 *item=&fragment->view.interfaces[i];
        if (item->direction==DOE_COMPILER_INTERFACE_INPUT && !item->builtin) {
            rows[0]=rows[1]=*item;
            found=true;
            break;
        }
    }
    assert(found);
    native_stage sparse=*fragment;
    sparse.view.interfaces=rows; sparse.view.interface_count=2;
    rows[0].location=2; rows[1].location=31;
    assert(native_d3d12_interstage_mask(&sparse,&mask) && mask==(UINT32_C(1)<<2 | UINT32_C(1)<<31));
    uint32_t saved=mask;
    rows[1].location=2;
    assert(!native_d3d12_interstage_mask(&sparse,&mask) && mask==saved);
    rows[1].location=32;
    assert(!native_d3d12_interstage_mask(&sparse,&mask) && mask==saved);
    rows[1].location=UINT32_MAX;
    assert(!native_d3d12_interstage_mask(&sparse,&mask) && mask==saved);
    sparse.view.interface_count=0;
    assert(native_d3d12_interstage_mask(&sparse,&mask) && mask==0);
    assert(!native_d3d12_interstage_mask(vertex,&mask) && mask==0);
    sparse.view.entries=NULL;
    assert(!native_d3d12_interstage_mask(&sparse,&mask) && mask==0);
}

int main(void)
{
    const char *vertex="@group(0) @binding(2) var<uniform> a:vec4f;"
        "@group(1) @binding(7) var<uniform> common_params:vec4f;"
        "struct Out{@builtin(position) position:vec4f,@location(0) @interpolate(flat) value:f32}"
        "@vertex fn vs(@builtin(vertex_index) index:u32)->Out{var out:Out;"
        "let points=array<vec2f,3>(vec2f(-1,-1),vec2f(3,-1),vec2f(-1,3));"
        "out.position=vec4f(points[index],0,1);out.value=a.x+common_params.x;return out;}";
    const char *fragment="@group(0) @binding(5) var<uniform> b:vec4f;"
        "@group(1) @binding(7) var<uniform> common_params:vec4f;"
        "@group(2) @binding(1) var image:texture_2d<f32>;@group(2) @binding(4) var sampling:sampler;"
        "@fragment fn fs(@location(0) @interpolate(flat) value:f32)->@location(0) vec4f{"
        "let color=textureSampleLevel(image,sampling,vec2f(0.5),0.0);"
        "return color+vec4f(value,b.y+common_params.y,0,0);}";
    doe_shader *shaders[2]={compile(vertex),compile(fragment)};
    native_stage stages[2]={0};
    for (size_t i=0;i<2;++i) {
        assert(doe_shader_borrow(shaders[i],&stages[i].view)==DOE_SHADER_OK);
        stages[i].entry=&stages[i].view.entries[0];
    }
    projection(&stages[1],&stages[0]);
    assert(stages[0].view.resources[0].binding==2 && stages[1].view.resources[0].binding==5);
    native_pipeline pipeline={.binding_count=5,.bindings={
        {.group=2,.binding=4,.kind=DOE_BIND_SAMPLER},
        {.group=0,.binding=5,.kind=DOE_BIND_BUFFER,.buffer_type=WGPUBufferBindingType_Uniform},
        {.group=1,.binding=7,.kind=DOE_BIND_BUFFER,.buffer_type=WGPUBufferBindingType_Uniform},
        {.group=0,.binding=2,.kind=DOE_BIND_BUFFER,.buffer_type=WGPUBufferBindingType_Uniform},
        {.group=2,.binding=1,.kind=DOE_BIND_TEXTURE}}};
    native_d3d12_layout layouts[2];
    native_d3d12_graphics maps;
    assert(native_d3d12_graphics_maps(&pipeline,stages,layouts,&maps));
    assert(maps.stages[0].buffer_count==2 && maps.stages[0].buffer_indices[0]==3 && maps.stages[0].buffer_indices[1]==2);
    assert(maps.stages[1].buffer_count==2 && maps.stages[1].buffer_indices[0]==1 && maps.stages[1].buffer_indices[1]==2);
    assert(maps.stages[1].texture_count==1 && maps.stages[1].texture_indices[0]==4);
    assert(maps.stages[1].sampler_count==1 && maps.stages[1].sampler_indices[0]==0);
    assert(layouts[0].buffers[0].shader_register==2 && layouts[1].buffers[0].shader_register==5);
    assert(layouts[0].buffers[1].shader_register==7 && layouts[1].buffers[1].register_space==1);
    native_d3d12_graphics before=maps;
    native_d3d12_layout prior[2];
    memcpy(prior,layouts,sizeof(prior));
    pipeline.bindings[3].binding=99;
    assert(!native_d3d12_graphics_maps(&pipeline,stages,layouts,&maps));
    assert(!memcmp(&maps,&before,sizeof(maps)) && !memcmp(layouts,prior,sizeof(layouts)));
    pipeline.bindings[3].binding=2;
    native_stage swapped[2]={stages[1],stages[0]};
    assert(!native_d3d12_graphics_maps(&pipeline,swapped,layouts,&maps));
    assert(!memcmp(&maps,&before,sizeof(maps)));
    assert(native_d3d12_graphics_maps_count(&pipeline,stages,1,layouts,&maps));
    assert(maps.stage_count==1 && maps.stages[0].buffer_count==2 && !maps.stages[1].buffer_count &&
        !maps.stages[1].texture_count && !maps.stages[1].sampler_count && !maps.draw_normalize && !maps.index_scan);
    before=maps; memcpy(prior,layouts,sizeof(prior));
    assert(!native_d3d12_graphics_maps_count(&pipeline,stages,0,layouts,&maps) &&
        !memcmp(&maps,&before,sizeof(maps)) && !memcmp(layouts,prior,sizeof(layouts)));
    doe_shader_release(shaders[0]);
    doe_shader_release(shaders[1]);
    puts("PASS D3D12 independent VS/PS module ordinals, differing selected subsets, canonical shared binding, sampler/texture rows and untouched rejection; actual compiler only, no SDK/GPU");
    return 0;
}
