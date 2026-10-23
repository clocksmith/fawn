/* SPDX-License-Identifier: MIT */
#include "doe_error_scopes_zig_bridge.h"
#include <string.h>

void doe_error_scopes_zig_stack_layout(size_t out[DOE_ERROR_SCOPES_STACK_LAYOUT_COUNT])
{
    const size_t layout[] = {
        sizeof(doe_error_scope_stack), _Alignof(doe_error_scope_stack),
        offsetof(doe_error_scope_stack, mutex), offsetof(doe_error_scope_stack, scopes),
        offsetof(doe_error_scope_stack, depth), offsetof(doe_error_scope_stack, callback),
        offsetof(doe_error_scope_stack, userdata1), offsetof(doe_error_scope_stack, userdata2),
        offsetof(doe_error_scope_stack, state)};
    memcpy(out, layout, sizeof(layout));
}

void doe_error_scopes_zig_entry_layout(size_t out[DOE_ERROR_SCOPES_ENTRY_LAYOUT_COUNT])
{
    const size_t layout[] = {
        sizeof(doe_error_scope_entry), _Alignof(doe_error_scope_entry),
        offsetof(doe_error_scope_entry, filter), offsetof(doe_error_scope_entry, error_type),
        offsetof(doe_error_scope_entry, length), offsetof(doe_error_scope_entry, bytes)};
    memcpy(out, layout, sizeof(layout));
}

void doe_error_scopes_zig_result_layout(size_t out[DOE_ERROR_SCOPES_RESULT_LAYOUT_COUNT])
{
    const size_t layout[] = {
        sizeof(doe_error_scope_result), _Alignof(doe_error_scope_result),
        offsetof(doe_error_scope_result, status), offsetof(doe_error_scope_result, system_error)};
    memcpy(out, layout, sizeof(layout));
}

void doe_error_scopes_zig_string_layout(size_t out[DOE_ERROR_SCOPES_STRING_LAYOUT_COUNT])
{
    const size_t layout[] = {
        sizeof(WGPUStringView), _Alignof(WGPUStringView),
        offsetof(WGPUStringView, data), offsetof(WGPUStringView, length)};
    memcpy(out, layout, sizeof(layout));
}

doe_error_scope_result doe_error_scopes_zig_init(doe_error_scope_stack *stack)
{
    doe_error_scope_stack *out = NULL;
    return doe_error_scope_init(DOE_ERROR_SCOPES_ABI_VERSION, stack, sizeof(*stack), &out);
}
