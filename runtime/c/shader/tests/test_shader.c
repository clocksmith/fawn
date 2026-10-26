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
    assert(doe_shader_matches(shader,DOE_SHADER_SPIRV, options.semantic_flags,source,strlen(source),options.compiler_identity,17,&options.selection));
    assert(!doe_shader_matches(shader,DOE_SHADER_SPIRV, options.semantic_flags,source,strlen(source),"developer-build-B",17,&options.selection));
    source[0]='!';
    assert(!doe_shader_matches(shader,DOE_SHADER_SPIRV, options.semantic_flags,source,strlen(source),options.compiler_identity,17,&options.selection));
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
    assert(view.module.schema_version==DOE_COMPILER_ATOMIC_SCHEMA_VERSION);
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
    const char *matrix_source =
        "struct Uniform{m:mat3x2f} @group(0) @binding(0) var<uniform> u:Uniform;"
        "@group(0) @binding(1) var<storage,read_write> result:array<vec2f>;"
        "@compute @workgroup_size(1) fn matrix(){result[0]=u.m*vec3f(1.0);}";
    const char *module_sources[] = {NULL, "// empty module", "fn helper()->u32{return 2u;}", matrix_source};
    for(size_t source_index=0;source_index<4;source_index++) {
        const char *text=module_sources[source_index];
        const size_t length=text?strlen(text):0;
        shader=sentinel;
        for(unsigned ordinal=1;ordinal<=2;ordinal++) {
            a=(allocations){.fail_at=ordinal};
            assert(doe_shader_create(&options,text,length,&shader,&error)==DOE_SHADER_OUT_OF_MEMORY);
            assert(shader==sentinel && a.live==0);
        }
        a=(allocations){0};
        assert(doe_shader_create(&options,text,length,&shader,&error)==DOE_SHADER_OK);
        assert(doe_shader_borrow(shader,&view)==DOE_SHADER_OK);
        assert(view.module.schema_version==DOE_COMPILER_ATOMIC_SCHEMA_VERSION);
        assert(doe_shader_matches(shader,DOE_SHADER_SPIRV, options.semantic_flags,text,length,options.compiler_identity,17,&options.selection));
        if(source_index<3) {
            assert(view.counts.words==0 && view.counts.entries==0);
        } else {
            assert(view.counts.words>0 && view.counts.entries==1);
            bool found=false;
            for(size_t type=0;type<view.counts.types;type++) {
                if(view.types[type].kind!=DOE_COMPILER_TYPE_MATRIX) continue;
                assert(view.types[type].array_count==3 && view.types[type].vector_width==2);
                assert(view.types[type].stride_bytes==8 && view.types[type].size_bytes==24);
                found=true;
            }
            assert(found && view.resources[0].minimum_binding_size_bytes==24);
        }
        doe_shader_release(shader);
        assert(a.live==0 && a.calls==2);
    }
    const char *atomic_source =
        "@group(0) @binding(0) var<storage,read_write> data:array<atomic<u32>>;"
        "@compute @workgroup_size(1) fn atomic_main(){atomicStore(&data[0],3u);}";
    shader = sentinel;
    for (unsigned ordinal = 1u; ordinal <= 2u; ++ordinal) {
        a = (allocations){.fail_at = ordinal};
        assert(doe_shader_create(&options, atomic_source, strlen(atomic_source), &shader, &error) == DOE_SHADER_OUT_OF_MEMORY);
        assert(shader == sentinel && a.live == 0u);
    }
    a = (allocations){0};
    assert(doe_shader_create(&options, atomic_source, strlen(atomic_source), &shader, &error) == DOE_SHADER_OK);
    assert(doe_shader_borrow(shader, &view) == DOE_SHADER_OK);
    assert(view.module.schema_version == DOE_COMPILER_ATOMIC_SCHEMA_VERSION);
    bool atomic_found = false;
    for (size_t i = 0u; i < view.counts.types; ++i) {
        if (view.types[i].kind != DOE_COMPILER_TYPE_ATOMIC) continue;
        assert(view.types[i].element_type < view.counts.types && view.types[i].size_bytes == 4u);
        assert(view.types[view.types[i].element_type].scalar_kind == DOE_COMPILER_LEAF_U32);
        assert((view.types[i].flags & DOE_COMPILER_TYPE_CONSTRUCTIBLE) == 0u);
        atomic_found = true;
    }
    assert(atomic_found);
    assert(doe_shader_retain(shader) == DOE_SHADER_OK);
    doe_shader_release(shader);
    assert(a.live == 1u && view.words[0] == 0x07230203u);
    doe_shader_release(shader);
    assert(a.live == 0u);
    /* One owned artifact, explicit target identity, no native source reparse. */
    options.target = DOE_SHADER_METAL;
    options.selection = (DoeCompilerEntrySelectionV5){
        .mode = DOE_COMPILER_SELECT_NAME, .name = "second", .name_length = 6};
    source[0] = '@';
    shader = sentinel;
    for (unsigned ordinal = 1u; ordinal <= 3u; ++ordinal) {
        a = (allocations){.fail_at = ordinal};
        assert(doe_shader_create(&options, source, strlen(source), &shader, &error) == DOE_SHADER_OUT_OF_MEMORY);
        assert(shader == sentinel && a.live == 0u);
    }
    a = (allocations){0};
    assert(doe_shader_create(&options, source, strlen(source), &shader, &error) == DOE_SHADER_OK);
    assert(a.calls == 3u && a.live == 1u);
    assert(doe_shader_borrow(shader, &view) == DOE_SHADER_OK);
    assert(view.target == DOE_SHADER_METAL && view.target_language_version == 300u);
    assert(view.words == NULL && view.counts.words == 0u && view.text_size != 0u);
    assert(view.counts.entries == 2u && view.metal_entry_count == 1u);
    assert(view.metal_entries[0].entry_index == 1u);
    assert(strcmp(view.metal_entries[0].name, "second") == 0);
    assert(view.metal_entries[0].sizes_slot == 30u && view.metal_entries[0].sizes_bytes == 4u);
    assert(view.metal_binding_count == 1u);
    assert(view.metal_bindings[0].resource_index == 1u && view.metal_bindings[0].slot == 0u);
    assert(view.metal_bindings[0].table == DOE_COMPILER_TEXT_TABLE_BUFFER);
    assert(view.metal_bindings[0].size_index == 0u);
    assert(doe_shader_matches(shader, DOE_SHADER_METAL, options.semantic_flags, source, strlen(source),
        options.compiler_identity, options.compiler_identity_size, &options.selection));
    assert(!doe_shader_matches(shader, DOE_SHADER_SPIRV, options.semantic_flags, source, strlen(source),
        options.compiler_identity, options.compiler_identity_size, &options.selection));
    char first_text_byte = view.text[0];
    source[0] = '!';
    assert(view.text[0] == first_text_byte);
    assert(doe_shader_retain(shader) == DOE_SHADER_OK);
    doe_shader_release(shader);
    assert(a.live == 1u && view.metal_entries[0].sizes_bytes == 4u);
    doe_shader_release(shader);
    assert(a.live == 0u);
    source[0] = '@';
    shader = sentinel;
    options.target = (doe_shader_target)99;
    a = (allocations){0};
    assert(doe_shader_create(&options, source, strlen(source), &shader, &error) == DOE_SHADER_INVALID_ARGUMENT);
    assert(shader == sentinel && a.calls == 0u);
    options.target = DOE_SHADER_METAL;
    options.selection = (DoeCompilerEntrySelectionV5){.mode = DOE_COMPILER_SELECT_NONE};
    for (unsigned ordinal = 1u; ordinal <= 3u; ++ordinal) {
        a = (allocations){.fail_at=ordinal};
        assert(doe_shader_create(&options,atomic_source,strlen(atomic_source),&shader,&error) == DOE_SHADER_OUT_OF_MEMORY);
        assert(shader == sentinel && !a.live);
    }
    a = (allocations){0};
    assert(doe_shader_create(&options,atomic_source,strlen(atomic_source),&shader,&error) == DOE_SHADER_OK);
    assert(doe_shader_borrow(shader,&view) == DOE_SHADER_OK);
    assert(view.target == DOE_SHADER_METAL && !view.words && view.text_size && view.metal_entry_count == 1);
    assert(!strcmp(view.metal_entries[0].name,"atomic_main"));
    doe_shader_release(shader);
    shader = sentinel;
    const char *invalid_atomic = "@group(0) @binding(0) var<storage,read_write> data:array<u32>;"
        "@compute @workgroup_size(1) fn bad(){atomicStore(&data[0],3u);}";
    assert(doe_shader_create(&options,invalid_atomic,strlen(invalid_atomic),&shader,&error) == DOE_SHADER_COMPILE_FAILED);
    assert(shader == sentinel && !a.live && error.diagnostic.message[0]);
    assert(doe_shader_create(&options, "", 0u, &shader, &error) == DOE_SHADER_OK);
    assert(doe_shader_borrow(shader, &view) == DOE_SHADER_OK);
    assert(view.target == DOE_SHADER_METAL && view.text == NULL && view.text_size == 0u);
    assert(view.metal_entry_count == 0u && view.metal_binding_count == 0u);
    doe_shader_release(shader);
    assert(a.live == 0u);
    options.target = DOE_SHADER_HLSL;
    options.selection = (DoeCompilerEntrySelectionV5){.mode=DOE_COMPILER_SELECT_NAME,
        .name="selected",.name_length=8};
    char hlsl_source[] =
        "@group(2) @binding(9) var<storage,read_write> output:array<u32>;"
        "@group(1) @binding(2) var<uniform> scale:vec4u;"
        "@compute @workgroup_size(1) fn first(){output[0]=0u;}"
        "@compute @workgroup_size(4) fn selected(@builtin(num_workgroups) groups:vec3u){"
        "output[0]=scale.x+groups.x;}";
    shader = sentinel;
    for (unsigned ordinal = 1; ordinal <= 3; ++ordinal) {
        a = (allocations){.fail_at=ordinal};
        assert(doe_shader_create(&options, hlsl_source, strlen(hlsl_source), &shader, &error) == DOE_SHADER_OUT_OF_MEMORY);
        assert(shader == sentinel && !a.live);
    }
    a = (allocations){0};
    assert(doe_shader_create(&options, hlsl_source, strlen(hlsl_source), &shader, &error) == DOE_SHADER_OK);
    assert(a.calls == 3 && a.live == 1);
    assert(doe_shader_borrow(shader, &view) == DOE_SHADER_OK);
    assert(view.target == DOE_SHADER_HLSL && view.target_language_version == 600);
    assert(!view.words && !view.counts.words && view.text_size && view.counts.entries == 2);
    assert(!view.metal_bindings && !view.metal_entries && !view.metal_binding_count && !view.metal_entry_count);
    assert(view.hlsl_binding_count == 2 && view.hlsl_entry_count == 1);
    assert(view.hlsl_bindings[0].register_kind == DOE_COMPILER_HLSL_REGISTER_UAV);
    assert(view.hlsl_bindings[0].shader_register == 9 && view.hlsl_bindings[0].register_space == 2);
    assert(view.hlsl_bindings[0].stride == 4 && view.hlsl_bindings[1].register_kind == DOE_COMPILER_HLSL_REGISTER_CBV);
    assert(view.hlsl_entries[0].entry_index == 1 && !strcmp(view.hlsl_entries[0].name, "selected"));
    assert(!view.hlsl_entries[0].flags && view.hlsl_entries[0].dispatch_bytes == 16);
    assert(view.hlsl_entries[0].dispatch_register == 0 && view.hlsl_entries[0].dispatch_space == 7);
    assert(doe_shader_matches(shader, DOE_SHADER_HLSL, options.semantic_flags, hlsl_source, strlen(hlsl_source),
        options.compiler_identity, options.compiler_identity_size, &options.selection));
    assert(!doe_shader_matches(shader, DOE_SHADER_METAL, options.semantic_flags, hlsl_source, strlen(hlsl_source),
        options.compiler_identity, options.compiler_identity_size, &options.selection));
    hlsl_source[0] = '!';
    assert(view.text[0] != '!' && doe_shader_retain(shader) == DOE_SHADER_OK);
    doe_shader_release(shader);
    assert(a.live == 1 && !strcmp(view.hlsl_entries[0].name, "selected"));
    doe_shader_release(shader);
    assert(!a.live);
    options.selection = (DoeCompilerEntrySelectionV5){0};
    assert(doe_shader_create(&options, "", 0, &shader, &error) == DOE_SHADER_OK);
    assert(doe_shader_borrow(shader, &view) == DOE_SHADER_OK && !view.text && !view.hlsl_entry_count);
    doe_shader_release(shader);
    assert(!a.live);
    /* Same semantic policy reaches measure/fill for every target and is part of
     * identity even when the caller deliberately supplies identical identity bytes. */
    const char sparse[] = "@group(3) @binding(999) var<storage,read_write> data:array<u32>;"
        "@compute @workgroup_size(1) fn main(){data[0]=19u;}";
    const char compact[] = "@group(3) @binding(999) var<uniform> data:array<u32,2>;"
        "@group(0) @binding(999) var<storage,read_write> output:array<u32>;"
        "@compute @workgroup_size(1) fn main(){output[0]=data[1];}";
    options.selection = (DoeCompilerEntrySelectionV5){.mode=DOE_COMPILER_SELECT_AUTO};
    for (unsigned target = DOE_SHADER_SPIRV; target <= DOE_SHADER_HLSL; ++target) {
        options.target = (doe_shader_target)target;
        options.semantic_flags = 0;
        a = (allocations){0};
        shader = sentinel;
        assert(doe_shader_create(&options, sparse, strlen(sparse), &shader, &error) == DOE_SHADER_COMPILE_FAILED);
        assert(shader == sentinel && !a.live);
        options.semantic_flags = DOE_COMPILER_CORE_BINDING_NUMBERS;
        unsigned acquisitions = target == DOE_SHADER_SPIRV ? 2u : 3u;
        for (unsigned ordinal = 1; ordinal <= acquisitions; ++ordinal) {
            a = (allocations){.fail_at=ordinal};
            assert(doe_shader_create(&options, sparse, strlen(sparse), &shader, &error) == DOE_SHADER_OUT_OF_MEMORY);
            assert(shader == sentinel && !a.live);
        }
        a = (allocations){0};
        assert(doe_shader_create(&options, sparse, strlen(sparse), &shader, &error) == DOE_SHADER_OK);
        assert(doe_shader_borrow(shader, &view) == DOE_SHADER_OK);
        assert(view.resources[0].group == 3 && view.resources[0].binding == 999);
        assert(doe_shader_matches(shader, options.target, options.semantic_flags,
            sparse, strlen(sparse), options.compiler_identity, options.compiler_identity_size, &options.selection));
        assert(!doe_shader_matches(shader, options.target, 0,
            sparse, strlen(sparse), options.compiler_identity, options.compiler_identity_size, &options.selection));
        assert(!doe_shader_matches(shader, options.target, options.semantic_flags | DOE_COMPILER_UNIFORM_BUFFER_STANDARD_LAYOUT,
            sparse, strlen(sparse), options.compiler_identity, options.compiler_identity_size, &options.selection));
        if (target == DOE_SHADER_METAL) {
            assert(view.metal_binding_count == 1 && view.metal_bindings[0].slot == 0);
            assert(!view.target_binding_count && !view.target_entry_count);
        }
        doe_shader_release(shader);
        assert(!a.live);
        options.semantic_flags |= DOE_COMPILER_UNIFORM_BUFFER_STANDARD_LAYOUT;
        assert(doe_shader_create(&options, compact, strlen(compact), &shader, &error) == DOE_SHADER_OK);
        assert(doe_shader_borrow(shader, &view) == DOE_SHADER_OK);
        assert(view.module.layout_profile == DOE_COMPILER_TYPED_PROFILE_STANDARD_UNIFORM);
        assert(view.resources[0].minimum_binding_size_bytes == 8 && view.resources[0].address_space_alignment_bytes == 4);
        doe_shader_release(shader);
        assert(!a.live);
        options.semantic_flags = DOE_COMPILER_TEXT_CODE; /* Output bits are not semantic options. */
        a = (allocations){0};
        shader = sentinel;
        assert(doe_shader_create(&options, sparse, strlen(sparse), &shader, &error) == DOE_SHADER_INVALID_ARGUMENT);
        assert(shader == sentinel && !a.calls);
        /* Bit32 now has the documented device-disabled-f16 meaning. Keep the
         * unknown-option rejection oracle on an actually unassigned bit. */
        options.semantic_flags = DOE_COMPILER_DISABLE_F16 | DOE_COMPILER_CORE_BINDING_NUMBERS;
        assert(doe_shader_create(&options, sparse, strlen(sparse), &shader, &error) == DOE_SHADER_OK);
        doe_shader_release(shader);
        assert(!a.live);
        a = (allocations){0};
        shader = sentinel;
        /* 64 is the admitted device-disabled-subgroups bit, not unknown input. */
        options.semantic_flags = DOE_COMPILER_DISABLE_SUBGROUPS | DOE_COMPILER_CORE_BINDING_NUMBERS;
        assert(doe_shader_create(&options, sparse, strlen(sparse), &shader, &error) == DOE_SHADER_OK);
        doe_shader_release(shader);
        assert(!a.live);
        a = (allocations){0};
        shader = sentinel;
        options.semantic_flags = 1024; /* Unassigned semantic option. */
        assert(doe_shader_create(&options, sparse, strlen(sparse), &shader, &error) == DOE_SHADER_INVALID_ARGUMENT);
        assert(shader == sentinel && !a.calls);
    }
    puts("owned shader: buffer/handle/stage metadata, matrix/library/owned Metal artifacts, identity, allocation rollback, lifetime PASS");
    return 0;
}
