/* SPDX-License-Identifier: MIT
 * Standard fenv symbol failure injection through a fresh source archive. */
#define _GNU_SOURCE
#include "support_m10.h"
enum {GET,HOLD,SET,UPDATE,ROUND,GETROUND,CLEAR,FLAGS,ENABLE,DISABLE,FUNCS};
static const char *const labels[]={"fegetenv","feholdexcept","fesetenv","feupdateenv","fesetround","fegetround","feclearexcept","fesetexceptflag","feenableexcept","fedisableexcept"};
static unsigned counts[FUNCS],selected_function=FUNCS,ordinal,after_effect,persistent,injected,early;
static Bulk *active;
int __real_fegetenv(fenv_t *);int __real_feholdexcept(fenv_t *);int __real_fesetenv(const fenv_t *);int __real_feupdateenv(const fenv_t *);int __real_fesetround(int);int __real_fegetround(void);int __real_feclearexcept(int);int __real_fesetexceptflag(const fexcept_t *,int);
static int hit(unsigned function){if(!native_inside)return 0;errno=EILSEQ;if(active&&!bulk_unchanged(active))early=1u;++counts[function];if(function!=selected_function||counts[function]<ordinal||(!persistent&&counts[function]!=ordinal))return 0;++injected;return 1;}
int __wrap_fegetenv(fenv_t *p){int fail=hit(GET);if(!fail||after_effect){int r=__real_fegetenv(p);if(!fail)return r;}return 1;}
int __wrap_feholdexcept(fenv_t *p){int fail=hit(HOLD);if(!fail||after_effect){int r=__real_feholdexcept(p);if(!fail)return r;}return 1;}
int __wrap_fesetenv(const fenv_t *p){int fail=hit(SET);if(!fail||after_effect){int r=__real_fesetenv(p);if(!fail)return r;}return 1;}
int __wrap_feupdateenv(const fenv_t *p){int fail=hit(UPDATE);if(!fail||after_effect){int r=__real_feupdateenv(p);if(!fail)return r;}return 1;}
int __wrap_fesetround(int p){int fail=hit(ROUND);if(!fail||after_effect){int r=__real_fesetround(p);if(!fail)return r;}return 1;}
int __wrap_fegetround(void){if(hit(GETROUND))return -1;return __real_fegetround();}
int __wrap_feclearexcept(int p){int fail=hit(CLEAR);if(!fail||after_effect){int r=__real_feclearexcept(p);if(!fail)return r;}return 1;}
int __wrap_fesetexceptflag(const fexcept_t *p,int bits){int fail=hit(FLAGS);if(!fail||after_effect){int r=__real_fesetexceptflag(p,bits);if(!fail)return r;}return 1;}
#ifdef __GLIBC__
int __real_feenableexcept(int);int __real_fedisableexcept(int);
int __wrap_feenableexcept(int p){int fail=hit(ENABLE);if(!fail||after_effect){int r=__real_feenableexcept(p);if(!fail)return r;}return -1;}
int __wrap_fedisableexcept(int p){int fail=hit(DISABLE);if(!fail||after_effect){int r=__real_fedisableexcept(p);if(!fail)return r;}return -1;}
#endif
static int exercise(Client *c,Bulk *b,unsigned flags,unsigned kind,unsigned fn,unsigned nth,unsigned after,unsigned forever,unsigned *observed){
 fenv_t before;CHECK(feclearexcept(FE_ALL_EXCEPT)==0&&fesetround(FE_DOWNWARD)==0&&feraiseexcept(FE_INEXACT)==0,"fault caller profile");CHECK(fegetenv(&before)==0,"fault snapshot");int old_round=fegetround(),old_flags=fetestexcept(FE_ALL_EXCEPT);
 bulk_mark(b);DoeCompilerTypedOutputs o=selected(b,flags);DoeCompilerTypedResult r;memset(counts,0,sizeof(counts));selected_function=fn;ordinal=nth;after_effect=after;persistent=forever;injected=early=0u;active=b;
 const char *source=kind?parse_source:numeric_source;errno=23333;DoeCompilerStatus s=invoke(c,source,strlen(source),flags,(flags&4u)?NULL:&o,&r);int returned_errno=errno;active=NULL;selected_function=FUNCS;if(observed)memcpy(observed,counts,sizeof(counts));
 CHECK(returned_errno==23333,"errno survives host errors");CHECK(early==0u,"no bulk output before last fallible host call");CHECK(bulk_guards(b)&&guard_ok(&c->workspace),"fault redzones");
 if(fn<FUNCS){CHECK(injected>0u,"injection reached named call");CHECK(s==DOE_COMPILER_INPUT_ERROR,"host failure explicit input error");CHECK(failure_result(&r,s,0)==0&&bulk_unchanged(b),"fault no exact/published data");CHECK(counts[SET]+counts[UPDATE]<=8u,"bounded environment recovery");if(!forever)CHECK(fegetround()==old_round&&fetestexcept(FE_ALL_EXCEPT)==old_flags,"one-shot fault restores observable state");printf("fault flags=%u kind=%u function=%s ordinal=%u after=%u persistent=%u injected=%u\n",flags,kind,labels[fn],nth,after,forever,injected);}
 else CHECK(s==(kind?DOE_COMPILER_PARSE_ERROR:DOE_COMPILER_OK),"fault baseline expected status");
 CHECK(__real_fesetenv(&before)==0,"harness restores even persistent failure");
 DoeCompilerTypedResult reused;CHECK(invoke(c,numeric_source,sizeof(numeric_source)-1u,7u,NULL,&reused)==DOE_COMPILER_OK,"workspace reusable after injected fault");return 0;
}
int main(void){CHECK(native_allocator_control()==0,"allocator interception positive control");Client c;CHECK(client_new(&c,&generous)==0,"fault client");DoeCompilerTypedResult r;CHECK(invoke(&c,numeric_source,sizeof(numeric_source)-1u,7u,NULL,&r)==DOE_COMPILER_OK,"fault positive fixture");Bulk b;CHECK(bulk_new(&b,&r.required)==0,"fault outputs");const unsigned modes[]={1u,2u,3u,5u,6u,7u};unsigned cases=0u,reached=0u;
 for(unsigned m=0;m<COUNT(modes);++m)for(unsigned k=0;k<2u;++k){unsigned observed[FUNCS];CHECK(exercise(&c,&b,modes[m],k,FUNCS,0u,0u,0u,observed)==0,"fault discovery");for(unsigned f=0;f<FUNCS;++f){printf("observed flags=%u kind=%u function=%s calls=%u\n",modes[m],k,labels[f],observed[f]);if(observed[f])++reached;for(unsigned n=1;n<=observed[f];++n){CHECK(exercise(&c,&b,modes[m],k,f,n,0u,0u,NULL)==0,"one-shot before-effect fault");++cases;if(f!=GETROUND){CHECK(exercise(&c,&b,modes[m],k,f,n,1u,0u,NULL)==0,"one-shot after-effect fault");++cases;}}if((f==SET||f==UPDATE)&&observed[f]){CHECK(exercise(&c,&b,modes[m],k,f,1u,0u,1u,NULL)==0,"persistent restore failure");++cases;}}}
 CHECK(reached>0u,"numeric environment wrapper coverage not vacuous");bulk_free(&b);client_free(&c);CHECK(atomic_load(&native_allocations)==0u,"fault path no allocation");printf("PASS %u injected faults across %u reached function/mode/source paths; unobserved symbols uncredited\n",cases,reached);return 0;}
