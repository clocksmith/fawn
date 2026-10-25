/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "numeric.h"
#include "numeric_host.h"
#include <string.h>

#if defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__))
#include <xmmintrin.h>
static uint16_t x87_control(void) {
    uint16_t control;
    __asm__ volatile("fnstcw %0" : "=m"(control));
    return control;
}
static void x87_set_control(uint16_t control) {
    __asm__ volatile("fldcw %0" : : "m"(control) : "memory");
}
static bool machine_save(NumericEnvironment *environment) {
    environment->saved_mxcsr = _mm_getcsr();
    environment->saved_x87_control = x87_control();
    return true;
}
static bool machine_normalize(void) {
    /* x87 is used by some libm implementations even when C expressions use
     * SSE. Normalize its precision as well as traps and rounding, not merely
     * MXCSR; an incoming 24-bit x87 mode must not narrow AbstractFloat math. */
    x87_set_control((uint16_t)((x87_control() | UINT16_C(0x033f)) & UINT16_C(0xf3ff)));
    _mm_setcsr((_mm_getcsr() | UINT32_C(0x1f80)) & ~UINT32_C(0xe07f));
    return (x87_control() & UINT16_C(0x0f3f)) == UINT16_C(0x033f) &&
        (_mm_getcsr() & UINT32_C(0xffff)) == UINT32_C(0x1f80);
}
static bool machine_restore(const NumericEnvironment *environment) {
    x87_set_control(environment->saved_x87_control);
    _mm_setcsr(environment->saved_mxcsr);
    return x87_control() == environment->saved_x87_control && _mm_getcsr() == environment->saved_mxcsr;
}
#elif (defined(__aarch64__) || defined(__arm64__)) && (defined(__GNUC__) || defined(__clang__))
static uint64_t arm_control(void) {
    uint64_t value;
    __asm__ volatile("mrs %0, fpcr" : "=r"(value));
    return value;
}
static uint64_t arm_status(void) {
    uint64_t value;
    __asm__ volatile("mrs %0, fpsr" : "=r"(value));
    return value;
}
static void arm_set_control(uint64_t value) {
    __asm__ volatile("msr fpcr, %0\n\tisb" : : "r"(value) : "memory");
}
static void arm_set_status(uint64_t value) {
    __asm__ volatile("msr fpsr, %0" : : "r"(value) : "memory");
}
static bool machine_save(NumericEnvironment *environment) {
    environment->saved_fpcr = arm_control();
    environment->saved_fpsr = arm_status();
    return true;
}
static bool machine_normalize(void) {
    /* FPCR: rounding[23:22], FZ24, DN25, AHP26, FZ16[19], exception enables
     * [12:8]/15, and FEAT_AFP AH1/FIZ0. Other caller control bits are preserved.
     * No fenv_t representation is guessed, including Apple's distinct layout. */
    const uint64_t controlled = UINT64_C(0x07c89f03);
    arm_set_control(arm_control() & ~controlled);
    arm_set_status(0u);
    return (arm_control() & controlled) == 0u && arm_status() == 0u;
}
static bool machine_restore(const NumericEnvironment *environment) {
    arm_set_control(environment->saved_fpcr);
    arm_set_status(environment->saved_fpsr);
    return arm_control() == environment->saved_fpcr && arm_status() == environment->saved_fpsr;
}
#elif defined(__wasm__)
/* WebAssembly fixes rounding to nearest/ties-even, preserves subnormals and
 * has no floating-point exception flags or configurable control registers.
 * The arithmetic probes below still run; WASI libc's fenv is the fixed owner. */
static bool machine_save(NumericEnvironment *environment) { (void)environment; return true; }
static bool machine_normalize(void) { return true; }
static bool machine_restore(const NumericEnvironment *environment) { (void)environment; return true; }
#else
static bool machine_save(NumericEnvironment *environment) { (void)environment; return false; }
static bool machine_normalize(void) { return false; }
static bool machine_restore(const NumericEnvironment *environment) { (void)environment; return false; }
#endif

static bool host_error(Source *source, const char *message) {
    return source_error(source, DOE_COMPILER_INPUT_ERROR, 0u, message);
}

/* Volatile operands prevent this admission check from being folded into an
 * assertion about the compiler's default environment. No persistent host cache
 * or process-global mode is introduced. Probe flags are cleared before sema. */
static bool host_arithmetic(void) {
    volatile float one = 1.0f, half_ulp = 0x1p-24f, normal = 0x1p-126f;
    volatile double wide_one = 1.0, wide_half_ulp = 0x1p-53, wide_normal = 0x1p-1022;
    float rounded = one + half_ulp;
    double wide_rounded = wide_one + wide_half_ulp;
    uint32_t narrow_bits;
    uint64_t wide_bits;
    memcpy(&narrow_bits, &rounded, sizeof(narrow_bits));
    memcpy(&wide_bits, &wide_rounded, sizeof(wide_bits));
    if (narrow_bits != UINT32_C(0x3f800000) || wide_bits != UINT64_C(0x3ff0000000000000)) return false;
    volatile float subnormal = normal * 0.5f;
    volatile double wide_subnormal = wide_normal * 0.5;
    rounded = subnormal * one;
    wide_rounded = wide_subnormal * wide_one;
    memcpy(&narrow_bits, &rounded, sizeof(narrow_bits));
    memcpy(&wide_bits, &wide_rounded, sizeof(wide_bits));
    return narrow_bits == UINT32_C(0x00400000) && wide_bits == UINT64_C(0x0008000000000000);
}

bool numeric_environment_begin(Source *source, NumericEnvironment *environment) {
    environment->active = false;
    if (!machine_save(environment))
        return host_error(source, "compiler host has no floating-point control owner");
    if (fegetenv(&environment->saved) != 0)
        return host_error(source, "cannot save compiler host floating-point environment");
    environment->active = true;
    fenv_t held;
    if (fegetround() == -1 || feholdexcept(&held) != 0 || fesetround(FE_TONEAREST) != 0 ||
        !machine_normalize()) {
        if (!numeric_environment_end(source, environment)) return false;
        return host_error(source, "cannot normalize compiler host floating-point environment");
    }
    const bool arithmetic = host_arithmetic();
    if (!arithmetic || feclearexcept(FE_ALL_EXCEPT) != 0 || !machine_normalize() ||
        fegetround() != FE_TONEAREST || fetestexcept(FE_ALL_EXCEPT) != 0) {
        if (!numeric_environment_end(source, environment)) return false;
        return host_error(source, "cannot verify compiler host floating-point environment");
    }
    return true;
}

bool numeric_environment_end(Source *source, NumericEnvironment *environment) {
    if (!environment->active) return true;
    const int restored = fesetenv(&environment->saved);
    const bool machine_restored = machine_restore(environment);
    environment->active = false;
    if (restored != 0 || !machine_restored) {
        /* Preserve the first restoration failure even when this bounded retry
         * recovers the caller state. Target publication must still fail. */
        (void)fesetenv(&environment->saved);
        (void)machine_restore(environment);
        return host_error(source, "compiler host floating-point environment restoration failed");
    }
    return true;
}
