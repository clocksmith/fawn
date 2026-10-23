/* SPDX-License-Identifier: MIT
 * Private build-bound storage bridge, not a stable public ABI. */
#ifndef DOE_ERROR_SCOPES_ZIG_BRIDGE_H
#define DOE_ERROR_SCOPES_ZIG_BRIDGE_H
#include "../src/internal.h"
#ifdef __cplusplus
extern "C" {
#endif

/* Size, alignment, then every field offset in declaration order. */
#define DOE_ERROR_SCOPES_STACK_LAYOUT_COUNT 9
#define DOE_ERROR_SCOPES_ENTRY_LAYOUT_COUNT 6
#define DOE_ERROR_SCOPES_RESULT_LAYOUT_COUNT 4
#define DOE_ERROR_SCOPES_STRING_LAYOUT_COUNT 4
void doe_error_scopes_zig_stack_layout(size_t out[DOE_ERROR_SCOPES_STACK_LAYOUT_COUNT]);
void doe_error_scopes_zig_entry_layout(size_t out[DOE_ERROR_SCOPES_ENTRY_LAYOUT_COUNT]);
void doe_error_scopes_zig_result_layout(size_t out[DOE_ERROR_SCOPES_RESULT_LAYOUT_COUNT]);
void doe_error_scopes_zig_string_layout(size_t out[DOE_ERROR_SCOPES_STRING_LAYOUT_COUNT]);
doe_error_scope_result doe_error_scopes_zig_init(doe_error_scope_stack *stack);

#ifdef __cplusplus
}
#endif
#endif
