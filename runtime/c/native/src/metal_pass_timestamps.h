/* SPDX-License-Identifier: MIT */
#ifndef DOE_NATIVE_METAL_PASS_TIMESTAMPS_H
#define DOE_NATIVE_METAL_PASS_TIMESTAMPS_H
#include "metal_frontend.h"

/* Borrowed view of one immutable resource pass, not a query or command owner. */
typedef struct native_metal_timestamp_span {
    size_t first, count, body_first, body_count;
    doe_resource_query_set *queries;
    uint32_t begin, end;
} native_metal_timestamp_span;
native_failure native_metal_next_timestamp_span(const doe_resource_command *records,
    size_t count, size_t *cursor, native_metal_timestamp_span *span);
native_failure native_metal_record_timestamp_span(doe_mtl_commands *commands,
    const doe_resource_command *records, const native_metal_timestamp_span *span, bool *handled);
#endif
