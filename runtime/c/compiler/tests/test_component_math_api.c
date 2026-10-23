/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "../include/doe_compiler.h"
#include <assert.h>
#include <errno.h>
#include <fenv.h>
#include <stdio.h>
#include <string.h>
#include <xmmintrin.h>

int main(void) {
    static const char source[] =
        "@group(0) @binding(0) var<storage,read_write> out:array<f32>;"
        "const a=round(2.5);const b=fract(-0x1p-149f);"
        "const c=sin(0.0)+log2(8.0)+asinh(0.0);"
        "@compute @workgroup_size(1) fn main(){"
        "out[0]=a;out[1]=b;out[2]=c;let x=out[3];out[4]=round(x)+fract(x)+sin(x);}"
        ;
    static const char bad[] = "const invalid=log(-1.0);@compute @workgroup_size(1) fn main(){}";
    uint32_t baseline[4096], words[4096], untouched[4096];
    DoeCompilerResult result;
    fenv_t saved;
    assert(fegetenv(&saved) == 0);
    const unsigned saved_mxcsr = _mm_getcsr();
    assert(doe_compiler_compile(1u, source, strlen(source), baseline, 4096u,
                                &result, sizeof(result)) == DOE_COMPILER_OK);
    const size_t count = result.word_count;
    const int modes[] = {FE_TONEAREST, FE_UPWARD, FE_DOWNWARD, FE_TOWARDZERO};
    for (size_t i = 0u; i < sizeof(modes) / sizeof(*modes); ++i) {
        assert(fesetround(modes[i]) == 0 && feclearexcept(FE_ALL_EXCEPT) == 0);
        assert(feraiseexcept(FE_INVALID | FE_INEXACT) == 0);
        _mm_setcsr(_mm_getcsr() | 0x8040u); /* Caller's FTZ and DAZ are not ours. */
        const unsigned mxcsr = _mm_getcsr();
        const int exceptions = fetestexcept(FE_ALL_EXCEPT);
        errno = EBUSY;
        assert(doe_compiler_compile(1u, source, strlen(source), words, 4096u,
                                    &result, sizeof(result)) == DOE_COMPILER_OK);
        assert(result.word_count == count && memcmp(words, baseline, count * 4u) == 0);
        assert(errno == EBUSY && fegetround() == modes[i]);
        assert(fetestexcept(FE_ALL_EXCEPT) == exceptions && _mm_getcsr() == mxcsr);
        memset(words, 0xa5, sizeof(words));
        memcpy(untouched, words, sizeof(words));
        assert(doe_compiler_compile(1u, bad, strlen(bad), words, 4096u,
                                    &result, sizeof(result)) == DOE_COMPILER_SEMANTIC_ERROR);
        assert(result.word_count == 0u && memcmp(words, untouched, sizeof(words)) == 0);
        assert(errno == EBUSY && fegetround() == modes[i]);
        assert(fetestexcept(FE_ALL_EXCEPT) == exceptions && _mm_getcsr() == mxcsr);
        assert(doe_compiler_compile(1u, source, strlen(source), words, count - 1u,
                                    &result, sizeof(result)) == DOE_COMPILER_OUTPUT_TOO_SMALL);
        assert(result.word_count == 0u && memcmp(words, untouched, sizeof(words)) == 0);
        assert(errno == EBUSY && fegetround() == modes[i]);
        assert(fetestexcept(FE_ALL_EXCEPT) == exceptions && _mm_getcsr() == mxcsr);
    }
    assert(fesetenv(&saved) == 0);
    _mm_setcsr(saved_mxcsr);
    puts("Component math API: identical words across four rounding modes, caller FP/errno restoration and atomic failure passed.");
    return 0;
}
