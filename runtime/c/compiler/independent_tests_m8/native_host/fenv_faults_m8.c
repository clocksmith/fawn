/* SPDX-License-Identifier: MIT
 * Symbol-level environmental failure injection, expectations frozen first. */
#define _GNU_SOURCE
#include "host_support_m8.h"
#include "fixtures_m8.h"
enum { GETENV,HOLDEXCEPT,SETENV,UPDATEENV,SETROUND,GETROUND,CLEAREXCEPT,SETEXCEPTFLAG,ENABLEEXCEPT,DISABLEEXCEPT,FUNCTIONS };
static const char *const names[FUNCTIONS]={"fegetenv","feholdexcept","fesetenv","feupdateenv","fesetround","fegetround","feclearexcept","fesetexceptflag","feenableexcept","fedisableexcept"};
static unsigned observed[FUNCTIONS],inside_library,selected,ordinal,after_real,persistent,injected,early_output;
static Output *active_output;
int __real_fegetenv(fenv_t *p); int __real_feholdexcept(fenv_t *p);
int __real_fesetenv(const fenv_t *p); int __real_feupdateenv(const fenv_t *p);
int __real_fesetround(int p); int __real_fegetround(void); int __real_feclearexcept(int p);
int __real_fesetexceptflag(const fexcept_t *p,int bits); int __real_feenableexcept(int p); int __real_fedisableexcept(int p);
int __wrap_fegetenv(fenv_t *p); int __wrap_feholdexcept(fenv_t *p);
int __wrap_fesetenv(const fenv_t *p); int __wrap_feupdateenv(const fenv_t *p);
int __wrap_fesetround(int p); int __wrap_fegetround(void); int __wrap_feclearexcept(int p);
int __wrap_fesetexceptflag(const fexcept_t *p,int bits); int __wrap_feenableexcept(int p); int __wrap_fedisableexcept(int p);
static int hit(unsigned which) {
    if (!inside_library) return 0;
    if (active_output!=NULL && !all_bytes(active_output->words,sizeof(active_output->words),BYTE_MARK)) early_output=1;
    observed[which]++;
    if (which!=selected || observed[which]<ordinal || (!persistent && observed[which]!=ordinal)) return 0;
    injected++;
    return 1;
}
int __wrap_fegetenv(fenv_t *p) { int fail=hit(GETENV); if (!fail || after_real) { int r=__real_fegetenv(p); if (!fail) return r; } return 1; }
int __wrap_feholdexcept(fenv_t *p) { int fail=hit(HOLDEXCEPT); if (!fail || after_real) { int r=__real_feholdexcept(p); if (!fail) return r; } return 1; }
int __wrap_fesetenv(const fenv_t *p) { int fail=hit(SETENV); if (!fail || after_real) { int r=__real_fesetenv(p); if (!fail) return r; } return 1; }
int __wrap_feupdateenv(const fenv_t *p) { int fail=hit(UPDATEENV); if (!fail || after_real) { int r=__real_feupdateenv(p); if (!fail) return r; } return 1; }
int __wrap_fesetround(int p) { int fail=hit(SETROUND); if (!fail || after_real) { int r=__real_fesetround(p); if (!fail) return r; } return 1; }
int __wrap_fegetround(void) { int fail=hit(GETROUND); if (!fail) return __real_fegetround(); return -1; }
int __wrap_feclearexcept(int p) { int fail=hit(CLEAREXCEPT); if (!fail || after_real) { int r=__real_feclearexcept(p); if (!fail) return r; } return 1; }
int __wrap_fesetexceptflag(const fexcept_t *p,int bits) { int fail=hit(SETEXCEPTFLAG); if (!fail || after_real) { int r=__real_fesetexceptflag(p,bits); if (!fail) return r; } return 1; }
int __wrap_feenableexcept(int p) { int fail=hit(ENABLEEXCEPT); if (!fail || after_real) { int r=__real_feenableexcept(p); if (!fail) return r; } return -1; }
int __wrap_fedisableexcept(int p) { int fail=hit(DISABLEEXCEPT); if (!fail || after_real) { int r=__real_fedisableexcept(p); if (!fail) return r; } return -1; }
static int exercise(Client *client,unsigned function,unsigned at,unsigned partial,unsigned forever,unsigned kind,unsigned *counts) {
    Env before,after; Output out; DoeCompilerResultV2 result;
    const char *source=kind==0u?numeric_source:kind==1u?rejected_numeric_source:rejected_parse_source;
    CHECK(configure(FE_DOWNWARD,0x8040u,FE_INEXACT,0)==0,"fault harness profile");
    CHECK(env_get(&before)==0,"fault snapshot");
    memset(&out,BYTE_MARK,sizeof(out)); memset(&result,BYTE_MARK,sizeof(result));
    memset(observed,0,sizeof(observed)); selected=function; ordinal=at; after_real=partial; persistent=forever; injected=0; early_output=0;
    active_output=&out; inside_library=1u;
    DoeCompilerStatus status=invoke(client,source,strlen(source),&out,WORD_CAP,&result);
    inside_library=0u; active_output=NULL;
    CHECK(env_get(&after)==0,"fault snapshot after");
    if (counts!=NULL) memcpy(counts,observed,sizeof(observed));
    CHECK(early_output==0u,"no output publication before last fallible environment action");
    CHECK(guards(client,&out),"fault redzones");
    if (function<FUNCTIONS) {
        printf("fault api=%u source=%u function=%s ordinal=%u after_real=%u persistent=%u injected=%u status=%d restored=%d diagnostic=%s\n",client->api,kind,names[function],at,partial,forever,injected,(int)status,env_equal(&before,&after),result.diagnostic.message);
        CHECK(injected>0u,"targeted environmental call reached");
        CHECK(status==DOE_COMPILER_INPUT_ERROR,"fallible host state failure is explicit INPUT_ERROR");
        CHECK(result.diagnostic.message[0]!='\0',"host failure diagnostic");
        CHECK(result.word_count==0u && all_bytes(&result.reflection,sizeof(result.reflection),0),"fault result atomicity");
        CHECK(all_bytes(out.words,sizeof(out.words),BYTE_MARK),"fault words unpublished");
        CHECK(observed[SETENV]+observed[UPDATEENV]<=8u,"bounded restoration attempts");
        /* Persistent restoring-call failures cannot establish restoration. They
         * must stay failed and unpublished; exact state is reported, not waived. */
        if (!forever) CHECK(env_equal(&before,&after),"one-shot failure restored saved caller environment");
    } else {
        DoeCompilerStatus expected=kind==0u?DOE_COMPILER_OK:kind==1u?DOE_COMPILER_SEMANTIC_ERROR:DOE_COMPILER_PARSE_ERROR;
        if (status!=expected) fprintf(stderr,"fault baseline api=%u kind=%u status=%d diagnostic=%s\n",client->api,kind,(int)status,result.diagnostic.message);
        CHECK(status==expected,"fault baseline status");
        CHECK(env_equal(&before,&after),"fault baseline environment");
    }
    CHECK(__real_fesetenv(&before.raw)==0,"harness restore after deliberate host fault"); _mm_setcsr(before.mxcsr);
    return 0;
}
int main(void) {
    (void)numeric_expected; (void)retained_source;
    unsigned tests=0,applicable=0;
    for (unsigned api=1u;api<=3u;++api) {
        Client c; CHECK(client_open(&c,api)==0,"fault client");
        for (unsigned kind=0;kind<3u;++kind) {
            unsigned baseline[FUNCTIONS];
            CHECK(exercise(&c,FUNCTIONS,0u,0u,0u,kind,baseline)==0,"fault discovery baseline");
            for (unsigned f=0;f<FUNCTIONS;++f) {
                if (baseline[f]) applicable++;
                printf("observed api=%u source=%u function=%s calls=%u\n",api,kind,names[f],baseline[f]);
                for (unsigned n=1;n<=baseline[f];++n) {
                    CHECK(exercise(&c,f,n,0u,0u,kind,NULL)==0,"one-shot no-effect fault"); tests++;
                    if (f==HOLDEXCEPT || f==SETENV || f==UPDATEENV || f==SETROUND || f==CLEAREXCEPT) {
                        CHECK(exercise(&c,f,n,1u,0u,kind,NULL)==0,"one-shot after-effect fault"); tests++;
                    }
                }
                if ((f==SETENV || f==UPDATEENV) && baseline[f]) {
                    CHECK(exercise(&c,f,1u,0u,1u,kind,NULL)==0,"persistent restoration failure"); tests++;
                }
            }
        }
        (void)configure(FE_TONEAREST,0u,0,0); free(c.workspace);
    }
    printf("{\"status\":\"PASS\",\"fault_cases\":%u,\"applicable_function_paths\":%u,\"unobserved_calls\":\"not applicable, never silently credited\"}\n",tests,applicable);
    return 0;
}
