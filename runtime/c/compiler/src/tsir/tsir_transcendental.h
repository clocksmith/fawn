/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#ifndef DOE_TSIR_TRANSCENDENTAL_H
#define DOE_TSIR_TRANSCENDENTAL_H

/* Pinned f32 compatibility with the retained Zig 0.15.2 interpreter:
 * exp and tanh preserve its actual operation ordering and coefficients rather
 * than assuming host double/float libm returns identical bits. The source file
 * retains upstream attribution. sqrt retains its independently checked path.
 * Compile without fast math or contraction. Runtime-bit differential coverage
 * lives in tests/test_numeric_parity.py; it is not universal GPU qualification.
 * Darwin exp uses the same native expf entrypoint as retained Zig @exp;
 * its target-specific rounding is not the portable compiler_rt algorithm.
 * No function changes the caller's floating-point rounding mode. */

float tsir_exp_f32(float x);
float tsir_tanh_f32(float x);
float tsir_sqrt_f32(float x);

#endif
