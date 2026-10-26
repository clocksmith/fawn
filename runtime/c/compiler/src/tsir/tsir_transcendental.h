/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#ifndef DOE_TSIR_TRANSCENDENTAL_H
#define DOE_TSIR_TRANSCENDENTAL_H

/* Transcendental evaluation for the extended-op interpreter.
 * Zig's interpreter header declares exp/log/sin/cos/tan/tanh/
 * rsqrt/recip as sollya-bounded minimax polynomials with ULP
 * bounds (1,1,1,1,2,2,1,1) and states libm is NOT used, but the
 * Zig source in this tree calls std.math.exp/tanh/sqrt, whose
 * exact bit output cannot be established from this VM (no Zig
 * toolchain, no Zig binary). These helpers therefore evaluate in
 * double precision and round to f32: within about 1 ULP of the
 * true value, but NOT claimed bit-equal to Zig. Tests assert
 * exactly-representable cases bit-exactly (exp(0) = 1, tanh(0) =
 * 0) and everything else within a stated tolerance, and gaps.md
 * records bit parity for every transcendental-bearing op as
 * unestablished pending a Zig cross-check. Do not use these
 * helpers to claim a Zig-equal reference hash. */

float tsir_exp_f32(float x);
float tsir_tanh_f32(float x);
float tsir_sqrt_f32(float x);

#endif
