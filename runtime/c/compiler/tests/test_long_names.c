/* SPDX-License-Identifier: MIT
 * Source-span identifiers and NAME selection are independent of legacy ABI1/2
 * fixed reflection names. No physical target execution is claimed. */
#include "../include/doe_compiler_hlsl.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const DoeCompilerModuleCapacities capacities = {
    .typed={.source_bytes=16384u,.tokens=2048u,.statements=64u,.expressions=512u,
        .types=64u,.members=64u,.resources=8u,.access_steps=128u,.constants=128u,
        .constant_edges=256u,.ir_instructions=1024u,.ir_operands=1024u,
        .target_declarations=1024u,.traversal_items=128u,.words=8192u,
        .expression_depth=32u,.control_depth=8u,.type_syntax_depth=32u},
    .entries=4u,.entry_inputs=16u,.entry_references=32u
};
static void identifier(char *name, size_t length, char first) {
    memset(name, 'x', length);
    name[0]=first;
    name[length]='\0';
}
static DoeCompilerStatus measured(uint32_t target, const char *source,
    const DoeCompilerEntrySelectionV5 *selection, size_t *name_bytes) {
    DoeCompilerWorkspaceRequirements requirements;
    DoeCompilerTextCapacities text={.stages={.module=capacities,
        .interface_attributes=16u,.stage_interfaces=16u},.text_bytes=65536u};
    text.stages.module.typed.target_declarations=0u;
    text.stages.module.typed.words=0u;
    DoeCompilerStatus status = target==0u ?
        doe_compiler_module_requirements(5u,&capacities,sizeof(capacities),&requirements,sizeof(requirements)) :
        target==1u ? doe_compiler_text_requirements(10u,1u,&text,sizeof(text),&requirements,sizeof(requirements)) :
        doe_compiler_hlsl_requirements(11u,&text,sizeof(text),&requirements,sizeof(requirements));
    assert(status==DOE_COMPILER_OK);
    void *workspace=malloc(requirements.size_bytes);
    assert(workspace);
    if (target==0u) {
        DoeCompilerModuleResult result;
        status=doe_compiler_compile_module(5u,&capacities,sizeof(capacities),workspace,requirements.size_bytes,
            source,strlen(source),selection,sizeof(*selection),7u,NULL,0u,&result,sizeof(result));
        if (status==DOE_COMPILER_OK) *name_bytes=result.required.name_bytes;
    } else {
        DoeCompilerTextResult result;
        status=target==1u ? doe_compiler_compile_text(10u,1u,&text,sizeof(text),workspace,requirements.size_bytes,
            source,strlen(source),selection,sizeof(*selection),7u,NULL,0u,&result,sizeof(result)) :
            doe_compiler_compile_hlsl(11u,&text,sizeof(text),workspace,requirements.size_bytes,
            source,strlen(source),selection,sizeof(*selection),7u,NULL,0u,&result,sizeof(result));
        if (status==DOE_COMPILER_OK) *name_bytes=result.stages.resources.module.required.name_bytes;
    }
    free(workspace);
    return status;
}
static void typed_names(void) {
    const size_t lengths[]={63u,64u,255u,1024u};
    for (size_t i=0u;i<sizeof(lengths)/sizeof(*lengths);++i) {
        const size_t length=lengths[i];
        char names[4][1025],source[16384];
        for (unsigned j=0u;j<4u;++j) identifier(names[j],length,(char)('A'+j));
        int written=snprintf(source,sizeof(source),
            "struct %s{v:u32} alias %s=%s; @group(0) @binding(0) var<storage,read_write> %s:array<u32>;"
            "@compute @workgroup_size(1) fn %s(){let a=%s(7u);let b=%s(9u);%s[0]=a.v+b.v;}",
            names[0],names[1],names[0],names[2],names[3],names[0],names[1],names[2]);
        assert(written>0 && (size_t)written<sizeof(source));
        DoeCompilerEntrySelectionV5 selection={DOE_COMPILER_SELECT_NAME,0u,names[3],length};
        for (uint32_t target=0u;target<3u;++target) {
            size_t bytes=0u;
            assert(measured(target,source,&selection,&bytes)==DOE_COMPILER_OK);
            assert(bytes>=3u*(length+1u));
            /* A valid prefix is not the same source entry. */
            selection.name_length=length-1u;
            assert(measured(target,source,&selection,&bytes)==DOE_COMPILER_SEMANTIC_ERROR);
            selection.name_length=SIZE_MAX;
            assert(measured(target,source,&selection,&bytes)==DOE_COMPILER_INPUT_ERROR);
            selection.name_length=length;
        }
    }
}
static void legacy_names(void) {
    char name[66],source[1024];
    identifier(name,65u,'n');
    DoeCompilerWorkspaceRequirements requirements;
    assert(doe_compiler_workspace_requirements(2u,&requirements,sizeof(requirements))==DOE_COMPILER_OK);
    void *workspace=malloc(requirements.size_bytes);
    assert(workspace);
    const char *formats[]={
        "@compute @workgroup_size(1) fn %s(){}",
        "@group(0) @binding(0) var<storage,read_write> %s:array<u32>; @compute @workgroup_size(1) fn main(){}",
        "struct %s{v:u32} @compute @workgroup_size(1) fn main(){}",
        "struct S{%s:u32} @compute @workgroup_size(1) fn main(){}"};
    for (size_t i=0u;i<sizeof(formats)/sizeof(*formats);++i) {
        int written=snprintf(source,sizeof(source),formats[i],name);
        assert(written>0 && (size_t)written<sizeof(source));
        uint32_t words[8192];
        memset(words,0xa5,sizeof(words));
        DoeCompilerResultV2 result;
        assert(doe_compiler_compile_v2(2u,workspace,requirements.size_bytes,source,strlen(source),
            words,8192u,&result,sizeof(result))==DOE_COMPILER_UNSUPPORTED);
        assert(!result.word_count && words[0]==UINT32_C(0xa5a5a5a5));
        assert(strstr(result.diagnostic.message,"legacy reflection projection"));
    }
    free(workspace);
}
int main(void) {
    typed_names();
    legacy_names();
    puts("long names: typed three-target selection and bounded legacy rejection passed");
    return 0;
}
