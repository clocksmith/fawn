/* SPDX-License-Identifier: MIT */
#ifndef DOE_TIMESTAMP_H
#define DOE_TIMESTAMP_H
#include "doe_timestamp_kernel.h"
#include <float.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

enum { DOE_TIMESTAMP_SOURCE_CAPACITY = 4096 };
typedef enum doe_timestamp_source_status {
    DOE_TIMESTAMP_SOURCE_OK, DOE_TIMESTAMP_SOURCE_INVALID, DOE_TIMESTAMP_SOURCE_OVERFLOW
} doe_timestamp_source_status;
/* Source construction only. Backend owners supply measured period/counter width,
 * compile this WGSL, and retain GPU normalization resources. Failed calls leave
 * both outputs unchanged. Kernel bytes and floor/modulo policy are retained. */
_Static_assert(sizeof(float) == sizeof(uint32_t), "timestamp period is binary32");
_Static_assert(FLT_RADIX == 2 && FLT_MANT_DIG == 24 && FLT_MAX_EXP == 128 &&
    FLT_MIN_EXP == -125, "timestamp period requires IEEE binary32 parameters");

static inline doe_timestamp_source_status doe_timestamp_source(float period_ns, uint32_t valid_bits,
    uint32_t workgroup_size, char *out, size_t capacity, size_t *out_length) {
    if (!isfinite(period_ns) || period_ns <= 0 || valid_bits == 0 || valid_bits > 64 ||
        workgroup_size == 0 || out == NULL || out_length == NULL)
        return DOE_TIMESTAMP_SOURCE_INVALID;
    uint32_t bits;
    memcpy(&bits, &period_ns, sizeof(bits));
    uint32_t exponent = (bits >> 23) & 255;
    uint32_t mantissa = (bits & UINT32_C(0x7fffff)) | (exponent == 0 ? 0 : UINT32_C(1) << 23);
    int32_t shift = exponent == 0 ? -149 : (int32_t)exponent - 150;
    uint64_t mask = UINT64_MAX >> (64 - valid_bits);
    char source[DOE_TIMESTAMP_SOURCE_CAPACITY];
    int length = snprintf(source, sizeof(source),
        "const MANTISSA = %" PRIu32 "u;\nconst PERIOD_SHIFT = %" PRId32 "i;\n"
        "const MASK_LOW = %" PRIu32 "u;\nconst MASK_HIGH = %" PRIu32 "u;\n"
        "const WORKGROUP_SIZE = %" PRIu32 "u;\n%s",
        mantissa, shift, (uint32_t)mask, (uint32_t)(mask >> 32), workgroup_size,
        doe_timestamp_kernel);
    if (length < 0 || (size_t)length >= sizeof(source)) return DOE_TIMESTAMP_SOURCE_OVERFLOW;
    if ((size_t)length >= capacity) return DOE_TIMESTAMP_SOURCE_INVALID;
    memcpy(out, source, (size_t)length + 1);
    *out_length = (size_t)length;
    return DOE_TIMESTAMP_SOURCE_OK;
}

#endif
