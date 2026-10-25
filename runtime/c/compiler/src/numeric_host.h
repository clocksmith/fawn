/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#ifndef DOE_COMPILER_NUMERIC_HOST_H
#define DOE_COMPILER_NUMERIC_HOST_H
#include <float.h>
#include <limits.h>
#include <stdint.h>

/* These are arithmetic requirements, not compiler-brand/version admission.
 * Each new host still needs its own numerical and environment qualification. */
#if defined(__FAST_MATH__) || (defined(__FINITE_MATH_ONLY__) && __FINITE_MATH_ONLY__ != 0)
#error "The numeric owner requires strict, finite-aware floating point evaluation"
#endif
_Static_assert(CHAR_BIT == 8 && sizeof(float) == 4u && sizeof(double) == 8u,
               "numeric payload widths must match IEEE binary32/binary64");
_Static_assert(FLT_RADIX == 2 && FLT_MANT_DIG == 24 && DBL_MANT_DIG == 53 &&
               FLT_MIN_EXP == -125 && FLT_MAX_EXP == 128 &&
               DBL_MIN_EXP == -1021 && DBL_MAX_EXP == 1024,
               "numeric evaluation requires IEEE binary32/binary64");
_Static_assert(FLT_EVAL_METHOD == 0 && FLT_HAS_SUBNORM == 1 && DBL_HAS_SUBNORM == 1,
               "numeric evaluation requires declared widths and gradual underflow");
_Static_assert(INT64_MIN == -INT64_MAX - 1 && INT32_MIN == -INT32_MAX - 1,
               "numeric integer representation must be two's complement");

/* GCC uses the canonical strict build flags. Clang additionally supports the
 * standard pragma, including exceptions and calls across environment changes. */
#if defined(__clang__) && !defined(__wasm__)
#pragma STDC FENV_ACCESS ON
#pragma STDC FP_CONTRACT OFF
#endif
#endif
