/* SPDX-License-Identifier: MIT
 * Portable observable C fenv checks; GNU x86 extension checks when available. */
#define _GNU_SOURCE
#include "support_m10.h"
#include <pthread.h>
#if defined(__linux__) && (defined(__i386__) || defined(__x86_64__))
#include <xmmintrin.h>
#define EXTENDED_ENV 1
#endif
typedef struct {fenv_t raw;int rounding,flags;
#ifdef EXTENDED_ENV
 unsigned mxcsr;int traps;
#endif
} Env;
static int snapshot(Env *e){memset(e,0,sizeof(*e));if(fegetenv(&e->raw)!=0)return 1;e->rounding=fegetround();e->flags=fetestexcept(FE_ALL_EXCEPT);
#ifdef EXTENDED_ENV
 e->mxcsr=_mm_getcsr();e->traps=fegetexcept();
#endif
 return e->rounding<0;}
static int equivalent(const Env *a,const Env *b){int same=a->rounding==b->rounding&&a->flags==b->flags;
#ifdef EXTENDED_ENV
 same=same&&a->mxcsr==b->mxcsr&&a->traps==b->traps&&memcmp(&a->raw,&b->raw,sizeof(fenv_t))==0;
#endif
 return same;}
static int configure(unsigned profile){const int round[]={FE_TONEAREST,FE_DOWNWARD,FE_UPWARD,FE_TOWARDZERO};
#ifdef EXTENDED_ENV
 if(fedisableexcept(FE_ALL_EXCEPT)<0)return 1;
#endif
 if(feclearexcept(FE_ALL_EXCEPT)!=0||fesetround(round[profile%4u])!=0)return 1;
 if(profile&4u)if(feraiseexcept(FE_INEXACT)!=0)return 1;
#ifdef EXTENDED_ENV
 unsigned denorm=(profile&8u)?0x8040u:0u;_mm_setcsr((_mm_getcsr()&~0x8040u)|denorm);if(profile&16u)if(feenableexcept(FE_INVALID|FE_DIVBYZERO)<0)return 1;
#endif
 return 0;}
typedef struct {DoeCompilerTypedOutputCounts count;unsigned char *bytes[5];} Reference;
static Reference reference;
static int baseline(void){Client c;CHECK(client_new(&c,&generous)==0,"host baseline client");DoeCompilerTypedResult r;CHECK(invoke(&c,numeric_source,sizeof(numeric_source)-1u,7u,NULL,&r)==DOE_COMPILER_OK,"host numeric admitted");reference.count=r.required;Bulk b;CHECK(bulk_new(&b,&reference.count)==0,"host baseline outputs");CHECK(invoke(&c,numeric_source,sizeof(numeric_source)-1u,3u,&b.o,&r)==DOE_COMPILER_OK,"host reference output");for(unsigned i=0;i<5u;++i){reference.bytes[i]=(unsigned char *)malloc(b.span[i].bytes);CHECK(reference.bytes[i]!=NULL,"reference allocation");memcpy(reference.bytes[i],b.span[i].data,b.span[i].bytes);}bulk_free(&b);client_free(&c);return 0;}
static int one(Client *c,Bulk *b,unsigned flags,unsigned kind){Env before,after;DoeCompilerTypedResult r;DoeCompilerTypedOutputs o=selected(b,flags);const char *source=kind==1u?parse_source:kind==2u?semantic_source:numeric_source;size_t n=strlen(source);DoeCompilerStatus expected=kind==1u?DOE_COMPILER_PARSE_ERROR:kind==2u?DOE_COMPILER_SEMANTIC_ERROR:DOE_COMPILER_OK;
 if(kind==3u&&!(flags&4u)){if(flags&1u)o.word_capacity=0u;else o.name_capacity=0u;expected=DOE_COMPILER_OUTPUT_TOO_SMALL;}
 bulk_mark(b);CHECK(snapshot(&before)==0,"host snapshot");errno=29999;DoeCompilerStatus s=invoke(c,source,n,flags,(flags&4u)?NULL:&o,&r);int saved_errno=errno;CHECK(snapshot(&after)==0,"host after snapshot");CHECK(saved_errno==29999&&equivalent(&before,&after),"errno and caller floating state");CHECK(s==expected,"host status");CHECK(guard_ok(&c->workspace)&&bulk_guards(b),"host redzones");
 if(s==DOE_COMPILER_OK){CHECK(r.required_is_exact==1u,"host exact");for(unsigned i=0;i<5u;++i){int active=!(flags&4u)&&(i==0u?(flags&1u):(flags&2u));CHECK(active?memcmp(reference.bytes[i],b->span[i].data,b->span[i].bytes)==0:bytes_are(b->span[i].data,b->span[i].bytes,MARK),"mode output deterministic across host state");}}
 else CHECK(failure_result(&r,s,s==DOE_COMPILER_OUTPUT_TOO_SMALL)==0&&bulk_unchanged(b),"host failure atomic");return 0;}
typedef struct {unsigned profile;int failed;} Thread;
static void *thread_main(void *p){Thread *t=(Thread *)p;Client c;Bulk b;Env initial;t->failed=snapshot(&initial);if(t->failed)return NULL;t->failed=client_new(&c,&generous);if(t->failed)return NULL;t->failed=bulk_new(&b,&reference.count);if(t->failed){client_free(&c);return NULL;}t->failed=configure(t->profile);
 const unsigned modes[]={1u,2u,3u,5u,6u,7u};for(unsigned i=0;i<36u&&!t->failed;++i)t->failed=one(&c,&b,modes[i%6u],(i/6u)%4u);
 bulk_free(&b);client_free(&c);if(fesetenv(&initial.raw)!=0)t->failed=1;return NULL;}
int main(void){CHECK(native_allocator_control()==0,"allocator interception positive control");Env original;CHECK(snapshot(&original)==0,"save host");CHECK(configure(0u)==0&&baseline()==0,"baseline independent of caller environment");Client c;CHECK(client_new(&c,&generous)==0,"host client");Bulk b;CHECK(bulk_new(&b,&reference.count)==0,"host outputs");
 const unsigned modes[]={1u,2u,3u,5u,6u,7u};unsigned profiles=8u;
#ifdef EXTENDED_ENV
 profiles=32u;
#endif
 for(unsigned p=0;p<profiles;++p){CHECK(configure(p)==0,"profile configure");for(unsigned f=0;f<COUNT(modes);++f)for(unsigned k=0;k<4u;++k)CHECK(one(&c,&b,modes[f],k)==0,"host matrix");}
 CHECK(configure(0u)==0,"harness reset");bulk_free(&b);client_free(&c);pthread_t ids[8];Thread threads[8];for(unsigned i=0;i<8u;++i){threads[i].profile=i*3u;threads[i].failed=0;CHECK(pthread_create(&ids[i],NULL,thread_main,&threads[i])==0,"thread creation");}for(unsigned i=0;i<8u;++i){CHECK(pthread_join(ids[i],NULL)==0&&threads[i].failed==0,"independent workspace and thread FP state");}
 for(unsigned i=0;i<5u;++i)free(reference.bytes[i]);CHECK(atomic_load(&native_allocations)==0u,"library does not allocate/free");CHECK(fesetenv(&original.raw)==0,"restore caller");printf("PASS %u host profiles, 8 independent concurrent workspaces; no physical device claim\n",profiles);return 0;}
