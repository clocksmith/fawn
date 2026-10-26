/* SPDX-License-Identifier: MIT
 * Opt-in growth is host allocation/admission evidence, not GPU execution. */
#include "doe_shader.h"
#include <assert.h>
#include <stdalign.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct allocation_state {
    size_t calls, fail_at, live, mutate_at;
    char *source;
    struct { void *memory; size_t size, alignment; } blocks[8];
} allocation_state;
static void *allocate(void *userdata, size_t size, size_t alignment)
{
    allocation_state *s = userdata;
    assert(size && alignment <= alignof(max_align_t));
    ++s->calls;
    if (s->calls == s->mutate_at) s->source[0] = '!';
    if (s->calls == s->fail_at) return NULL;
    void *memory = malloc(size);
    if (memory) {
        size_t i = 0;
        while (i < 8 && s->blocks[i].memory) ++i;
        assert(i < 8);
        s->blocks[i].memory = memory;
        s->blocks[i].size = size;
        s->blocks[i].alignment = alignment;
        ++s->live;
    }
    return memory;
}
static void deallocate(void *userdata, void *memory, size_t size, size_t alignment)
{
    allocation_state *s = userdata;
    size_t i = 0;
    while (i < 8 && s->blocks[i].memory != memory) ++i;
    assert(i < 8 && s->blocks[i].size == size && s->blocks[i].alignment == alignment);
    s->blocks[i].memory = NULL;
    assert(s->live);
    --s->live;
    free(memory);
}
static doe_shader_options options(allocation_state *s, doe_shader_target target)
{
    return (doe_shader_options){.allocator={s,allocate,deallocate},.target=target,
        .compiler_identity="growth-control",.compiler_identity_size=14,
        .capacities={.typed={.source_bytes=16384,.tokens=64,.statements=8,.expressions=32,
            .types=32,.members=32,.resources=8,.access_steps=64,.constants=32,.constant_edges=64,
            .ir_instructions=128,.ir_operands=64,.target_declarations=128,.traversal_items=64,
            .words=512,.expression_depth=16,.control_depth=8,.type_syntax_depth=16},
            .entries=4,.entry_inputs=8,.entry_references=32},
        .interface_attributes=16,.stage_interfaces=16,.sampling_pairs=16};
}

static void names_case(doe_shader_target target)
{
    allocation_state s = {0};
    doe_shader_options o = options(&s,target);
    o.grow_pools = true;
    char name[258], source[8192];
    memset(name,'n',sizeof(name)-1); name[sizeof(name)-1]=0;
    int written=snprintf(source,sizeof(source),"@compute @workgroup_size(1) fn %s(){var x=0u;",name);
    assert(written>0);
    for (unsigned i=0;i<300;++i) strcat(source,"x=x+1u;");
    strcat(source,"}");
    size_t source_size=strlen(source);
    o.selection=(DoeCompilerEntrySelectionV5){.mode=DOE_COMPILER_SELECT_NAME,.name=name,.name_length=strlen(name)};
    s.mutate_at=2; s.source=name;
    doe_shader *shader=NULL; doe_shader_error error;
    assert(doe_shader_create(&o,source,source_size,&shader,&error)==DOE_SHADER_OK);
    assert(name[0]=='!' && s.calls>2 && s.live==1);
    name[0]='n';
    assert(doe_shader_matches(shader,target,0,source,source_size,o.compiler_identity,o.compiler_identity_size,&o.selection));
    size_t calls=s.calls;
    doe_shader_release(shader); assert(!s.live);
    for (size_t fail=1;fail<=calls;++fail) {
        s=(allocation_state){.fail_at=fail}; shader=(doe_shader *)(uintptr_t)1;
        assert(doe_shader_create(&o,source,source_size,&shader,&error)==DOE_SHADER_OUT_OF_MEMORY);
        assert(shader==(doe_shader *)(uintptr_t)1 && !s.live);
    }
    s=(allocation_state){0}; o.selection=(DoeCompilerEntrySelectionV5){.mode=DOE_COMPILER_SELECT_NONE};
    doe_shader *module=NULL;
    assert(doe_shader_create_module(&o,0,source,source_size,&module,&error)==DOE_SHADER_OK);
    source[0]='!';
    DoeCompilerEntrySelectionV5 selection={.mode=DOE_COMPILER_SELECT_NAME,.name=name,.name_length=strlen(name)};
    s.mutate_at=s.calls+1; s.source=name;
    assert(doe_shader_specialize(module,&selection,NULL,0,&shader,&error)==DOE_SHADER_OK);
    assert(name[0]=='!'); name[0]='n';
    assert(doe_shader_specialization_matches(shader,module,&selection,NULL,0));
    doe_shader_release(module);
    doe_shader_view view;
    assert(doe_shader_borrow(shader,&view)==DOE_SHADER_OK && view.counts.entries==1);
    const DoeCompilerNameV4 entry=view.entries[0].name;
    assert(entry.length==strlen(name) && !memcmp(view.names+entry.offset,name,entry.length));
    doe_shader_release(shader); assert(!s.live);
}
int main(void)
{
    names_case(DOE_SHADER_SPIRV); names_case(DOE_SHADER_METAL); names_case(DOE_SHADER_HLSL);
    puts("long shader selection ownership passed");
    return 0;
}
