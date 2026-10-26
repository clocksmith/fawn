/* SPDX-License-Identifier: MIT
 * Shortest round-trip digits shared by TSIR identity and kernel emission. */
#include "tsir_format.h"
#include <errno.h>
#include <fenv.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void invalid(TsirBuffer *buffer) {
    if (buffer->err == TSIR_OK) buffer->err = TSIR_ERR_INVALID;
}

static void format_finite(TsirBuffer *buffer, double value) {
    char raw[64];
    char digits[17];
    uint64_t wanted;
    bool matched = false;
    if (value == 0.0) {
        tsir_buf_puts(buffer, signbit(value) ? "-0" : "0");
        return;
    }
    memcpy(&wanted, &value, sizeof(wanted));
    for (int precision = 1; precision <= 17; ++precision) {
        int length = snprintf(raw, sizeof(raw), "%.*e", precision - 1, value);
        if (length < 0 || (size_t)length >= sizeof(raw)) { invalid(buffer); return; }
        double parsed = strtod(raw, NULL);
        uint64_t actual;
        memcpy(&actual, &parsed, sizeof(actual));
        if (wanted == actual) { matched = true; break; }
    }
    if (!matched) { invalid(buffer); return; }
    const char *exponent = strchr(raw, 'e');
    if (exponent == NULL) { invalid(buffer); return; }
    int power = atoi(exponent + 1);
    size_t count = 0;
    /* printf/strtod use the same calling thread's locale. Retain only ASCII
     * digits, not its possibly multibyte decimal separator, in canonical text. */
    for (const char *p = raw; p < exponent; ++p) {
        if (*p >= '0' && *p <= '9') {
            if (count == sizeof(digits)) { invalid(buffer); return; }
            digits[count++] = *p;
        }
    }
    if (count == 0) { invalid(buffer); return; }
    while (count > 1 && digits[count - 1] == '0') --count;
    if (signbit(value)) tsir_buf_putc(buffer, '-');
    /* Unlike general/scientific formatting, retained Zig {d} never switches
     * to exponent notation. The growable buffer also handles 324 zeroes. */
    if (power >= 0) {
        size_t integer_digits = (size_t)power + 1u;
        for (size_t i = 0; i < integer_digits; ++i)
            tsir_buf_putc(buffer, i < count ? digits[i] : '0');
        if (count > integer_digits) {
            tsir_buf_putc(buffer, '.');
            tsir_buf_putn(buffer, digits + integer_digits, count - integer_digits);
        }
    } else {
        tsir_buf_puts(buffer, "0.");
        for (int i = -1; i > power; --i) tsir_buf_putc(buffer, '0');
        tsir_buf_putn(buffer, digits, count);
    }
}

void tsir_format_f64(TsirBuffer *buffer, double value) {
    if (buffer == NULL || buffer->err != TSIR_OK) return;
    const int saved_errno = errno;
    fenv_t environment;
    if (feholdexcept(&environment) != 0) {
        invalid(buffer);
        errno = saved_errno;
        return;
    }
    if (!isfinite(value)) invalid(buffer);
    else if (fesetround(FE_TONEAREST) == 0) format_finite(buffer, value);
    else invalid(buffer);
    if (fesetenv(&environment) != 0) invalid(buffer);
    errno = saved_errno;
}
