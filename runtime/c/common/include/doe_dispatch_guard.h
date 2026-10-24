/* SPDX-License-Identifier: MIT */
#ifndef DOE_DISPATCH_GUARD_H
#define DOE_DISPATCH_GUARD_H
#include <stddef.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

enum { DOE_DISPATCH_GUARD_SOURCE_CAPACITY = 512 };
typedef enum doe_dispatch_guard_source_status {
    DOE_DISPATCH_GUARD_SOURCE_OK,
    DOE_DISPATCH_GUARD_SOURCE_INVALID_ARGUMENT,
    DOE_DISPATCH_GUARD_SOURCE_TOO_SMALL,
    DOE_DISPATCH_GUARD_SOURCE_FORMAT_ERROR
} doe_dispatch_guard_source_status;

/* One shared backend guard description, not shader interpretation. The backend
 * validates limits/hardware, copies exactly three GPU words into private scratch,
 * executes the compiled source and owns all barriers/leases. The compiler owns
 * WGSL meaning and native reflection. Outputs remain untouched on any failure;
 * output and length storage must be live and disjoint. */
static inline doe_dispatch_guard_source_status doe_dispatch_guard_source(
    const uint32_t limits[3], char *out, size_t capacity, size_t *out_length) {
    if (!limits || !limits[0] || !limits[1] || !limits[2] || !out || !out_length)
        return DOE_DISPATCH_GUARD_SOURCE_INVALID_ARGUMENT;
    char source[DOE_DISPATCH_GUARD_SOURCE_CAPACITY];
    int length = snprintf(source, sizeof(source),
        "@group(0) @binding(0) var<storage, read_write> counts: array<u32>;\n"
        "@compute @workgroup_size(1) fn main() {\n"
        "    if (counts[0] > %" PRIu32 "u || counts[1] > %" PRIu32 "u || counts[2] > %" PRIu32 "u) {\n"
        "        counts[0] = 0u;\n        counts[1] = 0u;\n        counts[2] = 0u;\n    }\n}\n",
        limits[0], limits[1], limits[2]);
    if (length < 0 || (size_t)length >= sizeof(source)) return DOE_DISPATCH_GUARD_SOURCE_FORMAT_ERROR;
    if ((size_t)length >= capacity) return DOE_DISPATCH_GUARD_SOURCE_TOO_SMALL;
    memcpy(out, source, (size_t)length + 1);
    *out_length = (size_t)length;
    return DOE_DISPATCH_GUARD_SOURCE_OK;
}
#endif
