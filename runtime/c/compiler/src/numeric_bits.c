/* SPDX-License-Identifier: MIT */
#include "numeric.h"

/* Defined 32-bit operations, including zero and the signed all-ones case.
 * Host shifts never use 32, and signed WGSL values remain unsigned bit patterns. */
uint32_t numeric_integer_bits(NumericBuiltinKind kind, uint32_t input, bool sign) {
    uint32_t value = input, count = 0u;
    if (kind == BUILTIN_REVERSE_BITS) {
        uint32_t reversed = 0u;
        for (uint32_t i = 0u; i < 32u; ++i) {
            reversed = (reversed << 1u) | (value & 1u);
            value >>= 1u;
        }
        return reversed;
    }
    if (kind == BUILTIN_COUNT_ONE_BITS) {
        while (value != 0u) {
            value &= value - 1u;
            ++count;
        }
        return count;
    }
    const bool leading = kind == BUILTIN_COUNT_LEADING_ZEROS || kind == BUILTIN_FIRST_LEADING_BIT;
    if (kind == BUILTIN_FIRST_LEADING_BIT && sign && (value & UINT32_C(0x80000000)) != 0u)
        value = ~value;
    if (value == 0u)
        return kind == BUILTIN_COUNT_LEADING_ZEROS || kind == BUILTIN_COUNT_TRAILING_ZEROS ? 32u : UINT32_MAX;
    if (leading) {
        while ((value & UINT32_C(0x80000000)) == 0u) {
            value <<= 1u;
            ++count;
        }
        return kind == BUILTIN_FIRST_LEADING_BIT ? 31u - count : count;
    }
    while ((value & 1u) == 0u) {
        value >>= 1u;
        ++count;
    }
    return count;
}
