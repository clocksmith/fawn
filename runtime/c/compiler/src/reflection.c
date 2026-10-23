/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "internal.h"
#include <string.h>

void reflect_program(const IrProgram *program, DoeCompilerReflection *reflection) {
    memset(reflection, 0, sizeof(*reflection));
    memcpy(reflection->entry_point, program->entry.text, sizeof(reflection->entry_point));
    memcpy(reflection->workgroup_size, program->local_size, sizeof(reflection->workgroup_size));
    reflection->binding_count = (uint32_t)program->binding_count;
    for (size_t i = 0; i < program->binding_count; ++i) {
        DoeCompilerBinding *binding = &reflection->bindings[i];
        const IrBinding *ir = &program->bindings[i];
        memcpy(binding->name, ir->name.text, sizeof(binding->name));
        binding->group = ir->group;
        binding->binding = ir->binding;
        binding->scalar = ir->scalar;
        binding->access = ir->access;
        binding->element_stride = 4u;
        binding->runtime_sized = 1u;
    }
}

void reflect_program_v2(const IrProgram *program, DoeCompilerReflectionV2 *reflection) {
    memset(reflection, 0, sizeof(*reflection));
    memcpy(reflection->entry_point, program->entry.text, sizeof(reflection->entry_point));
    memcpy(reflection->workgroup_size, program->local_size, sizeof(reflection->workgroup_size));
    reflection->binding_count = (uint32_t)program->binding_count;
    reflection->struct_count = (uint32_t)program->struct_count;
    for (size_t i = 0; i < program->binding_count; ++i) {
        DoeCompilerBindingV2 *binding = &reflection->bindings[i];
        const IrBinding *ir = &program->bindings[i];
        memcpy(binding->name, ir->name.text, sizeof(binding->name));
        binding->group = ir->group;
        binding->binding = ir->binding;
        binding->kind = ir->kind;
        binding->access = (uint32_t)ir->access;
        binding->scalar = (uint32_t)ir->scalar;
        binding->element_stride = ir->element_stride;
        binding->runtime_sized = ir->runtime_sized;
        binding->minimum_binding_size_bytes = ir->minimum_binding_size_bytes;
        binding->type_alignment_bytes = ir->type_alignment_bytes;
        binding->address_space_alignment_bytes = ir->address_space_alignment_bytes;
        binding->struct_index = ir->struct_index;
    }
    for (size_t i = 0; i < program->struct_count; ++i) {
        DoeCompilerStructV2 *type = &reflection->structs[i];
        const IrStruct *ir = &program->structs[i];
        memcpy(type->name, ir->name.text, sizeof(type->name));
        type->member_count = ir->member_count;
        type->size_bytes = ir->size_bytes;
        type->alignment_bytes = ir->alignment_bytes;
        for (size_t j = 0; j < ir->member_count; ++j) {
            DoeCompilerMemberV2 *member = &type->members[j];
            const IrMember *field = &ir->members[j];
            memcpy(member->name, field->name.text, sizeof(member->name));
            member->scalar = (uint32_t)field->scalar;
            member->offset_bytes = field->offset_bytes;
            member->size_bytes = field->size_bytes;
            member->alignment_bytes = field->alignment_bytes;
        }
    }
}
