/* SPDX-License-Identifier: MIT */
#include "metal_pass_timestamps.h"
#include "metal_graphics_internal.h"

static bool boundary(const doe_resource_command *record, uint32_t position,
    size_t first, size_t count)
{
    return record->kind == DOE_COMMAND_WRITE_TIMESTAMP &&
        record->value.timestamp.position == position &&
        record->value.timestamp.pass_first == first && record->value.timestamp.pass_count == count;
}

native_failure native_metal_next_timestamp_span(const doe_resource_command *records,
    size_t count, size_t *cursor, native_metal_timestamp_span *span)
{
    *span = (native_metal_timestamp_span){0};
    const size_t lower = *cursor;
    while (*cursor < count) {
        const size_t marker = (*cursor)++;
        const doe_resource_command *record = &records[marker];
        if (record->kind != DOE_COMMAND_WRITE_TIMESTAMP || !record->value.timestamp.pass_count) continue;
        const size_t first = record->value.timestamp.pass_first, length = record->value.timestamp.pass_count;
        if (first < lower || first > marker || first > count || length > count - first || marker - first >= length)
            return native_failure_status(NATIVE_INVALID_ARGUMENT);
        const bool begin = boundary(&records[first], DOE_TIMESTAMP_PASS_BEGIN, first, length);
        const bool end = boundary(&records[first + length - 1], DOE_TIMESTAMP_PASS_END, first, length);
        if ((!begin && !end) || length < (size_t)begin + (size_t)end ||
            !record->value.timestamp.set ||
            (!begin && records[first].kind == DOE_COMMAND_WRITE_TIMESTAMP &&
                records[first].value.timestamp.position == DOE_TIMESTAMP_PASS_BEGIN) ||
            (!end && records[first + length - 1].kind == DOE_COMMAND_WRITE_TIMESTAMP &&
                records[first + length - 1].value.timestamp.position == DOE_TIMESTAMP_PASS_END) ||
            ((marker != first || !begin) && (marker != first + length - 1 || !end)) ||
            (begin && records[first].value.timestamp.index == UINT32_MAX) ||
            (end && records[first + length - 1].value.timestamp.index == UINT32_MAX) ||
            (begin && end && (records[first].value.timestamp.set != records[first + length - 1].value.timestamp.set ||
                records[first].value.timestamp.index == records[first + length - 1].value.timestamp.index)))
            return native_failure_status(NATIVE_INVALID_ARGUMENT);
        *span = (native_metal_timestamp_span){.first=first,.count=length,
            .body_first=first + (size_t)begin,.body_count=length - (size_t)begin - (size_t)end,
            .queries=record->value.timestamp.set,
            .begin=begin ? records[first].value.timestamp.index : UINT32_MAX,
            .end=end ? records[first + length - 1].value.timestamp.index : UINT32_MAX};
        *cursor = first + length;
        return native_failure_status(NATIVE_OK);
    }
    return native_failure_status(NATIVE_OK);
}

static bool direct_compute(const doe_resource_command *records, size_t count)
{
    if (!count) return false;
    for (size_t i = 0; i < count; ++i)
        if (records[i].kind != DOE_COMMAND_DISPATCH || !records[i].value.dispatch.x ||
            !records[i].value.dispatch.y || !records[i].value.dispatch.z) return false;
    return true;
}

static bool direct_render(const doe_resource_command *records, size_t count)
{
    bool work = false;
    if (count < 2 || records[0].kind != DOE_COMMAND_RENDER || records[count - 1].kind != DOE_COMMAND_RENDER)
        return false;
    for (size_t i = 0; i < count; ++i) {
        if (records[i].kind == DOE_COMMAND_BEGIN_OCCLUSION || records[i].kind == DOE_COMMAND_END_OCCLUSION) continue;
        if (records[i].kind != DOE_COMMAND_RENDER) return false;
        for (size_t j = 0; j < doe_resource_render_record_draw_count(records[i].value.render); ++j) {
            doe_resource_render_packet packet;
            if (!doe_resource_render_record_draw(records[i].value.render, j, &packet) ||
                packet.indirect || packet.indexed) return false;
            work |= packet.count != 0 && packet.instances != 0;
        }
    }
    return work;
}

native_failure native_metal_record_timestamp_span(doe_mtl_commands *commands,
    const doe_resource_command *records, const native_metal_timestamp_span *span, bool *handled)
{
    *handled = false;
    const doe_resource_command *body = records + span->body_first;
    const bool compute = direct_compute(body, span->body_count);
    if (!compute && !direct_render(body, span->body_count)) return native_failure_status(NATIVE_OK);
    native_queries *queries = doe_resource_query_set_native(span->queries);
    if (!queries || !queries->metal_timestamps) return native_failure_status(NATIVE_INVALID_ARGUMENT);
    const doe_mtl_pass_timestamps writes = {queries->metal_timestamps,span->begin,span->end};
    bool attach = false;
    doe_mtl_error detail = {0};
    native_failure failure = native_failure_metal(
        doe_mtl_commands_prepare_pass_timestamps(commands, &writes, &attach, &detail), &detail);
    if (failure.status != NATIVE_OK || !attach) return failure;
    if (compute) {
        for (size_t i = 0; failure.status == NATIVE_OK && i < span->body_count; ++i) {
            const doe_mtl_pass_timestamps endpoint = {writes.set,
                i == 0 ? writes.begin : UINT32_MAX, i + 1 == span->body_count ? writes.end : UINT32_MAX};
            failure = native_metal_dispatch_timed(commands, &body[i],
                endpoint.begin != UINT32_MAX || endpoint.end != UINT32_MAX ? &endpoint : NULL);
        }
    } else {
        size_t consumed = 0;
        failure = native_metal_render_timed(commands, body, span->body_count, &consumed, &writes);
        if (failure.status == NATIVE_OK && consumed != span->body_count)
            failure = native_failure_status(NATIVE_INVALID_ARGUMENT);
    }
    if (failure.status == NATIVE_OK) *handled = true;
    return failure;
}
