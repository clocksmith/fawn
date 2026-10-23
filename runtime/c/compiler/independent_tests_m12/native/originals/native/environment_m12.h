/* SPDX-License-Identifier: MIT
 * Independent M12 caller-state probe. Include after defining _GNU_SOURCE. */
#ifndef INDEPENDENT_ENVIRONMENT_M12_H
#define INDEPENDENT_ENVIRONMENT_M12_H
#include "support_m11.h"
#include <xmmintrin.h>

typedef struct {
    fenv_t raw;
    int rounding;
    int flags;
    int traps;
    unsigned mxcsr;
} NativeEnvironment;

static int environment_snapshot(NativeEnvironment *state)
{
    memset(state, 0, sizeof(*state));
    if (fegetenv(&state->raw) != 0) return 1;
    state->rounding = fegetround();
    state->flags = fetestexcept(FE_ALL_EXCEPT);
    state->traps = fegetexcept();
    state->mxcsr = _mm_getcsr();
    return state->rounding < 0 || state->traps < 0;
}

static int environment_equal(const NativeEnvironment *a,
                             const NativeEnvironment *b)
{
    return a->rounding == b->rounding && a->flags == b->flags &&
        a->traps == b->traps && a->mxcsr == b->mxcsr &&
        memcmp(&a->raw, &b->raw, sizeof(fenv_t)) == 0;
}

static int environment_configure(unsigned profile)
{
    const int rounds[] = {
        FE_TONEAREST, FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO
    };
    if (fedisableexcept(FE_ALL_EXCEPT) < 0) return 1;
    if (feclearexcept(FE_ALL_EXCEPT) != 0) return 1;
    if (fesetround(rounds[profile % 4u]) != 0) return 1;
    if ((profile & 4u) && feraiseexcept(FE_INEXACT) != 0) return 1;
    _mm_setcsr((_mm_getcsr() & ~0x8040u) |
               ((profile & 8u) ? 0x8040u : 0u));
    if ((profile & 16u) &&
        feenableexcept(FE_INVALID | FE_DIVBYZERO) < 0) return 1;
    return 0;
}

static int load_source(const char *path, char **source, size_t *length)
{
    FILE *file = fopen(path, "rb");
    CHECK(file != NULL, "source file open");
    CHECK(fseek(file, 0, SEEK_END) == 0, "source seek");
    long size = ftell(file);
    CHECK(size > 0 && size <= 65536, "frozen source length bound");
    CHECK(fseek(file, 0, SEEK_SET) == 0, "source rewind");
    *length = (size_t)size;
    *source = malloc(*length + 1u);
    CHECK(*source != NULL, "source allocation belongs to harness");
    CHECK(fread(*source, 1u, *length, file) == *length, "source full read");
    CHECK(fclose(file) == 0, "source close");
    (*source)[*length] = '\0';
    return 0;
}
#endif
