/* SPDX-License-Identifier: MIT
 * Hostile test-only definitions. A selected native DSO must not call these. */
#include <stddef.h>
#include <stdlib.h>

void doe_futures_zig_queue_layout(size_t *out);
void doe_futures_zig_completion_layout(size_t *out);
void doe_error_scopes_zig_stack_layout(size_t *out);
void doe_error_scopes_zig_entry_layout(size_t *out);
void doe_error_scopes_zig_result_layout(size_t *out);
void doe_error_scopes_zig_string_layout(size_t *out);

void doe_futures_zig_queue_layout(size_t *out) { (void)out; abort(); }
void doe_futures_zig_completion_layout(size_t *out) { (void)out; abort(); }
void doe_error_scopes_zig_stack_layout(size_t *out) { (void)out; abort(); }
void doe_error_scopes_zig_entry_layout(size_t *out) { (void)out; abort(); }
void doe_error_scopes_zig_result_layout(size_t *out) { (void)out; abort(); }
void doe_error_scopes_zig_string_layout(size_t *out) { (void)out; abort(); }
