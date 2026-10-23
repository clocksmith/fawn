/* SPDX-License-Identifier: MIT */
#ifndef HOST_SUPPORT_M8_H
#define HOST_SUPPORT_M8_H
#include "doe_compiler_job.h"
#include <fenv.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <xmmintrin.h>
#define WORD_CAP 4096u
#define BYTE_MARK 0xa5
#define CHECK(c, m) do { if (!(c)) { fprintf(stderr,"FAIL %s:%d %s\n",__FILE__,__LINE__,m); return 1; } } while (0)
static const DoeCompilerJobCapacities job_caps = {65536u,8192u,1024u,4096u,65536u,64u,32u};
typedef struct { fenv_t raw; unsigned mxcsr; int rounding, flags, traps; } Env;
typedef struct { void *workspace; size_t workspace_size; unsigned api; } Client;
typedef struct { uint32_t prefix[8], words[WORD_CAP], suffix[8]; } Output;
static int all_bytes(const void *data,size_t size,unsigned char byte) {
    const unsigned char *p=data;
    for (size_t i=0;i<size;++i) if (p[i]!=byte) return 0;
    return 1;
}
static int env_get(Env *env) {
    memset(env,0,sizeof(*env));
    if (fegetenv(&env->raw)!=0) return 1;
    env->mxcsr=_mm_getcsr();
    env->rounding=fegetround(); env->flags=fetestexcept(FE_ALL_EXCEPT);
    env->traps=fegetexcept();
    return env->rounding<0;
}
static int env_equal(const Env *a,const Env *b) {
    return memcmp(&a->raw,&b->raw,sizeof(fenv_t))==0 && a->mxcsr==b->mxcsr &&
        a->rounding==b->rounding && a->flags==b->flags && a->traps==b->traps;
}
static int configure(int mode,unsigned denorm,int flags,int traps) {
    if (fedisableexcept(FE_ALL_EXCEPT)<0 || feclearexcept(FE_ALL_EXCEPT)!=0 || fesetround(mode)!=0) return 1;
    _mm_setcsr((_mm_getcsr() & ~UINT32_C(0x8040)) | denorm);
    if (feraiseexcept(flags)!=0 || feenableexcept(traps)<0) return 1;
    return 0;
}
static int client_open(Client *c,unsigned api) {
    DoeCompilerWorkspaceRequirements req={0,0};
    c->api=api; c->workspace=NULL; c->workspace_size=0;
    if (api==1u) return 0;
    DoeCompilerStatus st=api==2u ? doe_compiler_workspace_requirements(2u,&req,sizeof(req)) :
        doe_compiler_job_requirements(3u,&job_caps,sizeof(job_caps),&req,sizeof(req));
    if (st!=DOE_COMPILER_OK || req.alignment_bytes>_Alignof(max_align_t)) return 1;
    if (api==2u && req.size_bytes>77760u) return 1;
    c->workspace=malloc(req.size_bytes+64u); c->workspace_size=req.size_bytes;
    if (c->workspace==NULL) return 1;
    memset(c->workspace,BYTE_MARK,req.size_bytes+64u);
    return 0;
}
static DoeCompilerStatus invoke(Client *c,const char *source,size_t length,Output *o,size_t cap,DoeCompilerResultV2 *r) {
    if (c->api==1u) {
        DoeCompilerResult old;
        memset(&old,BYTE_MARK,sizeof(old));
        DoeCompilerStatus st=doe_compiler_compile(1u,source,length,o->words,cap,&old,sizeof(old));
        memset(r,0,sizeof(*r)); r->word_count=old.word_count; r->diagnostic=old.diagnostic;
        /* Common reflection facts independently checked; struct padding excluded. */
        memcpy(r->reflection.entry_point,old.reflection.entry_point,64u);
        memcpy(r->reflection.workgroup_size,old.reflection.workgroup_size,3u*sizeof(uint32_t));
        r->reflection.binding_count=old.reflection.binding_count;
        if (st!=DOE_COMPILER_OK && !all_bytes(&old.reflection,sizeof(old.reflection),0)) return DOE_COMPILER_OK;
        return st;
    }
    if (c->api==2u) return doe_compiler_compile_v2(2u,c->workspace,c->workspace_size,source,length,o->words,cap,r,sizeof(*r));
    return doe_compiler_compile_job(3u,&job_caps,sizeof(job_caps),c->workspace,c->workspace_size,source,length,o->words,cap,r,sizeof(*r));
}
static int guards(const Client *c,const Output *o) {
    return all_bytes(o->prefix,sizeof(o->prefix),BYTE_MARK) && all_bytes(o->suffix,sizeof(o->suffix),BYTE_MARK) &&
        (c->workspace==NULL || all_bytes((unsigned char *)c->workspace+c->workspace_size,64u,BYTE_MARK));
}
#endif
