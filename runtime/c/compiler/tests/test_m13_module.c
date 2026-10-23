/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "../include/doe_compiler_module.h"
#include "../include/doe_compiler_job.h"
#include <assert.h>
#include <stdbool.h>
#include <errno.h>
#include <fenv.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const DoeCompilerModuleCapacities capacities={
    .typed={.source_bytes=16384u,.tokens=2048u,.statements=128u,.expressions=512u,
        .types=32u,.members=64u,.resources=8u,.access_steps=512u,.constants=64u,.constant_edges=128u,
        .ir_instructions=2048u,.ir_operands=512u,.target_declarations=512u,.traversal_items=97u,
        .words=16384u,.expression_depth=32u,.control_depth=16u,.type_syntax_depth=32u},
    .entries=8u,.entry_inputs=40u,.entry_references=64u};
typedef struct {
    uint32_t words[16384], references[64];
    DoeCompilerTypeV4 types[64];
    DoeCompilerMemberV4 members[64];
    DoeCompilerResourceV4 resources[8];
    DoeCompilerWorkgroupV5 workgroups[16];
    DoeCompilerEntryV5 entries[8];
    char names[4096];
} OutputStorage;
static const char asymmetric[]=
    "@group(0) @binding(0) var<storage,read_write> a:array<u32,2>;"
    "@group(1) @binding(2) var<storage,read_write> b:array<u32,7>;"
    "@compute @workgroup_size(1) fn main(){a[0]=1u;}"
    "@compute @workgroup_size(7) fn selected(@builtin(local_invocation_index) lane:u32){b[lane]=100u+lane;}"
    "fn helper(){}";
static const char closure[]=
    "@group(0) @binding(0) var<storage,read_write> a:array<u32>;"
    "@group(0) @binding(1) var<storage,read_write> b:array<u32>;"
    "@group(0) @binding(2) var<storage,read_write> unused:array<u32>;"
    "fn touch()->bool{b[0]=9u;return true;}"
    "fn left()->bool{return touch();}fn right()->bool{return touch();}"
    "@compute @workgroup_size(1) fn main(){a[0]=1u;let skipped=false&&(left()||right());return;a[1]=b[1];}"
    "@compute @workgroup_size(1) fn other(){unused[0]=3u;}"
    "@compute @workgroup_size(1) fn shadow(){let b=4u;a[0]=b;}";
static const char workgroups[]=
    "@group(0) @binding(0) var<storage,read_write> out:array<u32>;"
    "var<workgroup> small_shared:array<u32,2>;var<workgroup> wide_shared:array<u32,7>;"
    "var<workgroup> unused_shared:array<vec3u,4>;"
    "fn read_wide(i:u32)->u32{return wide_shared[i];}"
    "@compute @workgroup_size(2) fn small(@builtin(local_invocation_index) lane:u32,@builtin(workgroup_id) group:vec3u){"
    "out[group.x*2u+lane]=small_shared[lane];small_shared[lane]=31u;}"
    "@compute @workgroup_size(7) fn wide(@builtin(workgroup_id) group:vec3u,@builtin(local_invocation_index) lane:u32){"
    "out[group.x*7u+lane]=read_wide(lane);wide_shared[lane]=47u;}";
static const char aliases[]=
    "@group(0) @binding(0) var<storage,read_write> a:array<f32>;"
    "@group(0) @binding(0) var<storage,read_write> b:array<u32>;"
    "@compute @workgroup_size(1) fn first(){a[0]=2.0f;}"
    "@compute @workgroup_size(1) fn second(){b[0]=3u;}";
static DoeCompilerModuleOutputs outputs(OutputStorage *storage, uint32_t flags) {
    DoeCompilerModuleOutputs out={.type_record_size=sizeof(DoeCompilerTypeV4),
        .member_record_size=sizeof(DoeCompilerMemberV4),.resource_record_size=sizeof(DoeCompilerResourceV4),
        .workgroup_record_size=sizeof(DoeCompilerWorkgroupV5),.entry_record_size=sizeof(DoeCompilerEntryV5)};
    if ((flags&1u)!=0u) {out.words=storage->words;out.word_capacity=16384u;}
    if ((flags&2u)!=0u) {
        out.types=storage->types;out.type_capacity=64u;
        out.members=storage->members;out.member_capacity=64u;
        out.resources=storage->resources;out.resource_capacity=8u;
        out.workgroups=storage->workgroups;out.workgroup_capacity=16u;
        out.entries=storage->entries;out.entry_capacity=8u;
        out.entry_references=storage->references;out.entry_reference_capacity=64u;
        out.names=storage->names;out.name_capacity=4096u;
    }
    return out;
}
static DoeCompilerStatus compile(const DoeCompilerModuleCapacities *caps, void *workspace, size_t bytes,
    const char *source, DoeCompilerEntrySelectionV5 selection, uint32_t flags,
    const DoeCompilerModuleOutputs *out, DoeCompilerModuleResult *result) {
    const int saved_errno=errno, rounding=fegetround(), exceptions=fetestexcept(FE_ALL_EXCEPT);
    const DoeCompilerStatus status=doe_compiler_compile_module(5u,caps,sizeof(*caps),workspace,bytes,
        source,strlen(source),&selection,sizeof(selection),flags,out,out==NULL?0u:sizeof(*out),result,sizeof(*result));
    assert(errno==saved_errno && rounding==fegetround() && exceptions==fetestexcept(FE_ALL_EXCEPT));
    return status;
}
static DoeCompilerEntrySelectionV5 named(const char *name) {
    return (DoeCompilerEntrySelectionV5){.mode=DOE_COMPILER_SELECT_NAME,.name=name,.name_length=strlen(name)};
}
static void require_ok(DoeCompilerStatus status, const DoeCompilerModuleResult *result) {
    if (status!=DOE_COMPILER_OK) fprintf(stderr,"status %u, pool %u, offset %zu: %s\n",
        (unsigned)status,result->exhausted_pool,result->diagnostic.offset,result->diagnostic.message);
    assert(status==DOE_COMPILER_OK);
}
static bool zero(const void *data,size_t bytes) {
    const unsigned char *p=data;
    for (size_t i=0u;i<bytes;++i) if (p[i]!=0u) return false;
    return true;
}
static uint32_t instruction_count(const OutputStorage *storage,size_t words,uint32_t opcode) {
    uint32_t count=0u;
    for (size_t i=5u;i<words;i+=storage->words[i]>>16u) {
        assert((storage->words[i]>>16u)!=0u);
        if ((storage->words[i]&65535u)==opcode) ++count;
    }
    return count;
}
static void expect_failure(void *workspace,size_t bytes,const char *source,DoeCompilerEntrySelectionV5 selection,
    DoeCompilerStatus expected,OutputStorage *storage,OutputStorage *saved) {
    DoeCompilerModuleResult result;
    const DoeCompilerModuleOutputs out=outputs(storage,3u);
    memset(storage,0xa5,sizeof(*storage));
    memcpy(saved,storage,sizeof(*storage));
    assert(compile(&capacities,workspace,bytes,source,selection,3u,&out,&result)==expected);
    assert(memcmp(storage,saved,sizeof(*storage))==0);
    assert(zero(&result.published,sizeof(result.published)) && zero(&result.module,sizeof(result.module)));
    assert(result.required_is_exact==0u && zero(&result.required,sizeof(result.required)));
}
static void exact_spans(void *workspace,size_t bytes,OutputStorage *storage,OutputStorage *saved) {
    DoeCompilerModuleResult measured,result;
    const DoeCompilerEntrySelectionV5 selection={0};
    require_ok(compile(&capacities,workspace,bytes,workgroups,selection,7u,NULL,&measured),&measured);
    assert(zero(&measured.module,sizeof(measured.module)) && zero(&measured.published,sizeof(measured.published)));
    DoeCompilerModuleOutputs out=outputs(storage,3u);
    out.word_capacity=measured.required.words;out.type_capacity=measured.required.types;
    out.member_capacity=measured.required.members;out.resource_capacity=measured.required.resources;
    out.workgroup_capacity=measured.required.workgroups;out.entry_capacity=measured.required.entries;
    out.entry_reference_capacity=measured.required.entry_references;out.name_capacity=measured.required.name_bytes;
    require_ok(compile(&capacities,workspace,bytes,workgroups,selection,3u,&out,&result),&result);
    assert(memcmp(&measured.required,&result.published,sizeof(result.published))==0);
    size_t *spans[]={&out.word_capacity,&out.type_capacity,&out.resource_capacity,&out.workgroup_capacity,
        &out.entry_capacity,&out.entry_reference_capacity,&out.name_capacity};
    for (size_t i=0u;i<sizeof(spans)/sizeof(*spans);++i) {
        assert(*spans[i]!=0u);--*spans[i];
        memset(storage,0xa5,sizeof(*storage));memcpy(saved,storage,sizeof(*storage));
        assert(compile(&capacities,workspace,bytes,workgroups,selection,3u,&out,&result)==DOE_COMPILER_OUTPUT_TOO_SMALL);
        assert(memcmp(storage,saved,sizeof(*storage))==0);
        assert(result.required_is_exact==1u && memcmp(&measured.required,&result.required,sizeof(result.required))==0);
        assert(zero(&result.module,sizeof(result.module)) && zero(&result.published,sizeof(result.published)));
        ++*spans[i];
    }
}
static void overlap_regression(OutputStorage *storage) {
    DoeCompilerModuleCapacities caps=capacities;
    caps.typed.expressions=128u;caps.typed.statements=64u;caps.typed.types=4u;
    caps.typed.target_declarations=16u;caps.typed.traversal_items=8u;caps.typed.expression_depth=64u;
    char source[2048]="@group(0) @binding(0) var<storage,read_write> a:array<u32>;@compute @workgroup_size(1) fn main(){let p=";
    for (unsigned i=0u;i<35u;++i) strcat(source,i==0u?"1u":"+1u");
    strcat(source,";let q=a[0];let v:vec2<u32> =vec2u(1u);}");
    DoeCompilerWorkspaceRequirements required;
    assert(doe_compiler_module_requirements(5u,&caps,sizeof(caps),&required,sizeof(required))==DOE_COMPILER_OK);
    void *workspace=malloc(required.size_bytes);assert(workspace!=NULL);
    DoeCompilerModuleResult result;const DoeCompilerModuleOutputs out=outputs(storage,3u);
    require_ok(compile(&caps,workspace,required.size_bytes,source,(DoeCompilerEntrySelectionV5){0},3u,&out,&result),&result);
    assert(result.published.entry_references==1u && storage->references[0]==0u);
    assert(storage->entries[0].resource_reference_count==1u);
    free(workspace);
}
static void capacity_checks(void *workspace,size_t bytes,OutputStorage *storage) {
    DoeCompilerModuleResult result;
    DoeCompilerModuleCapacities caps=capacities;
    caps.entries=1u;
    assert(compile(&caps,workspace,bytes,asymmetric,(DoeCompilerEntrySelectionV5){0},7u,NULL,&result)==DOE_COMPILER_INPUT_ERROR);
    assert(result.exhausted_pool==20u && result.minimum_pool_capacity==2u);
    caps=capacities;caps.entry_inputs=0u;
    assert(compile(&caps,workspace,bytes,asymmetric,(DoeCompilerEntrySelectionV5){0},7u,NULL,&result)==DOE_COMPILER_INPUT_ERROR);
    assert(result.exhausted_pool==21u && result.minimum_pool_capacity==1u);
    caps=capacities;caps.entry_references=1u;
    assert(compile(&caps,workspace,bytes,asymmetric,(DoeCompilerEntrySelectionV5){0},7u,NULL,&result)==DOE_COMPILER_INPUT_ERROR);
    assert(result.exhausted_pool==22u && result.minimum_pool_capacity==2u);
    caps=capacities;caps.entries=2u;caps.entry_inputs=1u;caps.entry_references=2u;
    require_ok(compile(&caps,workspace,bytes,asymmetric,(DoeCompilerEntrySelectionV5){0},7u,NULL,&result),&result);
    caps=capacities;caps.typed.words=result.required.words;
    const DoeCompilerModuleOutputs out=outputs(storage,3u);
    require_ok(compile(&caps,workspace,bytes,asymmetric,(DoeCompilerEntrySelectionV5){0},3u,&out,&result),&result);
    --caps.typed.words;
    assert(compile(&caps,workspace,bytes,asymmetric,(DoeCompilerEntrySelectionV5){0},7u,NULL,&result)==DOE_COMPILER_OUTPUT_TOO_SMALL);
    assert(result.exhausted_pool==16u && result.required_is_exact==0u);
}
static void legacy_projections(OutputStorage *storage) {
    DoeCompilerResult legacy;
    assert(doe_compiler_compile(1u,asymmetric,sizeof(asymmetric)-1u,storage->words,16384u,
        &legacy,sizeof(legacy))==DOE_COMPILER_UNSUPPORTED);
    assert(strstr(legacy.diagnostic.message,"singular reflection projection")!=NULL);
    DoeCompilerWorkspaceRequirements required;
    assert(doe_compiler_typed_requirements(4u,&capacities.typed,sizeof(capacities.typed),&required,sizeof(required))==DOE_COMPILER_OK);
    void *workspace=malloc(required.size_bytes);assert(workspace!=NULL);
    DoeCompilerTypedOutputs out={.words=storage->words,.word_capacity=16384u,
        .type_record_size=sizeof(DoeCompilerTypeV4),.member_record_size=sizeof(DoeCompilerMemberV4),
        .resource_record_size=sizeof(DoeCompilerResourceV4)};
    DoeCompilerTypedResult result;
    assert(doe_compiler_compile_typed(4u,&capacities.typed,sizeof(capacities.typed),workspace,required.size_bytes,
        asymmetric,sizeof(asymmetric)-1u,1u,&out,sizeof(out),&result,sizeof(result))==DOE_COMPILER_OK);
    assert(result.published.words!=0u && result.published.types==0u && zero(&result.module,sizeof(result.module)));
    assert(instruction_count(storage,result.published.words,15u)==2u);
    out.types=storage->types;out.type_capacity=64u;out.members=storage->members;out.member_capacity=64u;
    out.resources=storage->resources;out.resource_capacity=8u;out.names=storage->names;out.name_capacity=4096u;
    memset(storage,0xa5,sizeof(*storage));
    assert(doe_compiler_compile_typed(4u,&capacities.typed,sizeof(capacities.typed),workspace,required.size_bytes,
        asymmetric,sizeof(asymmetric)-1u,3u,&out,sizeof(out),&result,sizeof(result))==DOE_COMPILER_UNSUPPORTED);
    assert(storage->words[0]==UINT32_C(0xa5a5a5a5) && storage->names[0]==(char)0xa5);
    assert(zero(&result.published,sizeof(result.published)) && zero(&result.module,sizeof(result.module)));
    free(workspace);
}
int main(void) {
    DoeCompilerWorkspaceRequirements required,fixed;
    assert(doe_compiler_module_requirements(5u,&capacities,sizeof(capacities),&required,sizeof(required))==DOE_COMPILER_OK);
    assert(doe_compiler_workspace_requirements(2u,&fixed,sizeof(fixed))==DOE_COMPILER_OK);
    assert(fixed.size_bytes==146048u);
    printf("ABI5 workspace %zu alignment %zu; fixed %zu\n",required.size_bytes,required.alignment_bytes,fixed.size_bytes);
    void *workspace=malloc(required.size_bytes);assert(workspace!=NULL);
    OutputStorage *storage=malloc(sizeof(*storage)),*saved=malloc(sizeof(*saved));assert(storage!=NULL && saved!=NULL);
    DoeCompilerModuleResult result;
    const DoeCompilerModuleOutputs out=outputs(storage,3u);
    require_ok(compile(&capacities,workspace,required.size_bytes,asymmetric,(DoeCompilerEntrySelectionV5){0},3u,&out,&result),&result);
    assert(result.module.selected_entry==UINT32_MAX && result.published.entries==2u && result.published.resources==2u);
    assert(storage->entries[1].workgroup_size[0]==7u && storage->resources[1].minimum_binding_size_bytes==28u);
    assert(storage->references[0]==0u && storage->references[1]==1u);
    assert(instruction_count(storage,result.published.words,15u)==2u && instruction_count(storage,result.published.words,54u)==3u);
    const size_t words=result.published.words;memcpy(saved,storage,sizeof(*saved));
    require_ok(compile(&capacities,workspace,required.size_bytes,asymmetric,named("selected"),3u,&out,&result),&result);
    assert(result.module.selected_entry==1u && result.published.words==words);
    assert(memcmp(saved->words,storage->words,words*sizeof(*storage->words))==0);
    require_ok(compile(&capacities,workspace,required.size_bytes,closure,(DoeCompilerEntrySelectionV5){0},3u,&out,&result),&result);
    assert(result.published.entries==3u && result.published.entry_references==4u);
    assert(storage->entries[0].resource_reference_count==2u && storage->entries[1].resource_reference_count==1u && storage->entries[2].resource_reference_count==1u);
    assert(storage->references[0]==0u && storage->references[1]==1u && storage->references[2]==2u && storage->references[3]==0u);
    require_ok(compile(&capacities,workspace,required.size_bytes,workgroups,(DoeCompilerEntrySelectionV5){0},3u,&out,&result),&result);
    assert(result.published.workgroups==3u && result.published.entry_references==4u);
    assert(storage->entries[0].workgroup_storage_bytes==8u && storage->entries[1].workgroup_storage_bytes==28u);
    assert(storage->entries[0].static_builtin_mask==20u && storage->entries[1].static_builtin_mask==20u);
    assert(storage->references[1]==0u && storage->references[3]==1u && instruction_count(storage,result.published.words,224u)==2u);
    require_ok(compile(&capacities,workspace,required.size_bytes,aliases,(DoeCompilerEntrySelectionV5){0},3u,&out,&result),&result);
    expect_failure(workspace,required.size_bytes,asymmetric,(DoeCompilerEntrySelectionV5){.mode=1u},DOE_COMPILER_SEMANTIC_ERROR,storage,saved);
    expect_failure(workspace,required.size_bytes,asymmetric,named("helper"),DOE_COMPILER_SEMANTIC_ERROR,storage,saved);
    expect_failure(workspace,required.size_bytes,asymmetric,named("Selected"),DOE_COMPILER_SEMANTIC_ERROR,storage,saved);
    expect_failure(workspace,required.size_bytes,asymmetric,named(""),DOE_COMPILER_INPUT_ERROR,storage,saved);
    expect_failure(workspace,required.size_bytes,"fn unused(){unknown();}@compute @workgroup_size(1) fn main(){}",named("main"),DOE_COMPILER_SEMANTIC_ERROR,storage,saved);
    expect_failure(workspace,required.size_bytes,"fn unused(){unused();}@compute @workgroup_size(1) fn main(){}",named("main"),DOE_COMPILER_SEMANTIC_ERROR,storage,saved);
    expect_failure(workspace,required.size_bytes,"@compute @workgroup_size(1) fn main(){}@compute @workgroup_size(1) fn bad(@builtin(local_invocation_index) lane:u32){if(lane==0u){workgroupBarrier();}}",named("main"),DOE_COMPILER_SEMANTIC_ERROR,storage,saved);
    const char collision[]="@group(0) @binding(0) var<storage,read_write> a:array<u32>;@group(0) @binding(0) var<storage,read_write> b:array<u32>;fn touch()->bool{b[0]=1u;return true;}@compute @workgroup_size(1) fn bad(){a[0]=1u;let x=false&&touch();}@compute @workgroup_size(1) fn good(){}";
    expect_failure(workspace,required.size_bytes,collision,named("good"),DOE_COMPILER_SEMANTIC_ERROR,storage,saved);
    exact_spans(workspace,required.size_bytes,storage,saved);
    capacity_checks(workspace,required.size_bytes,storage);
    overlap_regression(storage);
    legacy_projections(storage);
    memset(&result,0xa5,sizeof(result));DoeCompilerModuleResult untouched=result;
    assert(doe_compiler_compile_module(4u,&capacities,sizeof(capacities),workspace,required.size_bytes,
        asymmetric,sizeof(asymmetric)-1u,NULL,0u,3u,&out,sizeof(out),&result,sizeof(result))==DOE_COMPILER_ABI_MISMATCH);
    assert(memcmp(&result,&untouched,sizeof(result))==0);
    free(saved);free(storage);free(workspace);
    puts("M13 developer API: selection, all-entry words, static/dead closure, aliases, workgroup roots, uniformity, exact/short spans, pool limits, semantic-Walk overlap and reuse passed");
    return 0;
}
