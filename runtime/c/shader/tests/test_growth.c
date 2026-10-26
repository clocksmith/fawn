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
static void source_text(char source[16384])
{
    strcpy(source,"@compute @workgroup_size(1) fn main(){var x=0u;");
    for (unsigned i = 0; i < 1100; ++i) strcat(source,"x=x+1u;");
    strcat(source,"}");
}
static void target_case(doe_shader_target target)
{
    allocation_state s = {0};
    doe_shader_options o = options(&s,target);
    char source[16384];
    source_text(source);
    const size_t length = strlen(source);
    doe_shader *sentinel = (doe_shader *)(uintptr_t)1, *shader = sentinel;
    doe_shader_error error;
    assert(doe_shader_create(&o,source,length,&shader,&error) == DOE_SHADER_COMPILE_FAILED);
    assert(shader == sentinel && !s.live && s.calls == 1);
    assert(error.compiler_status == DOE_COMPILER_INPUT_ERROR);
    o.grow_pools = true;
    s = (allocation_state){.mutate_at=2,.source=source};
    assert(doe_shader_create(&o,source,length,&shader,&error) == DOE_SHADER_OK);
    assert(source[0] == '!' && s.calls > 2 && s.live == 1);
    doe_shader_view view;
    assert(doe_shader_borrow(shader,&view) == DOE_SHADER_OK);
    assert(target == DOE_SHADER_SPIRV ? view.counts.words > 0 : view.text_size > 0);
    const size_t allocations = s.calls;
    doe_shader_release(shader);
    assert(!s.live);
    source[0] = '@';
    for (size_t fail = 1; fail <= allocations; ++fail) {
        s = (allocation_state){.fail_at=fail};
        shader = sentinel;
        assert(doe_shader_create(&o,source,length,&shader,&error) == DOE_SHADER_OUT_OF_MEMORY);
        assert(shader == sentinel && !s.live);
    }
    o.capacities.typed.source_bytes = length - 1;
    s = (allocation_state){0};
    assert(doe_shader_create(&o,source,length,&shader,&error) == DOE_SHADER_COMPILE_FAILED);
    assert(shader == sentinel && !s.live && s.calls == 1);
    o.capacities.typed.source_bytes = 16384;
    s = (allocation_state){.mutate_at=2,.source=source};
    doe_shader *module = NULL;
    assert(doe_shader_create_module(&o,8,source,length,&module,&error) == DOE_SHADER_OK);
    assert(source[0] == '!' && s.live == 1);
    DoeCompilerEntrySelectionV5 selection = {.mode=DOE_COMPILER_SELECT_AUTO};
    shader = sentinel;
    assert(doe_shader_specialize(module,&selection,NULL,0,&shader,&error) == DOE_SHADER_OK);
    doe_shader_release(module);
    assert(doe_shader_borrow(shader,&view) == DOE_SHADER_OK);
    assert(target == DOE_SHADER_SPIRV ? view.counts.words > 0 : view.text_size > 0);
    doe_shader_release(shader);
    assert(!s.live);
    source[0] = '@';
    s = (allocation_state){0};
    assert(doe_shader_create_module(&o,8,source,length,&module,&error) == DOE_SHADER_OK);
    const size_t module_allocations = s.calls;
    bool completed = false;
    for (size_t fail = 1; fail <= 40; ++fail) {
        s.calls = 0;
        s.fail_at = fail;
        shader = sentinel;
        doe_shader_status status = doe_shader_specialize(module,&selection,NULL,0,&shader,&error);
        if (status == DOE_SHADER_OK) {
            doe_shader_release(shader);
            completed = true;
            break;
        }
        assert(status == DOE_SHADER_OUT_OF_MEMORY && shader == sentinel && s.live == 1);
        doe_shader_module_view retained;
        assert(doe_shader_borrow_module(module,&retained) == DOE_SHADER_OK && retained.entry_count == 1);
    }
    assert(completed && s.live == 1);
    doe_shader_release(module);
    for (size_t fail = 1; fail <= module_allocations; ++fail) {
        s = (allocation_state){.fail_at=fail};
        module = sentinel;
        assert(doe_shader_create_module(&o,8,source,length,&module,&error) == DOE_SHADER_OUT_OF_MEMORY);
        assert(module == sentinel && !s.live);
    }
}
int main(void)
{
    for (unsigned target = 0; target < 3; ++target) target_case((doe_shader_target)target);
    puts("shader pool growth: all3 targets, fixed opt-out, source bound, snapshot retry and allocation cleanup passed");
    return 0;
}
