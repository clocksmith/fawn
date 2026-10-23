/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "../include/doe_compiler_v2.h"
#include <stdio.h>
#define OFFSET(type, field, value) _Static_assert(offsetof(type, field) == value, #type "." #field)
OFFSET(DoeCompilerWorkspaceRequirements, size_bytes, 0u);
OFFSET(DoeCompilerWorkspaceRequirements, alignment_bytes, 8u);
OFFSET(DoeCompilerMemberV2, name, 0u);
OFFSET(DoeCompilerMemberV2, scalar, 64u);
OFFSET(DoeCompilerMemberV2, offset_bytes, 68u);
OFFSET(DoeCompilerMemberV2, size_bytes, 72u);
OFFSET(DoeCompilerMemberV2, alignment_bytes, 76u);
OFFSET(DoeCompilerStructV2, name, 0u);
OFFSET(DoeCompilerStructV2, member_count, 64u);
OFFSET(DoeCompilerStructV2, size_bytes, 68u);
OFFSET(DoeCompilerStructV2, alignment_bytes, 72u);
OFFSET(DoeCompilerStructV2, members, 76u);
OFFSET(DoeCompilerBindingV2, name, 0u);
OFFSET(DoeCompilerBindingV2, group, 64u);
OFFSET(DoeCompilerBindingV2, binding, 68u);
OFFSET(DoeCompilerBindingV2, kind, 72u);
OFFSET(DoeCompilerBindingV2, access, 76u);
OFFSET(DoeCompilerBindingV2, scalar, 80u);
OFFSET(DoeCompilerBindingV2, element_stride, 84u);
OFFSET(DoeCompilerBindingV2, runtime_sized, 88u);
OFFSET(DoeCompilerBindingV2, minimum_binding_size_bytes, 92u);
OFFSET(DoeCompilerBindingV2, type_alignment_bytes, 96u);
OFFSET(DoeCompilerBindingV2, address_space_alignment_bytes, 100u);
OFFSET(DoeCompilerBindingV2, struct_index, 104u);
OFFSET(DoeCompilerReflectionV2, entry_point, 0u);
OFFSET(DoeCompilerReflectionV2, workgroup_size, 64u);
OFFSET(DoeCompilerReflectionV2, binding_count, 76u);
OFFSET(DoeCompilerReflectionV2, bindings, 80u);
OFFSET(DoeCompilerReflectionV2, struct_count, 944u);
OFFSET(DoeCompilerReflectionV2, structs, 948u);
OFFSET(DoeCompilerResultV2, word_count, 0u);
OFFSET(DoeCompilerResultV2, reflection, 8u);
OFFSET(DoeCompilerResultV2, diagnostic, 6688u);
#undef OFFSET
int main(void) { puts("v2 native field offsets passed (Linux x86-64)"); return 0; }
