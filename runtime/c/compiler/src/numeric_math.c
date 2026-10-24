/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "numeric.h"
#include "numeric_host.h"
#include <math.h>

/* Only const-stage component operations reach libm. Runtime operations retain
 * their typed IR identities, including when the argument happens to be literal.
 * The enclosing pipeline establishes nearest-even and gradual underflow. */
bool numeric_math(Source *source, size_t offset, NumericBuiltinKind kind,
                  bool single, double input, double other, double *result) {
    if (((kind == BUILTIN_ASIN || kind == BUILTIN_ACOS) && (input < -1.0 || input > 1.0)) ||
        (kind == BUILTIN_ACOSH && input < 1.0) ||
        (kind == BUILTIN_ATANH && (input < -1.0 || input > 1.0)) ||
        ((kind == BUILTIN_LOG || kind == BUILTIN_LOG2) && input <= 0.0) ||
        ((kind == BUILTIN_SQRT || kind == BUILTIN_INVERSE_SQRT) && input < 0.0) ||
        (kind == BUILTIN_POW && input < 0.0))
        return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, offset,
                            "constant numeric builtin operand is outside its domain");
    const float narrow = single ? (float)input : 0.0f;
    const float narrow_other = single ? (float)other : 0.0f;
    switch (kind) {
        case BUILTIN_EXP: *result = single ? (double)expf(narrow) : exp(input); break;
        case BUILTIN_TANH: *result = single ? (double)tanhf(narrow) : tanh(input); break;
        case BUILTIN_SQRT: *result = single ? (double)sqrtf(narrow) : sqrt(input); break;
        case BUILTIN_CEIL: *result = single ? (double)ceilf(narrow) : ceil(input); break;
        case BUILTIN_FLOOR: *result = single ? (double)floorf(narrow) : floor(input); break;
        case BUILTIN_ROUND: *result = single ? (double)nearbyintf(narrow) : nearbyint(input); break;
        case BUILTIN_TRUNC: *result = single ? (double)truncf(narrow) : trunc(input); break;
        case BUILTIN_SIN: *result = single ? (double)sinf(narrow) : sin(input); break;
        case BUILTIN_COS: *result = single ? (double)cosf(narrow) : cos(input); break;
        case BUILTIN_TAN: *result = single ? (double)tanf(narrow) : tan(input); break;
        case BUILTIN_ASIN: *result = single ? (double)asinf(narrow) : asin(input); break;
        case BUILTIN_ACOS: *result = single ? (double)acosf(narrow) : acos(input); break;
        case BUILTIN_ATAN: *result = single ? (double)atanf(narrow) : atan(input); break;
        case BUILTIN_ATAN2:
            *result = single ? (double)atan2f(narrow, narrow_other) : atan2(input, other);
            break;
        case BUILTIN_POW:
            *result = single ? (double)powf(narrow, narrow_other) : pow(input, other);
            break;
        case BUILTIN_SINH: *result = single ? (double)sinhf(narrow) : sinh(input); break;
        case BUILTIN_COSH: *result = single ? (double)coshf(narrow) : cosh(input); break;
        case BUILTIN_ASINH: *result = single ? (double)asinhf(narrow) : asinh(input); break;
        case BUILTIN_ACOSH: *result = single ? (double)acoshf(narrow) : acosh(input); break;
        case BUILTIN_ATANH: *result = single ? (double)atanhf(narrow) : atanh(input); break;
        case BUILTIN_LOG: *result = single ? (double)logf(narrow) : log(input); break;
        case BUILTIN_LOG2: *result = single ? (double)log2f(narrow) : log2(input); break;
        case BUILTIN_EXP2: *result = single ? (double)exp2f(narrow) : exp2(input); break;
        case BUILTIN_SIGN: *result = input < 0.0 ? -1.0 : input > 0.0 ? 1.0 : 0.0; break;
        case BUILTIN_FRACT:
            if (single) {
                volatile float value = narrow - floorf(narrow);
                *result = (double)value;
            } else *result = input - floor(input);
            break;
        case BUILTIN_INVERSE_SQRT:
            if (single) {
                volatile float value = 1.0f / sqrtf(narrow);
                *result = (double)value;
            } else *result = 1.0 / sqrt(input);
            break;
        case BUILTIN_DEGREES:
        case BUILTIN_RADIANS: {
            const double factor = kind == BUILTIN_DEGREES ? 0x1.ca5dc1a63c1f8p+5 : 0x1.1df46a2529d39p-6;
            if (single) {
                volatile float rounded_factor = (float)factor;
                volatile float value = narrow * rounded_factor;
                *result = (double)value;
            } else *result = input * factor;
            break;
        }
        case BUILTIN_ABS: case BUILTIN_MIN: case BUILTIN_MAX: case BUILTIN_CLAMP:
        case BUILTIN_DOT: case BUILTIN_LENGTH: case BUILTIN_TRANSPOSE: case BUILTIN_DETERMINANT:
        case BUILTIN_DISTANCE: case BUILTIN_NORMALIZE: case BUILTIN_CROSS: case BUILTIN_REFLECT:
        case BUILTIN_SUBGROUP_ADD: case BUILTIN_SUBGROUP_MIN: case BUILTIN_SUBGROUP_MAX:
        case BUILTIN_SUBGROUP_MUL: case BUILTIN_SUBGROUP_AND: case BUILTIN_SUBGROUP_OR:
        case BUILTIN_SUBGROUP_XOR: case BUILTIN_SUBGROUP_EXCLUSIVE_ADD: case BUILTIN_SUBGROUP_INCLUSIVE_ADD:
        case BUILTIN_SUBGROUP_EXCLUSIVE_MUL: case BUILTIN_SUBGROUP_INCLUSIVE_MUL:
            return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, offset,
                                "non-floating component builtin reached math primitive");
    }
    return true;
}
