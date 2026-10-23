/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "numeric.h"
#include "numeric_host.h"
#include <math.h>
#include <string.h>

static bool numeric_error(Source *source, size_t offset, const char *message) {
    return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, offset, message);
}

static bool integer_type(IrType type) {
    return type == IR_U32 || type == IR_I32 || type == IR_ABSTRACT_INT;
}
static bool floating_type(IrType type) {
    return type == IR_F32 || type == IR_ABSTRACT_FLOAT;
}
static uint64_t integer_mask(IrType type) {
    return type == IR_ABSTRACT_INT ? UINT64_MAX : UINT64_C(0xffffffff);
}
static int64_t signed_value(NumericValue value) {
    uint64_t bits = value.bits;
    if (value.type == IR_I32 && (bits & UINT64_C(0x80000000)) != 0u)
        bits |= UINT64_C(0xffffffff00000000);
    int64_t result;
    memcpy(&result, &bits, sizeof(result));
    return result;
}
static float float_value(uint64_t bits) {
    const uint32_t narrow = (uint32_t)bits;
    float result;
    memcpy(&result, &narrow, sizeof(result));
    return result;
}
static double double_value(uint64_t bits) {
    double result;
    memcpy(&result, &bits, sizeof(result));
    return result;
}
static uint64_t float_bits(float value) {
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}
static uint64_t double_bits(double value) {
    uint64_t bits;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}
static bool finite_value(NumericValue value) {
    if (value.type == IR_F32)
        return (value.bits & UINT64_C(0x7f800000)) != UINT64_C(0x7f800000);
    if (value.type == IR_ABSTRACT_FLOAT)
        return (value.bits & UINT64_C(0x7ff0000000000000)) != UINT64_C(0x7ff0000000000000);
    return true;
}
static bool zero_value(NumericValue value) {
    if (value.type == IR_F32) return (value.bits & UINT64_C(0x7fffffff)) == 0u;
    if (value.type == IR_ABSTRACT_FLOAT)
        return (value.bits & UINT64_C(0x7fffffffffffffff)) == 0u;
    return value.bits == 0u;
}
static bool checked_float(Source *source, size_t offset, NumericValue value,
                          NumericValue *result) {
    if (!finite_value(value))
        return numeric_error(source, offset, "evaluated constant floating-point value is not finite");
    *result = value;
    return true;
}

/* Inputs have one checked numeric type and finite payloads. Strict comparison
 * selects the right operand on equality, preserving its signed-zero bits. */
static bool numeric_less(NumericValue left, NumericValue right) {
    if (left.type == IR_F32) return float_value(left.bits) < float_value(right.bits);
    if (left.type == IR_ABSTRACT_FLOAT) return double_value(left.bits) < double_value(right.bits);
    if (left.type == IR_U32) return left.bits < right.bits;
    return signed_value(left) < signed_value(right);
}

static NumericValue numeric_minimum(NumericValue left, NumericValue right) {
    return numeric_less(left, right) ? left : right;
}

static NumericValue numeric_maximum(NumericValue left, NumericValue right) {
    return numeric_less(right, left) ? left : right;
}

bool numeric_builtin(Source *source, size_t offset, const NumericBuiltin *signature,
                     const NumericValue *arguments, NumericValue *result) {
    if (signature == NULL)
        return numeric_error(source, offset, "invalid scalar builtin identity");
    const NumericBuiltinKind kind = signature->kind;
    const IrType type = arguments[0].type;
    if (!floating_type(type) && (signature->family == BUILTIN_COMPONENT_FLOAT || !integer_type(type)))
        return numeric_error(source, offset, "scalar builtin operand type is not supported");
    for (uint32_t i = 0u; i < signature->arity; ++i) {
        if (arguments[i].type != type)
            return numeric_error(source, offset, "scalar builtin operands have different types");
        if (!finite_value(arguments[i]))
            return numeric_error(source, offset, "scalar builtin operand is a nonfinite evaluated constant");
    }
    switch (kind) {
        case BUILTIN_MIN:
            *result = numeric_minimum(arguments[0], arguments[1]);
            return true;
        case BUILTIN_MAX:
            *result = numeric_maximum(arguments[0], arguments[1]);
            return true;
        case BUILTIN_CLAMP:
            if (numeric_less(arguments[2], arguments[1]))
                return numeric_error(source, offset, "constant clamp lower bound exceeds upper bound");
            *result = numeric_minimum(numeric_maximum(arguments[0], arguments[1]), arguments[2]);
            return true;
        case BUILTIN_ABS:
            *result = arguments[0];
            if (floating_type(type))
                result->bits &= type == IR_F32 ? UINT64_C(0x7fffffff) : UINT64_C(0x7fffffffffffffff);
            else if (type != IR_U32 && signed_value(*result) < 0)
                result->bits = (UINT64_C(0) - result->bits) & integer_mask(type);
            return true;
        case BUILTIN_SIGN:
            if (type == IR_U32) return numeric_error(source, offset, "sign has no unsigned overload");
            if (integer_type(type)) {
                const int64_t input = signed_value(arguments[0]);
                *result = (NumericValue){input < 0 ? integer_mask(type) : input > 0 ? 1u : 0u, type};
                return true;
            }
            break;
        case BUILTIN_EXP:
        case BUILTIN_TANH:
        case BUILTIN_SQRT:
        case BUILTIN_CEIL:
        case BUILTIN_FLOOR:
        case BUILTIN_ROUND:
        case BUILTIN_TRUNC:
        case BUILTIN_FRACT:
        case BUILTIN_SIN:
        case BUILTIN_COS:
        case BUILTIN_TAN:
        case BUILTIN_ASIN:
        case BUILTIN_ACOS:
        case BUILTIN_ATAN:
        case BUILTIN_SINH:
        case BUILTIN_COSH:
        case BUILTIN_ASINH:
        case BUILTIN_ACOSH:
        case BUILTIN_ATANH:
        case BUILTIN_LOG:
        case BUILTIN_LOG2:
        case BUILTIN_EXP2:
        case BUILTIN_INVERSE_SQRT:
        case BUILTIN_DEGREES:
        case BUILTIN_RADIANS:
            break;
        case BUILTIN_DOT: case BUILTIN_LENGTH: case BUILTIN_TRANSPOSE: case BUILTIN_DETERMINANT:
        case BUILTIN_DISTANCE: case BUILTIN_NORMALIZE: case BUILTIN_CROSS:
        case BUILTIN_SUBGROUP_ADD: case BUILTIN_SUBGROUP_MIN: case BUILTIN_SUBGROUP_MAX:
        case BUILTIN_SUBGROUP_MUL: case BUILTIN_SUBGROUP_AND: case BUILTIN_SUBGROUP_OR:
        case BUILTIN_SUBGROUP_XOR: case BUILTIN_SUBGROUP_EXCLUSIVE_ADD: case BUILTIN_SUBGROUP_INCLUSIVE_ADD:
        case BUILTIN_SUBGROUP_EXCLUSIVE_MUL: case BUILTIN_SUBGROUP_INCLUSIVE_MUL:
            return numeric_error(source, offset, "non-component builtin reached scalar primitive");
    }
    const bool single = type == IR_F32;
    const double input = single ? (double)float_value(arguments[0].bits) : double_value(arguments[0].bits);
    double value;
    if (!numeric_math(source, offset, kind, single, input, &value)) return false;
    /* Each math primitive already evaluated at the selected width. The store
     * preserves that width before another constant operation consumes it. */
    NumericValue answer = {.type = type};
    if (single) {
        volatile float rounded = (float)value;
        answer.bits = float_bits(rounded);
    } else {
        volatile double rounded = value;
        answer.bits = double_bits(rounded);
    }
    return checked_float(source, offset, answer, result);
}

/* The five-limb sums represent up to four products of signed64 magnitudes.
 * The largest positive sum is 4 * 2^126 = 2^128, so even unsigned128 is short. */
bool numeric_dot_accumulate(Source *source, size_t offset, NumericDotAccumulator *sum,
                            NumericValue left, NumericValue right) {
    if (left.type != IR_ABSTRACT_INT || right.type != IR_ABSTRACT_INT)
        return numeric_error(source, offset, "exact dot requires AbstractInt operands");
    const bool left_negative = (left.bits >> 63u) != 0u;
    const bool right_negative = (right.bits >> 63u) != 0u;
    const uint64_t left_magnitude = left_negative ? UINT64_C(0) - left.bits : left.bits;
    const uint64_t right_magnitude = right_negative ? UINT64_C(0) - right.bits : right.bits;
    const uint32_t a[2] = {(uint32_t)left_magnitude, (uint32_t)(left_magnitude >> 32u)};
    const uint32_t b[2] = {(uint32_t)right_magnitude, (uint32_t)(right_magnitude >> 32u)};
    uint32_t product[4] = {0};
    for (uint32_t i = 0u; i < 2u; ++i) {
        uint64_t carry = 0u;
        for (uint32_t j = 0u; j < 2u; ++j) {
            /* (2^32-1)^2 + two (2^32-1) terms is exactly 2^64-1. */
            const uint64_t part = (uint64_t)a[i] * b[j] + product[i + j] + carry;
            product[i + j] = (uint32_t)part;
            carry = part >> 32u;
        }
        product[i + 2u] = (uint32_t)carry;
    }
    uint32_t *destination = left_negative != right_negative ? sum->negative : sum->positive;
    uint64_t carry = 0u;
    for (uint32_t i = 0u; i < 5u; ++i) {
        const uint64_t part = (uint64_t)destination[i] + (i < 4u ? product[i] : 0u) + carry;
        destination[i] = (uint32_t)part;
        carry = part >> 32u;
    }
    if (carry != 0u) return numeric_error(source, offset, "exact dot accumulator exceeded its four-lane bound");
    return true;
}

bool numeric_dot_finish(Source *source, size_t offset, const NumericDotAccumulator *sum, NumericValue *out) {
    bool negative = false;
    for (uint32_t i = 5u; i != 0u; --i) {
        if (sum->positive[i - 1u] != sum->negative[i - 1u]) {
            negative = sum->positive[i - 1u] < sum->negative[i - 1u];
            break;
        }
    }
    const uint32_t *larger = negative ? sum->negative : sum->positive;
    const uint32_t *smaller = negative ? sum->positive : sum->negative;
    uint32_t magnitude[5];
    uint64_t borrow = 0u;
    for (uint32_t i = 0u; i < 5u; ++i) {
        const uint64_t subtrahend = (uint64_t)smaller[i] + borrow;
        magnitude[i] = (uint32_t)((uint64_t)larger[i] - subtrahend);
        borrow = (uint64_t)larger[i] < subtrahend ? 1u : 0u;
    }
    const uint64_t value = (uint64_t)magnitude[0] | ((uint64_t)magnitude[1] << 32u);
    const uint64_t limit = negative ? UINT64_C(0x8000000000000000) : UINT64_C(0x7fffffffffffffff);
    if (magnitude[2] != 0u || magnitude[3] != 0u || magnitude[4] != 0u || value > limit)
        return numeric_error(source, offset, "AbstractInt dot final result is outside signed64 range");
    *out = (NumericValue){negative ? UINT64_C(0) - value : value, IR_ABSTRACT_INT};
    return true;
}

/* Literal precision is bounded by WGSL, not source length. The largest retained
 * decimal significand has 67 bits. A relevant decimal exponent is in [-400,310],
 * so its denominator needs at most 1330 bits and its numerator 1097 bits.
 * Extracting binary64 subnormal precision shifts the numerator by at most 1074
 * bits; after exponent normalization all temporaries fit in 1536 bits.
 * Hexadecimal literals retain 64 bits and relevant powers in [-1200,1024]. */
enum { BIG_LIMBS = 48 };
typedef struct { uint32_t limb[BIG_LIMBS]; } LiteralInteger;

static void big_set(LiteralInteger *value, uint64_t number) {
    memset(value, 0, sizeof(*value));
    value->limb[0] = (uint32_t)number;
    value->limb[1] = (uint32_t)(number >> 32u);
}
static bool big_multiply_add(LiteralInteger *value, uint32_t factor, uint32_t digit) {
    uint64_t carry = digit;
    for (unsigned i = 0u; i < BIG_LIMBS; ++i) {
        carry += (uint64_t)value->limb[i] * factor;
        value->limb[i] = (uint32_t)carry;
        carry >>= 32u;
    }
    return carry == 0u;
}
static unsigned big_width(const LiteralInteger *value) {
    for (unsigned i = BIG_LIMBS; i != 0u; --i) {
        uint32_t limb = value->limb[i - 1u];
        if (limb != 0u) {
            unsigned width = (i - 1u) * 32u;
            while (limb != 0u) { ++width; limb >>= 1u; }
            return width;
        }
    }
    return 0u;
}
static int big_compare(const LiteralInteger *left, const LiteralInteger *right) {
    for (unsigned i = BIG_LIMBS; i != 0u; --i) {
        if (left->limb[i - 1u] != right->limb[i - 1u])
            return left->limb[i - 1u] < right->limb[i - 1u] ? -1 : 1;
    }
    return 0;
}
static bool big_shift(LiteralInteger *value, unsigned shift) {
    if (shift == 0u || big_width(value) == 0u) return true;
    if (shift >= BIG_LIMBS * 32u || big_width(value) > BIG_LIMBS * 32u - shift)
        return false;
    const unsigned words = shift / 32u, bits = shift % 32u;
    for (unsigned i = BIG_LIMBS; i != 0u; --i) {
        const unsigned destination = i - 1u;
        uint32_t limb = 0u;
        if (destination >= words) {
            limb = value->limb[destination - words] << bits;
            if (bits != 0u && destination > words)
                limb |= value->limb[destination - words - 1u] >> (32u - bits);
        }
        value->limb[destination] = limb;
    }
    return true;
}
static void big_subtract(LiteralInteger *left, const LiteralInteger *right) {
    uint64_t borrow = 0u;
    for (unsigned i = 0u; i < BIG_LIMBS; ++i) {
        const uint64_t subtrahend = (uint64_t)right->limb[i] + borrow;
        const uint64_t original = left->limb[i];
        left->limb[i] = (uint32_t)(original - subtrahend);
        borrow = original < subtrahend ? 1u : 0u;
    }
}
static bool big_quotient(LiteralInteger *numerator, const LiteralInteger *denominator,
                         uint64_t *quotient) {
    *quotient = 0u;
    const int difference = (int)big_width(numerator) - (int)big_width(denominator);
    if (difference >= 64) return false;
    for (int bit = difference; bit >= 0; --bit) {
        LiteralInteger term = *denominator;
        if (!big_shift(&term, (unsigned)bit)) return false;
        if (big_compare(numerator, &term) >= 0) {
            big_subtract(numerator, &term);
            *quotient |= UINT64_C(1) << (unsigned)bit;
        }
    }
    return true;
}
static bool literal_capacity(Source *source, size_t offset) {
    return source_error(source, DOE_COMPILER_INPUT_ERROR, offset,
                        "internal bounded literal conversion capacity exceeded");
}

static bool rational_float(Source *source, size_t offset, LiteralInteger numerator,
                           LiteralInteger denominator, IrType type, bool exact,
                           NumericValue *result) {
    const unsigned precision = type == IR_F32 ? 24u : 53u;
    const int minimum = type == IR_F32 ? -126 : -1022;
    const int maximum = type == IR_F32 ? 127 : 1023;
    const unsigned exponent_bias = type == IR_F32 ? 127u : 1023u;
    int exponent = (int)big_width(&numerator) - (int)big_width(&denominator);
    LiteralInteger comparison;
    if (exponent >= 0) {
        comparison = denominator;
        if (!big_shift(&comparison, (unsigned)exponent)) return literal_capacity(source, offset);
        if (big_compare(&numerator, &comparison) < 0) --exponent;
    } else {
        comparison = numerator;
        if (!big_shift(&comparison, (unsigned)(-exponent))) return literal_capacity(source, offset);
        if (big_compare(&comparison, &denominator) < 0) --exponent;
    }
    if (exponent > maximum)
        return numeric_error(source, offset, "floating-point literal is outside its finite range");
    const int effective_exponent = exponent < minimum ? minimum : exponent;
    const int shift = (int)precision - 1 - effective_exponent;
    if (!(shift >= 0 ? big_shift(&numerator, (unsigned)shift)
                     : big_shift(&denominator, (unsigned)(-shift))))
        return literal_capacity(source, offset);
    uint64_t significand;
    if (!big_quotient(&numerator, &denominator, &significand))
        return literal_capacity(source, offset);
    const bool remainder = big_width(&numerator) != 0u;
    const uint64_t one = UINT64_C(1) << (precision - 1u);
    if (exponent == maximum && significand == one * 2u - 1u && remainder)
        return numeric_error(source, offset, "floating-point literal is outside its finite range");
    if (exact && remainder)
        return numeric_error(source, offset, "hexadecimal f32 literal is not exactly representable");
    comparison = denominator;
    big_subtract(&comparison, &numerator);
    const int halfway = big_compare(&numerator, &comparison);
    if (halfway > 0 || (halfway == 0 && (significand & 1u) != 0u)) ++significand;
    exponent = effective_exponent;
    if (significand == one * 2u) { significand >>= 1u; ++exponent; }
    if (exponent > maximum)
        return numeric_error(source, offset, "floating-point literal is outside its finite range");
    uint64_t bits = significand;
    if (significand >= one)
        bits = ((uint64_t)(exponent + (int)exponent_bias) << (precision - 1u)) |
               (significand - one);
    *result = (NumericValue){ .bits = bits, .type = type };
    return true;
}

typedef struct {
    LiteralInteger significand;
    uint64_t integer;
    uint32_t digits, fractional, dropped, retained;
    bool nonzero, integer_overflow;
} LiteralDigits;
static unsigned digit_value(unsigned char character) {
    if (character >= '0' && character <= '9') return (unsigned)(character - '0');
    if (character >= 'a' && character <= 'f') return (unsigned)(character - 'a') + 10u;
    if (character >= 'A' && character <= 'F') return (unsigned)(character - 'A') + 10u;
    return 16u;
}
static void retain_digit(LiteralDigits *digits, unsigned digit, unsigned base, bool fractional) {
    ++digits->digits;
    if (fractional) ++digits->fractional;
    if (!digits->integer_overflow) {
        if (digits->integer > (UINT64_MAX - digit) / base) digits->integer_overflow = true;
        else digits->integer = digits->integer * base + digit;
    }
    if (digit != 0u) digits->nonzero = true;
    if (digits->nonzero) {
        if (digits->retained < (base == 10u ? 20u : 16u)) {
            (void)big_multiply_add(&digits->significand, base, digit);
            ++digits->retained;
        } else ++digits->dropped;
    }
}
static bool malformed_literal(Source *source, size_t offset) {
    return source_error(source, DOE_COMPILER_PARSE_ERROR, offset, "malformed numeric literal");
}

bool numeric_parse_literal(Source *source, NameSpan literal, NumericValue *result) {
    const size_t offset = literal.offset, length = literal.length;
    if (offset > source->length || length > source->length - offset || length == 0u)
        return source_error(source, DOE_COMPILER_INPUT_ERROR, offset, "numeric source span is out of bounds");
    const char *text = source->source + offset;
    if ((length == 4u && memcmp(text, "true", 4u) == 0) ||
        (length == 5u && memcmp(text, "false", 5u) == 0)) {
        *result = (NumericValue){ .bits = length == 4u ? 1u : 0u, .type = IR_BOOL };
        return true;
    }
    const bool hex = length >= 2u && text[0] == '0' && (text[1] == 'x' || text[1] == 'X');
    const unsigned base = hex ? 16u : 10u;
    size_t cursor = hex ? 2u : 0u;
    LiteralDigits digits = { 0 };
    while (cursor < length && digit_value((unsigned char)text[cursor]) < base) {
        retain_digit(&digits, digit_value((unsigned char)text[cursor]), base, false);
        ++cursor;
    }
    const bool point = cursor < length && text[cursor] == '.';
    if (point) {
        ++cursor;
        while (cursor < length && digit_value((unsigned char)text[cursor]) < base) {
            retain_digit(&digits, digit_value((unsigned char)text[cursor]), base, true);
            ++cursor;
        }
    }
    if (digits.digits == 0u) return malformed_literal(source, offset);
    const bool exponent_present = cursor < length &&
        (hex ? text[cursor] == 'p' || text[cursor] == 'P'
             : text[cursor] == 'e' || text[cursor] == 'E');
    int64_t exponent = 0;
    if (exponent_present) {
        ++cursor;
        bool negative = false;
        if (cursor < length && (text[cursor] == '+' || text[cursor] == '-')) {
            negative = text[cursor] == '-';
            ++cursor;
        }
        const size_t start = cursor;
        while (cursor < length && text[cursor] >= '0' && text[cursor] <= '9') {
            const int64_t digit = text[cursor] - '0';
            /* Saturation is far beyond any possible source-point displacement.
             * Keep scanning: enormous exponents do not impose a token limit. */
            if (exponent <= (INT64_MAX / 4 - digit) / 10) exponent = exponent * 10 + digit;
            else exponent = INT64_MAX / 4;
            ++cursor;
        }
        if (cursor == start) return malformed_literal(source, offset);
        if (negative) exponent = -exponent;
    }
    char suffix = '\0';
    if (cursor < length) suffix = text[cursor++];
    if (cursor != length) return malformed_literal(source, offset);
    const bool floating = point || exponent_present || (!hex && (suffix == 'f' || suffix == 'h'));
    if (!floating) {
        if ((suffix != '\0' && suffix != 'i' && suffix != 'u') ||
            (!hex && digits.digits > 1u && text[0] == '0'))
            return malformed_literal(source, offset);
        const IrType type = suffix == 'u' ? IR_U32 : suffix == 'i' ? IR_I32 : IR_ABSTRACT_INT;
        const uint64_t maximum = type == IR_U32 ? UINT32_MAX :
            type == IR_I32 ? (uint64_t)INT32_MAX : (uint64_t)INT64_MAX;
        if (digits.integer_overflow || digits.integer > maximum)
            return numeric_error(source, offset, "integer literal is outside its declared type range");
        *result = (NumericValue){ .bits = digits.integer, .type = type };
        return true;
    }
    if ((suffix != '\0' && suffix != 'f' && suffix != 'h') ||
        (hex && suffix != '\0' && !exponent_present) ||
        (!hex && !point && !exponent_present && digits.digits > 1u && text[0] == '0'))
        return malformed_literal(source, offset);
    if (suffix == 'h')
        return source_error(source, DOE_COMPILER_UNSUPPORTED, offset, "f16 literals are unsupported");
    const IrType type = suffix == 'f' ? IR_F32 : IR_ABSTRACT_FLOAT;
    if (!digits.nonzero) {
        *result = (NumericValue){ .bits = 0u, .type = type };
        return true;
    }
    exponent += ((int64_t)digits.dropped - (int64_t)digits.fractional) * (hex ? 4 : 1);
    if (exponent > (hex ? 1024 : 310))
        return numeric_error(source, offset, "floating-point literal is outside its finite range");
    if (exponent < (hex ? -1200 : -400)) {
        if (hex && type == IR_F32)
            return numeric_error(source, offset, "hexadecimal f32 literal is not exactly representable");
        *result = (NumericValue){ .bits = 0u, .type = type };
        return true;
    }
    LiteralInteger denominator;
    big_set(&denominator, 1u);
    if (hex) {
        if (!(exponent >= 0 ? big_shift(&digits.significand, (unsigned)exponent)
                            : big_shift(&denominator, (unsigned)(-exponent))))
            return literal_capacity(source, offset);
    } else {
        const unsigned power = (unsigned)(exponent < 0 ? -exponent : exponent);
        for (unsigned i = 0u; i < power; ++i)
            if (!big_multiply_add(exponent < 0 ? &denominator : &digits.significand, 10u, 0u))
                return literal_capacity(source, offset);
    }
    return rational_float(source, offset, digits.significand, denominator,
                          type, hex && type == IR_F32, result);
}

bool numeric_convert(Source *source, size_t offset, NumericValue input,
                     IrType target, NumericValue *result) {
    if (!finite_value(input))
        return numeric_error(source, offset, "cannot convert a nonfinite evaluated constant");
    if (input.type == target) { *result = input; return true; }
    NumericValue converted = { .bits = 0u, .type = target };
    if (target == IR_BOOL) {
        converted.bits = zero_value(input) ? 0u : 1u;
    } else if (input.type == IR_BOOL) {
        if (target == IR_F32) converted.bits = float_bits(input.bits == 0u ? 0.0f : 1.0f);
        else if (target == IR_ABSTRACT_FLOAT)
            converted.bits = double_bits(input.bits == 0u ? 0.0 : 1.0);
        else if (integer_type(target)) converted.bits = input.bits == 0u ? 0u : 1u;
        else return numeric_error(source, offset, "invalid scalar conversion");
    } else if (integer_type(target)) {
        if (input.type == IR_ABSTRACT_INT) {
            const int64_t integer = signed_value(input);
            if ((target == IR_U32 && (integer < 0 || (uint64_t)integer > UINT32_MAX)) ||
                (target == IR_I32 && (integer < INT32_MIN || integer > INT32_MAX)))
                return numeric_error(source, offset, "abstract integer conversion is out of range");
            converted.bits = input.bits & integer_mask(target);
        } else if (integer_type(input.type)) {
            if (target == IR_ABSTRACT_INT)
                converted.bits = input.type == IR_I32 ? (uint64_t)signed_value(input) : input.bits;
            else converted.bits = input.bits & UINT64_C(0xffffffff);
        } else if (floating_type(input.type) && target != IR_ABSTRACT_INT) {
            const double number = input.type == IR_F32 ? (double)float_value(input.bits) : double_value(input.bits);
            if (target == IR_U32) {
                const double maximum = input.type == IR_F32 ? 4294967040.0 : 4294967295.0;
                converted.bits = number <= 0.0 ? 0u : number >= maximum ? (uint64_t)maximum : (uint64_t)number;
            } else {
                const double maximum = input.type == IR_F32 ? 2147483520.0 : 2147483647.0;
                const int64_t integer = number <= -2147483648.0 ? INT32_MIN :
                    number >= maximum ? (int64_t)maximum : (int64_t)number;
                converted.bits = (uint32_t)(uint64_t)integer;
            }
        } else return numeric_error(source, offset, "invalid integer conversion");
    } else if (target == IR_F32) {
        if (input.type == IR_ABSTRACT_FLOAT) {
            const double number = double_value(input.bits);
            if (number > (double)FLT_MAX || number < -(double)FLT_MAX)
                return numeric_error(source, offset, "floating-point materialization is out of range");
            volatile float narrowed = (float)number;
            converted.bits = float_bits(narrowed);
        } else if (integer_type(input.type)) {
            if (input.type == IR_U32) {
                volatile float narrowed = (float)(uint32_t)input.bits;
                converted.bits = float_bits(narrowed);
            } else {
                volatile float narrowed = (float)signed_value(input);
                converted.bits = float_bits(narrowed);
            }
        } else return numeric_error(source, offset, "invalid floating-point conversion");
    } else if (target == IR_ABSTRACT_FLOAT) {
        if (input.type == IR_F32) converted.bits = double_bits((double)float_value(input.bits));
        else if (integer_type(input.type)) {
            if (input.type == IR_U32) {
                volatile double widened = (double)(uint32_t)input.bits;
                converted.bits = double_bits(widened);
            } else {
                volatile double widened = (double)signed_value(input);
                converted.bits = double_bits(widened);
            }
        } else return numeric_error(source, offset, "invalid abstract floating-point conversion");
    } else return numeric_error(source, offset, "invalid scalar conversion target");
    return checked_float(source, offset, converted, result);
}

bool numeric_materialize(Source *source, size_t offset, NumericValue input,
                         IrType target, NumericValue *result) {
    if (input.type == target) return checked_float(source, offset, input, result);
    if ((input.type == IR_ABSTRACT_INT &&
         (target == IR_U32 || target == IR_I32 || target == IR_F32 || target == IR_ABSTRACT_FLOAT)) ||
        (input.type == IR_ABSTRACT_FLOAT && target == IR_F32))
        return numeric_convert(source, offset, input, target, result);
    return numeric_error(source, offset, "no feasible automatic scalar conversion");
}

bool numeric_bitcast(Source *source, size_t offset, NumericValue input,
                     IrType target, NumericValue *result) {
    if (!(input.type == IR_U32 || input.type == IR_I32 || input.type == IR_F32) ||
        !(target == IR_U32 || target == IR_I32 || target == IR_F32))
        return numeric_error(source, offset, "bitcast requires a concrete 32-bit numeric scalar");
    if (!finite_value(input))
        return numeric_error(source, offset, "cannot bitcast a nonfinite evaluated constant");
    return checked_float(source, offset,
                         (NumericValue){ .bits = input.bits & UINT64_C(0xffffffff), .type = target }, result);
}

bool numeric_unary(Source *source, size_t offset, ExpressionKind operation,
                   NumericValue input, NumericValue *result) {
    if (!finite_value(input))
        return numeric_error(source, offset, "unary operand is a nonfinite evaluated constant");
    NumericValue value = input;
    if (operation == EXPRESSION_NOT && input.type == IR_BOOL) value.bits = input.bits == 0u ? 1u : 0u;
    else if (operation == EXPRESSION_COMPLEMENT && integer_type(input.type))
        value.bits = ~input.bits & integer_mask(input.type);
    else if (operation == EXPRESSION_NEGATE) {
        if (input.type == IR_I32 || input.type == IR_ABSTRACT_INT)
            value.bits = (UINT64_C(0) - input.bits) & integer_mask(input.type);
        else if (input.type == IR_F32) value.bits ^= UINT64_C(0x80000000);
        else if (input.type == IR_ABSTRACT_FLOAT) value.bits ^= UINT64_C(0x8000000000000000);
        else return numeric_error(source, offset, "unary negation requires a signed numeric scalar");
    } else return numeric_error(source, offset, "invalid unary scalar operation");
    *result = value;
    return true;
}

static bool comparison_result(Comparison operation, bool equal, bool less) {
    switch (operation) {
        case COMPARE_EQUAL: return equal;
        case COMPARE_NOT_EQUAL: return !equal;
        case COMPARE_LESS: return less;
        case COMPARE_LESS_EQUAL: return less || equal;
        case COMPARE_GREATER: return !less && !equal;
        case COMPARE_GREATER_EQUAL: return !less;
    }
    return false;
}
static bool abstract_arithmetic(Source *source, size_t offset, ExpressionKind operation,
                                NumericValue left, NumericValue right, NumericValue *result) {
    const uint64_t a = left.bits, b = right.bits, sign = UINT64_C(0x8000000000000000);
    uint64_t answer;
    if (operation == EXPRESSION_ADD || operation == EXPRESSION_SUBTRACT) {
        answer = operation == EXPRESSION_ADD ? a + b : a - b;
        const uint64_t changed_sign = operation == EXPRESSION_ADD ? ~(a ^ b) : a ^ b;
        if ((changed_sign & (a ^ answer) & sign) != 0u)
            return numeric_error(source, offset, "abstract integer arithmetic overflow");
    } else {
        const bool negative_a = (a & sign) != 0u, negative_b = (b & sign) != 0u;
        const uint64_t magnitude_a = negative_a ? UINT64_C(0) - a : a;
        const uint64_t magnitude_b = negative_b ? UINT64_C(0) - b : b;
        const bool negative = negative_a != negative_b;
        const uint64_t maximum = negative ? sign : sign - 1u;
        if (magnitude_b != 0u && magnitude_a > maximum / magnitude_b)
            return numeric_error(source, offset, "abstract integer arithmetic overflow");
        answer = magnitude_a * magnitude_b;
        if (negative) answer = UINT64_C(0) - answer;
    }
    *result = (NumericValue){ .bits = answer, .type = IR_ABSTRACT_INT };
    return true;
}
static bool integer_shift(Source *source, size_t offset, ExpressionKind operation,
                          NumericValue left, NumericValue right, NumericValue *result) {
    if (!integer_type(left.type) || right.type != IR_U32)
        return numeric_error(source, offset, "shift requires an integer and a u32 count");
    const unsigned width = left.type == IR_ABSTRACT_INT ? 64u : 32u;
    const uint64_t mask = integer_mask(left.type), bits = left.bits & mask;
    const uint32_t count = (uint32_t)right.bits;
    const bool negative = left.type != IR_U32 && (bits & (UINT64_C(1) << (width - 1u))) != 0u;
    if (width == 32u && count >= 32u)
        return numeric_error(source, offset, "constant concrete shift count must be less than 32");
    uint64_t answer = bits;
    if (operation == EXPRESSION_SHIFT_RIGHT) {
        if (count >= width) answer = negative ? mask : 0u;
        else if (count != 0u) {
            answer = bits >> count;
            if (negative) answer |= mask ^ (mask >> count);
        }
    } else if (count >= width) {
        if (bits != 0u) return numeric_error(source, offset, "constant left shift overflows");
        answer = 0u;
    } else if (count != 0u) {
        const uint64_t leading = negative ? ~bits & mask : bits;
        const unsigned keep = left.type == IR_U32 ? width - count : width - count - 1u;
        if ((leading >> keep) != 0u)
            return numeric_error(source, offset, "constant left shift overflows");
        answer = (bits << count) & mask;
    }
    *result = (NumericValue){ .bits = answer, .type = left.type };
    return true;
}
static bool float_binary(Source *source, size_t offset, ExpressionKind operation,
                         NumericValue left, NumericValue right, NumericValue *result) {
    NumericValue answer = { .bits = 0u, .type = left.type };
    if (left.type == IR_F32) {
        const float a = float_value(left.bits), b = float_value(right.bits);
        volatile float value;
        if (operation == EXPRESSION_ADD) value = a + b;
        else if (operation == EXPRESSION_SUBTRACT) value = a - b;
        else if (operation == EXPRESSION_MULTIPLY) value = a * b;
        else if (operation == EXPRESSION_DIVIDE) value = a / b;
        else if (operation == EXPRESSION_REMAINDER) {
            if (b == 0.0f) return numeric_error(source, offset, "constant floating-point remainder has zero divisor");
            /* fmod is the truncating remainder, not remainder's nearest-integer
             * quotient or floor modulo. It avoids spurious quotient overflow. */
            value = fmodf(a, b);
        } else return numeric_error(source, offset, "invalid floating-point binary operation");
        answer.bits = float_bits(value);
    } else {
        const double a = double_value(left.bits), b = double_value(right.bits);
        volatile double value;
        if (operation == EXPRESSION_ADD) value = a + b;
        else if (operation == EXPRESSION_SUBTRACT) value = a - b;
        else if (operation == EXPRESSION_MULTIPLY) value = a * b;
        else if (operation == EXPRESSION_DIVIDE) value = a / b;
        else if (operation == EXPRESSION_REMAINDER) {
            if (b == 0.0) return numeric_error(source, offset, "constant floating-point remainder has zero divisor");
            value = fmod(a, b);
        } else return numeric_error(source, offset, "invalid floating-point binary operation");
        answer.bits = double_bits(value);
    }
    return checked_float(source, offset, answer, result);
}

bool numeric_binary(Source *source, size_t offset, ExpressionKind operation,
                    Comparison comparison, NumericValue left, NumericValue right,
                    NumericValue *result) {
    if (!finite_value(left) || !finite_value(right))
        return numeric_error(source, offset, "binary operand is a nonfinite evaluated constant");
    if (operation == EXPRESSION_MAX) {
        if (left.type != right.type || (!integer_type(left.type) && !floating_type(left.type)))
            return numeric_error(source, offset, "max operands have no common numeric type");
        *result = numeric_maximum(left, right);
        return true;
    }
    if (operation == EXPRESSION_SHIFT_LEFT || operation == EXPRESSION_SHIFT_RIGHT)
        return integer_shift(source, offset, operation, left, right, result);
    if (left.type != right.type)
        return numeric_error(source, offset, "binary scalar operands have different types");
    if (operation == EXPRESSION_COMPARE) {
        bool equal, less;
        if (left.type == IR_F32) {
            const float a = float_value(left.bits), b = float_value(right.bits);
            equal = a == b; less = a < b;
        } else if (left.type == IR_ABSTRACT_FLOAT) {
            const double a = double_value(left.bits), b = double_value(right.bits);
            equal = a == b; less = a < b;
        } else if (left.type == IR_I32 || left.type == IR_ABSTRACT_INT) {
            const int64_t a = signed_value(left), b = signed_value(right);
            equal = a == b; less = a < b;
        } else { equal = left.bits == right.bits; less = left.bits < right.bits; }
        if (left.type == IR_BOOL && comparison != COMPARE_EQUAL && comparison != COMPARE_NOT_EQUAL)
            return numeric_error(source, offset, "bool does not support numeric ordering");
        *result = (NumericValue){ .bits = comparison_result(comparison, equal, less) ? 1u : 0u,
                                  .type = IR_BOOL };
        return true;
    }
    if (left.type == IR_BOOL) {
        uint64_t answer;
        if (operation == EXPRESSION_AND || operation == EXPRESSION_BIT_AND)
            answer = left.bits & right.bits;
        else if (operation == EXPRESSION_OR || operation == EXPRESSION_BIT_OR)
            answer = left.bits | right.bits;
        else return numeric_error(source, offset, "invalid bool binary operation");
        *result = (NumericValue){ .bits = answer, .type = IR_BOOL };
        return true;
    }
    if (floating_type(left.type)) return float_binary(source, offset, operation, left, right, result);
    if (!integer_type(left.type)) return numeric_error(source, offset, "invalid binary scalar type");
    uint64_t answer;
    const uint64_t a = left.bits, b = right.bits;
    if (operation == EXPRESSION_BIT_AND) answer = a & b;
    else if (operation == EXPRESSION_BIT_OR) answer = a | b;
    else if (operation == EXPRESSION_BIT_XOR) answer = a ^ b;
    else if (operation == EXPRESSION_ADD || operation == EXPRESSION_SUBTRACT || operation == EXPRESSION_MULTIPLY) {
        if (left.type == IR_ABSTRACT_INT) return abstract_arithmetic(source, offset, operation, left, right, result);
        if (operation == EXPRESSION_ADD) answer = a + b;
        else if (operation == EXPRESSION_SUBTRACT) answer = a - b;
        else answer = a * b;
    } else if (operation == EXPRESSION_DIVIDE || operation == EXPRESSION_REMAINDER) {
        if (b == 0u) return numeric_error(source, offset, "constant integer divisor is zero");
        if (left.type == IR_U32) answer = operation == EXPRESSION_DIVIDE ? a / b : a % b;
        else {
            const int64_t signed_a = signed_value(left), signed_b = signed_value(right);
            const int64_t minimum = left.type == IR_I32 ? INT32_MIN : INT64_MIN;
            if (signed_a == minimum && signed_b == -1)
                return numeric_error(source, offset, "constant signed division or remainder overflows");
            answer = (uint64_t)(operation == EXPRESSION_DIVIDE ? signed_a / signed_b : signed_a % signed_b);
        }
    } else return numeric_error(source, offset, "invalid integer binary operation");
    *result = (NumericValue){ .bits = answer & integer_mask(left.type), .type = left.type };
    return true;
}
