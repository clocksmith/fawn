/* SPDX-License-Identifier: MIT
 * Independent every-public-entry FP/errno preflight and failure checks. */
#define _GNU_SOURCE
#include "host_support_m9.h"
#include "fixtures_m9.h"
static const char unsupported_source[] =
    "struct P{n:array<u32,4>} @group(0) @binding(0) var<uniform> p:P;"
    "@compute @workgroup_size(1) fn main(){}";
static const char overflow_f32[] = "const x=exp(100f);@compute @workgroup_size(1) fn main(){}";
static const char negative_sqrt[] = "const x=sqrt(-1.0);@compute @workgroup_size(1) fn main(){}";
static const char wrong_call[] = "fn f(x:u32){} @compute @workgroup_size(1) fn main(){f(true);}";
static int query_one(unsigned api,unsigned kind) {
    Env before,after; DoeCompilerWorkspaceRequirements req;
    DoeCompilerJobCapacities caps=job_caps;
    memset(&req,BYTE_MARK,sizeof(req));
    unsigned version=kind==1u?99u:api;
    size_t size=kind==2u?sizeof(req)-1u:sizeof(req);
    size_t capsize=kind==4u?sizeof(caps)-1u:sizeof(caps);
    if (kind==5u) caps.expressions=SIZE_MAX;
    CHECK(env_get(&before)==0,"query before"); errno=13579;
    DoeCompilerStatus st=api==2u?doe_compiler_workspace_requirements(version,kind==3u?NULL:&req,size):
        doe_compiler_job_requirements(version,kind==6u?NULL:&caps,capsize,kind==3u?NULL:&req,size);
    int observed_errno=errno;
    CHECK(env_get(&after)==0,"query after");
    DoeCompilerStatus expected=kind==0u?DOE_COMPILER_OK:kind==3u||kind==5u||kind==6u?DOE_COMPILER_INPUT_ERROR:DOE_COMPILER_ABI_MISMATCH;
    CHECK(st==expected,"query status");
    CHECK(observed_errno==13579,"query errno");
    CHECK(env_equal(&before,&after),"query FP state");
    if (st) CHECK(all_bytes(&req,sizeof(req),BYTE_MARK),"failed query unchanged");
    return 0;
}
static int compile_one(Client *c,unsigned kind) {
    Output out; DoeCompilerResult r1; DoeCompilerResultV2 r2; Env before,after;
    const char *source=numeric_source; size_t length=strlen(source),cap=WORD_CAP;
    unsigned version=c->api; size_t result_size=c->api==1u?sizeof(r1):sizeof(r2);
    void *workspace=c->workspace; size_t workspace_size=c->workspace_size;
    DoeCompilerJobCapacities caps=job_caps; size_t caps_size=sizeof(caps);
    DoeCompilerStatus expected=DOE_COMPILER_INPUT_ERROR;
    switch(kind) {
        case 0u: expected=DOE_COMPILER_OK; break;
        case 1u: version=99u; expected=DOE_COMPILER_ABI_MISMATCH; break;
        case 2u: --result_size; expected=DOE_COMPILER_ABI_MISMATCH; break;
        case 3u: break; /* NULL result */
        case 4u: source=NULL; break;
        case 5u: break; /* NULL words */
        case 6u: source=rejected_parse_source; length=strlen(source); expected=DOE_COMPILER_PARSE_ERROR; break;
        case 7u: source=rejected_numeric_source; length=strlen(source); expected=DOE_COMPILER_SEMANTIC_ERROR; break;
        case 8u: source=unsupported_source; length=strlen(source); expected=DOE_COMPILER_SEMANTIC_ERROR; break;
        case 9u: cap=1u; expected=DOE_COMPILER_OUTPUT_TOO_SMALL; break;
        case 10u: source=overflow_f32; length=strlen(source); expected=DOE_COMPILER_SEMANTIC_ERROR; break;
        case 11u: source=negative_sqrt; length=strlen(source); expected=DOE_COMPILER_SEMANTIC_ERROR; break;
        case 12u: source=wrong_call; length=strlen(source); expected=DOE_COMPILER_SEMANTIC_ERROR; break;
        case 13u: workspace=NULL; break;
        case 14u: --workspace_size; break;
        case 15u: workspace=(unsigned char *)workspace+1u; break;
        case 16u: --caps_size; expected=DOE_COMPILER_ABI_MISMATCH; break;
        case 17u: caps.expression_depth=0u; break;
        default: return 1;
    }
    memset(&out,BYTE_MARK,sizeof(out)); memset(&r1,BYTE_MARK,sizeof(r1)); memset(&r2,BYTE_MARK,sizeof(r2));
    CHECK(env_get(&before)==0,"compile snapshot"); errno=24680;
    DoeCompilerStatus st;
    if (c->api==1u) st=doe_compiler_compile(version,source,length,kind==5u?NULL:out.words,cap,kind==3u?NULL:&r1,result_size);
    else if (c->api==2u) st=doe_compiler_compile_v2(version,workspace,workspace_size,source,length,kind==5u?NULL:out.words,cap,kind==3u?NULL:&r2,result_size);
    else st=doe_compiler_compile_job(version,&caps,caps_size,workspace,workspace_size,source,length,kind==5u?NULL:out.words,cap,kind==3u?NULL:&r2,result_size);
    int observed_errno=errno;
    CHECK(env_get(&after)==0,"compile snapshot after");
    if (st!=expected) fprintf(stderr,"public exit api=%u kind=%u st=%d expected=%d diagnostic=%s\n",c->api,kind,(int)st,(int)expected,c->api==1u?r1.diagnostic.message:r2.diagnostic.message);
    CHECK(st==expected,"public status"); CHECK(observed_errno==24680,"every exit errno");
    CHECK(env_equal(&before,&after),"every exit exact FP state"); CHECK(guards(c,&out),"exit guards");
    if (st) {
        CHECK(all_bytes(out.words,sizeof(out.words),BYTE_MARK),"failed exit output unchanged");
        if (st==DOE_COMPILER_ABI_MISMATCH||kind==3u) {
            CHECK(all_bytes(&r1,sizeof(r1),BYTE_MARK)&&all_bytes(&r2,sizeof(r2),BYTE_MARK),"ABI or NULL result untouched");
        } else if (c->api==1u) {
            CHECK(!r1.word_count&&all_bytes(&r1.reflection,sizeof(r1.reflection),0),"ABI1 failed result cleared");
        } else CHECK(!r2.word_count&&all_bytes(&r2.reflection,sizeof(r2.reflection),0),"workspace failed result cleared");
    }
    return 0;
}
int main(void) {
    (void)numeric_expected; (void)retained_source; (void)invoke;
    const int modes[]={FE_TONEAREST,FE_DOWNWARD,FE_UPWARD,FE_TOWARDZERO};
    const unsigned denorms[]={0u,0x40u,0x8000u,0x8040u};
    const int flags[]={0,0,0,0,0,0,0,FE_ALL_EXCEPT,FE_INEXACT};
    const int traps[]={0,FE_INVALID,FE_DIVBYZERO,FE_OVERFLOW,FE_UNDERFLOW,FE_INEXACT,FE_ALL_EXCEPT,0,FE_INVALID|FE_DIVBYZERO};
    unsigned calls=0u;
    for (unsigned api=1u;api<=3u;++api) {
        Client client; CHECK(client_open(&client,api)==0,"public client");
        for (unsigned m=0;m<4u;++m) for (unsigned d=0;d<4u;++d) for (unsigned t=0;t<9u;++t) {
            CHECK(configure(modes[m],denorms[d],flags[t],traps[t])==0,"public profile");
            for (unsigned k=0;k<(api==1u?13u:api==2u?16u:18u);++k) { CHECK(compile_one(&client,k)==0,"compile exit matrix"); ++calls; }
            if (api>1u) for (unsigned k=0;k<(api==2u?4u:7u);++k) { CHECK(query_one(api,k)==0,"query matrix"); ++calls; }
        }
        CHECK(configure(FE_TONEAREST,0u,0,0)==0,"public harness reset"); free(client.workspace);
    }
    printf("PASS %u public preflight/status exits preserve caller errno and full FP state\n",calls);
    return 0;
}
