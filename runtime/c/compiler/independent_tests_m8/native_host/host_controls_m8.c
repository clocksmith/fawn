/* SPDX-License-Identifier: MIT
 * Candidate-independent controls for raw host state comparisons. */
#define _GNU_SOURCE
#include "host_support_m8.h"
int main(void) {
    const int modes[]={FE_TONEAREST,FE_DOWNWARD,FE_UPWARD,FE_TOWARDZERO};
    const unsigned denorms[]={0u,0x40u,0x8000u,0x8040u};
    unsigned controls=0;
    for (unsigned m=0;m<4u;++m) for (unsigned d=0;d<4u;++d) {
        Env before,after;
        CHECK(configure(modes[m],denorms[d],FE_INEXACT,FE_INVALID|FE_DIVBYZERO)==0,"configure control");
        CHECK(env_get(&before)==0 && env_get(&after)==0 && env_equal(&before,&after),"unchanged raw state control");
        CHECK(fesetround(modes[(m+1u)%4u])==0,"mutate round");
        CHECK(env_get(&after)==0 && !env_equal(&before,&after),"round mutation detected");
        CHECK(fesetenv(&before.raw)==0,"restore round"); _mm_setcsr(before.mxcsr);
        _mm_setcsr(before.mxcsr^0x8000u);
        CHECK(env_get(&after)==0 && !env_equal(&before,&after),"FTZ mutation detected");
        _mm_setcsr(before.mxcsr^0x40u);
        CHECK(env_get(&after)==0 && !env_equal(&before,&after),"DAZ mutation detected");
        _mm_setcsr(before.mxcsr);
        CHECK(feclearexcept(FE_ALL_EXCEPT)==0,"clear flags mutation");
        CHECK(env_get(&after)==0 && !env_equal(&before,&after),"flag mutation detected");
        CHECK(fesetenv(&before.raw)==0,"restore flags"); _mm_setcsr(before.mxcsr);
        CHECK(fedisableexcept(FE_ALL_EXCEPT)>=0,"disable traps mutation");
        CHECK(env_get(&after)==0 && !env_equal(&before,&after),"trap mutation detected");
        CHECK(fesetenv(&before.raw)==0,"restore traps"); _mm_setcsr(before.mxcsr);
        fenv_t held;
        CHECK(feholdexcept(&held)==0,"control hold");
        volatile double one=1.0,ten=10.0,quotient=one/ten;
        (void)quotient;
        CHECK(fesetenv(&held)==0,"control restore");
        CHECK(env_get(&after)==0 && env_equal(&before,&after),"hold/evaluate/restore raw state control");
        controls+=7u;
    }
    CHECK(configure(FE_TONEAREST,0u,0,0)==0,"reset control harness");
    printf("PASS %u candidate-independent environment comparator controls\n",controls);
    return 0;
}
