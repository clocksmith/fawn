/* SPDX-License-Identifier: MIT
 * Linux x86-64 host proof; no claim for other architectures or physical GPU. */
#define _GNU_SOURCE
#include "host_support_m9.h"
#include "fixtures_m9.h"
#include <pthread.h>
#include <stdatomic.h>
static _Thread_local unsigned inside_library;
static atomic_uint allocator_calls;
#ifdef WRAP_ALLOCATORS
void *__real_malloc(size_t n); void *__real_calloc(size_t n,size_t z);
void *__real_realloc(void *p,size_t n); void __real_free(void *p);
void *__real_aligned_alloc(size_t a,size_t n); int __real_posix_memalign(void **p,size_t a,size_t n);
void *__wrap_malloc(size_t n); void *__wrap_calloc(size_t n,size_t z);
void *__wrap_realloc(void *p,size_t n); void __wrap_free(void *p);
void *__wrap_aligned_alloc(size_t a,size_t n); int __wrap_posix_memalign(void **p,size_t a,size_t n);
static void allocation(void) { if (inside_library) (void)atomic_fetch_add(&allocator_calls,1u); }
void *__wrap_malloc(size_t n) { allocation(); return __real_malloc(n); }
void *__wrap_calloc(size_t n,size_t z) { allocation(); return __real_calloc(n,z); }
void *__wrap_realloc(void *p,size_t n) { allocation(); return __real_realloc(p,n); }
void __wrap_free(void *p) { allocation(); __real_free(p); }
void *__wrap_aligned_alloc(size_t a,size_t n) { allocation(); return __real_aligned_alloc(a,n); }
int __wrap_posix_memalign(void **p,size_t a,size_t n) { allocation(); return __real_posix_memalign(p,a,n); }
#endif
static uint32_t reference_words[WORD_CAP];
static size_t reference_count;
static atomic_uint completed;
static const int rounding_modes[]={FE_TONEAREST,FE_DOWNWARD,FE_UPWARD,FE_TOWARDZERO};
static const unsigned denorm_modes[]={0u,0x40u,0x8000u,0x8040u};
static const int trap_modes[]={0,FE_INVALID,FE_DIVBYZERO,FE_OVERFLOW,FE_UNDERFLOW,FE_INEXACT,FE_ALL_EXCEPT};
static int check_constants(const Output *o,size_t count) {
    for (size_t expected=0;expected<8u;++expected) {
        int found=0;
        for (size_t at=5;at<count;) {
            unsigned width=o->words[at]>>16u, op=o->words[at]&0xffffu;
            if (width==0u || width>count-at) return 1;
            if (op==43u && width==4u && o->words[at+3u]==numeric_expected[expected]) found=1;
            at+=width;
        }
        if (!found) { fprintf(stderr,"missing expected constant %08x\n",numeric_expected[expected]); return 1; }
    }
    return 0;
}
static int one_call(Client *c,unsigned kind) {
    Output out; DoeCompilerResultV2 result; Env before,after;
    memset(&out,BYTE_MARK,sizeof(out)); memset(&result,BYTE_MARK,sizeof(result));
    const char *source=kind==1u?rejected_numeric_source:kind==2u?rejected_parse_source:numeric_source;
    const DoeCompilerStatus expected=kind==0u?DOE_COMPILER_OK:kind==1u?DOE_COMPILER_SEMANTIC_ERROR:kind==2u?DOE_COMPILER_PARSE_ERROR:DOE_COMPILER_OUTPUT_TOO_SMALL;
    CHECK(env_get(&before)==0,"snapshot before");
    errno=12345; inside_library=1u;
    DoeCompilerStatus status=invoke(c,source,strlen(source),&out,kind==3u?1u:WORD_CAP,&result);
    int returned_errno=errno; inside_library=0u;
    CHECK(returned_errno==12345,"caller errno preserved");
    CHECK(env_get(&after)==0,"snapshot after");
    if (status!=expected) fprintf(stderr,"api=%u kind=%u status=%d expected=%d diagnostic=%s\n",c->api,kind,(int)status,(int)expected,result.diagnostic.message);
    CHECK(status==expected,"frozen status");
    if (!env_equal(&before,&after)) fprintf(stderr,"env api=%u kind=%u round=%x/%x flags=%x/%x traps=%x/%x mxcsr=%x/%x raw=%d\n",c->api,kind,before.rounding,after.rounding,before.flags,after.flags,before.traps,after.traps,before.mxcsr,after.mxcsr,memcmp(&before.raw,&after.raw,sizeof(fenv_t)));
    CHECK(env_equal(&before,&after),"complete caller floating environment preserved");
    CHECK(guards(c,&out),"workspace/output redzones");
    if (status==DOE_COMPILER_OK) {
        CHECK(result.word_count==reference_count,"same word count across environment/API");
        CHECK(memcmp(out.words,reference_words,reference_count*sizeof(uint32_t))==0,"same exact words across environment/API");
        CHECK(all_bytes(out.words+result.word_count,sizeof(out.words)-result.word_count*sizeof(uint32_t),BYTE_MARK),"success word tail");
    } else {
        CHECK(result.word_count==0u && all_bytes(&result.reflection,sizeof(result.reflection),0),"failure result atomicity");
        CHECK(all_bytes(out.words,sizeof(out.words),BYTE_MARK),"failure output atomicity");
    }
    (void)atomic_fetch_add(&completed,1u);
    return 0;
}
static int matrix(void) {
    for (unsigned api=1u;api<=3u;++api) {
        Client c; CHECK(client_open(&c,api)==0,"client allocation/query");
        for (unsigned round=0;round<4u;++round) for (unsigned denorm=0;denorm<4u;++denorm) {
            for (unsigned trap=0;trap<7u;++trap) for (unsigned kind=0;kind<4u;++kind) {
                CHECK(configure(rounding_modes[round],denorm_modes[denorm],0,trap_modes[trap])==0,"trap profile");
                CHECK(one_call(&c,kind)==0,"trap matrix");
            }
            for (unsigned kind=0;kind<4u;++kind) {
                CHECK(configure(rounding_modes[round],denorm_modes[denorm],FE_ALL_EXCEPT,0)==0,"raised flags profile");
                CHECK(one_call(&c,kind)==0,"flags matrix");
                CHECK(configure(rounding_modes[round],denorm_modes[denorm],FE_INEXACT,FE_INVALID|FE_DIVBYZERO)==0,"flags plus disjoint traps profile");
                CHECK(one_call(&c,kind)==0,"mixed flags/traps matrix");
            }
        }
        CHECK(configure(FE_TONEAREST,0u,0,0)==0,"reset matrix harness environment");
        free(c.workspace);
    }
    return 0;
}
typedef struct { unsigned index; int status; } Thread;
static void *thread_work(void *value) {
    Thread *thread=value; Client c;
    thread->status=client_open(&c,3u); if (thread->status) return NULL;
    unsigned i=thread->index;
    if (configure(rounding_modes[i%4u],denorm_modes[(i/4u)%4u],FE_INEXACT,FE_INVALID|FE_DIVBYZERO)) thread->status=1;
    for (unsigned iteration=0;iteration<40u && !thread->status;++iteration) thread->status=one_call(&c,iteration%4u);
    (void)configure(FE_TONEAREST,0u,0,0); free(c.workspace); return NULL;
}
int main(void) {
    (void)retained_source;
    Env original; CHECK(env_get(&original)==0,"save harness caller");
    CHECK(configure(FE_TONEAREST,0u,0,0)==0,"baseline environment");
    Client baseline; CHECK(client_open(&baseline,3u)==0,"baseline client");
    Output out; DoeCompilerResultV2 result; memset(&out,BYTE_MARK,sizeof(out));
    inside_library=1u; DoeCompilerStatus status=invoke(&baseline,numeric_source,strlen(numeric_source),&out,WORD_CAP,&result); inside_library=0u;
    if (status) fprintf(stderr,"baseline status=%d diagnostic=%s\n",(int)status,result.diagnostic.message);
    CHECK(status==DOE_COMPILER_OK,"independent positive baseline");
    CHECK(result.word_count>5u && result.word_count<=WORD_CAP,"valid module word count");
    CHECK(check_constants(&out,result.word_count)==0,"independently known constant payloads");
    reference_count=result.word_count; memcpy(reference_words,out.words,reference_count*sizeof(uint32_t)); free(baseline.workspace);
    CHECK(matrix()==0,"host profile matrix");
    pthread_t ids[16]; Thread threads[16];
    for (unsigned i=0;i<16u;++i) { threads[i].index=i; threads[i].status=0; CHECK(pthread_create(&ids[i],NULL,thread_work,&threads[i])==0,"thread create"); }
    for (unsigned i=0;i<16u;++i) { CHECK(pthread_join(ids[i],NULL)==0,"thread join"); CHECK(threads[i].status==0,"distinct-thread environment isolation"); }
    CHECK(atomic_load(&allocator_calls)==0u,"library must not allocate or free");
    CHECK(fesetenv(&original.raw)==0,"restore harness caller"); _mm_setcsr(original.mxcsr);
    printf("{\"status\":\"PASS\",\"host_profiles\":432,\"calls\":%u,\"threads\":16,\"allocator_calls\":%u,\"words\":%zu}\n",atomic_load(&completed),atomic_load(&allocator_calls),reference_count);
    return 0;
}
