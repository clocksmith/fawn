/* SPDX-License-Identifier: MIT
 * One finite binary64 -> retained Zig {d} decimal spelling owner. */
#ifndef DOE_COMPILER_DECIMAL_H
#define DOE_COMPILER_DECIMAL_H
#include <errno.h>
#include <fenv.h>
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
    DOE_DECIMAL_OK, DOE_DECIMAL_INVALID, DOE_DECIMAL_TOO_SMALL
} DoeDecimalStatus;

/* No allocation or locale mutation. Output is an exact byte span without a
 * terminator. The caller owns output/capacity and a distinct written pointer.
 * Invalid inputs and capacity failures leave both untouched. errno and the
 * floating-point environment are restored before publication. */
static inline DoeDecimalStatus doe_decimal_f64(double value, char *output,
    size_t capacity, size_t *written) {
    if (output == NULL || written == NULL) return DOE_DECIMAL_INVALID;
    const int saved_errno = errno;
    fenv_t environment;
    if (feholdexcept(&environment) != 0) {
        errno = saved_errno;
        return DOE_DECIMAL_INVALID;
    }
    DoeDecimalStatus status = DOE_DECIMAL_INVALID;
    char text[384];
    size_t length = 0u;
    if (!isfinite(value) || fesetround(FE_TONEAREST) != 0) goto finish;
    if (signbit(value)) text[length++] = '-';
    if (value == 0.0) {
        text[length++] = '0';
        status = DOE_DECIMAL_OK;
        goto finish;
    }
    char raw[64], digits[17];
    uint64_t wanted;
    bool matched = false;
    memcpy(&wanted, &value, sizeof(wanted));
    for (int precision = 1; precision <= 17; ++precision) {
        const int count = snprintf(raw, sizeof(raw), "%.*e", precision - 1, value);
        if (count < 0 || (size_t)count >= sizeof(raw)) goto finish;
        const double parsed = strtod(raw, NULL);
        uint64_t actual;
        memcpy(&actual, &parsed, sizeof(actual));
        if (actual == wanted) { matched = true; break; }
    }
    if (!matched) goto finish;
    const char *exponent = strchr(raw, 'e');
    if (exponent == NULL) goto finish;
    const int power = atoi(exponent + 1);
    if (power < -324 || power > 308) goto finish;
    size_t count = 0u;
    /* printf and strtod use the same calling thread's locale. Discard its
     * possibly multibyte decimal separator and emit ASCII punctuation only. */
    for (const char *p = raw; p < exponent; ++p) {
        if (*p >= '0' && *p <= '9') {
            if (count == sizeof(digits)) goto finish;
            digits[count++] = *p;
        }
    }
    if (count == 0u) goto finish;
    while (count > 1u && digits[count - 1u] == '0') --count;
    if (power >= 0) {
        const size_t integer_digits = (size_t)power + 1u;
        for (size_t i = 0u; i < integer_digits; ++i)
            text[length++] = i < count ? digits[i] : '0';
        if (count > integer_digits) {
            text[length++] = '.';
            memcpy(text + length, digits + integer_digits, count - integer_digits);
            length += count - integer_digits;
        }
    } else {
        text[length++] = '0'; text[length++] = '.';
        for (int i = -1; i > power; --i) text[length++] = '0';
        memcpy(text + length, digits, count);
        length += count;
    }
    status = DOE_DECIMAL_OK;
finish:
    if (fesetenv(&environment) != 0) status = DOE_DECIMAL_INVALID;
    if (status == DOE_DECIMAL_OK) {
        if (length > capacity) status = DOE_DECIMAL_TOO_SMALL;
        else { memcpy(output, text, length); *written = length; }
    }
    errno = saved_errno;
    return status;
}
#endif
