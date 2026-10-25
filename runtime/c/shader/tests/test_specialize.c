/* SPDX-License-Identifier: MIT */
#include "doe_shader.h"
#include <assert.h>
#include <errno.h>
#include <fenv.h>
#include <math.h>
#include <stdalign.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct allocation_state {
    unsigned calls, fail_at, live, mutate_at;
    char *source, *identity, *entry;
    const char *replacement;
    DoeCompilerOverrideValueV13 *row;
    DoeCompilerMetalPipelineOptionsV15 *pipeline;
    struct { void *memory; size_t size, alignment; } blocks[16];
} allocation_state;
static void *allocate(void *userdata, size_t size, size_t alignment)
{
    allocation_state *state = userdata;
    assert(alignment <= alignof(max_align_t));
    ++state->calls;
    if (state->calls == state->mutate_at) {
        if (state->source) memcpy(state->source,state->replacement,strlen(state->replacement));
        if (state->identity) memcpy(state->identity,"compiler-B",10);
        if (state->entry) memcpy(state->entry,"mutate",6);
        if (state->row) ++state->row->key_length;
        if (state->pipeline) state->pipeline->sample_mask = 0;
    }
    if (state->calls == state->fail_at) return NULL;
    void *memory = malloc(size);
    if (memory) {
        size_t slot = 0;
        while (slot < 16 && state->blocks[slot].memory) ++slot;
        assert(slot < 16);
        state->blocks[slot].memory = memory;
        state->blocks[slot].size = size;
        state->blocks[slot].alignment = alignment;
        ++state->live;
    }
    return memory;
}
static void deallocate(void *userdata, void *memory, size_t size, size_t alignment)
{
    allocation_state *state = userdata;
    assert(size && alignment <= alignof(max_align_t) && state->live);
    size_t slot = 0;
    while (slot < 16 && state->blocks[slot].memory != memory) ++slot;
    assert(slot < 16 && state->blocks[slot].size == size && state->blocks[slot].alignment == alignment);
    state->blocks[slot].memory = NULL;
    --state->live;
    free(memory);
}
static doe_shader_options options(allocation_state *state, doe_shader_target target)
{
    return (doe_shader_options){.target=target,.allocator={state,allocate,deallocate},
        .capacities={.typed={.source_bytes=8192,.tokens=1024,.statements=256,.expressions=1024,
            .types=64,.members=128,.resources=16,.access_steps=1024,.constants=256,.constant_edges=512,
            .ir_instructions=4096,.ir_operands=2048,.target_declarations=4096,.traversal_items=193,
            .words=32768,.expression_depth=32,.control_depth=16,.type_syntax_depth=32},
            .entries=8,.entry_inputs=64,.entry_references=128},.interface_attributes=64,.stage_interfaces=64,.sampling_pairs=128,
        .compiler_identity="ABI13-owner-control",.compiler_identity_size=19,
        .semantic_flags=DOE_COMPILER_CORE_BINDING_NUMBERS};
}
static const char source[] =
    "override width:u32; @id(7) override bias:i32=-3; override missing:u32;"
    "@group(0) @binding(2) var<storage,read_write> unused:array<u32>;"
    "@group(3) @binding(999) var<storage,read_write> output:array<u32>;"
    "var<workgroup> scratch:array<u32,width>; var<workgroup> other:array<u32,missing>;"
    "fn helper()->u32{return u32(bias)+width;}"
    "@compute @workgroup_size(width) fn chosen(@builtin(local_invocation_id) id:vec3u){"
    "scratch[id.x]=id.x;workgroupBarrier();output[id.x]=scratch[id.x]+helper();}"
    "@compute @workgroup_size(missing) fn sibling(){other[0]=missing;unused[0]=other[0];}";
static DoeCompilerEntrySelectionV5 selection(const char *name)
{
    return (DoeCompilerEntrySelectionV5){.mode=DOE_COMPILER_SELECT_NAME,.name=name,.name_length=strlen(name)};
}
static void executable(doe_shader *artifact, doe_shader_target target, uint32_t width)
{
    doe_shader_view view;
    assert(doe_shader_borrow(artifact,&view) == DOE_SHADER_OK);
    assert(view.target == target && view.counts.entries == 1 && view.module.selected_entry == 0);
    assert(view.entries[0].stage == DOE_COMPILER_STAGE_COMPUTE && view.entries[0].workgroup_size[0] == width);
    assert(view.entries[0].workgroup_storage_bytes == width * 4);
    assert(!strcmp(view.names + view.entries[0].name.offset,"chosen"));
    assert(view.entries[0].resource_reference_count == 1);
    uint32_t resource = view.entry_references[view.entries[0].first_resource_reference];
    assert(resource == 1 && view.resources[resource].group == 3 && view.resources[resource].binding == 999);
    if (target == DOE_SHADER_SPIRV) assert(view.words && view.words[0] == 0x07230203 && !view.text);
    else {
        assert(!view.words && view.text && view.text_size);
        if (target == DOE_SHADER_METAL) {
            assert(view.metal_entry_count == 1 && !view.hlsl_entry_count);
            assert(view.metal_entries[0].entry_index == 0 && !strcmp(view.metal_entries[0].name,"chosen"));
            assert(view.metal_binding_count == 1 && view.metal_bindings[0].resource_index == resource);
        } else {
            assert(view.hlsl_entry_count == 1 && !view.metal_entry_count);
            assert(view.hlsl_entries[0].entry_index == 0 && !strcmp(view.hlsl_entries[0].name,"chosen"));
        }
    }
}
static void target_cases(doe_shader_target target)
{
    allocation_state state = {0};
    doe_shader_options opts = options(&state,target);
    doe_shader_error error;
    doe_shader *intact = (doe_shader *)(uintptr_t)1, *module = intact;
    doe_shader_options short_source = opts;
    short_source.capacities.typed.source_bytes = 8;
    assert(doe_shader_create_module(&short_source,8,source,sizeof(source)-1,&module,&error) == DOE_SHADER_COMPILE_FAILED);
    assert(module == intact && !state.live && error.diagnostic.message[0]);
    state.calls = 0;
    opts.selection = selection("chosen");
    assert(doe_shader_create_module(&opts,8,source,sizeof(source)-1,&module,&error) == DOE_SHADER_INVALID_ARGUMENT);
    assert(module == intact && !state.calls);
    opts.selection = (DoeCompilerEntrySelectionV5){.mode=DOE_COMPILER_SELECT_NONE,.name=""};
    assert(doe_shader_create_module(&opts,8,source,sizeof(source)-1,&module,&error) == DOE_SHADER_INVALID_ARGUMENT);
    assert(module == intact && !state.calls);
    opts.selection = (DoeCompilerEntrySelectionV5){.mode=DOE_COMPILER_SELECT_NONE};
    for (unsigned ordinal = 1; ordinal <= 2; ++ordinal) {
        state = (allocation_state){.fail_at=ordinal};
        assert(doe_shader_create_module(&opts,8,source,sizeof(source)-1,&module,&error) == DOE_SHADER_OUT_OF_MEMORY);
        assert(module == intact && !state.live);
    }
    state = (allocation_state){0};
    char copied[sizeof(source)], identity[] = "ABI13-owner-control";
    memcpy(copied,source,sizeof(source));
    opts.compiler_identity = identity;
    assert(doe_shader_create_module(&opts,8,copied,strlen(copied),&module,&error) == DOE_SHADER_OK);
    assert(state.calls == 2 && state.live == 1);
    memset(copied,'!',sizeof(copied));
    memset(identity,'!',sizeof(identity));
    opts.target = (doe_shader_target)99;
    opts.capacities.typed.ir_instructions = 0;
    doe_shader_module_view metadata;
    assert(doe_shader_borrow_module(module,&metadata) == DOE_SHADER_OK);
    assert(metadata.target == target && metadata.override_count == 3 && metadata.entry_count == 2);
    assert(!strcmp(metadata.names+metadata.entries[0].name.offset,"chosen"));
    assert(!strcmp(metadata.names+metadata.entries[1].name.offset,"sibling"));
    bool identified = false;
    for (size_t i = 0; i < metadata.override_count; ++i)
        if (metadata.overrides[i].flags & DOE_COMPILER_OVERRIDE_HAS_ID) {
            assert(metadata.overrides[i].id == 7);
            identified = true;
        }
    assert(identified);
    doe_shader_view unchanged, before;
    memset(&unchanged,0xa5,sizeof(unchanged));
    memcpy(&before,&unchanged,sizeof(before));
    assert(doe_shader_borrow(module,&unchanged) == DOE_SHADER_INVALID_ARGUMENT);
    assert(!memcmp(&unchanged,&before,sizeof(before)));
    DoeCompilerEntrySelectionV5 chosen = selection("chosen");
    DoeCompilerEntrySelectionV5 malformed_auto = {.mode=DOE_COMPILER_SELECT_AUTO,.name=""};
    doe_shader *rejected_shape = intact;
    unsigned before_calls = state.calls;
    assert(doe_shader_specialize(module,&malformed_auto,NULL,0,&rejected_shape,&error) == DOE_SHADER_INVALID_ARGUMENT);
    assert(rejected_shape == intact && state.calls == before_calls);
    char key[] = "width", id[] = "7";
    DoeCompilerOverrideValueV13 values[] = {{key,5,4},{id,1,-5}};
    doe_shader *artifact = intact;
    state.calls = 0;
    assert(doe_shader_specialize(module,&chosen,values,SIZE_MAX,&artifact,&error) == DOE_SHADER_OVERFLOW);
    assert(artifact == intact && !state.calls && state.live == 1);
    assert(doe_shader_specialize(module,&chosen,values,2,&artifact,&error) == DOE_SHADER_OK);
    unsigned acquisitions = state.calls;
    assert(acquisitions == (target == DOE_SHADER_SPIRV ? 3u : 4u) && state.live == 2);
    executable(artifact,target,4);
    assert(doe_shader_specialization_matches(artifact,module,&chosen,values,2));
    doe_shader_module_view rejected, untouched_metadata;
    memset(&rejected,0xa5,sizeof(rejected));
    memcpy(&untouched_metadata,&rejected,sizeof(rejected));
    assert(doe_shader_borrow_module(artifact,&rejected) == DOE_SHADER_INVALID_ARGUMENT);
    assert(!memcmp(&rejected,&untouched_metadata,sizeof(rejected)));
    doe_shader_options equivalent = options(&state,target);
    doe_shader *different_module = NULL;
    assert(doe_shader_create_module(&equivalent,8,source,sizeof(source)-1,&different_module,&error) == DOE_SHADER_OK);
    assert(!doe_shader_specialization_matches(artifact,different_module,&chosen,values,2));
    doe_shader_release(different_module);

    assert(!doe_shader_matches(artifact,target,metadata.semantic_flags,source,sizeof(source)-1,
        "ABI13-owner-control",19,&chosen));
    key[0] = '!';
    values[1].value = -6;
    assert(!doe_shader_specialization_matches(artifact,module,&chosen,values,2));
    executable(artifact,target,4);
    key[0] = 'w';
    values[1].value = -5;
    DoeCompilerOverrideValueV13 reordered[] = {values[1],values[0]};
    assert(!doe_shader_specialization_matches(artifact,module,&chosen,reordered,2));
    for (unsigned ordinal = 1; ordinal <= acquisitions; ++ordinal) {
        state.calls = 0;
        state.fail_at = ordinal;
        doe_shader *failed = intact;
        assert(doe_shader_specialize(module,&chosen,values,2,&failed,&error) == DOE_SHADER_OUT_OF_MEMORY);
        assert(failed == intact && state.live == 2);
        executable(artifact,target,4); /* Failed replacement preserves live artifact. */
    }
    state.fail_at = 0;
    const DoeCompilerOverrideValueV13 bad[] = {{"width",5,NAN},{"width",5,INFINITY},
        {"width",5,-1},{"bias",4,3},{"unknown",7,1}};
    for (size_t i = 0; i < sizeof(bad)/sizeof(*bad); ++i) {
        doe_shader *failed = intact;
        assert(doe_shader_specialize(module,&chosen,&bad[i],1,&failed,&error) == DOE_SHADER_COMPILE_FAILED);
        assert(failed == intact && state.live == 2 && error.diagnostic.message[0]);
    }
    DoeCompilerEntrySelectionV5 sibling = selection("sibling");
    doe_shader *failed = intact;
    assert(doe_shader_specialize(module,&sibling,values,2,&failed,&error) == DOE_SHADER_COMPILE_FAILED);
    assert(failed == intact && state.live == 2);
    assert(doe_shader_specialize(module,&chosen,NULL,0,&failed,&error) == DOE_SHADER_COMPILE_FAILED);
    assert(failed == intact && state.live == 2);
    values[0].value = 2;
    doe_shader *second = NULL;
    assert(doe_shader_specialize(module,&chosen,values,2,&second,&error) == DOE_SHADER_OK);
    executable(second,target,2);
    assert(!doe_shader_specialization_matches(artifact,module,&chosen,values,2));
    doe_shader_release(module); /* Both artifacts independently retain saved module. */
    assert(state.live == 3);
    doe_shader_release(artifact);
    executable(second,target,2);
    doe_shader_release(second);
    assert(!state.live);
}
static void defaults_and_graphics(void)
{
    const char defaults[] = "override zero=0u;override quotient=1u/zero;"
        "@compute @workgroup_size(1) fn skip(){}"
        "@compute @workgroup_size(1) fn uses_default(){_=quotient;}";
    const char graphics[] = "override dx:f32;override shade=0.25;override width:u32;"
        "@vertex fn vertex()->@builtin(position) vec4f{return vec4f(dx,0.0,0.0,1.0);}"
        "@fragment fn fragment()->@location(0) vec4f{return vec4f(shade,0.5,0.75,1.0);}"
        "@compute @workgroup_size(width) fn compute_entry(){}";
    for (unsigned target = 0; target < 3; ++target) {
        allocation_state state = {0};
        doe_shader_options opts = options(&state,(doe_shader_target)target);
        doe_shader *module = NULL, *artifact = NULL;
        doe_shader_error error;
        assert(doe_shader_create_module(&opts,8,defaults,sizeof(defaults)-1,&module,&error) == DOE_SHADER_OK);
        DoeCompilerEntrySelectionV5 skip = selection("skip"), use = selection("uses_default");
        assert(doe_shader_specialize(module,&skip,NULL,0,&artifact,&error) == DOE_SHADER_OK);
        doe_shader_release(artifact);
        artifact = NULL;
        assert(doe_shader_specialize(module,&use,NULL,0,&artifact,&error) == DOE_SHADER_COMPILE_FAILED && !artifact);
        const DoeCompilerOverrideValueV13 quotient = {"quotient",8,9};
        assert(doe_shader_specialize(module,&use,&quotient,1,&artifact,&error) == DOE_SHADER_OK);
        doe_shader_release(artifact);
        doe_shader_release(module);
        assert(!state.live);
        assert(doe_shader_create_module(&opts,8,graphics,sizeof(graphics)-1,&module,&error) == DOE_SHADER_OK);
        DoeCompilerEntrySelectionV5 vertex = selection("vertex"), fragment = selection("fragment");
        DoeCompilerOverrideValueV13 dx = {"dx",2,-0.0};
        assert(doe_shader_specialize(module,&vertex,&dx,1,&artifact,&error) == DOE_SHADER_OK);
        assert(doe_shader_specialization_matches(artifact,module,&vertex,&dx,1));
        dx.value = 0.0;
        assert(!doe_shader_specialization_matches(artifact,module,&vertex,&dx,1));
        doe_shader_view view;
        assert(doe_shader_borrow(artifact,&view) == DOE_SHADER_OK && view.entries[0].stage == DOE_COMPILER_STAGE_VERTEX);
        doe_shader_release(artifact);
        assert(doe_shader_specialize(module,&fragment,NULL,0,&artifact,&error) == DOE_SHADER_OK);
        assert(doe_shader_borrow(artifact,&view) == DOE_SHADER_OK && view.entries[0].stage == DOE_COMPILER_STAGE_FRAGMENT);
        doe_shader_release(artifact);
        doe_shader_release(module);
        assert(!state.live);
        const char invalid[] = "override n:u32;@compute @workgroup_size(1) fn good(){}fn bad(){let x=unknown;}";
        module = NULL;
        assert(doe_shader_create_module(&opts,8,invalid,sizeof(invalid)-1,&module,&error) == DOE_SHADER_COMPILE_FAILED);
        assert(!module && !state.live && error.diagnostic.message[0]);
        const char single[] = "override width=4u;@compute @workgroup_size(width) fn single(){}";
        assert(doe_shader_create_module(&opts,4,single,sizeof(single)-1,&module,&error) == DOE_SHADER_OK);
        DoeCompilerEntrySelectionV5 automatic = {.mode=DOE_COMPILER_SELECT_AUTO};
        assert(doe_shader_specialize(module,&automatic,NULL,0,&artifact,&error) == DOE_SHADER_OK);
        DoeCompilerEntrySelectionV5 malformed = {.mode=DOE_COMPILER_SELECT_AUTO,.name=""};
        assert(!doe_shader_specialization_matches(artifact,module,&malformed,NULL,0));
        assert(doe_shader_borrow(artifact,&view) == DOE_SHADER_OK && view.entries[0].workgroup_size[0] == 4);
        doe_shader_release(artifact);
        doe_shader_release(module);
        assert(!state.live);
        assert(doe_shader_create_module(&opts,0,NULL,0,&module,&error) == DOE_SHADER_OK);
        doe_shader_module_view library;
        assert(doe_shader_borrow_module(module,&library) == DOE_SHADER_OK && !library.entry_count && !library.override_count);
        artifact = NULL;
        assert(doe_shader_specialize(module,&automatic,NULL,0,&artifact,&error) == DOE_SHADER_COMPILE_FAILED && !artifact);
        doe_shader_release(module);
        assert(!state.live);
    }
}
static void allocator_reentry(void)
{
    for (unsigned target = 0; target < 3; ++target) {
        allocation_state state = {0};
        doe_shader_options opts = options(&state,(doe_shader_target)target);
        char entry[] = "chosen", identity[] = "compiler-A";
        char original[] = "@compute @workgroup_size(4) fn chosen(){}";
        const char replacement[] = "@compute @workgroup_size(8) fn mutate(){}";
        const char initial[] = "@compute @workgroup_size(4) fn chosen(){}";
        assert(sizeof(original) == sizeof(replacement));
        opts.compiler_identity = identity;
        opts.compiler_identity_size = 10;
        opts.selection = (DoeCompilerEntrySelectionV5){.mode=DOE_COMPILER_SELECT_NAME,.name=entry,.name_length=6};
        state.mutate_at = 2;
        state.source = original;
        state.replacement = replacement;
        state.identity = identity;
        state.entry = entry;
        doe_shader *artifact = NULL;
        doe_shader_error error;
        assert(doe_shader_create(&opts,original,strlen(original),&artifact,&error) == DOE_SHADER_OK);
        doe_shader_view view;
        assert(doe_shader_borrow(artifact,&view) == DOE_SHADER_OK && view.entries[0].workgroup_size[0] == 4);
        DoeCompilerEntrySelectionV5 chosen = selection("chosen");
        assert(doe_shader_matches(artifact,(doe_shader_target)target,opts.semantic_flags,initial,sizeof(initial)-1,
            "compiler-A",10,&chosen));
        doe_shader_release(artifact);
        assert(!state.live);
        state = (allocation_state){0};
        opts = options(&state,(doe_shader_target)target);
        char module_source[] = "@id(7) override width=4u;@compute @workgroup_size(width) fn chosen(){}";
        const char module_replacement[] = "@id(7) override width=8u;@compute @workgroup_size(width) fn mutate(){}";
        state.mutate_at = 2;
        state.source = module_source;
        state.replacement = module_replacement;
        doe_shader *module = NULL;
        assert(doe_shader_create_module(&opts,8,module_source,strlen(module_source),&module,&error) == DOE_SHADER_OK);
        state.mutate_at = 0;
        doe_shader_module_view metadata;
        assert(doe_shader_borrow_module(module,&metadata) == DOE_SHADER_OK);
        assert(!strcmp(metadata.names+metadata.entries[0].name.offset,"chosen"));
        assert(doe_shader_specialize(module,&chosen,NULL,0,&artifact,&error) == DOE_SHADER_OK);
        assert(doe_shader_borrow(artifact,&view) == DOE_SHADER_OK && view.entries[0].workgroup_size[0] == 4);
        doe_shader_release(artifact);
        char key[] = "7x";
        DoeCompilerOverrideValueV13 growing = {key,1,1};
        assert(doe_shader_specialize(module,&chosen,&growing,1,&artifact,&error) == DOE_SHADER_OK);
        assert(doe_shader_borrow(artifact,&view) == DOE_SHADER_OK && view.entries[0].workgroup_size[0] == 1);
        doe_shader_release(artifact);
        state.calls = 0;
        state.mutate_at = 1;
        state.row = &growing;
        doe_shader *untouched = (doe_shader *)(uintptr_t)1;
        artifact = untouched;
        assert(doe_shader_specialize(module,&chosen,&growing,1,&artifact,&error) == DOE_SHADER_INVALID_ARGUMENT);
        assert(growing.key_length == 2 && artifact == untouched && state.live == 1);
        state.mutate_at = 0;
        doe_shader_release(module);
        assert(!state.live);
    }
}
static void metal_pipeline(void)
{
    allocation_state state = {0};
    doe_shader_options opts = options(&state,DOE_SHADER_METAL);
    const char code[] = "@fragment fn chosen()->@location(0) vec4f{return vec4f(1);}";
    doe_shader *module = NULL, *artifact = NULL;
    doe_shader_error error;
    assert(doe_shader_create_module(&opts,0,code,sizeof(code)-1,&module,&error) == DOE_SHADER_OK);
    const DoeCompilerEntrySelectionV5 chosen = selection("chosen");
    DoeCompilerMetalPipelineOptionsV15 policy = {5};
    state.calls = 0;
    state.mutate_at = 1;
    state.pipeline = &policy;
    assert(doe_shader_specialize_metal_pipeline(module,&chosen,NULL,0,&policy,&artifact,&error) == DOE_SHADER_OK);
    assert(policy.sample_mask == 0);
    doe_shader_view view;
    assert(doe_shader_borrow(artifact,&view) == DOE_SHADER_OK && view.metal_sample_mask == 5);
    assert(!doe_shader_metal_pipeline_matches(artifact,module,&chosen,NULL,0,&policy));
    policy.sample_mask = 5;
    assert(doe_shader_metal_pipeline_matches(artifact,module,&chosen,NULL,0,&policy));
    assert(!doe_shader_specialization_matches(artifact,module,&chosen,NULL,0));
    assert(view.interface_count == 1 && view.entries[0].stage == DOE_COMPILER_STAGE_FRAGMENT);
    doe_shader_release(artifact);
    state.mutate_at = 0;
    for (unsigned ordinal = 1; ordinal <= 2; ++ordinal) {
        state.calls = 0;
        state.fail_at = ordinal;
        artifact = (doe_shader *)(uintptr_t)1;
        assert(doe_shader_specialize_metal_pipeline(module,&chosen,NULL,0,&policy,&artifact,&error) == DOE_SHADER_OUT_OF_MEMORY);
        assert(artifact == (doe_shader *)(uintptr_t)1 && state.live == 1);
    }
    state.fail_at = 0;
    policy.sample_mask = UINT32_MAX;
    assert(doe_shader_specialize_metal_pipeline(module,&chosen,NULL,0,&policy,&artifact,&error) == DOE_SHADER_OK);
    assert(doe_shader_specialization_matches(artifact,module,&chosen,NULL,0));
    assert(doe_shader_metal_pipeline_matches(artifact,module,&chosen,NULL,0,&policy));
    doe_shader_release(module);
    assert(doe_shader_borrow(artifact,&view) == DOE_SHADER_OK && view.metal_sample_mask == UINT32_MAX);
    doe_shader_release(artifact);
    assert(!state.live);
}
static void sampling_pairs(void)
{
    const char code[] =
        "@group(0) @binding(0) var loaded:texture_2d<f32>;"
        "@group(0) @binding(1) var sampled:texture_2d<f32>;"
        "@group(1) @binding(2) var sampling:sampler;"
        "@group(0) @binding(3) var<storage,read_write> result:array<f32>;"
        "fn leaf(t:texture_2d<f32>,s:sampler)->f32{return textureSampleLevel(t,s,vec2f(0.5),0.0).r;}"
        "fn middle(s:sampler,t:texture_2d<f32>)->f32{return leaf(t,s);}"
        "@compute @workgroup_size(1) fn chosen(){result[0]=textureLoad(loaded,vec2i(0),0).r+middle(sampling,sampled);}";
    for (unsigned target = 0; target < 3; ++target) {
        allocation_state state = {0};
        doe_shader_options opts = options(&state, (doe_shader_target)target);
        doe_shader *module = NULL, *artifact = NULL;
        doe_shader_error error;
        assert(doe_shader_create_module(&opts, 8, code, sizeof(code)-1, &module, &error) == DOE_SHADER_OK);
        unsigned live = state.live, before = state.calls;
        DoeCompilerEntrySelectionV5 chosen = selection("chosen");
        assert(doe_shader_specialize(module, &chosen, NULL, 0, &artifact, &error) == DOE_SHADER_OK);
        unsigned allocations = state.calls - before;
        doe_shader_view view;
        assert(doe_shader_borrow(artifact, &view) == DOE_SHADER_OK);
        assert(view.sampling_pairs_complete && view.sampling_pair_count == 1);
        const DoeCompilerSamplingPairV16 *pair = view.sampling_pairs;
        assert(pair->entry_index == 0 && !pair->reserved);
        assert(pair->texture_resource_index < view.counts.resources && pair->sampler_resource_index < view.counts.resources);
        assert(view.resources[pair->texture_resource_index].group == 0 &&
            view.resources[pair->texture_resource_index].binding == 1);
        assert(view.resources[pair->sampler_resource_index].group == 1 &&
            view.resources[pair->sampler_resource_index].binding == 2);
        doe_shader_release(artifact);
        for (unsigned fault = 1; fault <= allocations; ++fault) {
            doe_shader *failed = (doe_shader *)&state;
            state.fail_at = state.calls + fault;
            assert(doe_shader_specialize(module, &chosen, NULL, 0, &failed, &error) == DOE_SHADER_OUT_OF_MEMORY);
            assert(failed == (doe_shader *)&state && state.live == live);
        }
        state.fail_at = 0;
        assert(doe_shader_specialize(module, &chosen, NULL, 0, &artifact, &error) == DOE_SHADER_OK);
        doe_shader_release(module);
        assert(doe_shader_borrow(artifact, &view) == DOE_SHADER_OK && view.sampling_pair_count == 1);
        assert(view.resources[view.sampling_pairs[0].texture_resource_index].binding == 1);
        doe_shader_release(artifact);
        assert(!state.live);
        opts.sampling_pairs = 0;
        assert(doe_shader_create_module(&opts, 8, code, sizeof(code)-1, &module, &error) == DOE_SHADER_OK);
        artifact = (doe_shader *)&state;
        assert(doe_shader_specialize(module, &chosen, NULL, 0, &artifact, &error) == DOE_SHADER_COMPILE_FAILED);
        assert(artifact == (doe_shader *)&state && error.compiler_status == DOE_COMPILER_INPUT_ERROR);
        doe_shader_release(module);
        assert(!state.live);
    }
}

int main(void)
{
    sampling_pairs();
    metal_pipeline();
    allocator_reentry();
    int original = fegetround();
    const int modes[] = {FE_TONEAREST,FE_UPWARD,FE_DOWNWARD,FE_TOWARDZERO};
    for (unsigned target = 0; target < 3; ++target) target_cases((doe_shader_target)target);
    for (size_t i = 0; i < sizeof(modes)/sizeof(*modes); ++i) {
        assert(!fesetround(modes[i]));
        errno = ERANGE;
        defaults_and_graphics();
        assert(fegetround() == modes[i] && errno == ERANGE);
    }
    assert(!fesetround(original));
    puts("PASS owned module/specialization: all3 targets, symbolic metadata only, copied source/policy, stage0 closure/layout, exact values, allocation rollback, retained lifetimes and FP preservation; no GPU execution");
}
