/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors
 * Copyright (c) Zig contributors
 * Copyright (c) 2005-2020 Rich Felker, et al.
 *
 * Source-faithful f32 paths from Zig 0.15.2 lib/compiler_rt/exp.zig and
 * lib/std/math/{expm1,tanh}.zig, themselves derived from musl. This is
 * compatibility with the retained reference, not a new approximation policy.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 * The underlying fdlibm float conversion is by Ian Lance Taylor, Cygnus
 * Support. Copyright (C) 1993 by Sun Microsystems, Inc. All rights reserved.
 * Developed at SunPro, a Sun Microsystems, Inc. business. Permission to use,
 * copy, modify, and distribute this software is freely granted, provided that
 * this notice is preserved.
 */
#include "tsir_transcendental.h"
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

static uint32_t float_bits(float value) {
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}
static float from_bits(uint32_t bits) {
    float value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

float tsir_exp_f32(float x) {
#if defined(__APPLE__)
    /* Retained Zig @exp links Darwin expf on this target. Its finite rounding
     * and signaling-NaN quieting differ from compiler_rt's portable expf. */
    return expf(x);
#else
    const uint32_t bits = float_bits(x);
    const uint32_t magnitude = bits & UINT32_C(0x7fffffff);
    const int sign = (int)(bits >> 31);
    if (magnitude > UINT32_C(0x7f800000)) return x;
    if (magnitude >= UINT32_C(0x42aeac50)) {
        if (magnitude >= UINT32_C(0x42b17218) && sign == 0)
            return x * 0x1p127f;
        if (sign != 0 && magnitude >= UINT32_C(0x42cff1b5)) return 0.0f;
    }
    int k;
    float hi, lo;
    if (magnitude > UINT32_C(0x3eb17218)) {
        k = magnitude > UINT32_C(0x3f851592)
            ? (int)(1.4426950216e+0f * x + (sign ? -0.5f : 0.5f))
            : 1 - sign - sign;
        const float fk = (float)k;
        hi = x - fk * 6.9314575195e-1f;
        lo = fk * 1.4286067653e-6f;
        x = hi - lo;
    } else if (magnitude > UINT32_C(0x39000000)) {
        k = 0;
        hi = x;
        lo = 0.0f;
    } else return 1.0f + x;
    const float xx = x * x;
    const float c = x - xx * (1.6666625440e-1f + xx * -2.7667332906e-3f);
    const float y = 1.0f + (x * c / (2.0f - c) - lo + hi);
    return k == 0 ? y : scalbnf(y, k);
#endif
}

/* Private expm1 path used only by the retained f32 tanh algorithm. */
static float reference_expm1(float x) {
    const uint32_t bits = float_bits(x);
    const uint32_t magnitude = bits & UINT32_C(0x7fffffff);
    const bool negative = (bits >> 31) != 0u;
    if (magnitude > UINT32_C(0x7f800000)) return from_bits(UINT32_C(0x7fc00000));
    if (bits == UINT32_C(0xff800000)) return -1.0f;
    if (magnitude >= UINT32_C(0x4195b844)) {
        if (negative) return -1.0f;
        if (x > 8.8721679688e+01f) return x * 0x1p127f;
    }
    float hi, lo, correction = 0.0f;
    int k;
    if (magnitude > UINT32_C(0x3eb17218)) {
        if (magnitude < UINT32_C(0x3f851592)) {
            hi = negative ? x + 6.9313812256e-01f : x - 6.9313812256e-01f;
            lo = negative ? -9.0580006145e-06f : 9.0580006145e-06f;
            k = negative ? -1 : 1;
        } else {
            float kf = 1.4426950216e+00f * x;
            kf += negative ? -0.5f : 0.5f;
            k = (int)kf;
            const float t = (float)k;
            hi = x - t * 6.9313812256e-01f;
            lo = t * 9.0580006145e-06f;
        }
        x = hi - lo;
        correction = (hi - x) - lo;
    } else if (magnitude < UINT32_C(0x33000000)) return x;
    else k = 0;
    const float half_x = 0.5f * x;
    const float half_square = x * half_x;
    const float r = 1.0f + half_square * (-3.3333212137e-2f + half_square * 1.5807170421e-3f);
    const float t = 3.0f - r * half_x;
    float error = half_square * ((r - t) / (6.0f - x * t));
    if (k == 0) return x - (x * error - half_square);
    error = x * (error - correction) - correction;
    error -= half_square;
    if (k == -1) return 0.5f * (x - error) - 0.5f;
    if (k == 1) return x < -0.25f ? -2.0f * (error - (x + 0.5f))
        : 1.0f + 2.0f * (x - error);
    const float two_k = from_bits((uint32_t)(127 + k) << 23);
    if (k < 0 || k > 56) {
        float y = x - error + 1.0f;
        y = k == 128 ? y * 2.0f * 0x1p127f : y * two_k;
        return y - 1.0f;
    }
    const float inverse_two_k = from_bits((uint32_t)(127 - k) << 23);
    return k < 23 ? (x - error + (1.0f - inverse_two_k)) * two_k
        : (x - (error + inverse_two_k) + 1.0f) * two_k;
}

float tsir_tanh_f32(float x) {
    const uint32_t bits = float_bits(x);
    const uint32_t magnitude = bits & UINT32_C(0x7fffffff);
    const float absolute = from_bits(magnitude);
    float t;
    if (magnitude > UINT32_C(0x3f0c9f54)) {
        if (magnitude > UINT32_C(0x41200000)) t = 1.0f + 0.0f / x;
        else {
            t = reference_expm1(2.0f * absolute);
            t = 1.0f - 2.0f / (t + 2.0f);
        }
    } else if (magnitude > UINT32_C(0x3e82c578)) {
        t = reference_expm1(2.0f * absolute);
        t = t / (t + 2.0f);
    } else if (magnitude >= UINT32_C(0x00800000)) {
        t = reference_expm1(-2.0f * absolute);
        t = -t / (t + 2.0f);
    } else t = absolute;
    return (bits >> 31) != 0u ? -t : t;
}

float tsir_sqrt_f32(float x) {
    return (float)sqrt((double)x);
}
