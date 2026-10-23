/* SPDX-License-Identifier: MIT
 * Postinspection measurement adapter only; no candidate source is changed. */
#include "internal.h"
#include <stdio.h>
int main(void) {
    printf("{\"FunctionRecord\":{\"size\":%zu,\"alignment\":%zu},"
           "\"FormalRecord\":{\"size\":%zu,\"alignment\":%zu},"
           "\"ArgumentSyntax\":{\"size\":%zu,\"alignment\":%zu},"
           "\"CompilerWorkspace\":{\"size\":%zu,\"alignment\":%zu},"
           "\"LegacyWorkspace\":{\"size\":%zu,\"alignment\":%zu},"
           "\"fixed_phase\":{\"tokens\":%zu,\"lowered\":%zu,\"union\":%zu},"
           "\"fixed_arrays\":{\"functions\":%zu,\"formals\":%zu,\"arguments\":%zu,\"callees\":%zu}}\n",
           sizeof(FunctionRecord), _Alignof(FunctionRecord),
           sizeof(FormalRecord), _Alignof(FormalRecord),
           sizeof(ArgumentSyntax), _Alignof(ArgumentSyntax),
           sizeof(CompilerWorkspace), _Alignof(CompilerWorkspace),
           sizeof(LegacyWorkspace), _Alignof(LegacyWorkspace),
           sizeof(((LegacyWorkspace *)0)->phase.tokens),
           sizeof(((LegacyWorkspace *)0)->phase.lowered),
           sizeof(((LegacyWorkspace *)0)->phase),
           sizeof(((LegacyWorkspace *)0)->functions),
           sizeof(((LegacyWorkspace *)0)->formals),
           sizeof(((LegacyWorkspace *)0)->arguments),
           sizeof(((LegacyWorkspace *)0)->callees));
    return !(sizeof(FunctionRecord) <= 80u && _Alignof(FunctionRecord) <= 4u &&
             sizeof(FormalRecord) == 16u && _Alignof(FormalRecord) == 4u &&
             sizeof(ArgumentSyntax) == 8u && _Alignof(ArgumentSyntax) == 4u &&
             sizeof(LegacyWorkspace) <= 122880u);
}
