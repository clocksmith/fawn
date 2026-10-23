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
    puts("owned shader: metadata, identity, allocation rollback, lifetime PASS");
    return 0;
}
