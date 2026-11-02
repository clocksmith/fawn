/* SPDX-License-Identifier: MIT
 * Count the real compiler analysis boundary across shader-owned publication. */
#include "doe_shader.h"
#include "../../compiler/src/compile_internal.h"
#include <assert.h>
#include <stdalign.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static size_t analysis_calls, live_allocations;
bool __real_compiler_analyze(Source *, CompilerWorkspace *, NumericEnvironment *);
bool __wrap_compiler_analyze(Source *, CompilerWorkspace *, NumericEnvironment *);
bool __wrap_compiler_analyze(Source *source, CompilerWorkspace *compiler, NumericEnvironment *environment)
{
    ++analysis_calls;
    return __real_compiler_analyze(source, compiler, environment);
}
static void *allocate(void *context, size_t bytes, size_t alignment)
{
    (void)context;
    assert(bytes && alignment <= alignof(max_align_t));
    void *result = malloc(bytes);
    if (result) ++live_allocations;
    return result;
}
static void release(void *context, void *memory, size_t bytes, size_t alignment)
{
    (void)context;
    assert(memory && bytes && alignment <= alignof(max_align_t) && live_allocations);
    --live_allocations;
    free(memory);
}
static doe_shader_options options(doe_shader_target target)
{
    return (doe_shader_options){.allocator={NULL,allocate,release},.target=target,
        .compiler_identity="single-analysis",.compiler_identity_size=15u,
        .capacities={.typed={.source_bytes=4096,.tokens=2048,.statements=256,.expressions=512,
            .types=64,.members=64,.resources=8,.access_steps=1024,.constants=256,.constant_edges=1024,
            .ir_instructions=4096,.ir_operands=2048,.target_declarations=4096,.traversal_items=128,
            .words=8192,.expression_depth=16,.control_depth=8,.type_syntax_depth=16},
            .entries=4,.entry_inputs=16,.entry_references=32},
        .interface_attributes=32,.stage_interfaces=32,.sampling_pairs=16};
}
int main(void)
{
    static const char direct[] = "@compute @workgroup_size(1) fn main(){}";
    static const char symbolic[] = "override n:u32;@compute @workgroup_size(n) fn main(){}";
    for (uint32_t target=0u; target<=2u; ++target) {
        doe_shader_options policy = options((doe_shader_target)target);
        doe_shader *shader = NULL, *module = NULL;
        doe_shader_error error;
        analysis_calls = 0u;
        assert(doe_shader_create(&policy,direct,sizeof(direct)-1u,&shader,&error) == DOE_SHADER_OK);
        assert(analysis_calls == 1u && live_allocations == 1u);
        doe_shader_release(shader);
        analysis_calls = 0u;
        assert(doe_shader_create_module(&policy,8u,symbolic,sizeof(symbolic)-1u,&module,&error) == DOE_SHADER_OK);
        assert(analysis_calls == 1u && live_allocations == 1u);
        const DoeCompilerEntrySelectionV5 selection = {.mode=DOE_COMPILER_SELECT_NAME,.name="main",.name_length=4u};
        const DoeCompilerOverrideValueV13 value = {"n",1u,3.0};
        analysis_calls = 0u;
        assert(doe_shader_specialize(module,&selection,&value,1u,&shader,&error) == DOE_SHADER_OK);
        assert(analysis_calls == 1u && live_allocations == 2u);
        doe_shader_view view;
        assert(doe_shader_borrow(shader,&view) == DOE_SHADER_OK && view.counts.entries == 1u);
        assert(view.entries[0].workgroup_size[0] == 3u);
        doe_shader_release(module);
        doe_shader_release(shader);
        assert(!live_allocations);
    }
    puts("shader single analysis: all3 targets, direct/module/specialization and temporary lifetime passed");
    return 0;
}
