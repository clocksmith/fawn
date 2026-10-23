/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors
 * Real libc failure boundaries, no production mutation hooks. */
#include "../src/numeric.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#if defined(__x86_64__)
#include <xmmintrin.h>
#endif

typedef enum { FAIL_NONE, FAIL_SAVE, FAIL_ROUND_READ, FAIL_HOLD, FAIL_ROUND_WRITE,
               FAIL_CLEAR, FAIL_FLAGS, FAIL_RESTORE } Failure;
static Failure failure;
static unsigned restoration_calls;
int __real_fegetenv(fenv_t *);
int __real_fegetround(void);
int __real_feholdexcept(fenv_t *);
int __real_fesetround(int);
int __real_feclearexcept(int);
int __real_fetestexcept(int);
int __real_fesetenv(const fenv_t *);
int __wrap_fegetenv(fenv_t *);
int __wrap_fegetround(void);
int __wrap_feholdexcept(fenv_t *);
int __wrap_fesetround(int);
int __wrap_feclearexcept(int);
int __wrap_fetestexcept(int);
int __wrap_fesetenv(const fenv_t *);
static bool fail(Failure which) {
    if (failure != which) return false;
    failure = FAIL_NONE;
    return true;
}
int __wrap_fegetenv(fenv_t *environment) { return fail(FAIL_SAVE) ? 1 : __real_fegetenv(environment); }
int __wrap_fegetround(void) { return fail(FAIL_ROUND_READ) ? -1 : __real_fegetround(); }
int __wrap_feholdexcept(fenv_t *environment) { return fail(FAIL_HOLD) ? 1 : __real_feholdexcept(environment); }
int __wrap_fesetround(int mode) { return fail(FAIL_ROUND_WRITE) ? 1 : __real_fesetround(mode); }
int __wrap_feclearexcept(int flags) { return fail(FAIL_CLEAR) ? 1 : __real_feclearexcept(flags); }
int __wrap_fetestexcept(int flags) { return fail(FAIL_FLAGS) ? FE_INVALID : __real_fetestexcept(flags); }
int __wrap_fesetenv(const fenv_t *environment) {
    ++restoration_calls;
    return fail(FAIL_RESTORE) ? 1 : __real_fesetenv(environment);
}
bool source_error(Source *source, DoeCompilerStatus status, size_t offset, const char *message) {
    source->diagnostic->status = status;
    source->diagnostic->offset = offset;
    snprintf(source->diagnostic->message, sizeof(source->diagnostic->message), "%s", message);
    return false;
}
#if defined(__x86_64__)
static uint16_t control(void) {
    uint16_t result;
    __asm__ volatile("fnstcw %0" : "=m"(result));
    return result;
}
static void set_control(uint16_t value) {
    __asm__ volatile("fldcw %0" : : "m"(value) : "memory");
}
#endif
int main(void) {
    fenv_t original;
    assert(fegetenv(&original) == 0);
#if defined(__x86_64__)
    const unsigned original_mxcsr = _mm_getcsr();
    const uint16_t original_control = control();
#endif
    assert(fesetround(FE_UPWARD) == 0 && feclearexcept(FE_ALL_EXCEPT) == 0);
    assert(feraiseexcept(FE_INEXACT | FE_UNDERFLOW) == 0);
#if defined(__x86_64__)
    _mm_setcsr(_mm_getcsr() | 0x8042u);
    set_control((uint16_t)(control() & UINT16_C(0xfcff))); /* caller's 24-bit x87 precision */
    const unsigned expected_mxcsr = _mm_getcsr();
    const uint16_t expected_control = control();
#endif
    const int expected_flags = fetestexcept(FE_ALL_EXCEPT);
    DoeCompilerDiagnostic diagnostic;
    Source source = {.diagnostic = &diagnostic};
    NumericEnvironment environment;
    for (unsigned which = FAIL_NONE; which <= FAIL_RESTORE; ++which) {
        diagnostic = (DoeCompilerDiagnostic){0};
        restoration_calls = 0u;
        failure = (Failure)which;
        const bool began = numeric_environment_begin(&source, &environment);
        if (which == FAIL_NONE || which == FAIL_RESTORE) {
            assert(began && environment.active && fegetround() == FE_TONEAREST);
            assert(fetestexcept(FE_ALL_EXCEPT) == 0);
#if defined(__x86_64__)
            assert((control() & 0x0f3fu) == 0x033fu);
            assert((_mm_getcsr() & 0xffffu) == 0x1f80u);
#endif
            assert(numeric_environment_end(&source, &environment) == (which == FAIL_NONE));
            assert(restoration_calls == (which == FAIL_NONE ? 1u : 2u));
        } else {
            assert(!began && failure == FAIL_NONE);
            assert(restoration_calls == (which == FAIL_SAVE ? 0u : 1u));
        }
        assert(!environment.active);
        assert(numeric_environment_end(&source, &environment));
        if (which != FAIL_NONE) assert(diagnostic.status == DOE_COMPILER_INPUT_ERROR);
        assert(fegetround() == FE_UPWARD && fetestexcept(FE_ALL_EXCEPT) == expected_flags);
#if defined(__x86_64__)
        assert(_mm_getcsr() == expected_mxcsr && control() == expected_control);
#endif
    }
    assert(fesetenv(&original) == 0);
#if defined(__x86_64__)
    _mm_setcsr(original_mxcsr);
    set_control(original_control);
#endif
    puts("Numeric environment: native precision/modes restored across save, hold, rounding, flags and final-restore failures.");
    return 0;
}
