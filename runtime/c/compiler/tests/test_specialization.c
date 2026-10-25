/* SPDX-License-Identifier: MIT */
#include "hlsl_fixture.h"
#include "../include/doe_compiler_specialization.h"
#include <assert.h>
#include <errno.h>
#include <fenv.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

static DoeCompilerSpecializationCapacities capacities(void) {
    DoeCompilerSpecializationCapacities caps = {hlsl_capacities(), 32u};
    caps.target.stages.module.typed.words = 65536u;
    caps.target.stages.module.typed.target_declarations = 8192u;
    return caps;
}
static DoeCompilerStatus compile(uint32_t target, const char *source, const char *entry,
    const DoeCompilerOverrideValueV13 *values, size_t count, HlslFixture *storage,
    uint32_t *words, DoeCompilerTextResult *result, bool measure) {
    DoeCompilerSpecializationCapacities caps = capacities();
    DoeCompilerWorkspaceRequirements requirements;
    assert(doe_compiler_specialization_requirements(13u, target, &caps, sizeof(caps),
        &requirements, sizeof(requirements)) == DOE_COMPILER_OK);
    void *workspace = malloc(requirements.size_bytes);
    assert(workspace != NULL);
    DoeCompilerEntrySelectionV5 selection = {DOE_COMPILER_SELECT_NAME, 0u, entry, strlen(entry)};
    DoeCompilerSpecializationOutputs outputs = {0};
    DoeCompilerMetalBindingV12 metal_bindings[64];
    DoeCompilerMetalEntryV12 metal_entries[8];
    DoeCompilerHlslOutputs hlsl = hlsl_outputs(storage);
    if (target == DOE_COMPILER_TARGET_SPIRV) {
        outputs.spirv = hlsl.stages;
        outputs.spirv.resources.module.words = words;
        outputs.spirv.resources.module.word_capacity = 65536u;
    } else if (target == DOE_COMPILER_TARGET_METAL)
        outputs.metal = (DoeCompilerMetalOutputs){hlsl.stages, hlsl.text, hlsl.text_capacity,
            metal_bindings, 64u, sizeof(metal_bindings[0]), metal_entries, 8u, sizeof(metal_entries[0])};
    else outputs.hlsl = hlsl;
    const DoeCompilerStatus status = doe_compiler_specialize(13u, target, &caps, sizeof(caps),
        workspace, requirements.size_bytes, source, strlen(source), &selection, sizeof(selection),
        values, count, sizeof(*values), 3u | (measure ? 4u : 0u) | DOE_COMPILER_CORE_BINDING_NUMBERS,
        measure ? NULL : &outputs, measure ? 0u : sizeof(outputs), result, sizeof(*result));
    free(workspace);
    return status;
}
static void expect(uint32_t target, const char *source, const char *entry,
    const DoeCompilerOverrideValueV13 *values, size_t count, bool success) {
    HlslFixture *storage = calloc(1u, sizeof(*storage));
    uint32_t *words = calloc(65536u, sizeof(*words));
    assert(storage != NULL && words != NULL);
    if (!success) {
        memset(storage, 0x5a, sizeof(*storage));
        memset(words, 0x5a, 65536u * sizeof(*words));
    }
    DoeCompilerTextResult result;
    const DoeCompilerStatus status = compile(target, source, entry, values, count, storage, words, &result, false);
    if ((status == DOE_COMPILER_OK) != success)
        fprintf(stderr, "target%u status%u offset%zu %s\n%s\n", target, (unsigned)status,
            result.stages.resources.module.diagnostic.offset, result.stages.resources.module.diagnostic.message, source);
    assert((status == DOE_COMPILER_OK) == success);
    if (success) {
        assert(result.stages.resources.module.published.entries == 1u);
        assert(strcmp(storage->names + storage->entries[0].name.offset, entry) == 0);
        DoeCompilerTextResult measured;
        assert(compile(target, source, entry, values, count, storage, words, &measured, true) == DOE_COMPILER_OK);
        assert(measured.required_text_bytes == result.required_text_bytes);
        assert(measured.stages.resources.module.required.words == result.stages.resources.module.required.words);
    } else {
        assert(result.stages.resources.module.published.words == 0u && result.published_text_bytes == 0u);
        const unsigned char *bytes = (const unsigned char *)storage;
        for (size_t i = 0u; i < sizeof(*storage); ++i) assert(bytes[i] == 0x5a);
        for (size_t i = 0u; i < 65536u; ++i) assert(words[i] == UINT32_C(0x5a5a5a5a));
    }
    free(words);
    free(storage);
}
static void artifact(const char *directory, const char *name, const void *bytes, size_t size) {
    if (directory == NULL) return;
    char path[1024];
    const int length = snprintf(path, sizeof(path), "%s/%s", directory, name);
    assert(length > 0 && (size_t)length < sizeof(path));
    FILE *file = fopen(path, "wbx");
    assert(file != NULL && fwrite(bytes, 1u, size, file) == size && fclose(file) == 0);
}
int main(int argc, char **argv) {
    assert(argc <= 2);
    const char *directory = argc == 2 ? argv[1] : NULL;
    const char *module = "override n:u32; @id(7) override bias:i32=-3;"
        "override unused:u32; override scale:f32=1.25; override enabled:bool=true;"
        "@group(0) @binding(999) var<storage,read_write> output:array<u32>;"
        "var<workgroup> scratch:array<u32,n>; var<workgroup> sibling:array<u32,unused>;"
        "fn helper()->u32{return u32(bias)+n;}"
        "@compute @workgroup_size(n) fn main(@builtin(local_invocation_id) id:vec3u){"
        "scratch[id.x]=id.x; workgroupBarrier(); if(enabled){output[id.x]=scratch[id.x]+helper()+u32(scale*4.0);}}"
        "@compute @workgroup_size(unused) fn other(){sibling[0]=unused; output[0]=sibling[0];}";
    DoeCompilerOverrideValueV13 values[] = {{"n",1u,3.0},{"7",1u,-5.0}};
    const char *defaults = "override zero=0u; override quotient=1u/zero;"
        "@compute @workgroup_size(1) fn uses_default(){_=quotient;}"
        "@compute @workgroup_size(1) fn skip(){}";
    for (uint32_t target = 0u; target < 3u; ++target) {
        expect(target,module,"main",values,2u,true);
        expect(target,module,"other",values,2u,false);
        expect(target,module,"main",values,0u,false);
        expect(target,defaults,"skip",NULL,0u,true);
        expect(target,defaults,"uses_default",NULL,0u,false);
        const DoeCompilerOverrideValueV13 quotient={"quotient",8u,9.0};
        expect(target,defaults,"uses_default",&quotient,1u,true);
        const char *required="override dependency:u32; override outer=1u/dependency;"
            "@compute @workgroup_size(1) fn main(){_=outer;}";
        const DoeCompilerOverrideValueV13 outer={"outer",5u,9.0};
        expect(target,required,"main",&outer,1u,false);
        const DoeCompilerOverrideValueV13 both[]={{"outer",5u,9.0},{"dependency",10u,0.0}};
        expect(target,required,"main",both,2u,true);
        const DoeCompilerOverrideValueV13 invalids[] = {
            {"unknown",7u,2.0},{"bias",4u,2.0},{"07",2u,2.0},{"n",1u,-1.0},
            {"n",1u,3.5},{"unused",6u,NAN},{"unused",6u,4294967296.0}
        };
        for (size_t i = 0u; i < sizeof(invalids)/sizeof(invalids[0]); ++i)
            expect(target,module,"main",&invalids[i],1u,false);
        const DoeCompilerOverrideValueV13 duplicate[]={{"n",1u,3.0},{"n",1u,5.0}};
        expect(target,module,"main",duplicate,2u,false);
        const char *graphics = "override dx:f32; override shade=0.25; override count:u32;"
            "@vertex fn vertex()->@builtin(position) vec4f{return vec4f(dx,0.0,0.0,1.0);}"
            "@fragment fn fragment()->@location(0) vec4f{return vec4f(shade,0.5,0.75,1.0);}"
            "@compute @workgroup_size(count) fn compute_entry(){}";
        const DoeCompilerOverrideValueV13 dx={"dx",2u,0.125};
        expect(target,graphics,"vertex",&dx,1u,true);
        expect(target,graphics,"fragment",NULL,0u,true);
        expect(target,graphics,"compute_entry",NULL,0u,false);
        const char *logical="override enabled:bool;override both=all(vec2<bool>(enabled,true));"
            "override either=any(vec3<bool>(false,enabled,false));"
            "@compute @workgroup_size(select(1u,2u,all(vec2<bool>(both,either)))) fn main(){}";
        expect(target,logical,"main",NULL,0u,false);
        HlslFixture *logical_storage=calloc(1u,sizeof(*logical_storage));
        uint32_t *logical_words=calloc(65536u,sizeof(*logical_words));
        assert(logical_storage!=NULL && logical_words!=NULL);
        for (uint32_t flag=0u;flag<2u;++flag) {
            const DoeCompilerOverrideValueV13 enabled={"enabled",7u,(double)flag};
            DoeCompilerTextResult logical_result;
            assert(compile(target,logical,"main",&enabled,1u,logical_storage,logical_words,&logical_result,false)==DOE_COMPILER_OK);
            assert(logical_storage->entries[0].workgroup_size[0]==flag+1u);
        }
        free(logical_words);
        free(logical_storage);
        const char *barriers="fn publish(){storageBarrier();textureBarrier();}"
            "@compute @workgroup_size(4) fn main(){publish();workgroupBarrier();}";
        expect(target,barriers,"main",NULL,0u,true);
        const char *divergent="@compute @workgroup_size(4) fn main(@builtin(local_invocation_index) i:u32){"
            "if(i>0u){storageBarrier();}}";
        expect(target,divergent,"main",NULL,0u,false);
        const char *limits="override n:u32; @compute @workgroup_size(n,2) fn main(){}";
        const DoeCompilerOverrideValueV13 wider={"n",1u,512.0};
        const DoeCompilerOverrideValueV13 too_big={"n",1u,2147483648.0};
        const DoeCompilerOverrideValueV13 zero={"n",1u,0.0};
        expect(target,limits,"main",&wider,1u,true);
        expect(target,limits,"main",&too_big,1u,false);
        expect(target,limits,"main",&zero,1u,false);
        const char *clamp="override lower=0u; override upper=1u;"
            "@group(0) @binding(0) var<storage,read_write> output:array<u32>;"
            "@compute @workgroup_size(1) fn main(){output[0]=clamp(output[0],lower,upper);}";
        const DoeCompilerOverrideValueV13 lower={"lower",5u,2.0};
        expect(target,clamp,"main",&lower,1u,false);
        const char *invalid_sibling="override n:u32; @compute @workgroup_size(1) fn good(){}"
            "fn bad(){let x=unknown;}";
        expect(target,invalid_sibling,"good",NULL,0u,false);
        const DoeCompilerOverrideValueV13 negative={"n",1u,-1.0};
        const char *runtime_stage="override n:f32; @compute @workgroup_size(1) fn main(){let x=n;_=sqrt(x);}";
        const char *override_stage="override n:f32; @compute @workgroup_size(1) fn main(){_=sqrt(n);}";
        expect(target,runtime_stage,"main",&negative,1u,true);
        expect(target,override_stage,"main",&negative,1u,false);
    }
    const int original_mode=fegetround();
    const int modes[]={FE_TONEAREST,FE_UPWARD,FE_DOWNWARD,FE_TOWARDZERO};
    for (size_t i=0u;i<sizeof(modes)/sizeof(modes[0]);++i) {
        assert(fesetround(modes[i])==0 && feraiseexcept(FE_INVALID|FE_INEXACT)==0);
        const int exceptions=fetestexcept(FE_ALL_EXCEPT);
        errno=ERANGE;
        for (uint32_t target=0u;target<3u;++target) {
            expect(target,module,"main",values,2u,true);
            assert(errno==ERANGE && fegetround()==modes[i] && fetestexcept(FE_ALL_EXCEPT)==exceptions);
            expect(target,defaults,"uses_default",NULL,0u,false);
            assert(errno==ERANGE && fegetround()==modes[i] && fetestexcept(FE_ALL_EXCEPT)==exceptions);
        }
    }
    assert(fesetround(original_mode)==0);
    HlslFixture *storage=calloc(1u,sizeof(*storage));
    uint32_t *words=calloc(65536u,sizeof(*words));
    assert(storage != NULL && words != NULL);
    const char *unused_alias="override unused:u32; alias Unused=array<u32,unused>;"
        "@compute @workgroup_size(1) fn main(){}";
    for(uint32_t target=0u;target<3u;++target) {
        DoeCompilerTextResult unused_result;
        assert(compile(target,unused_alias,"main",NULL,0u,storage,words,&unused_result,false)==DOE_COMPILER_OK);
        /* Aliases have no independent reflection identity. An unreachable
         * deferred array must publish no concrete type/count/size on any target. */
        assert(unused_result.stages.resources.module.published.types==0u);
        assert(unused_result.stages.resources.module.published.workgroups==0u);
        for(size_t i=5u;i<unused_result.stages.resources.module.published.words;i+=words[i]>>16u) {
            assert((words[i]>>16u)>0u);
            assert((words[i]&UINT32_C(0xffff))!=28u); /* No executable deferred OpTypeArray. */
        }
    }
    for(uint32_t target=0u;target<3u;++target) {
        DoeCompilerTextResult result;
        assert(compile(target,module,"main",values,2u,storage,words,&result,false)==DOE_COMPILER_OK);
        assert(storage->entries[0].workgroup_size[0]==3u && storage->entries[0].workgroup_size[1]==1u);
        assert(storage->entries[0].workgroup_storage_bytes==12u);
        assert(result.stages.resources.module.published.workgroups==1u);
        assert(storage->workgroups[0].size_bytes==12u);
        assert(storage->types[storage->workgroups[0].type_index].array_count==3u);
        assert(strcmp(storage->names+storage->workgroups[0].name.offset,"scratch")==0);
        if(target==DOE_COMPILER_TARGET_SPIRV)
            artifact(directory,"selected.spv",words,result.stages.resources.module.published.words*sizeof(*words));
        else artifact(directory,target==DOE_COMPILER_TARGET_METAL?"selected.metal":"selected.hlsl",
            storage->text,result.published_text_bytes);
        values[0].value=7.0;
        assert(compile(target,module,"main",values,2u,storage,words,&result,false)==DOE_COMPILER_OK);
        assert(storage->entries[0].workgroup_size[0]==7u && storage->entries[0].workgroup_storage_bytes==28u);
        assert(storage->workgroups[0].size_bytes==28u);
        values[0].value=3.0;
    }
    artifact(directory,"selected.wgsl",module,strlen(module));
    free(words);
    free(storage);
    puts("selected override specialization target controls passed");
    return 0;
}
