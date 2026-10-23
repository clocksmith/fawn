/* SPDX-License-Identifier: MIT */
#include "doe_error_scopes_zig_bridge.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    size_t stack[DOE_ERROR_SCOPES_STACK_LAYOUT_COUNT];
    size_t entry[DOE_ERROR_SCOPES_ENTRY_LAYOUT_COUNT];
    size_t result[DOE_ERROR_SCOPES_RESULT_LAYOUT_COUNT];
    size_t string[DOE_ERROR_SCOPES_STRING_LAYOUT_COUNT];
    doe_error_scopes_zig_stack_layout(stack);
    doe_error_scopes_zig_entry_layout(entry);
    doe_error_scopes_zig_result_layout(result);
    doe_error_scopes_zig_string_layout(string);
    assert(stack[0] == sizeof(doe_error_scope_stack));
    assert(stack[1] == _Alignof(doe_error_scope_stack));
    assert(stack[0] <= 36u * 1024u);
    assert(entry[0] == sizeof(doe_error_scope_entry));
    assert(result[0] == sizeof(doe_error_scope_result));
    assert(string[0] == sizeof(WGPUStringView));
    printf("stack size=%zu align=%zu entry size=%zu align=%zu result size=%zu StringView size=%zu\n",
           stack[0], stack[1], entry[0], entry[1], result[0], string[0]);
    const char *names[] = {"stack", "entry", "result", "StringView"};
    const size_t *layouts[] = {stack, entry, result, string};
    const size_t counts[] = {9, 6, 4, 4};
    for (size_t record = 0; record < 4; ++record) {
        printf("%s:", names[record]);
        for (size_t field = 0; field < counts[record]; ++field) printf(" %zu", layouts[record][field]);
        puts("");
    }
    return 0;
}
