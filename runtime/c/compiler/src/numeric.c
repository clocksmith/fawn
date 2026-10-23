/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "internal.h"

bool parse_decimal(Source *source, const Token *token, Decimal *decimal) {
    size_t length = token->length;
    const char *text = source->source + token->offset;
    if (length != 0u && text[length - 1u] == 'f') --length;
    uint32_t numerator = 0u, denominator = 1u;
    unsigned before = 0u, after = 0u;
    bool point = false;
    for (size_t i = 0u; i < length; ++i) {
        if (text[i] == '.' && !point) { point = true; continue; }
        if (text[i] < '0' || text[i] > '9' || before + after == 9u)
            return source_error(source, DOE_COMPILER_UNSUPPORTED, token->offset,
                                "decimal float requires at most nine digits and optional f suffix");
        const uint32_t digit = (uint32_t)(text[i] - '0');
        if (numerator > (UINT32_MAX - digit) / 10u ||
            (point && denominator > UINT32_MAX / 10u))
            return source_error(source, DOE_COMPILER_INPUT_ERROR, token->offset,
                                "decimal rational capacity exceeded");
        numerator = numerator * 10u + digit;
        if (point) { ++after; denominator *= 10u; }
        else ++before;
    }
    if (!point || before == 0u || after == 0u)
        return source_error(source, DOE_COMPILER_UNSUPPORTED, token->offset,
                            "decimal float requires digits on both sides of its point");
    *decimal = (Decimal){ .numerator = numerator, .denominator = denominator,
                          .offset = token->offset };
    return true;
}

static bool conversion_limit(Source *source, size_t offset) {
    return source_error(source, DOE_COMPILER_INPUT_ERROR, offset,
                        "decimal binary32 conversion capacity exceeded");
}
bool decimal_binary32(Source *source, const Decimal *decimal, uint32_t *bits) {
    if (decimal->denominator == 0u) return conversion_limit(source, decimal->offset);
    if (decimal->numerator == 0u) { *bits = 0u; return true; }
    uint64_t numerator = decimal->numerator, denominator = decimal->denominator;
    int exponent = 0;
    /* Normalize the exact rational, never a host floating-point value. */
    while (numerator < denominator) {
        if (numerator > UINT64_MAX / 2u || exponent <= -126)
            return conversion_limit(source, decimal->offset);
        numerator *= 2u;
        --exponent;
    }
    for (;;) {
        if (denominator > UINT64_MAX / 2u) return conversion_limit(source, decimal->offset);
        if (numerator < denominator * 2u) break;
        if (exponent >= 127) return conversion_limit(source, decimal->offset);
        denominator *= 2u;
        ++exponent;
    }
    if (numerator > (UINT64_MAX >> 23u)) return conversion_limit(source, decimal->offset);
    const uint64_t scaled = numerator << 23u;
    uint64_t significand = scaled / denominator;
    const uint64_t remainder = scaled % denominator;
    /* remainder versus denominator-remainder avoids doubling overflow. */
    if (remainder > denominator - remainder ||
        (remainder == denominator - remainder && (significand & 1u) != 0u)) ++significand;
    if (significand == (UINT64_C(1) << 24u)) { significand >>= 1u; ++exponent; }
    if (exponent < -126 || exponent > 127 || significand < (UINT64_C(1) << 23u) ||
        significand >= (UINT64_C(1) << 24u)) return conversion_limit(source, decimal->offset);
    *bits = ((uint32_t)(exponent + 127) << 23u) |
            (uint32_t)(significand - (UINT64_C(1) << 23u));
    return true;
}
