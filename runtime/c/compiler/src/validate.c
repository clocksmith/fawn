/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "internal.h"
#include <string.h>

/* Normative keyword/reserved spelling data, WGSL CRD 2026-09-21 sections 3.7/16.
 * Contextual names such as storage, read_write and f32 are not reserved. */
static const char *const forbidden_names[] = {
    "alias", "break", "case", "const", "const_assert", "continue", "continuing", "default",
    "diagnostic", "discard", "else", "enable", "false", "fn", "for", "if", "let", "loop",
    "override", "requires", "return", "struct", "switch", "true", "var", "while",
    "NULL", "Self", "abstract", "active", "alignas", "alignof", "as", "asm", "asm_fragment",
    "async", "attribute", "auto", "await", "become", "cast", "catch", "class", "co_await",
    "co_return", "co_yield", "coherent", "column_major", "common", "compile", "compile_fragment",
    "concept", "const_cast", "consteval", "constexpr", "constinit", "crate", "debugger",
    "decltype", "delete", "demote", "demote_to_helper", "do", "dynamic_cast", "enum", "explicit",
    "export", "extends", "extern", "external", "fallthrough", "filter", "final", "finally",
    "friend", "from", "fxgroup", "get", "goto", "groupshared", "highp", "impl", "implements",
    "import", "inline", "instanceof", "interface", "layout", "lowp", "macro", "macro_rules",
    "match", "mediump", "meta", "mod", "module", "move", "mut", "mutable", "namespace", "new",
    "nil", "noexcept", "noinline", "nointerpolation", "non_coherent", "noncoherent", "noperspective",
    "null", "nullptr", "of", "operator", "package", "packoffset", "partition", "pass", "patch",
    "pixelfragment", "precise", "precision", "premerge", "priv", "protected", "pub", "public",
    "readonly", "ref", "regardless", "register", "reinterpret_cast", "require", "resource",
    "restrict", "self", "set", "shared", "sizeof", "smooth", "snorm", "static", "static_assert",
    "static_cast", "std", "subroutine", "super", "target", "template", "this", "thread_local",
    "throw", "trait", "try", "type", "typedef", "typeid", "typename", "typeof", "union", "unless",
    "unorm", "unsafe", "unsized", "use", "using", "varying", "virtual", "volatile", "wgsl",
    "where", "with", "writeonly", "yield"
};
static bool valid_name(Source *source, const Name *name) {
    if (strcmp(name->text, "_") == 0 || strncmp(name->text, "__", 2u) == 0)
        return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, name->offset, "reserved WGSL identifier spelling");
    for (size_t i = 0; i < sizeof(forbidden_names) / sizeof(forbidden_names[0]); ++i)
        if (strcmp(name->text, forbidden_names[i]) == 0)
            return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, name->offset, "reserved WGSL keyword or word cannot name a declaration");
    return true;
}
static bool same_name(const Name *a, const Name *b) { return strcmp(a->text, b->text) == 0; }
static bool required_type(const Syntax *s, const Name *name) {
    if (strcmp(name->text, "array") == 0 ||
        strcmp(name->text, s->vector_alias ? "vec3u" : "vec3") == 0) return true;
    bool uses_u32 = s->has_constant || !s->vector_alias;
    bool uses_f32 = false;
    for (size_t i = 0; i < s->storage_count; ++i) {
        uses_u32 = uses_u32 || s->storage[i].scalar == DOE_COMPILER_U32;
        uses_f32 = uses_f32 || s->storage[i].scalar == DOE_COMPILER_F32;
    }
    return (uses_u32 && strcmp(name->text, "u32") == 0) ||
           (uses_f32 && strcmp(name->text, "f32") == 0);
}
static bool module_name(Source *source, const Syntax *s, const Name *name) {
    if (!valid_name(source, name)) return false;
    if (required_type(s, name))
        return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, name->offset,
                            "module declaration shadows a type required by this shader");
    return true;
}
static bool validate_declarations(Source *source, const Syntax *s) {
    if (!module_name(source, s, &s->entry) || !valid_name(source, &s->invocation)) return false;
    if (s->has_constant && !module_name(source, s, &s->constant_name)) return false;
    if (s->has_constant && same_name(&s->constant_name, &s->entry))
        return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, s->entry.offset,
                            "entry point collides with module constant");
    for (size_t i = 0; i < s->storage_count; ++i) {
        const StorageSyntax *binding = &s->storage[i];
        if (!module_name(source, s, &binding->name)) return false;
        if (same_name(&binding->name, &s->entry) ||
            (s->has_constant && same_name(&binding->name, &s->constant_name)))
            return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, binding->name.offset,
                                "storage name collides with another module declaration");
        if (binding->group.value > 3u || binding->binding.value > 31u)
            return source_error(source, DOE_COMPILER_UNSUPPORTED,
                                binding->group.value > 3u ? binding->group.offset : binding->binding.offset,
                                "storage requires group 0..3 and binding 0..31");
        for (size_t j = 0; j < i; ++j) {
            if (same_name(&binding->name, &s->storage[j].name))
                return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, binding->name.offset,
                                    "duplicate storage name at module scope");
            if (binding->group.value == s->storage[j].group.value &&
                binding->binding.value == s->storage[j].binding.value)
                return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, binding->binding.offset,
                                    "duplicate storage group and binding pair");
        }
    }
    if (s->local_size.value < 1u || s->local_size.value > 256u)
        return source_error(source, DOE_COMPILER_UNSUPPORTED, s->local_size.offset,
                            "workgroup size must be between 1 and 256");
    if (s->has_alias) {
        if (!valid_name(source, &s->alias)) return false;
        if (same_name(&s->alias, &s->invocation))
            return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, s->alias.offset,
                                "index alias redeclares the invocation parameter in the same scope");
        if (!same_name(&s->alias_source, &s->invocation))
            return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, s->alias_source.offset,
                                "index alias must read the invocation parameter's x component");
    }
    return true;
}
static bool resolve_reference(Source *source, const Syntax *s, const Reference *reference,
                              uint32_t *binding_index) {
    const Name *name = &reference->storage;
    if (same_name(name, &s->invocation) || (s->has_alias && same_name(name, &s->alias)))
        return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, name->offset,
                            "storage reference resolves to a shadowing local declaration");
    size_t index = 0u;
    while (index < s->storage_count && !same_name(name, &s->storage[index].name)) ++index;
    if (index == s->storage_count)
        return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, name->offset,
                            "storage reference does not resolve to a declared buffer");
    const Index *subscript = &reference->index;
    if (subscript->member_x) {
        if (!same_name(&subscript->name, &s->invocation))
            return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, subscript->name.offset,
                                "member index must be the invocation parameter's x component");
    } else if (!s->has_alias || !same_name(&subscript->name, &s->alias)) {
        return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, subscript->name.offset,
                            "bare index must resolve to the declared invocation-x alias");
    }
    *binding_index = (uint32_t)index;
    return true;
}
static bool lower_operand(Source *source, const Syntax *s, const Operand *operand,
                          uint32_t value, IrInstruction *instruction) {
    *instruction = (IrInstruction){ .result = value, .scalar = DOE_COMPILER_U32 };
    switch (operand->kind) {
        case OPERAND_LOAD:
            if (!resolve_reference(source, s, &operand->reference, &instruction->binding)) return false;
            instruction->opcode = IR_ROBUST_LOAD;
            instruction->scalar = s->storage[instruction->binding].scalar;
            instruction->left = 1u;
            instruction->source_offset = operand->reference.storage.offset;
            return true;
        case OPERAND_LITERAL:
            instruction->opcode = IR_CONSTANT;
            instruction->literal = operand->literal.value;
            instruction->source_offset = operand->literal.offset;
            return true;
        case OPERAND_NAME:
            if (same_name(&operand->name, &s->invocation) ||
                (s->has_alias && same_name(&operand->name, &s->alias)))
                return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, operand->name.offset,
                                    "operand resolves to a local declaration, not the module constant");
            if (!s->has_constant || !same_name(&operand->name, &s->constant_name))
                return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, operand->name.offset,
                                    "operand name must resolve to the declared u32 constant");
            instruction->opcode = IR_CONSTANT;
            instruction->literal = s->constant_value.value;
            instruction->source_offset = operand->name.offset;
            return true;
    }
    return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, source->length, "invalid parsed operand kind");
}

bool validate_syntax(Source *source, const Syntax *s, IrProgram *program) {
    if (!validate_declarations(source, s)) return false;
    uint32_t target;
    if (!resolve_reference(source, s, &s->target, &target)) return false;
    if (s->storage[target].access != DOE_COMPILER_READ_WRITE)
        return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, s->target.storage.offset,
                            "assignment target requires read_write storage access");
    memset(program, 0, sizeof(*program));
    program->entry = s->entry;
    program->invocation = s->invocation;
    program->binding_count = s->storage_count;
    for (size_t i = 0; i < s->storage_count; ++i) {
        const StorageSyntax *binding = &s->storage[i];
        program->bindings[i] = (IrBinding){ .name = binding->name,
            .group = binding->group.value, .binding = binding->binding.value,
            .scalar = binding->scalar, .access = binding->access };
    }
    program->local_size[0] = s->local_size.value;
    program->local_size[1] = 1u;
    program->local_size[2] = 1u;
    program->instruction_count = 5u;
    program->instructions[0] = (IrInstruction){ .opcode = IR_INVOCATION_X,
        .scalar = DOE_COMPILER_U32, .result = 1u, .source_offset = s->invocation.offset };
    if (!lower_operand(source, s, &s->operands[0], 2u, &program->instructions[1]) ||
        !lower_operand(source, s, &s->operands[1], 3u, &program->instructions[2])) return false;
    if (program->instructions[1].opcode != IR_ROBUST_LOAD &&
        program->instructions[2].opcode != IR_ROBUST_LOAD)
        return source_error(source, DOE_COMPILER_UNSUPPORTED, s->target.storage.offset,
                            "addition requires at least one storage load");
    const DoeCompilerScalar scalar = s->storage[target].scalar;
    for (size_t i = 1u; i <= 2u; ++i) {
        if (program->instructions[i].scalar != scalar)
            return source_error(source, DOE_COMPILER_SEMANTIC_ERROR,
                                program->instructions[i].source_offset,
                                "addition operands and destination must have the same scalar type");
    }
    program->instructions[3] = (IrInstruction){ .opcode = IR_ADD, .scalar = scalar,
        .result = 4u, .left = 2u, .right = 3u, .source_offset = s->target.storage.offset };
    program->instructions[4] = (IrInstruction){ .opcode = IR_ROBUST_STORE, .scalar = scalar,
        .left = 1u, .right = 4u, .binding = target, .source_offset = s->target.storage.offset };
    return true;
}

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
