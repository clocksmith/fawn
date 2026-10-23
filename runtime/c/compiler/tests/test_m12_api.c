/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "../include/doe_compiler_typed.h"
#include "../include/doe_compiler_job.h"
#include <assert.h>
#include <errno.h>
#include <fenv.h>
#include <pthread.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <xmmintrin.h>

static const DoeCompilerTypedCapacities caps={.source_bytes=8192u,.tokens=1024u,.statements=64u,.expressions=256u,
    .types=32u,.members=64u,.resources=8u,.access_steps=320u,.constants=64u,.constant_edges=128u,
    .ir_instructions=1600u,.ir_operands=256u,.target_declarations=512u,.traversal_items=97u,.words=8192u,
    .expression_depth=32u,.control_depth=16u,.type_syntax_depth=32u};
/* M12 author adapter controls derived from the immutable M11 API test. */
static const char good[]="@group(0) @binding(0) var<storage,read_write> out:array<u32>;"
    "fn calc(x:vec4u)->u32{return dot(clamp(x,vec4u(1u),vec4u(4u)),vec4u(65536u));}"
    "@compute @workgroup_size(4) fn main(@builtin(local_invocation_index) lane:u32){"
    "out[lane]=calc(vec4u(lane));}";
static const char bad[]="@compute @workgroup_size(4) fn main(@builtin(local_invocation_index) lane:u32){"
    "if(dot(vec2u(lane),vec2u(0u))==0u){workgroupBarrier();}}";
static const char *const numeric_errors[] = {
    "const x=dot(vec2(9223372036854775807,1),vec2(1,1));@compute @workgroup_size(1) fn main(){}",
    "const x=normalize(vec2f(0));@compute @workgroup_size(1) fn main(){}",
    "const x=sqrt(vec2f(1,-1));@compute @workgroup_size(1) fn main(){}",
    "@compute @workgroup_size(1) fn main(){let x=clamp(vec2i(0),vec2i(1,5),vec2i(2,4));}"
};
static const char anonymous[]="var<workgroup> x:array<vec3u,4>;@compute @workgroup_size(1) fn main(){return;}";
static const char nominal[]="struct S{x:bool,}var<workgroup> x:S;@compute @workgroup_size(1) fn main(){}";
static DoeCompilerStatus call(void *workspace,size_t bytes,const char *source,uint32_t flags,DoeCompilerTypedOutputs *outputs,DoeCompilerTypedResult *result) {
    const int before=errno;
    const int rounding=fegetround(), exceptions=fetestexcept(FE_ALL_EXCEPT);
    const uint32_t mxcsr = _mm_getcsr();
    const DoeCompilerStatus status=doe_compiler_compile_typed(4u,&caps,sizeof(caps),workspace,bytes,source,strlen(source),flags,
        outputs,outputs==NULL?0u:sizeof(*outputs),result,sizeof(*result));
    assert(errno==before); assert(fegetround()==rounding); assert(fetestexcept(FE_ALL_EXCEPT)==exceptions);
    assert(_mm_getcsr() == mxcsr);
    return status;
}
static bool empty_counts(DoeCompilerTypedOutputCounts n) {return n.words==0u && n.types==0u && n.members==0u && n.resources==0u && n.name_bytes==0u;}
static void *parallel(void *unused) {
    (void)unused;
    assert(fesetround(FE_DOWNWARD) == 0);
    assert(feraiseexcept(FE_INVALID | FE_DIVBYZERO) == 0);
    _mm_setcsr(_mm_getcsr() | UINT32_C(0x8040));
    DoeCompilerWorkspaceRequirements requirements;
    assert(doe_compiler_typed_requirements(4u,&caps,sizeof(caps),&requirements,sizeof(requirements))==DOE_COMPILER_OK);
    void *workspace=malloc(requirements.size_bytes); assert(workspace!=NULL);
    uint32_t *words=malloc(caps.words*sizeof(*words)); assert(words!=NULL);
    DoeCompilerTypeV4 types[32]; DoeCompilerMemberV4 members[64]; DoeCompilerResourceV4 resources[8];char names[1024];
    for (uint32_t repeat=0u;repeat<32u;++repeat) for (uint32_t mode=1u;mode<=7u;++mode) {
        if (mode==4u) continue;
        DoeCompilerTypedOutputs outputs={.type_record_size=sizeof(*types),.member_record_size=sizeof(*members),.resource_record_size=sizeof(*resources)};
        if ((mode&1u)!=0u) {outputs.words=words;outputs.word_capacity=caps.words;}
        if ((mode&2u)!=0u) {outputs.types=types;outputs.type_capacity=32u;outputs.members=members;outputs.member_capacity=64u;
            outputs.resources=resources;outputs.resource_capacity=8u;outputs.names=names;outputs.name_capacity=sizeof(names);}
        DoeCompilerTypedResult result;
        errno=EDOM;
        assert(call(workspace,requirements.size_bytes,good,mode,(mode&4u)!=0u?NULL:&outputs,&result)==DOE_COMPILER_OK);
        assert(result.required_is_exact==1u);
        if ((mode&4u)!=0u) assert(empty_counts(result.published));
        else {
            assert(memcmp(&result.published,&result.required,sizeof(result.required))==0);
            if ((mode&2u)!=0u) {assert(result.published.resources==1u);assert(result.module.declared_struct_count==0u);}
        }
        memset(words,0xa5,caps.words*sizeof(*words));memset(types,0xa5,sizeof(types));
        assert(call(workspace,requirements.size_bytes,bad,mode,(mode&4u)!=0u?NULL:&outputs,&result)==DOE_COMPILER_SEMANTIC_ERROR);
        assert(empty_counts(result.published));assert(result.exhausted_pool==DOE_COMPILER_POOL_NONE);assert(result.minimum_pool_capacity==0u);
        assert(strstr(result.diagnostic.message,"uniform control cannot be established")!=NULL);
        assert(words[0]==UINT32_C(0xa5a5a5a5) && ((unsigned char *)types)[0]==0xa5u);
        for (size_t i = 0u; i < sizeof(numeric_errors) / sizeof(*numeric_errors); ++i) {
            assert(call(workspace, requirements.size_bytes, numeric_errors[i], mode,
                        (mode & 4u) != 0u ? NULL : &outputs, &result) == DOE_COMPILER_SEMANTIC_ERROR);
            assert(empty_counts(result.published));
            assert(result.exhausted_pool == DOE_COMPILER_POOL_NONE);
            assert(words[0] == UINT32_C(0xa5a5a5a5) && ((unsigned char *)types)[0] == 0xa5u);
        }
    }
    DoeCompilerTypedResult measured,result;
    assert(call(workspace,requirements.size_bytes,anonymous,7u,NULL,&measured)==DOE_COMPILER_OK);
    assert(measured.required.types==0u && measured.required.resources==0u && measured.required.members==0u);
    assert(call(workspace,requirements.size_bytes,nominal,7u,NULL,&measured)==DOE_COMPILER_OK);
    assert(measured.required.types==2u && measured.required.members==1u && measured.required.resources==0u);
    assert(call(workspace,requirements.size_bytes,good,5u,NULL,&measured)==DOE_COMPILER_OK);
    DoeCompilerTypedOutputs outputs={.words=words,.word_capacity=measured.required.words,.type_record_size=sizeof(*types),
        .member_record_size=sizeof(*members),.resource_record_size=sizeof(*resources)};
    assert(call(workspace,requirements.size_bytes,good,1u,&outputs,&result)==DOE_COMPILER_OK);
    --outputs.word_capacity;memset(words,0xa5,caps.words*sizeof(*words));
    assert(call(workspace,requirements.size_bytes,good,1u,&outputs,&result)==DOE_COMPILER_OUTPUT_TOO_SMALL);
    assert(empty_counts(result.published) && result.required_is_exact==1u && words[0]==UINT32_C(0xa5a5a5a5));
    assert(call(workspace,requirements.size_bytes-1u,good,7u,NULL,&result)==DOE_COMPILER_INPUT_ERROR);
    free(words);free(workspace);return NULL;
}
int main(void) {
    DoeCompilerWorkspaceRequirements fixed; assert(doe_compiler_workspace_requirements(2u,&fixed,sizeof(fixed))==DOE_COMPILER_OK);
    assert(fixed.size_bytes==146256u);
    void *workspace=malloc(fixed.size_bytes); assert(workspace!=NULL);
    uint32_t words[4096];DoeCompilerResult a;DoeCompilerResultV2 b;
    assert(doe_compiler_compile(1u,good,sizeof(good)-1u,words,4096u,&a,sizeof(a))==DOE_COMPILER_OK);
    assert(doe_compiler_compile_v2(2u,workspace,fixed.size_bytes,good,sizeof(good)-1u,words,4096u,&b,sizeof(b))==DOE_COMPILER_OK);
    assert(doe_compiler_compile_v2(2u,workspace,140056u,good,sizeof(good)-1u,words,4096u,&b,sizeof(b))==DOE_COMPILER_INPUT_ERROR);
    assert(doe_compiler_compile(1u,bad,sizeof(bad)-1u,words,4096u,&a,sizeof(a))==DOE_COMPILER_SEMANTIC_ERROR);
    const DoeCompilerJobCapacities job={8192u,1024u,64u,256u,8192u,32u,16u};
    DoeCompilerWorkspaceRequirements required; assert(doe_compiler_job_requirements(3u,&job,sizeof(job),&required,sizeof(required))==DOE_COMPILER_OK);
    free(workspace);workspace=malloc(required.size_bytes);assert(workspace!=NULL);
    assert(doe_compiler_compile_job(3u,&job,sizeof(job),workspace,required.size_bytes,good,sizeof(good)-1u,words,4096u,&b,sizeof(b))==DOE_COMPILER_OK);
    assert(doe_compiler_compile_job(3u,&job,sizeof(job),workspace,required.size_bytes,bad,sizeof(bad)-1u,words,4096u,&b,sizeof(b))==DOE_COMPILER_SEMANTIC_ERROR);
    free(workspace);
    pthread_t threads[4];for (size_t i=0u;i<4u;++i) assert(pthread_create(&threads[i],NULL,parallel,NULL)==0);
    for (size_t i=0u;i<4u;++i) assert(pthread_join(threads[i],NULL)==0);
    puts("M12 API: four adapters, six output modes, exact/short/stale spans, reflection closure, errno/fenv/MXCSR and 768 concurrent mode iterations with numeric-error controls passed");
    return 0;
}
