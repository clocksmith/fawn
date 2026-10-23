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
static bool span_name(Source *source, NameSpan span, Name *name) {
    if (span.length == 0u || span.length > DOE_COMPILER_MAX_NAME_BYTES ||
        span.offset > source->length || span.length > source->length - span.offset)
        return source_error(source, DOE_COMPILER_INPUT_ERROR, span.offset,
                            "invalid internal source name span");
    memcpy(name->text, source->source + span.offset, span.length);
    name->text[span.length] = '\0';
    name->offset = span.offset;
    return true;
}
static size_t find_parameter(const Syntax *s, const Name *name) {
    for (size_t i = 0; i < s->parameter_count; ++i)
        if (same_name(name, &s->parameters[i].name)) return i;
    return s->parameter_count;
}
static bool required_type(const Syntax *s, const Name *name) {
    bool uses_u32 = s->has_constant;
    for (size_t i = 0; i < s->parameter_count; ++i) {
        const EntryParameter *parameter = &s->parameters[i];
        if (parameter->kind != INPUT_LOCAL_INDEX &&
            strcmp(name->text, parameter->vector_alias ? "vec3u" : "vec3") == 0) return true;
        uses_u32 = uses_u32 || !parameter->vector_alias;
    }
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
    if (!module_name(source, s, &s->entry)) return false;
    for (size_t i = 0; i < s->parameter_count; ++i) {
        const EntryParameter *parameter = &s->parameters[i];
        if (!valid_name(source, &parameter->name)) return false;
        for (size_t j = 0; j < i; ++j) {
            if (same_name(&parameter->name, &s->parameters[j].name))
                return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, parameter->name.offset,
                                    "duplicate entry parameter name");
            if (parameter->kind == s->parameters[j].kind)
                return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, parameter->name.offset,
                                    "duplicate compute input builtin");
        }
    }
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
    uint32_t workgroup_product = 1u;
    for (size_t i = 0; i < 3u; ++i) {
        const Integer dimension = s->local_size[i];
        if (dimension.value < 1u || dimension.value > 256u ||
            dimension.value > 256u / workgroup_product)
            return source_error(source, DOE_COMPILER_UNSUPPORTED, dimension.offset,
                                "workgroup dimensions and product must be between 1 and 256");
        workgroup_product *= dimension.value;
    }
    if (s->has_alias) {
        if (!valid_name(source, &s->alias)) return false;
        if (find_parameter(s, &s->alias) != s->parameter_count)
            return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, s->alias.offset,
                                "index alias redeclares an entry parameter in the same scope");
        const size_t parameter = find_parameter(s, &s->alias_source);
        if (parameter == s->parameter_count ||
            s->parameters[parameter].kind != INPUT_GLOBAL_INVOCATION)
            return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, s->alias_source.offset,
                                "index alias must read the global invocation parameter's x component");
    }
    return true;
}
static bool resolve_reference(Source *source, const Syntax *s, const Reference *reference,
                              uint32_t *binding_index) {
    Name storage;
    if (!span_name(source, reference->storage, &storage)) return false;
    const Name *name = &storage;
    if (find_parameter(s, name) != s->parameter_count || (s->has_alias && same_name(name, &s->alias)))
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
    if (program->instruction_count == program->instruction_capacity)
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
    if (find_parameter(s, name) != s->parameter_count || (s->has_alias && same_name(name, &s->alias)))
        return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, name->offset,
                            "operand resolves to a local declaration, not the module constant");
    if (!s->has_constant || !same_name(name, &s->constant_name))
        return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, name->offset,
                            "operand name must resolve to the declared u32 constant");
    return literal_value(source, program, s->constant_value.value, name->offset, value);
}
typedef struct LoopScope LoopScope;
struct LoopScope {
    LoopScope *parent;
    size_t body_scope_start, body_scope_end;
    uint32_t first_continue;
    bool continuing;
};
typedef struct {
    LocalBinding *values;
    size_t count, capacity, scope_start, level;
    LoopScope *loop;
} LocalScope;
typedef struct { IrValue value; bool runtime, abstract_integer; uint32_t integer; } ExpressionValue;
enum { BEHAVIOR_NEXT = 1u, BEHAVIOR_BREAK = 2u, BEHAVIOR_CONTINUE = 4u, BEHAVIOR_RETURN = 8u };

static const LocalBinding *find_local(const LocalScope *scope, const Name *name) {
    for (size_t i = scope->count; i != 0u; --i)
        if (same_name(&scope->values[i - 1u].name, name)) return &scope->values[i - 1u];
    return NULL;
}
static bool check_local_use(Source *source, const LocalScope *scope,
                            const LocalBinding *binding, size_t offset) {
    const size_t index = (size_t)(binding - scope->values);
    for (const LoopScope *loop = scope->loop; loop != NULL; loop = loop->parent)
        if (loop->continuing && index >= loop->body_scope_start && index < loop->body_scope_end &&
            binding->declaration_offset > loop->first_continue)
            return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, offset,
                                "continuing references a declaration bypassed by continue");
    return true;
}
static bool user_declared(const Syntax *s, const LocalScope *scope, const Name *name) {
    if (same_name(name, &s->entry) || find_parameter(s, name) != s->parameter_count ||
        (s->has_alias && same_name(name, &s->alias)) ||
        (s->has_constant && same_name(name, &s->constant_name)) ||
        find_local(scope, name) != NULL) return true;
    for (size_t i = 0u; i < s->resource_count; ++i)
        if (same_name(name, &s->resources[i].name)) return true;
    for (size_t i = 0u; i < s->struct_count; ++i)
        if (same_name(name, &s->structs[i].name)) return true;
    return false;
}
static bool lower_input(Source *source, IrProgram *program, size_t parameter,
                        uint32_t component, size_t offset, IrValue *value) {
    return append(source, program, (IrInstruction){ .opcode = IR_INPUT,
        .type = IR_U32, .binding = (uint32_t)parameter, .literal = component,
        .source_offset = offset }, value);
}
static bool lower_member(Source *source, const Syntax *s, const MemberSyntax *member,
                         const LocalScope *scope, IrProgram *program, IrValue *value) {
    Name object_name, field_name;
    if (!span_name(source, member->object, &object_name) ||
        !span_name(source, member->member, &field_name)) return false;
    const Name *object = &object_name;
    /* The retained leading alias may shadow a module declaration. */
    if ((s->has_alias && same_name(object, &s->alias)) || find_local(scope, object) != NULL)
        return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, object->offset,
                            "scalar local has no members");
    const size_t parameter = find_parameter(s, object);
    if (parameter != s->parameter_count) {
        if (s->parameters[parameter].kind == INPUT_LOCAL_INDEX ||
            field_name.text[1] != '\0' || field_name.text[0] < 'x' || field_name.text[0] > 'z')
            return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, field_name.offset,
                                "vector input requires scalar x, y or z component");
        return lower_input(source, program, parameter,
                           (uint32_t)(field_name.text[0] - 'x'), object->offset, value);
    }
    size_t binding = 0u;
    while (binding < s->resource_count && !same_name(object, &s->resources[binding].name)) ++binding;
    if (binding == s->resource_count || s->resources[binding].kind != DOE_COMPILER_UNIFORM_BUFFER)
        return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, object->offset,
                            "member object must resolve to a vector input or uniform resource");
    const IrStruct *type = &program->structs[program->bindings[binding].struct_index];
    size_t field = 0u;
    while (field < type->member_count && !same_name(&field_name, &type->members[field].name)) ++field;
    if (field == type->member_count)
        return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, field_name.offset,
                            "unknown uniform member");
    return append(source, program, (IrInstruction){ .opcode = IR_UNIFORM_LOAD,
        .type = value_type(type->members[field].scalar), .binding = (uint32_t)binding,
        .literal = (uint32_t)field, .source_offset = object->offset }, value);
}

static bool local_reference(Source *source, const Syntax *s, const Reference *reference,
                            const LocalScope *scope, uint32_t *binding) {
    Name name;
    if (!span_name(source, reference->storage, &name)) return false;
    if (find_local(scope, &name) != NULL)
        return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, name.offset,
                            "indexed resource is shadowed by a scalar local");
    return resolve_reference(source, s, reference, binding);
}
static bool materialize(Source *source, IrProgram *program, ExpressionValue *value, IrType type) {
    if (!value->abstract_integer) return true;
    uint32_t bits = value->integer;
    if (type == IR_BOOL)
        return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, value->value.source_offset,
                            "integer literal cannot materialize as bool");
    if (type == IR_F32) {
        const Decimal decimal = { .numerator = bits, .denominator = 1u,
                                  .offset = value->value.source_offset };
        if (!decimal_binary32(source, &decimal, &bits)) return false;
    }
    value->abstract_integer = false;
    return append(source, program, (IrInstruction){ .opcode = IR_CONSTANT, .type = type,
        .literal = bits, .source_offset = value->value.source_offset }, &value->value);
}
static bool lower_expression(Source *, const Syntax *, uint32_t, const LocalScope *,
                             IrProgram *, ExpressionValue *);
static bool lower_unsigned(Source *source, const Syntax *s, uint32_t id,
                           const LocalScope *scope, IrProgram *program, IrValue *value) {
    ExpressionValue result = {0};
    if (!lower_expression(source, s, id, scope, program, &result)) return false;
    if (result.abstract_integer && result.integer > INT32_MAX)
        return source_error(source, DOE_COMPILER_UNSUPPORTED, result.value.source_offset,
                            "direct abstract index exceeds default i32 range");
    if (!materialize(source, program, &result, IR_U32)) return false;
    if (result.value.type != IR_U32)
        return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, result.value.source_offset,
                            "storage index requires an admitted integer type");
    *value = result.value;
    return true;
}
static bool compare_integer(uint32_t left, uint32_t right, Comparison comparison) {
    switch (comparison) {
        case COMPARE_EQUAL: return left == right;
        case COMPARE_NOT_EQUAL: return left != right;
        case COMPARE_LESS: return left < right;
        case COMPARE_LESS_EQUAL: return left <= right;
        case COMPARE_GREATER: return left > right;
        case COMPARE_GREATER_EQUAL: return left >= right;
    }
    return false;
}
static bool lower_expression(Source *source, const Syntax *s, uint32_t id,
                             const LocalScope *scope, IrProgram *program,
                             ExpressionValue *out) {
    if (id >= s->expression_count)
        return source_error(source, DOE_COMPILER_INPUT_ERROR, source->length,
                            "invalid internal expression identity");
    const Expression *node = &s->expressions[id];
    *out = (ExpressionValue){0};
    switch (node->kind) {
        case EXPRESSION_LOAD: {
            const Reference *reference = &node->value.reference;
            uint32_t binding;
            IrValue index;
            if (!local_reference(source, s, reference, scope, &binding) ||
                !lower_unsigned(source, s, reference->index, scope, program, &index)) return false;
            out->runtime = true;
            return append(source, program, (IrInstruction){ .opcode = IR_ROBUST_LOAD,
                .type = value_type(s->resources[binding].scalar), .binding = binding,
                .left = index.id, .source_offset = reference->storage.offset }, &out->value);
        }
        case EXPRESSION_ABSTRACT_INTEGER:
            out->abstract_integer = true;
            out->integer = node->value.integer;
            out->value.source_offset = node->source_offset;
            return true;
        case EXPRESSION_INTEGER:
            return literal_value(source, program, node->value.integer, node->source_offset, &out->value);
        case EXPRESSION_BOOLEAN:
            return append(source, program, (IrInstruction){ .opcode = IR_CONSTANT, .type = IR_BOOL,
                .literal = node->value.integer, .source_offset = node->source_offset }, &out->value);
        case EXPRESSION_DECIMAL: {
            uint32_t bits;
            const Decimal decimal = { .numerator = node->value.decimal.numerator,
                .denominator = node->value.decimal.denominator, .offset = node->source_offset };
            if (!decimal_binary32(source, &decimal, &bits)) return false;
            return append(source, program, (IrInstruction){ .opcode = IR_CONSTANT,
                .type = IR_F32, .literal = bits, .source_offset = node->source_offset }, &out->value);
        }
        case EXPRESSION_MEMBER:
            out->runtime = true;
            return lower_member(source, s, &node->value.member, scope, program, &out->value);
        case EXPRESSION_NAME: {
            Name name;
            if (!span_name(source, node->value.name, &name)) return false;
            const LocalBinding *local = find_local(scope, &name);
            if (local != NULL) {
                if (!check_local_use(source, scope, local, node->source_offset)) return false;
                out->runtime = true;
                if (local->mutable)
                    return append(source, program, (IrInstruction){ .opcode = IR_LOCAL_LOAD,
                        .type = local->value.type, .binding = local->local_id,
                        .source_offset = node->source_offset }, &out->value);
                out->value = local->value;
                out->value.source_offset = node->source_offset;
                return true;
            }
            if (s->has_alias && same_name(&name, &s->alias)) {
                out->runtime = true;
                out->value = (IrValue){ .id = 1u, .type = IR_U32, .source_offset = node->source_offset };
                return true;
            }
            const size_t parameter = find_parameter(s, &name);
            if (parameter != s->parameter_count) {
                if (s->parameters[parameter].kind != INPUT_LOCAL_INDEX)
                    return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, name.offset,
                                        "vector input is not a scalar expression");
                out->runtime = true;
                return lower_input(source, program, parameter, 0u, name.offset, &out->value);
            }
            return constant_value(source, s, &name, program, &out->value);
        }
        case EXPRESSION_MAX: {
            Name name;
            if (!span_name(source, node->value.name, &name)) return false;
            if (user_declared(s, scope, &name))
                return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, node->source_offset,
                                    "max resolves to a user declaration, not the builtin");
            break;
        }
        case EXPRESSION_ADD:
        case EXPRESSION_SUBTRACT:
        case EXPRESSION_MULTIPLY:
        case EXPRESSION_COMPARE:
        case EXPRESSION_NOT:
        case EXPRESSION_AND:
        case EXPRESSION_OR: break;
    }
    ExpressionValue left = {0}, right = {0};
    if (!lower_expression(source, s, node->left, scope, program, &left)) return false;
    if (node->kind == EXPRESSION_NOT) {
        if (left.abstract_integer || left.value.type != IR_BOOL)
            return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, node->source_offset,
                                "logical not requires bool");
        out->runtime = left.runtime;
        return append(source, program, (IrInstruction){ .opcode = IR_NOT, .type = IR_BOOL,
            .left = left.value.id, .source_offset = node->source_offset }, &out->value);
    }
    const bool logical = node->kind == EXPRESSION_AND || node->kind == EXPRESSION_OR;
    if (logical) {
        if (left.abstract_integer || left.value.type != IR_BOOL)
            return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, node->source_offset,
                                "logical operands require bool");
        if (!append(source, program, (IrInstruction){ .opcode = IR_SHORT_BEGIN,
            .left = left.value.id, .literal = node->kind == EXPRESSION_OR,
            .source_offset = node->source_offset }, NULL)) return false;
    }
    if (!lower_expression(source, s, node->right, scope, program, &right)) return false;
    if (logical) {
        if (right.abstract_integer || right.value.type != IR_BOOL)
            return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, node->source_offset,
                                "logical operands require bool");
        out->runtime = left.runtime || right.runtime;
        return append(source, program, (IrInstruction){ .opcode = IR_SHORT_END, .type = IR_BOOL,
            .left = left.value.id, .right = right.value.id,
            .source_offset = node->source_offset }, &out->value);
    }
    if (left.abstract_integer && right.abstract_integer) {
        if (node->kind != EXPRESSION_COMPARE)
            return source_error(source, DOE_COMPILER_UNSUPPORTED, node->source_offset,
                                "abstract-only arithmetic and max require a future constant evaluator");
        return append(source, program, (IrInstruction){ .opcode = IR_CONSTANT, .type = IR_BOOL,
            .literal = compare_integer(left.integer, right.integer, node->value.comparison),
            .source_offset = node->source_offset }, &out->value);
    }
    if (left.abstract_integer && !materialize(source, program, &left, right.value.type)) return false;
    if (right.abstract_integer && !materialize(source, program, &right, left.value.type)) return false;
    if (left.value.type != right.value.type)
        return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, node->source_offset,
                            "scalar expression operands must have the same type");
    const bool comparison = node->kind == EXPRESSION_COMPARE;
    if ((!comparison && left.value.type == IR_BOOL) || (comparison && left.value.type == IR_BOOL &&
        node->value.comparison != COMPARE_EQUAL && node->value.comparison != COMPARE_NOT_EQUAL))
        return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, node->source_offset,
                            "operator is not defined for bool operands");
    if (comparison && !source->limits->extended && left.value.type != IR_U32)
        return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, node->source_offset,
                            "legacy conditions require u32 comparisons");
    if (left.value.type == IR_F32 && !left.runtime && !right.runtime)
        return source_error(source, DOE_COMPILER_UNSUPPORTED, node->source_offset,
                            "constant-only floating arithmetic, max and comparisons are unsupported");
    out->runtime = left.runtime || right.runtime;
    const IrOpcode opcode = comparison ? IR_COMPARE : node->kind == EXPRESSION_ADD ? IR_ADD :
        node->kind == EXPRESSION_SUBTRACT ? IR_SUBTRACT : node->kind == EXPRESSION_MULTIPLY ? IR_MULTIPLY : IR_MAX;
    return append(source, program, (IrInstruction){ .opcode = opcode,
        .type = comparison ? IR_BOOL : left.value.type, .comparison_type = left.value.type,
        .comparison = node->value.comparison, .left = left.value.id, .right = right.value.id,
        .source_offset = node->source_offset }, &out->value);
}
static bool lower_boolean(Source *source, const Syntax *s, uint32_t id, const LocalScope *scope,
                          IrProgram *program, IrValue *value) {
    ExpressionValue result;
    if (!lower_expression(source, s, id, scope, program, &result)) return false;
    if (result.abstract_integer || result.value.type != IR_BOOL)
        return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, result.value.source_offset,
                            "control condition requires bool");
    *value = result.value;
    return true;
}
static bool lower_assignment(Source *source, const Syntax *s, const Assignment *assignment,
                             const LocalScope *scope, IrProgram *program) {
    uint32_t binding = 0u;
    IrType type;
    IrValue index = {0}, previous = {0};
    if (assignment->local) {
        Name name;
        if (!span_name(source, assignment->target.storage, &name)) return false;
        const LocalBinding *local = find_local(scope, &name);
        if (local == NULL || !local->mutable)
            return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, name.offset,
                                "assignment target must be a visible mutable local");
        if (!check_local_use(source, scope, local, name.offset)) return false;
        binding = local->local_id;
        type = local->value.type;
    } else {
        if (!local_reference(source, s, &assignment->target, scope, &binding)) return false;
        if (s->resources[binding].access != DOE_COMPILER_READ_WRITE)
            return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, assignment->target.storage.offset,
                                "assignment target requires read_write storage access");
        type = value_type(s->resources[binding].scalar);
        if (!lower_unsigned(source, s, assignment->target.index, scope, program, &index)) return false;
    }
    const size_t offset = assignment->target.storage.offset;
    const bool increment = assignment->operation == UPDATE_INCREMENT || assignment->operation == UPDATE_DECREMENT;
    if (assignment->operation != UPDATE_SET) {
        if (type == IR_BOOL || (increment && type != IR_U32))
            return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, offset,
                                "compound update requires numeric type and increment requires u32");
        if (!append(source, program, (IrInstruction){
            .opcode = assignment->local ? IR_LOCAL_LOAD : IR_ROBUST_LOAD,
            .type = type, .binding = binding, .left = index.id, .source_offset = offset }, &previous)) return false;
    }
    ExpressionValue result = {0};
    if (increment) {
        if (!literal_value(source, program, 1u, offset, &result.value)) return false;
    } else if (!lower_expression(source, s, assignment->expression, scope, program, &result) ||
               !materialize(source, program, &result, type)) return false;
    if (result.value.type != type)
        return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, result.value.source_offset,
                            "assignment value and destination must have the same scalar type");
    if (assignment->operation != UPDATE_SET) {
        const IrOpcode opcode = assignment->operation == UPDATE_MULTIPLY ? IR_MULTIPLY :
            (assignment->operation == UPDATE_SUBTRACT || assignment->operation == UPDATE_DECREMENT) ? IR_SUBTRACT : IR_ADD;
        if (!append(source, program, (IrInstruction){ .opcode = opcode, .type = type,
            .left = previous.id, .right = result.value.id, .source_offset = offset }, &result.value)) return false;
    }
    return append(source, program, (IrInstruction){
        .opcode = assignment->local ? IR_LOCAL_STORE : IR_ROBUST_STORE,
        .type = type, .left = index.id, .right = result.value.id, .binding = binding,
        .source_offset = offset }, NULL);
}
static bool lower_local(Source *source, const Syntax *s, const LocalSyntax *local,
                        LocalScope *scope, IrProgram *program) {
    Name name, annotation;
    if (!span_name(source, local->name, &name) || !valid_name(source, &name)) return false;
    bool duplicate = !source->limits->extended && user_declared(s, scope, &name);
    for (size_t i = scope->scope_start; i < scope->count; ++i)
        duplicate = duplicate || same_name(&name, &scope->values[i].name);
    if (scope->level == 0u && find_parameter(s, &name) != s->parameter_count) duplicate = true;
    if (duplicate)
        return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, name.offset,
                            "local declaration duplicates a binding in its scope");
    if (local->annotated && (!span_name(source, local->annotation, &annotation) ||
        user_declared(s, scope, &annotation)))
        return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, local->annotation.offset,
                            "local annotation resolves to a user value, not a type");
    ExpressionValue result = {0};
    if (local->initialized) {
        if (!lower_expression(source, s, local->expression, scope, program, &result)) return false;
        if (local->annotated && !materialize(source, program, &result, local->type)) return false;
        if (result.abstract_integer)
            return source_error(source, DOE_COMPILER_UNSUPPORTED, name.offset,
                                "inferred abstract integer local defaults to unsupported i32");
        if (local->annotated && local->type != result.value.type)
            return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, local->annotation.offset,
                                "local annotation and initializer have incompatible scalar types");
    } else if (!append(source, program, (IrInstruction){ .opcode = IR_CONSTANT,
        .type = local->type, .literal = 0u, .source_offset = name.offset }, &result.value)) return false;
    if (scope->count == scope->capacity)
        return source_error(source, DOE_COMPILER_INPUT_ERROR, name.offset, "local scope capacity exceeded");
    LocalBinding binding = { .name = name, .value = result.value, .mutable = local->mutable,
                             .declaration_offset = (uint32_t)name.offset };
    if (local->mutable) {
        if (program->local_count == program->local_capacity)
            return source_error(source, DOE_COMPILER_INPUT_ERROR, name.offset, "mutable local capacity exceeded");
        binding.local_id = (uint32_t)program->local_count;
        program->locals[program->local_count++] = (IrLocal){ .type = result.value.type, .mutable = true };
        if (!append(source, program, (IrInstruction){ .opcode = IR_LOCAL_STORE,
            .type = result.value.type, .binding = binding.local_id, .right = result.value.id,
            .source_offset = name.offset }, NULL)) return false;
    }
    scope->values[scope->count++] = binding;
    return true;
}
static bool lower_block(Source *, const Syntax *, size_t, size_t, LocalScope *, IrProgram *,
                        LoopScope *, bool, bool, bool, unsigned *);
static bool marker(Source *source, IrProgram *program, IrOpcode opcode, size_t offset, uint32_t literal) {
    return append(source, program, (IrInstruction){ .opcode = opcode, .literal = literal,
                   .source_offset = offset }, NULL);
}
static bool inside_continuing(const LoopScope *loop) {
    for (; loop != NULL; loop = loop->parent) if (loop->continuing) return true;
    return false;
}
static bool lower_loop(Source *source, const Syntax *s, const Statement *st,
                       LocalScope *scope, IrProgram *program, LoopScope *outer, unsigned *behavior) {
    const size_t saved_count = scope->count, saved_start = scope->scope_start, saved_level = scope->level;
    const bool is_for = st->kind == STATEMENT_FOR;
    if (is_for) {
        scope->scope_start = scope->count; ++scope->level;
        if (st->initializer != UINT32_MAX) {
            unsigned ignored;
            if (!lower_block(source, s, st->initializer, st->initializer + 1u, scope, program,
                             outer, true, true, false, &ignored)) return false;
        }
    }
    const size_t begin = program->instruction_count;
    if (!marker(source, program, IR_LOOP_BEGIN, st->source_offset, 0u)) return false;
    IrValue test = {0};
    const bool has_test = st->kind == STATEMENT_WHILE || (is_for && st->value.condition != UINT32_MAX);
    if (has_test && !lower_boolean(source, s, st->value.condition, scope, program, &test)) return false;
    if (!append(source, program, (IrInstruction){ .opcode = IR_LOOP_TEST, .left = test.id,
        .source_offset = st->source_offset }, NULL)) return false;
    size_t continuing = st->end;
    for (size_t i = st->body_start; i < st->end; i = s->statements[i].end) {
        if (s->statements[i].kind == STATEMENT_CONTINUING) {
            if (st->kind != STATEMENT_LOOP || s->statements[i].end != st->end || continuing != st->end)
                return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, s->statements[i].source_offset,
                                    "continuing must be the final direct child of loop");
            continuing = i;
        }
    }
    LoopScope loop = { .parent = outer, .body_scope_start = scope->count,
                       .first_continue = UINT32_MAX };
    const size_t body_start = scope->scope_start, body_level = scope->level;
    scope->scope_start = scope->count; ++scope->level; scope->loop = &loop;
    unsigned body, tail = BEHAVIOR_NEXT;
    if (!lower_block(source, s, st->body_start, continuing, scope, program, &loop,
                     true, true, false, &body)) return false;
    loop.body_scope_end = scope->count;
    if (!marker(source, program, IR_LOOP_CONTINUING, st->source_offset, 0u)) return false;
    if (is_for) {
        scope->count = loop.body_scope_start;
        scope->scope_start = body_start; scope->level = body_level;
        if (st->update != UINT32_MAX) {
            unsigned ignored;
            if (!lower_block(source, s, st->update, st->update + 1u, scope, program, outer,
                             true, true, false, &ignored)) return false;
        }
    } else if (continuing != st->end) {
        const Statement *part = &s->statements[continuing];
        loop.continuing = true;
        if (!lower_block(source, s, part->body_start, part->end, scope, program, &loop,
                         false, true, true, &tail)) return false;
    }
    *behavior = body & BEHAVIOR_RETURN;
    if ((body & BEHAVIOR_BREAK) != 0u || has_test) *behavior |= BEHAVIOR_NEXT;
    if ((body & (BEHAVIOR_NEXT | BEHAVIOR_CONTINUE)) != 0u) {
        *behavior |= tail & BEHAVIOR_RETURN;
        if ((tail & BEHAVIOR_BREAK) != 0u) *behavior |= BEHAVIOR_NEXT;
    }
    if (*behavior == 0u)
        return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, st->source_offset,
                            "loop has an empty WGSL behavior set");
    program->instructions[begin].literal = *behavior;
    if (!marker(source, program, IR_END_LOOP, st->source_offset, *behavior)) return false;
    scope->count = saved_count; scope->scope_start = saved_start; scope->level = saved_level; scope->loop = outer;
    return true;
}
static bool lower_block(Source *source, const Syntax *s, size_t start, size_t end,
                        LocalScope *scope, IrProgram *program, LoopScope *loop,
                        bool retain_scope, bool reachable, bool continuing_direct, unsigned *behavior) {
    const size_t saved_count = scope->count, saved_start = scope->scope_start, saved_level = scope->level;
    LoopScope *saved_loop = scope->loop;
    scope->loop = loop;
    if (!retain_scope) { scope->scope_start = scope->count; ++scope->level; }
    *behavior = BEHAVIOR_NEXT;
    for (size_t i = start; i < end; i = s->statements[i].end) {
        const Statement *st = &s->statements[i];
        const bool live = reachable && ((*behavior & BEHAVIOR_NEXT) != 0u);
        unsigned current = BEHAVIOR_NEXT;
        switch (st->kind) {
            case STATEMENT_ASSIGN:
                if (!lower_assignment(source, s, &st->value.assignment, scope, program)) return false;
                break;
            case STATEMENT_LET:
            case STATEMENT_VAR:
                if (!lower_local(source, s, &st->value.local, scope, program)) return false;
                break;
            case STATEMENT_RETURN:
                if (inside_continuing(loop))
                    return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, st->source_offset,
                                        "return is prohibited inside continuing");
                current = BEHAVIOR_RETURN;
                if (!marker(source, program, IR_RETURN, st->source_offset, 0u)) return false;
                break;
            case STATEMENT_BREAK:
            case STATEMENT_CONTINUE:
                if (loop == NULL || loop->continuing)
                    return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, st->source_offset,
                                        "break or continue requires a non-continuing nearest loop");
                current = st->kind == STATEMENT_BREAK ? BEHAVIOR_BREAK : BEHAVIOR_CONTINUE;
                if (current == BEHAVIOR_CONTINUE && live && st->source_offset < loop->first_continue)
                    loop->first_continue = st->source_offset;
                if (!marker(source, program, st->kind == STATEMENT_BREAK ? IR_BREAK : IR_CONTINUE,
                            st->source_offset, 0u)) return false;
                break;
            case STATEMENT_BREAK_IF: {
                if (!continuing_direct || loop == NULL || !loop->continuing || st->end != end)
                    return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, st->source_offset,
                                        "break if must end its direct continuing block");
                IrValue test;
                if (!lower_boolean(source, s, st->value.condition, scope, program, &test) ||
                    !append(source, program, (IrInstruction){ .opcode = IR_BREAK_IF,
                        .left = test.id, .source_offset = st->source_offset }, NULL)) return false;
                current = BEHAVIOR_BREAK | BEHAVIOR_NEXT;
                break;
            }
            case STATEMENT_IF: {
                IrValue test;
                unsigned yes, no;
                if (!lower_boolean(source, s, st->value.condition, scope, program, &test) ||
                    !append(source, program, (IrInstruction){ .opcode = IR_IF,
                        .left = test.id, .source_offset = st->source_offset }, NULL) ||
                    !lower_block(source, s, st->body_start, st->else_start, scope, program, loop,
                                 false, live, false, &yes) ||
                    !marker(source, program, IR_ELSE, st->source_offset, 0u) ||
                    !lower_block(source, s, st->else_start, st->end, scope, program, loop,
                                 false, live, false, &no) ||
                    !marker(source, program, IR_END_IF, st->source_offset, 0u)) return false;
                current = yes | no;
                break;
            }
            case STATEMENT_BLOCK:
                if (!lower_block(source, s, st->body_start, st->end, scope, program, loop,
                                 false, live, false, &current)) return false;
                break;
            case STATEMENT_FOR:
            case STATEMENT_WHILE:
            case STATEMENT_LOOP:
                if (!lower_loop(source, s, st, scope, program, loop, &current)) return false;
                break;
            case STATEMENT_CONTINUING:
                return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, st->source_offset,
                                    "continuing must be the final direct child of loop");
            case STATEMENT_EMPTY: break;
        }
        if ((*behavior & BEHAVIOR_NEXT) != 0u) *behavior = (*behavior & ~(unsigned)BEHAVIOR_NEXT) | current;
    }
    if (!retain_scope) scope->count = saved_count;
    scope->scope_start = saved_start; scope->level = saved_level; scope->loop = saved_loop;
    return true;
}

bool validate_syntax(Source *source, const Syntax *s, IrProgram *program, LocalBinding *bindings) {
    if (!validate_declarations(source, s)) return false;
    program->instruction_count = 0u;
    program->local_count = 0u;
    program->entry = s->entry;
    program->parameter_count = s->parameter_count;
    for (size_t i = 0; i < s->parameter_count; ++i) program->parameters[i] = s->parameters[i];
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
    for (size_t i = 0; i < 3u; ++i) program->local_size[i] = s->local_size[i].value;
    if (s->has_alias) {
        IrValue alias;
        if (!lower_input(source, program, find_parameter(s, &s->alias_source),
                         0u, s->alias_source.offset, &alias)) return false;
    }
    /* Unreachable statements are lowered too: no dead-code validation bypass. */
    LocalScope scope = { .values = bindings, .capacity = source->limits->locals };
    unsigned behavior;
    return lower_block(source, s, 0u, s->statement_count, &scope, program, NULL,
                       true, true, false, &behavior);
}
