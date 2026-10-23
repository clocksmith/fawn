/* SPDX-License-Identifier: MIT */
#ifndef DOE_COPY_FOOTPRINT_H
#define DOE_COPY_FOOTPRINT_H
#include <stdbool.h>
#include <stdint.h>

/* Required bytes through the last occupied row, excluding trailing padding.
 * Counts and pitches have already been normalized by the caller. This is only
 * arithmetic: no format, zero-extent, pitch, alignment or capacity admission.
 * layers == 0 gives zero; rows == 0 retains any preceding-image stride term.
 * Null output or overflow fails without modifying output. */
static inline bool doe_copy_footprint(uint64_t row_bytes, uint64_t rows,
    uint64_t rows_per_image, uint64_t pitch, uint64_t layers, uint64_t *out)
{
    if (!out) return false;
    uint64_t required = 0;
    if (layers) {
        if (rows_per_image && pitch > UINT64_MAX / rows_per_image) return false;
        uint64_t stride = pitch * rows_per_image;
        if (layers > 1 && stride > UINT64_MAX / (layers - 1)) return false;
        required = stride * (layers - 1);
        if (rows) {
            if (rows > 1 && pitch > UINT64_MAX / (rows - 1)) return false;
            uint64_t preceding_rows = pitch * (rows - 1);
            if (preceding_rows > UINT64_MAX - required) return false;
            required += preceding_rows;
            if (row_bytes > UINT64_MAX - required) return false;
            required += row_bytes;
        }
    }
    *out = required;
    return true;
}
#endif
