/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "numeric.h"
#include <math.h>

/* Binary16 is represented by its payload, independently of host _Float16 ABI. */
double numeric_half_value(uint64_t bits) {
    const unsigned exponent = (unsigned)((bits >> 10u) & 31u);
    const unsigned fraction = (unsigned)(bits & 1023u);
    double value = exponent == 0u ? ldexp((double)fraction, -24) :
        exponent == 31u ? (fraction == 0u ? INFINITY : NAN) :
        ldexp((double)(1024u + fraction), (int)exponent - 25);
    return (bits & 32768u) != 0u ? -value : value;
}
bool numeric_half_pack(Source *source, size_t offset, double value, NumericValue *out) {
    if (!isfinite(value))
        return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, offset,
                            "evaluated f16 value is not finite");
    const uint64_t sign = signbit(value) ? 32768u : 0u;
    const double magnitude = fabs(value);
    int exponent = 0;
    (void)frexp(magnitude, &exponent);
    const int shift = exponent < -13 ? 24 : 11 - exponent;
    const double scaled = ldexp(magnitude, shift);
    if (scaled > 2048.0 || exponent > 16)
        return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, offset,
                            "floating-point conversion is outside f16 finite range");
    uint64_t significand = (uint64_t)floor(scaled);
    const double remainder = scaled - (double)significand;
    if (remainder > 0.5 || (remainder == 0.5 && (significand & 1u) != 0u)) ++significand;
    uint64_t bits = significand;
    if (exponent >= -13) {
        if (significand == 2048u) { significand = 1024u; ++exponent; }
        if (exponent > 16)
            return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, offset,
                                "floating-point conversion is outside f16 finite range");
        bits = ((uint64_t)(exponent + 14) << 10u) | (significand & 1023u);
    }
    if (magnitude == 0.0) bits = 0u;
    *out = (NumericValue){sign | bits, IR_F16};
    return true;
}
