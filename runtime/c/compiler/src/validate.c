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
    if (strcmp(name->text, s->vector_alias ? "vec3u" : "vec3") == 0) return true;
    bool uses_u32 = s->has_constant || !s->vector_alias;
    bool uses_f32 = false, uses_array = false;
    for (size_t i = 0; i < s->resource_count; ++i) {
        uses_array = uses_array || s->resources[i].kind == DOE_COMPILER_STORAGE_BUFFER;
        uses_u32 = uses_u32 || s->resources[i].scalar == DOE_COMPILER_U32;
        uses_f32 = uses_f32 || s->resources[i].scalar == DOE_COMPILER_F32;
    }
    for (size_t i = 0; i < s->struct_count; ++i)
        for (size_t j = 0; j < s->structs[i].member_count; ++j) {
            uses_u32 = uses_u32 || s->structs[i].members[j].scalar == DOE_COMPILER_U32;
            uses_f32 = uses_f32 || s->structs[i].members[j].scalar == DOE_COMPILER_F32;
        }
    return (uses_array && strcmp(name->text, "array") == 0) ||
           (uses_u32 && strcmp(name->text, "u32") == 0) ||
           (uses_f32 && strcmp(name->text, "f32") == 0);
}
static size_t find_struct(const Syntax *s, const Name *name) {
    for (size_t i = 0; i < s->struct_count; ++i)
        if (same_name(name, &s->structs[i].name)) return i;
    return s->struct_count;
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
    for (size_t i = 0; i < s->resource_count; ++i) {
        const ResourceSyntax *binding = &s->resources[i];
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
            if (same_name(&binding->name, &s->resources[j].name))
                return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, binding->name.offset,
                                    "duplicate storage name at module scope");
            if (binding->group.value == s->resources[j].group.value &&
                binding->binding.value == s->resources[j].binding.value)
                return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, binding->binding.offset,
                                    "duplicate storage group and binding pair");
        }
    }
    for (size_t i = 0; i < s->struct_count; ++i) {
        const StructSyntax *type = &s->structs[i];
        if (!module_name(source, s, &type->name)) return false;
        if (same_name(&type->name, &s->entry) ||
            (s->has_constant && same_name(&type->name, &s->constant_name)))
            return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, type->name.offset,
                                "struct name collides with a module declaration");
        for (size_t j = 0; j < s->resource_count; ++j)
            if (same_name(&type->name, &s->resources[j].name))
                return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, type->name.offset,
                                    "struct and resource names collide");
        for (size_t j = 0; j < i; ++j)
            if (same_name(&type->name, &s->structs[j].name))
                return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, type->name.offset,
                                    "duplicate module struct name");
        for (size_t j = 0; j < type->member_count; ++j) {
            if (!valid_name(source, &type->members[j].name)) return false;
            for (size_t k = 0; k < j; ++k)
                if (same_name(&type->members[j].name, &type->members[k].name))
                    return source_error(source, DOE_COMPILER_SEMANTIC_ERROR,
                                        type->members[j].name.offset, "duplicate struct member name");
        }
    }
    for (size_t i = 0; i < s->resource_count; ++i)
        if (s->resources[i].kind == DOE_COMPILER_UNIFORM_BUFFER &&
            find_struct(s, &s->resources[i].type_name) == s->struct_count)
            return source_error(source, DOE_COMPILER_SEMANTIC_ERROR,
                                s->resources[i].type_name.offset,
                                "uniform type must resolve to a declared flat struct");
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
    while (index < s->resource_count && !same_name(name, &s->resources[index].name)) ++index;
    if (index == s->resource_count)
        return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, name->offset,
                            "storage reference does not resolve to a declared buffer");
    if (s->resources[index].kind != DOE_COMPILER_STORAGE_BUFFER)
        return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, name->offset,
                            "indexed reference requires a storage array, not a uniform struct");
    *binding_index = (uint32_t)index;
    return true;
}
static IrType value_type(DoeCompilerScalar scalar) {
    return scalar == DOE_COMPILER_F32 ? IR_F32 : IR_U32;
}
static bool append(Source *source, IrProgram *program, IrInstruction instruction,
                   IrValue *value) {
    if (program->instruction_count == MAX_IR_INSTRUCTIONS)
        return source_error(source, DOE_COMPILER_INPUT_ERROR, instruction.source_offset,
                            "internal typed instruction capacity exceeded");
    if (value != NULL) {
        instruction.result = (uint32_t)program->instruction_count + 1u;
        *value = (IrValue){ .id = instruction.result, .type = instruction.type,
                            .source_offset = instruction.source_offset };
    }
    program->instructions[program->instruction_count++] = instruction;
    return true;
}
static bool literal_value(Source *source, IrProgram *program, uint32_t literal,
                          size_t offset, IrValue *value) {
    return append(source, program, (IrInstruction){ .opcode = IR_CONSTANT,
        .type = IR_U32, .literal = literal, .source_offset = offset }, value);
}
static bool constant_value(Source *source, const Syntax *s, const Name *name,
                           IrProgram *program, IrValue *value) {
    if (same_name(name, &s->invocation) || (s->has_alias && same_name(name, &s->alias)))
        return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, name->offset,
                            "operand resolves to a local declaration, not the module constant");
    if (!s->has_constant || !same_name(name, &s->constant_name))
        return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, name->offset,
                            "operand name must resolve to the declared u32 constant");
    return literal_value(source, program, s->constant_value.value, name->offset, value);
}
typedef struct { Name name; IrValue value; } LocalValue;
typedef struct { LocalValue values[MAX_STATEMENTS]; size_t count; } LocalScope;
typedef struct { IrValue value; bool runtime; } ExpressionValue;

static const IrValue *find_local(const LocalScope *scope, const Name *name) {
    for (size_t i = scope->count; i != 0u; --i)
        if (same_name(&scope->values[i - 1u].name, name)) return &scope->values[i - 1u].value;
    return NULL;
}
static bool user_declared(const Syntax *s, const LocalScope *scope, const Name *name) {
    if (same_name(name, &s->entry) || same_name(name, &s->invocation) ||
        (s->has_alias && same_name(name, &s->alias)) ||
        (s->has_constant && same_name(name, &s->constant_name)) ||
        find_local(scope, name) != NULL) return true;
    for (size_t i = 0u; i < s->resource_count; ++i)
        if (same_name(name, &s->resources[i].name)) return true;
    for (size_t i = 0u; i < s->struct_count; ++i)
        if (same_name(name, &s->structs[i].name)) return true;
    return false;
}
static bool lower_member(Source *source, const Syntax *s, const MemberSyntax *member,
                         bool allow_invocation, const LocalScope *scope,
                         IrProgram *program, IrValue *value) {
    const Name *object = &member->object;
    if (same_name(object, &s->invocation)) {
        if (!allow_invocation || strcmp(member->member.text, "x") != 0)
            return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, member->member.offset,
                                "only invocation x is admitted in this operand");
        *value = (IrValue){ .id = 1u, .type = IR_U32, .source_offset = object->offset };
        return true;
    }
    if ((s->has_alias && same_name(object, &s->alias)) || find_local(scope, object) != NULL)
        return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, object->offset,
                            "scalar local has no members");
    size_t binding = 0u;
    while (binding < s->resource_count && !same_name(object, &s->resources[binding].name)) ++binding;
    if (binding == s->resource_count || s->resources[binding].kind != DOE_COMPILER_UNIFORM_BUFFER)
        return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, object->offset,
                            "member object must resolve to a uniform resource");
    const IrStruct *type = &program->structs[program->bindings[binding].struct_index];
    size_t field = 0u;
    while (field < type->member_count && !same_name(&member->member, &type->members[field].name)) ++field;
    if (field == type->member_count)
        return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, member->member.offset,
                            "unknown uniform member");
    return append(source, program, (IrInstruction){ .opcode = IR_UNIFORM_LOAD,
        .type = value_type(type->members[field].scalar), .binding = (uint32_t)binding,
        .literal = (uint32_t)field, .source_offset = object->offset }, value);
}
static bool lower_atom(Source *source, const Syntax *s, const Atom *atom,
                       bool allow_invocation, const LocalScope *scope, IrProgram *program, IrValue *value) {
    switch (atom->kind) {
        case ATOM_LITERAL:
            return literal_value(source, program, atom->value.literal.value,
                                 atom->value.literal.offset, value);
        case ATOM_MEMBER:
            if (!lower_member(source, s, &atom->value.member, allow_invocation, scope, program, value))
                return false;
            if (value->type != IR_U32)
                return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, value->source_offset,
                                    "index and condition members require u32 type");
            return true;
        case ATOM_NAME:
            if (allow_invocation && s->has_alias && same_name(&atom->value.name, &s->alias)) {
                *value = (IrValue){ .id = 1u, .type = IR_U32, .source_offset = atom->value.name.offset };
                return true;
            }
            if (allow_invocation) {
                const IrValue *local = find_local(scope, &atom->value.name);
                if (local != NULL) {
                    if (local->type != IR_U32)
                        return source_error(source, DOE_COMPILER_SEMANTIC_ERROR,
                                            atom->value.name.offset, "index local must have u32 type");
                    *value = *local;
                    value->source_offset = atom->value.name.offset;
                    return true;
                }
            }
            return constant_value(source, s, &atom->value.name, program, value);
    }
    return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, source->length, "invalid parsed atom kind");
}
static bool lower_index(Source *source, const Syntax *s, const Index *index,
                        const LocalScope *scope, IrProgram *program, IrValue *value) {
    IrValue base, offset;
    if (!lower_atom(source, s, &index->base, true, scope, program, &base)) return false;
    switch (index->operation) {
        case INDEX_BASE: *value = base; return true;
        case INDEX_ADD:
        case INDEX_SUBTRACT:
            if (!lower_atom(source, s, &index->offset, false, scope, program, &offset)) return false;
            return append(source, program, (IrInstruction){
                .opcode = index->operation == INDEX_ADD ? IR_ADD : IR_SUBTRACT,
                .type = IR_U32, .left = base.id, .right = offset.id,
                .source_offset = base.source_offset }, value);
    }
    return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, source->length, "invalid parsed index operation");
}
static bool lower_expression(Source *source, const Syntax *s, uint32_t id,
                             const LocalScope *scope, IrProgram *program,
                             ExpressionValue *out) {
    if (id >= s->expression_count)
        return source_error(source, DOE_COMPILER_INPUT_ERROR, source->length,
                            "invalid internal expression identity");
    const Expression *node = &s->expressions[id];
    out->runtime = false;
    switch (node->kind) {
        case EXPRESSION_LOAD: {
            const Reference *reference = &node->value.reference;
            uint32_t binding;
            IrValue index;
            if (!resolve_reference(source, s, reference, &binding) ||
                !lower_index(source, s, &reference->index, scope, program, &index)) return false;
            out->runtime = true;
            return append(source, program, (IrInstruction){ .opcode = IR_ROBUST_LOAD,
                .type = value_type(s->resources[binding].scalar), .binding = binding,
                .left = index.id, .source_offset = reference->storage.offset }, &out->value);
        }
        case EXPRESSION_INTEGER:
            return literal_value(source, program, node->value.integer.value,
                                 node->source_offset, &out->value);
        case EXPRESSION_DECIMAL: {
            uint32_t bits;
            if (!decimal_binary32(source, &node->value.decimal, &bits)) return false;
            return append(source, program, (IrInstruction){ .opcode = IR_CONSTANT,
                .type = IR_F32, .literal = bits, .source_offset = node->source_offset }, &out->value);
        }
        case EXPRESSION_MEMBER:
            out->runtime = true;
            return lower_member(source, s, &node->value.member, true, scope, program, &out->value);
        case EXPRESSION_NAME: {
            const IrValue *local = find_local(scope, &node->value.name);
            if (local != NULL) {
                out->value = *local;
                out->value.source_offset = node->source_offset;
                out->runtime = true;
                return true;
            }
            if (s->has_alias && same_name(&node->value.name, &s->alias)) {
                out->runtime = true;
                out->value = (IrValue){ .id = 1u, .type = IR_U32, .source_offset = node->source_offset };
                return true;
            }
            return constant_value(source, s, &node->value.name, program, &out->value);
        }
        case EXPRESSION_MAX:
            if (user_declared(s, scope, &node->value.name))
                return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, node->source_offset,
                                    "max resolves to a user declaration, not the builtin");
            break;
        case EXPRESSION_ADD:
        case EXPRESSION_SUBTRACT:
        case EXPRESSION_MULTIPLY:
            break;
    }
    ExpressionValue left = {0}, right = {0};
    if (!lower_expression(source, s, node->left, scope, program, &left) ||
        !lower_expression(source, s, node->right, scope, program, &right)) return false;
    if (left.value.type != right.value.type)
        return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, node->source_offset,
                            "scalar expression operands must have the same type");
    if (left.value.type == IR_F32 && !left.runtime && !right.runtime)
        return source_error(source, DOE_COMPILER_UNSUPPORTED, node->source_offset,
                            "constant-only f32 arithmetic and max are not supported");
    out->runtime = left.runtime || right.runtime;
    const IrOpcode opcode = node->kind == EXPRESSION_ADD ? IR_ADD :
                            node->kind == EXPRESSION_SUBTRACT ? IR_SUBTRACT :
                            node->kind == EXPRESSION_MULTIPLY ? IR_MULTIPLY : IR_MAX;
    return append(source, program, (IrInstruction){ .opcode = opcode,
        .type = left.value.type, .left = left.value.id, .right = right.value.id,
        .source_offset = node->source_offset }, &out->value);
}
static bool lower_assignment(Source *source, const Syntax *s, const Assignment *assignment,
                             const LocalScope *scope, IrProgram *program) {
    uint32_t binding;
    if (!resolve_reference(source, s, &assignment->target, &binding)) return false;
    if (s->resources[binding].access != DOE_COMPILER_READ_WRITE)
        return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, assignment->target.storage.offset,
                            "assignment target requires read_write storage access");
    const IrType type = value_type(s->resources[binding].scalar);
    IrValue index;
    ExpressionValue result = {0};
    if (!lower_index(source, s, &assignment->target.index, scope, program, &index) ||
        !lower_expression(source, s, assignment->expression, scope, program, &result)) return false;
    if (result.value.type != type)
        return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, result.value.source_offset,
                            "assignment value and destination must have the same scalar type");
    return append(source, program, (IrInstruction){ .opcode = IR_ROBUST_STORE,
        .type = type, .left = index.id, .right = result.value.id, .binding = binding,
        .source_offset = assignment->target.storage.offset }, NULL);
}
static bool lower_local(Source *source, const Syntax *s, const LocalSyntax *local,
                        LocalScope *scope, IrProgram *program) {
    if (!valid_name(source, &local->name)) return false;
    if (user_declared(s, scope, &local->name))
        return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, local->name.offset,
                            "local declaration duplicates or shadows a visible user declaration");
    if (local->annotated && user_declared(s, scope, &local->annotation))
        return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, local->annotation.offset,
                            "local annotation resolves to a user value, not a type");
    ExpressionValue result = {0};
    if (!lower_expression(source, s, local->expression, scope, program, &result)) return false;
    if (local->annotated && value_type(local->scalar) != result.value.type)
        return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, local->annotation.offset,
                            "local annotation and initializer have incompatible scalar types");
    if (scope->count == MAX_STATEMENTS)
        return source_error(source, DOE_COMPILER_INPUT_ERROR, local->name.offset,
                            "internal local capacity exceeded");
    /* Store the already evaluated value, never an expression to reevaluate. */
    scope->values[scope->count++] = (LocalValue){ .name = local->name, .value = result.value };
    return true;
}
static bool lower_block(Source *source, const Syntax *s, size_t start, size_t end,
                        LocalScope *scope, IrProgram *program) {
    const size_t outer_count = scope->count;
    for (size_t i = start; i < end;) {
        const Statement *statement = &s->statements[i];
        switch (statement->kind) {
            case STATEMENT_ASSIGN:
                if (!lower_assignment(source, s, &statement->value.assignment, scope, program)) return false;
                ++i;
                break;
            case STATEMENT_LET:
                if (!lower_local(source, s, &statement->value.local, scope, program)) return false;
                ++i;
                break;
            case STATEMENT_RETURN:
                if (!append(source, program, (IrInstruction){ .opcode = IR_RETURN,
                    .source_offset = statement->source_offset }, NULL)) return false;
                ++i;
                break;
            case STATEMENT_IF: {
                const Condition *condition = &statement->value.condition;
                IrValue left, right, test = {0};
                if (!lower_index(source, s, &condition->left, scope, program, &left) ||
                    !lower_atom(source, s, &condition->right, false, scope, program, &right) ||
                    !append(source, program, (IrInstruction){ .opcode = IR_COMPARE,
                        .type = IR_BOOL, .comparison = condition->comparison,
                        .left = left.id, .right = right.id,
                        .source_offset = statement->source_offset }, &test) ||
                    !append(source, program, (IrInstruction){ .opcode = IR_IF,
                        .left = test.id, .source_offset = statement->source_offset }, NULL) ||
                    !lower_block(source, s, i + 1u, statement->else_start, scope, program) ||
                    !append(source, program, (IrInstruction){ .opcode = IR_ELSE,
                        .source_offset = statement->source_offset }, NULL) ||
                    !lower_block(source, s, statement->else_start, statement->end, scope, program) ||
                    !append(source, program, (IrInstruction){ .opcode = IR_END_IF,
                        .source_offset = statement->source_offset }, NULL)) return false;
                i = statement->end;
                break;
            }
        }
    }
    scope->count = outer_count;
    return true;
}

bool validate_syntax(Source *source, const Syntax *s, IrProgram *program) {
    if (!validate_declarations(source, s)) return false;
    memset(program, 0, sizeof(*program));
    program->entry = s->entry;
    program->invocation = s->invocation;
    program->binding_count = s->resource_count;
    program->struct_count = s->struct_count;
    for (size_t i = 0; i < s->struct_count; ++i) {
        const StructSyntax *syntax = &s->structs[i];
        IrStruct *type = &program->structs[i];
        type->name = syntax->name;
        type->member_count = (uint32_t)syntax->member_count;
        type->size_bytes = 4u * type->member_count;
        type->alignment_bytes = 4u;
        for (size_t j = 0; j < syntax->member_count; ++j)
            type->members[j] = (IrMember){ .name = syntax->members[j].name,
                .scalar = syntax->members[j].scalar, .offset_bytes = 4u * (uint32_t)j,
                .size_bytes = 4u, .alignment_bytes = 4u };
    }
    for (size_t i = 0; i < s->resource_count; ++i) {
        const ResourceSyntax *binding = &s->resources[i];
        IrBinding *ir = &program->bindings[i];
        *ir = (IrBinding){ .name = binding->name,
            .group = binding->group.value, .binding = binding->binding.value,
            .scalar = binding->scalar, .access = binding->access, .kind = binding->kind,
            .struct_index = DOE_COMPILER_NO_STRUCT, .type_alignment_bytes = 4u };
        if (binding->kind == DOE_COMPILER_STORAGE_BUFFER) {
            ir->element_stride = 4u;
            ir->runtime_sized = 1u;
            ir->minimum_binding_size_bytes = 4u;
            ir->address_space_alignment_bytes = 4u;
        } else {
            ir->struct_index = (uint32_t)find_struct(s, &binding->type_name);
            ir->minimum_binding_size_bytes = program->structs[ir->struct_index].size_bytes;
            ir->address_space_alignment_bytes = 16u;
        }
    }
    program->local_size[0] = s->local_size.value;
    program->local_size[1] = 1u;
    program->local_size[2] = 1u;
    IrValue invocation;
    if (!append(source, program, (IrInstruction){ .opcode = IR_INVOCATION_X,
        .type = IR_U32, .source_offset = s->invocation.offset }, &invocation)) return false;
    /* Unreachable statements are lowered too: no dead-code validation bypass. */
    LocalScope scope = {0};
    return lower_block(source, s, 0u, s->statement_count, &scope, program);
}
