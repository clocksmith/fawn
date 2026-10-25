/* SPDX-License-Identifier: MIT */
#include "mock_native.h"
#include <math.h>
#include <stdio.h>

static doe_dx_texture *texture(doe_dx_context *context,uint32_t usage)
{
    doe_dx_texture *result = NULL;
    const doe_dx_texture_desc desc = {1,1,DOE_DX_R32_UINT,usage|DOE_DX_TEXTURE_COPY_DST};
    assert(doe_dx_texture_create(context,&desc,&result,NULL) == DOE_DX_OK);
    return result;
}
static void disjoint_storage_views(void)
{
    mock_reset(); doe_dx_context *context=NULL;
    assert(doe_dx_context_create(NULL,&context,NULL)==DOE_DX_OK);
    doe_dx_texture_shape shape={.image={4,4,DOE_DX_RGBA8_UNORM,DOE_DX_TEXTURE_SAMPLED|DOE_DX_TEXTURE_STORAGE},
        .dimension=2,.depth_or_layers=2,.mip_levels=2,.samples=1};
    doe_dx_texture *texture=NULL;
    assert(doe_dx_texture_create_shape(context,&shape,&texture,NULL)==DOE_DX_OK);
    unsigned char code=1;
    doe_dx_texture_binding_layout images[2]={{.format=DOE_DX_RGBA8_UNORM,.access=DOE_DX_READ_ONLY},
        {.shader_register=1,.format=DOE_DX_RGBA8_UNORM,.access=DOE_DX_READ_WRITE}};
    doe_dx_texture_contract contracts[2]={{.sample_class=DOE_DX_SAMPLE_FLOAT,.dimension=2},
        {.sample_class=DOE_DX_SAMPLE_EXACT,.storage=true,.dimension=2}};
    doe_dx_dynamic_pipeline_desc desc={.resources={.buffers={&code,1,NULL,0},.textures=images,.texture_count=2},
        .texture_contracts=contracts};
    doe_dx_pipeline *pipeline=NULL;
    assert(doe_dx_pipeline_create_dynamic(context,&desc,&pipeline,NULL)==DOE_DX_OK);
    doe_dx_commands *commands=NULL; assert(doe_dx_commands_create(context,&commands,NULL)==DOE_DX_OK);
    assert(doe_dx_commands_initialize_texture(commands,texture,NULL)==DOE_DX_OK);
    doe_dx_texture *textures[2]={texture,texture};
    doe_dx_texture_view_desc views[2]={{.format=DOE_DX_RGBA8_UNORM,.dimension=2,.mip_count=1,.layer_count=1},
        {.format=DOE_DX_RGBA8_UNORM,.dimension=2,.mip_count=1,.layer_count=1,.base_layer=1}};
    doe_dx_dynamic_resources values={.resources={.textures=textures,.texture_count=2},.texture_views=views};
    assert(doe_dx_commands_dispatch_dynamic(commands,pipeline,&values,1,1,1,NULL)==DOE_DX_OK && commands->texture_count==1);
    views[0].swizzle[0]=DOE_DX_SWIZZLE_B;
    assert(doe_dx_commands_dispatch_dynamic(commands,pipeline,&values,1,1,1,NULL)==DOE_DX_OK);
    views[1].swizzle[0]=DOE_DX_SWIZZLE_B;
    assert(doe_dx_commands_dispatch_dynamic(commands,pipeline,&values,1,1,1,NULL)==DOE_DX_INVALID_ARGUMENT);
    views[1].swizzle[0]=DOE_DX_SWIZZLE_IDENTITY;
    views[1].base_layer=0;
    assert(doe_dx_commands_dispatch_dynamic(commands,pipeline,&values,1,1,1,NULL)==DOE_DX_UNSUPPORTED);
    views[1].base_mip=1;
    assert(doe_dx_commands_dispatch_dynamic(commands,pipeline,&values,1,1,1,NULL)==DOE_DX_OK);
    doe_dx_texture_release(texture); doe_dx_pipeline_release(pipeline); doe_dx_commands_release(commands);
    assert(doe_dx_context_destroy(context)==DOE_DX_OK); mock_reset();
}
int main(void)
{
    disjoint_storage_views();
    mock_reset();
    doe_dx_context *context = NULL;
    assert(doe_dx_context_create(NULL,&context,NULL) == DOE_DX_OK);
    doe_dx_texture *source = texture(context,DOE_DX_TEXTURE_SAMPLED|DOE_DX_TEXTURE_STORAGE);
    doe_dx_texture *writable = texture(context,DOE_DX_TEXTURE_STORAGE);
    doe_dx_texture *storage_only = texture(context,DOE_DX_TEXTURE_STORAGE);
    doe_dx_buffer *zero = NULL;
    assert(doe_dx_buffer_create(context,4,DOE_DX_UPLOAD,&zero,NULL) == DOE_DX_OK);
    doe_dx_commands *commands = NULL;
    assert(doe_dx_commands_create(context,&commands,NULL) == DOE_DX_OK);
    const unsigned char bytecode = 1;
    doe_dx_texture_binding_layout textures[3] = {
        {0,0,DOE_DX_RGBA8_UNORM,DOE_DX_READ_ONLY, false},
        {1,0,DOE_DX_R32_UINT,DOE_DX_READ_ONLY, false},
        {2,0,DOE_DX_R32_UINT,DOE_DX_READ_WRITE, false}};
    doe_dx_texture_contract contracts[3] = {{.sample_class=DOE_DX_SAMPLE_UINT},
        {.sample_class=DOE_DX_SAMPLE_EXACT,.storage=true},{.sample_class=DOE_DX_SAMPLE_EXACT,.storage=true}};
    doe_dx_sampler_layout sampler_layout = {7,3,false};
    doe_dx_dynamic_pipeline_desc desc = {.resources={.buffers={&bytecode,1,NULL,0},
        .textures=textures,.texture_count=3},.texture_contracts=contracts,.samplers=&sampler_layout,.sampler_count=1};
    doe_dx_pipeline *pipeline = NULL;
    assert(doe_dx_pipeline_create_dynamic(context,&desc,&pipeline,NULL) == DOE_DX_OK);
    assert(mock.dynamic_sampler_count == 1 && mock.dynamic_layouts[0].shader_register == 7);
    contracts[0].sample_class = DOE_DX_SAMPLE_FLOAT;
    sampler_layout.comparison = true; /* Caller mutation cannot change published declarations. */
    doe_dx_sampler_desc sampler = {.min_filter=DOE_DX_LINEAR,.mag_filter=DOE_DX_NEAREST,
        .mip_filter=DOE_DX_LINEAR,.address_u=DOE_DX_REPEAT,.address_v=DOE_DX_MIRROR,
        .address_w=DOE_DX_CLAMP,.min_lod=0.5f,.max_lod=8,.max_anisotropy=1};
    doe_dx_texture *images[3] = {source,source,writable};
    doe_dx_dynamic_resources values = {.resources={NULL,0,images,3},.samplers=&sampler,.sampler_count=1};
    assert(doe_dx_commands_dispatch_dynamic(commands,pipeline,&values,1,1,1,NULL) == DOE_DX_UNINITIALIZED);
    assert(!commands->texture_count && !commands->pipeline_count && !mock.dispatches);
    assert(doe_dx_commands_upload_texture(commands,zero,(doe_dx_texture_layout){0},source,NULL) == DOE_DX_OK);
    assert(doe_dx_commands_upload_texture(commands,zero,(doe_dx_texture_layout){0},writable,NULL) == DOE_DX_OK);
    assert(doe_dx_commands_upload_texture(commands,zero,(doe_dx_texture_layout){0},storage_only,NULL) == DOE_DX_OK);
    values.sampler_count = 0;
    assert(doe_dx_commands_dispatch_dynamic(commands,pipeline,&values,1,1,1,NULL) == DOE_DX_INVALID_ARGUMENT);
    values.sampler_count = 1;
    sampler.compare = DOE_DX_SAMPLER_LESS;
    assert(doe_dx_commands_dispatch_dynamic(commands,pipeline,&values,1,1,1,NULL) == DOE_DX_INVALID_ARGUMENT);
    sampler.compare = DOE_DX_SAMPLER_NO_COMPARE;
    sampler.max_anisotropy = 2;
    assert(doe_dx_commands_dispatch_dynamic(commands,pipeline,&values,1,1,1,NULL) == DOE_DX_INVALID_ARGUMENT);
    sampler.max_anisotropy = 1;
    sampler.min_lod = NAN;
    assert(doe_dx_commands_dispatch_dynamic(commands,pipeline,&values,1,1,1,NULL) == DOE_DX_INVALID_ARGUMENT);
    sampler.min_lod = 0.5f;
    images[0] = storage_only;
    assert(doe_dx_commands_dispatch_dynamic(commands,pipeline,&values,1,1,1,NULL) == DOE_DX_UNSUPPORTED);
    images[0] = source;
    images[2] = source;
    assert(doe_dx_commands_dispatch_dynamic(commands,pipeline,&values,1,1,1,NULL) == DOE_DX_UNSUPPORTED);
    images[2] = writable;
    mock.failure = MOCK_DISPATCH;
    assert(doe_dx_commands_dispatch_dynamic(commands,pipeline,&values,1,1,1,NULL) == DOE_DX_NATIVE_FAILURE);
    mock.failure = MOCK_NONE;
    assert(!commands->pipeline_count && pipeline->references == 1 && !mock.dispatches);
    assert(doe_dx_commands_dispatch_dynamic(commands,pipeline,&values,1,1,1,NULL) == DOE_DX_OK);
    assert(commands->texture_count == 3 && mock.texture_count == 3 && mock.dynamic_sampler_count == 1);
    assert(mock.texture_bindings[0] == mock.texture_bindings[1]);
    assert(mock.dynamic_values[0].min_lod == 0.5f && mock.dynamic_values[0].address_v == DOE_DX_MIRROR);
    sampler.min_lod = 2;
    assert(mock.dynamic_values[0].min_lod == 0.5f); /* Borrowed values were copied at record time. */
    doe_dx_dynamic_pipeline_desc read_desc = {.resources={.buffers={&bytecode,1,NULL,0},
        .textures=&textures[1],.texture_count=1},.texture_contracts=&contracts[1]};
    doe_dx_pipeline *read_pipeline = NULL;
    assert(doe_dx_pipeline_create_dynamic(context,&read_desc,&read_pipeline,NULL) == DOE_DX_OK);
    doe_dx_dynamic_resources read_values = {.resources={NULL,0,&storage_only,1}};
    assert(doe_dx_commands_dispatch_dynamic(commands,read_pipeline,&read_values,1,1,1,NULL) == DOE_DX_OK);
    assert(!(storage_only->desc.usage & DOE_DX_TEXTURE_SAMPLED));
    doe_dx_pipeline *untouched = (doe_dx_pipeline *)(uintptr_t)1;
    contracts[0].storage = true;
    assert(doe_dx_pipeline_create_dynamic(context,&desc,&untouched,NULL) == DOE_DX_INVALID_ARGUMENT);
    assert(untouched == (doe_dx_pipeline *)(uintptr_t)1);
    contracts[0] = (doe_dx_texture_contract){.sample_class=DOE_DX_SAMPLE_UINT};
    desc.sampler_count = DOE_DX_MAX_DYNAMIC_SAMPLERS + 1;
    assert(doe_dx_pipeline_create_dynamic(context,&desc,&untouched,NULL) == DOE_DX_RANGE);
    doe_dx_pipeline_release(pipeline);
    doe_dx_pipeline_release(read_pipeline);
    doe_dx_texture_release(source);
    doe_dx_texture_release(writable);
    doe_dx_texture_release(storage_only);
    doe_dx_buffer_release(zero);
    mock.failure = MOCK_SIGNAL;
    assert(doe_dx_commands_submit(commands,NULL) == DOE_DX_COMPLETION_UNKNOWN);
    doe_dx_commands_release(commands);
    assert(mock.textures == 3 && mock.pipelines == 2 && mock.commands == 1);
    mock.failure = MOCK_NONE;
    assert(doe_dx_context_poll(context,0,NULL) == DOE_DX_PENDING);
    mock.completed = 1;
    assert(doe_dx_context_poll(context,0,NULL) == DOE_DX_OK);
    assert(!mock.textures && !mock.pipelines && !mock.commands && !mock.buffers);
    assert(doe_dx_context_destroy(context) == DOE_DX_OK);
    mock_reset();
    puts("PASS D3D12 sampled/storage SRV aliases and immutable runtime sampler values: actual owner preflight/rollback/unknown leases; no SDK/GPU");
}
