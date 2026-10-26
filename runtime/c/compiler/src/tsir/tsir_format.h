/* SPDX-License-Identifier: MIT */
#ifndef DOE_TSIR_FORMAT_H
#define DOE_TSIR_FORMAT_H
#include "tsir_schema.h"
/* Shared retained-Zig {d} formatting for canonical identities and literal
 * emission. Finite values use shortest round-trip digits in plain decimal,
 * including subnormals and signed zero; nonfinite values set TSIR_ERR_INVALID.
 * Appends to the caller's buffer, preserving its first error, errno and fenv. */
void tsir_format_f64(TsirBuffer *buffer, double value);
#endif
