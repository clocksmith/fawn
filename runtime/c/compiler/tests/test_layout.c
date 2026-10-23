/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "../include/doe_compiler.h"
#include <stdio.h>

/* This is the explicitly exercised ABI, not a claim about 32-bit or Windows. */
#if defined(__linux__) && defined(__x86_64__)
_Static_assert(sizeof(size_t) == 8u, "Linux x86-64 size_t width changed");
_Static_assert(sizeof(DoeCompilerStatus) == 4u, "status enum ABI width changed");
_Static_assert(sizeof(DoeCompilerScalar) == 4u, "scalar enum ABI width changed");
_Static_assert(sizeof(DoeCompilerAccess) == 4u, "access enum ABI width changed");
_Static_assert(sizeof(DoeCompilerBinding) == 88u, "binding ABI size changed");
_Static_assert(offsetof(DoeCompilerBinding, name) == 0u, "binding name offset changed");
_Static_assert(offsetof(DoeCompilerBinding, group) == 64u, "binding group offset changed");
_Static_assert(offsetof(DoeCompilerBinding, binding) == 68u, "binding number offset changed");
_Static_assert(offsetof(DoeCompilerBinding, scalar) == 72u, "binding scalar offset changed");
_Static_assert(offsetof(DoeCompilerBinding, access) == 76u, "binding access offset changed");
_Static_assert(offsetof(DoeCompilerBinding, element_stride) == 80u, "binding stride offset changed");
_Static_assert(offsetof(DoeCompilerBinding, runtime_sized) == 84u, "binding runtime flag offset changed");
_Static_assert(sizeof(DoeCompilerReflection) == 784u, "reflection ABI size changed");
_Static_assert(offsetof(DoeCompilerReflection, workgroup_size) == 64u, "workgroup offset changed");
_Static_assert(offsetof(DoeCompilerReflection, binding_count) == 76u, "binding count offset changed");
_Static_assert(offsetof(DoeCompilerReflection, bindings) == 80u, "bindings offset changed");
_Static_assert(sizeof(DoeCompilerDiagnostic) == 216u, "diagnostic ABI size changed");
_Static_assert(offsetof(DoeCompilerDiagnostic, status) == 0u, "diagnostic status offset changed");
_Static_assert(offsetof(DoeCompilerDiagnostic, offset) == 8u, "diagnostic byte offset changed");
_Static_assert(offsetof(DoeCompilerDiagnostic, line) == 16u, "diagnostic line offset changed");
_Static_assert(offsetof(DoeCompilerDiagnostic, column) == 20u, "diagnostic column offset changed");
_Static_assert(offsetof(DoeCompilerDiagnostic, message) == 24u, "diagnostic message offset changed");
_Static_assert(sizeof(DoeCompilerResult) == 1008u, "result ABI size changed");
_Static_assert(offsetof(DoeCompilerResult, word_count) == 0u, "word count offset changed");
_Static_assert(offsetof(DoeCompilerResult, reflection) == 8u, "reflection offset changed");
_Static_assert(offsetof(DoeCompilerResult, diagnostic) == 792u, "diagnostic offset changed");
#endif

int main(void) {
#if defined(__linux__) && defined(__x86_64__)
    puts("ABI layout: Linux x86-64 sizes, enum widths and field offsets passed");
#else
    puts("ABI layout: Linux x86-64 assertions not applicable on this target");
#endif
    return 0;
}
