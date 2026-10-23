/* SPDX-License-Identifier: MIT */
#include "doe_shader.h"
#include <assert.h>
#include <stdalign.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

typedef struct { unsigned calls, fail_at, live; } allocations;
static void *allocate(void *userdata, size_t size, size_t alignment)
{
    allocations *a = userdata;
    assert(alignment <= alignof(max_align_t));
    if (++a->calls == a->fail_at) return NULL;
    void *p = malloc(size);
    if (p) ++a->live;
    return p;
}
static void deallocate(void *userdata, void *memory, size_t size, size_t alignment)
{
    allocations *a = userdata;
    (void)size;
    assert(alignment <= alignof(max_align_t) && a->live);
    --a->live;
    free(memory);
}
int main(void)
{
    allocations a = {0};
    doe_shader_options options = {
        .allocator = {&a, allocate, deallocate},
        .capacities = {
            .typed = {.source_bytes=16384,.tokens=2048,.statements=128,.expressions=512,
                .types=32,.members=64,.resources=8,.access_steps=512,.constants=64,.constant_edges=128,
                .ir_instructions=2048,.ir_operands=512,.target_declarations=512,.traversal_items=97,
                .words=16384,.expression_depth=32,.control_depth=16,.type_syntax_depth=32},
            .entries=8,.entry_inputs=40,.entry_references=64},
        .selection = {.mode=DOE_COMPILER_SELECT_NAME,.name="second",.name_length=6},
        .compiler_identity="developer-build-A", .compiler_identity_size=17
    };
    char source[] = "@group(0) @binding(0) var<storage,read_write> a:array<u32>;"
        "@group(0) @binding(1) var<storage,read_write> b:array<u32>;"
        "@compute @workgroup_size(1) fn first(){a[0]=2u;}"
        "@compute @workgroup_size(7) fn second(){b[0]=3u;}";
    doe_shader *sentinel = (doe_shader *)(uintptr_t)1;
    doe_shader *shader = sentinel;
    doe_shader_error error;
    for (unsigned ordinal=1; ordinal<=2; ++ordinal) {
        a = (allocations){.fail_at=ordinal};
        assert(doe_shader_create(&options,source,strlen(source),&shader,&error)==DOE_SHADER_OUT_OF_MEMORY);
        assert(shader==sentinel && a.live==0);
    }
    a = (allocations){0};
    assert(doe_shader_create(&options,source,strlen(source),&shader,&error)==DOE_SHADER_OK);
    assert(a.live==1 && a.calls==2);
    doe_shader_view view;
    assert(doe_shader_borrow(shader,&view)==DOE_SHADER_OK);
    assert(view.words[0]==0x07230203 && view.counts.entries==2 && view.module.selected_entry==1);
    const DoeCompilerEntryV5 *entry=&view.entries[1];
    assert(entry->workgroup_size[0]==7 && entry->resource_reference_count==1);
    uint32_t index=view.entry_references[entry->first_resource_reference];
    assert(view.resources[index].binding==1);
    assert(doe_shader_matches(shader,source,strlen(source),options.compiler_identity,17,&options.selection));
    assert(!doe_shader_matches(shader,source,strlen(source),"developer-build-B",17,&options.selection));
    source[0]='!';
    assert(!doe_shader_matches(shader,source,strlen(source),options.compiler_identity,17,&options.selection));
    assert(view.words[0]==0x07230203);
    assert(doe_shader_retain(shader)==DOE_SHADER_OK);
    doe_shader_release(shader);
    assert(a.live==1);
    doe_shader_release(shader);
    assert(a.live==0);
    shader=sentinel;
    assert(doe_shader_create(&options,source,strlen(source),&shader,&error)==DOE_SHADER_COMPILE_FAILED);
    assert(shader==sentinel && a.live==0 && error.diagnostic.status!=DOE_COMPILER_OK);
    options.selection.name="missing"; options.selection.name_length=7;
    source[0]='@';
    assert(doe_shader_create(&options,source,strlen(source),&shader,&error)==DOE_SHADER_COMPILE_FAILED);
    assert(shader==sentinel && a.live==0);
    const char *image_source =
        "@group(0) @binding(2) var input_tex:texture_2d<f32>;"
        "@group(0) @binding(3) var output_tex:texture_storage_2d<rgba8unorm,write>;"
        "@compute @workgroup_size(1) fn copy_image(){"
        "textureStore(output_tex,vec2i(0),textureLoad(input_tex,vec2i(0),0));}";
    options.selection = (DoeCompilerEntrySelectionV5){.mode=DOE_COMPILER_SELECT_NONE};
    for (unsigned ordinal=1; ordinal<=2; ++ordinal) {
        a = (allocations){.fail_at=ordinal};
        assert(doe_shader_create(&options,image_source,strlen(image_source),&shader,&error)==DOE_SHADER_OUT_OF_MEMORY);
        assert(shader==sentinel && a.live==0);
    }
    a = (allocations){0};
    assert(doe_shader_create(&options,image_source,strlen(image_source),&shader,&error)==DOE_SHADER_OK);
    assert(doe_shader_borrow(shader,&view)==DOE_SHADER_OK);
    assert(view.module.schema_version==DOE_COMPILER_STAGES_SCHEMA_VERSION);
    assert(view.handle_count==2 && view.counts.resources==2);
    for (size_t i=0; i<view.handle_count; ++i) {
        const DoeCompilerHandleV6 *handle=&view.handles[i];
        assert(handle->resource_index==i && handle->view_dimension==DOE_COMPILER_VIEW_2D);
        assert(handle->sample_scalar==DOE_COMPILER_LEAF_F32);
        const DoeCompilerResourceV4 *resource=&view.resources[handle->resource_index];
        assert(resource->type_index==DOE_COMPILER_MODULE_NONE);
        assert(resource->minimum_binding_size_bytes==0);
    }
    assert(view.resources[0].kind==DOE_COMPILER_SAMPLED_TEXTURE);
    assert(view.resources[1].kind==DOE_COMPILER_STORAGE_TEXTURE);
    assert(view.handles[1].storage_format==DOE_COMPILER_FORMAT_RGBA8UNORM);
    doe_shader_release(shader);
    assert(a.live==0);
    const char *graphics_source =
        "struct V{@builtin(position) p:vec4f,@location(3) @interpolate(flat) tag:u32}"
        "@vertex fn vertex(@builtin(vertex_index) index:u32)->V{return V(vec4f(1.0),index);}"
        "@fragment fn fragment(input:V)->@location(0) vec4f{return vec4f(f32(input.tag));}";
    shader=sentinel;
    assert(doe_shader_create(&options,graphics_source,strlen(graphics_source),&shader,&error)==DOE_SHADER_COMPILE_FAILED);
    assert(shader==sentinel && a.live==0);
    options.interface_attributes=32;
    options.stage_interfaces=32;
    for(unsigned ordinal=1;ordinal<=2;ordinal++) {
        a=(allocations){.fail_at=ordinal};
        assert(doe_shader_create(&options,graphics_source,strlen(graphics_source),&shader,&error)==DOE_SHADER_OUT_OF_MEMORY);
        assert(shader==sentinel && a.live==0);
    }
    a=(allocations){0};
    assert(doe_shader_create(&options,graphics_source,strlen(graphics_source),&shader,&error)==DOE_SHADER_OK);
    assert(doe_shader_borrow(shader,&view)==DOE_SHADER_OK);
    assert(view.counts.entries==2 && view.interface_count==6 && view.handle_count==0);
    assert(view.entries[0].stage==DOE_COMPILER_STAGE_VERTEX);
    assert(view.entries[1].stage==DOE_COMPILER_STAGE_FRAGMENT);
    assert(view.entries[0].workgroup_size[0]==0 && view.entries[1].static_builtin_mask==0);
    assert(view.interfaces[0].builtin==DOE_COMPILER_BUILTIN_VERTEX_INDEX);
    assert(view.interfaces[0].direction==DOE_COMPILER_INTERFACE_INPUT);
    assert(view.interfaces[2].entry_index==0 && view.interfaces[2].location==3);
    assert(view.interfaces[2].interpolation==DOE_COMPILER_INTERPOLATION_FLAT);
    assert(view.interfaces[5].entry_index==1 && view.interfaces[5].location==0);
    assert(view.interfaces[5].direction==DOE_COMPILER_INTERFACE_OUTPUT);
    for(size_t i=0;i<view.interface_count;i++) {
        assert(view.interfaces[i].name.offset+view.interfaces[i].name.length<view.counts.name_bytes);
        assert(view.names[view.interfaces[i].name.offset+view.interfaces[i].name.length]=='\0');
    }
    assert(a.calls==2 && a.live==1);
    doe_shader_release(shader);
    assert(a.live==0);
    puts("owned shader: buffer/handle/stage metadata, identity, allocation rollback, lifetime PASS");
    return 0;
}
