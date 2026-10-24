/* SPDX-License-Identifier: MIT */
#include "../include/doe_compiler.h"
#include <assert.h>
#include <errno.h>
#include <fenv.h>
#include <process.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>
#include <xmmintrin.h>

#if !defined(_WIN32) || !defined(__clang__) || !defined(__x86_64__)
#error "This control requires actual x64 Windows clang-cl"
#endif

/* The real DLL performs normalization. This client only changes and observes
 * caller state, and compares complete emitted words against its nearest-mode run.
 * It neither emulates WGSL nor reaches into the DLL's private numeric owner. */
static const char good[] =
    "@group(0) @binding(0) var<storage,read_write> out:array<u32>;"
    "const exact=16777217.0-16777216.0;"
    "const tiny=0x1p-149f;"
    "@compute @workgroup_size(1) fn main(){"
    "out[0]=u32(exact);out[1]=bitcast<u32>(tiny);}";
static const char bad[] =
    "const invalid=sqrt(-1.0);@compute @workgroup_size(1) fn main(){}";

static uint16_t control(void)
{
    uint16_t result;
    __asm__ volatile("fnstcw %0" : "=m"(result));
    return result;
}

static void set_control(uint16_t value)
{
    __asm__ volatile("fldcw %0" : : "m"(value) : "memory");
}

static unsigned __stdcall exercise(void *unused)
{
    (void)unused;
    fenv_t saved;
    assert(fegetenv(&saved) == 0);
    const uint32_t original_mxcsr = _mm_getcsr();
    const uint16_t original_control = control();
    assert(fesetround(FE_TONEAREST) == 0);
    assert(feclearexcept(FE_ALL_EXCEPT) == 0);
    uint32_t baseline[DOE_COMPILER_MAX_WORDS];
    DoeCompilerResult expected;
    assert(doe_compiler_compile(1u, good, sizeof(good) - 1u, baseline,
        DOE_COMPILER_MAX_WORDS, &expected, sizeof(expected)) == DOE_COMPILER_OK);
    assert(expected.word_count > 5u);
    const int modes[] = {FE_TONEAREST, FE_UPWARD, FE_DOWNWARD, FE_TOWARDZERO};
    for (size_t mode = 0u; mode < sizeof(modes) / sizeof(*modes); ++mode) {
        assert(fesetround(modes[mode]) == 0);
        assert(feclearexcept(FE_ALL_EXCEPT) == 0);
        assert(feraiseexcept(FE_INEXACT | FE_UNDERFLOW) == 0);
        _mm_setcsr(_mm_getcsr() | UINT32_C(0x8042));
        set_control((uint16_t)(control() & UINT16_C(0xfcff)));
        const uint32_t caller_mxcsr = _mm_getcsr();
        const uint16_t caller_control = control();
        const int caller_flags = fetestexcept(FE_ALL_EXCEPT);
        for (unsigned repeat = 0u; repeat < 8u; ++repeat) {
            uint32_t words[DOE_COMPILER_MAX_WORDS];
            DoeCompilerResult result;
            errno = EBUSY;
            assert(doe_compiler_compile(1u, good, sizeof(good) - 1u, words,
                DOE_COMPILER_MAX_WORDS, &result, sizeof(result)) == DOE_COMPILER_OK);
            assert(errno == EBUSY && result.word_count == expected.word_count);
            assert(memcmp(words, baseline, expected.word_count * sizeof(*words)) == 0);
            assert(_mm_getcsr() == caller_mxcsr && control() == caller_control);
            assert(fegetround() == modes[mode] && fetestexcept(FE_ALL_EXCEPT) == caller_flags);
            memset(words, 0xa5, sizeof(words));
            assert(doe_compiler_compile(1u, bad, sizeof(bad) - 1u, words,
                DOE_COMPILER_MAX_WORDS, &result, sizeof(result)) == DOE_COMPILER_SEMANTIC_ERROR);
            assert(errno == EBUSY && result.word_count == 0u && words[0] == UINT32_C(0xa5a5a5a5));
            assert(_mm_getcsr() == caller_mxcsr && control() == caller_control);
            assert(fegetround() == modes[mode] && fetestexcept(FE_ALL_EXCEPT) == caller_flags);
        }
    }
    assert(fesetenv(&saved) == 0);
    set_control(original_control);
    _mm_setcsr(original_mxcsr);
    return 0u;
}

int main(void)
{
    HANDLE threads[2];
    for (size_t i = 0u; i < 2u; ++i) {
        const uintptr_t thread = _beginthreadex(NULL, 0u, exercise, NULL, 0u, NULL);
        assert(thread != 0u);
        threads[i] = (HANDLE)thread;
    }
    assert(WaitForMultipleObjects(2u, threads, TRUE, 30000u) == WAIT_OBJECT_0);
    for (size_t i = 0u; i < 2u; ++i) {
        DWORD status;
        assert(GetExitCodeThread(threads[i], &status) && status == 0u);
        assert(CloseHandle(threads[i]));
    }
    puts("Windows DLL: concurrent exact-word emission, failure nonpublication, errno, rounding, x87 and MXCSR restoration passed");
    return 0;
}
