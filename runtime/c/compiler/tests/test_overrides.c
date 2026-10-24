/* SPDX-License-Identifier: MIT */
#include "hlsl_fixture.h"
#include "../include/doe_compiler_specialization.h"
#include <assert.h>
#include <errno.h>
#include <fenv.h>
#include <stdio.h>
#include <stdlib.h>

static DoeCompilerSpecializationCapacities capacities(void) {
    return (DoeCompilerSpecializationCapacities){hlsl_capacities(), 32u};
}
static DoeCompilerStatus validate(const char *source, DoeCompilerValidationOutputs *outputs,
                                 DoeCompilerValidationResult *result) {
    DoeCompilerSpecializationCapacities caps = capacities();
    DoeCompilerWorkspaceRequirements requirements;
    assert(doe_compiler_specialization_requirements(13u, 0u, &caps, sizeof(caps),
        &requirements, sizeof(requirements)) == DOE_COMPILER_OK);
    void *workspace = malloc(requirements.size_bytes);
    assert(workspace != NULL);
    const DoeCompilerStatus status = doe_compiler_validate_module(13u, &caps, sizeof(caps), workspace,
        requirements.size_bytes, source, strlen(source), DOE_COMPILER_CORE_BINDING_NUMBERS,
        outputs, outputs == NULL ? 0u : sizeof(*outputs), result, sizeof(*result));
    free(workspace);
    return status;
}
static void accepted(const char *source) {
    DoeCompilerValidationResult result;
    const DoeCompilerStatus status = validate(source, NULL, &result);
    if (status != DOE_COMPILER_OK) fprintf(stderr, "%zu: %s\n%s\n", result.diagnostic.offset,
        result.diagnostic.message, source);
    assert(status == DOE_COMPILER_OK && result.required_is_exact == 1u);
}
static void rejected(const char *source) {
    DoeCompilerValidationResult result;
    assert(validate(source, NULL, &result) != DOE_COMPILER_OK);
    assert(result.published_overrides == 0u && result.published_entries == 0u);
}
int main(void) {
    const char legacy_source[]="override n:u32;";
    DoeCompilerResult legacy_result;
    assert(doe_compiler_compile(1u,legacy_source,sizeof(legacy_source)-1u,NULL,0u,
        &legacy_result,sizeof(legacy_result))==DOE_COMPILER_PARSE_ERROR);
    const char legacy_id_source[]="@id(7) override n:u32;";
    assert(doe_compiler_compile(1u,legacy_id_source,sizeof(legacy_id_source)-1u,NULL,0u,
        &legacy_result,sizeof(legacy_result))==DOE_COMPILER_UNSUPPORTED);
    accepted("");
    accepted("@id(7,) override n:u32;");
    accepted("override n:u32; @compute @workgroup_size(n) fn main(){let x=n+1u; _=x;}");
    accepted("override n=0u; @compute @workgroup_size(n) fn main(){}");
    accepted("override n:u32; @compute @workgroup_size(n,2) fn main(){}");
    accepted("@compute @workgroup_size(1024) fn main(){}");
    accepted("override n:u32; alias A=array<u32,n>; var<workgroup> a:A;"
        "@compute @workgroup_size(1) fn main(){a[0]=n;}");
    accepted("override n:u32; override x=vec2u(n,1u)[n]; @compute @workgroup_size(1) fn main(){_=x;}");
    accepted("override n:f32; override x=mat2x2f(n,1.0,2.0,3.0)[1].x;"
        "@compute @workgroup_size(1) fn main(){_=x;}");
    accepted("override enabled:bool; override x=select(1u,2u,enabled);"
        "@compute @workgroup_size(1) fn main(){_=x;}");
    accepted("override n:i32=0; override x=1/n; @compute @workgroup_size(1) fn main(){_=x;}");
    accepted("override n:u32; @compute @workgroup_size(1) fn main(){let x=sqrt(f32(n)); _=x;}");
    accepted("override n:u32; @compute @workgroup_size(1) fn main(){if(n==1u){workgroupBarrier();}}");
    rejected("override n; @compute @workgroup_size(1) fn main(){}");
    rejected("override n:vec2u; @compute @workgroup_size(1) fn main(){}");
    rejected("override n:u32; const x=n; @compute @workgroup_size(1) fn main(){}");
    rejected("override n:u32; @compute @workgroup_size(1) fn main(){const x=n;}");
    rejected("override enabled:bool; const x=select(1u,2u,enabled);"
        "@compute @workgroup_size(1) fn main(){}");
    rejected("override n:u32; struct S{a:array<u32,n>} @compute @workgroup_size(1) fn main(){}");
    rejected("override n:u32; @group(0) @binding(0) var<storage,read> a:array<u32,n>;"
        "@compute @workgroup_size(1) fn main(){}");
    rejected("override n:u32; override x=n/0u; @compute @workgroup_size(1) fn main(){}");
    rejected("override n:u32; override x=clamp(n,3u,2u); @compute @workgroup_size(1) fn main(){}");
    rejected("override n=unknown; @compute @workgroup_size(1) fn main(){}");
    rejected("override n=x; override x=n; @compute @workgroup_size(1) fn main(){}");
    rejected("@id(7) override n:u32; @id(7u) override x:i32; @compute @workgroup_size(1) fn main(){}");
    rejected("@id(65536) override n:u32; @compute @workgroup_size(1) fn main(){}");
    rejected("override n:u32; @id(n) override x:i32; @compute @workgroup_size(1) fn main(){}");
    rejected("@compute @workgroup_size(0) fn main(){}");
    rejected("override n:u32; @compute @workgroup_size(n,2i) fn main(){}");
    DoeCompilerOverrideV13 declarations[4];
    DoeCompilerModuleEntryV13 entries[4];
    char names[128];
    const char *source="const ID=12; @id(ID+1) override n:u32; override x=2.5;"
        "@compute @workgroup_size(n) fn main(){}";
    DoeCompilerValidationOutputs outputs={declarations,4u,sizeof(declarations[0]),
        entries,4u,sizeof(entries[0]),names,sizeof(names)};
    DoeCompilerValidationResult result;
    assert(validate(source,&outputs,&result)==DOE_COMPILER_OK);
    assert(result.published_overrides==2u && result.published_entries==1u);
    assert(declarations[0].id==13u && declarations[0].scalar_kind==DOE_COMPILER_LEAF_U32);
    assert(declarations[1].scalar_kind==DOE_COMPILER_LEAF_F32);
    assert(strcmp(names+declarations[0].name.offset,"n")==0);
    assert(strcmp(names+entries[0].name.offset,"main")==0);
    memset(names,0x5a,sizeof(names));
    outputs.override_capacity=0u;
    assert(validate(source,&outputs,&result)==DOE_COMPILER_OUTPUT_TOO_SMALL);
    for(size_t i=0u;i<sizeof(names);++i) assert(names[i]==0x5a);
    int modes[]={FE_TONEAREST,FE_UPWARD,FE_DOWNWARD,FE_TOWARDZERO};
    const int old_round=fegetround();
    for(size_t i=0u;i<sizeof(modes)/sizeof(modes[0]);++i){
        assert(fesetround(modes[i])==0);
        assert(feraiseexcept(FE_INVALID|FE_INEXACT)==0);
        const int exceptions=fetestexcept(FE_ALL_EXCEPT);
        errno=ERANGE;
        accepted(source);
        assert(errno==ERANGE && fegetround()==modes[i] && fetestexcept(FE_ALL_EXCEPT)==exceptions);
        rejected("override x=1/0;");
        assert(errno==ERANGE && fegetround()==modes[i] && fetestexcept(FE_ALL_EXCEPT)==exceptions);
    }
    assert(fesetround(old_round)==0);
    puts("override module validation controls passed");
    return 0;
}
