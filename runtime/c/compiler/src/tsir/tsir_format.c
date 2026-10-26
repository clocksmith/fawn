/* SPDX-License-Identifier: MIT
 * Shortest round-trip digits shared by TSIR identity and kernel emission. */
#include "tsir_format.h"
#include "../decimal.h"

void tsir_format_f64(TsirBuffer *buffer, double value) {
    if (buffer == NULL || buffer->err != TSIR_OK) return;
    const int saved_errno = errno;
    char text[384];
    size_t length = 0u;
    const DoeDecimalStatus status = doe_decimal_f64(value, text, sizeof(text), &length);
    if (status == DOE_DECIMAL_OK) tsir_buf_putn(buffer, text, length);
    else buffer->err = TSIR_ERR_INVALID;
    errno = saved_errno;
}
