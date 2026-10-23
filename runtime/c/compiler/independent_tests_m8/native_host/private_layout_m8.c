/* SPDX-License-Identifier: MIT
 * Private measurement is evidence, never a new public layout guarantee. */
#include "internal.h"
#include <stdio.h>
#define TYPE(t) printf("\"" #t "\":{\"size\":%zu,\"alignment\":%zu},",sizeof(t),_Alignof(t))
int main(void) {
    printf("{"); TYPE(CompilerLimits); TYPE(Source); TYPE(Token); TYPE(NameSpan);
    TYPE(Expression); TYPE(Statement); TYPE(Syntax); TYPE(IrInstruction); TYPE(IrLocal);
    TYPE(LocalBinding); TYPE(IrProgram); TYPE(Spirv); TYPE(CompilerWorkspace);
    printf("\"LegacyWorkspace\":{\"size\":%zu,\"alignment\":%zu}}\n",sizeof(LegacyWorkspace),_Alignof(LegacyWorkspace));
    return sizeof(LegacyWorkspace)>77760u;
}
