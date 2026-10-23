/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "numeric.h"
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
static bool span_matches(const Source *source, NameSpan span, const Name *name) {
    return strlen(name->text) == span.length &&
        memcmp(source->source + span.offset, name->text, span.length) == 0;
}
static bool spans_equal(const Source *source, NameSpan a, NameSpan b) {
    return a.length == b.length && memcmp(source->source + a.offset, source->source + b.offset, a.length) == 0;
}
static size_t find_constant(const Source *source, const Syntax *s, const Name *name) {
    for (size_t i = 0; i < s->constant_count; ++i)
        if (span_matches(source, s->constants[i].name, name)) return i;
    return s->constant_count;
}
static size_t find_struct(const Source *source, const Syntax *s, NameSpan name) {
    for (size_t i = 0; i < s->struct_count; ++i)
        if (spans_equal(source, name, s->structs[i].name)) return i;
    return s->struct_count;
}
static bool module_declared(const Source *source, const Syntax *s, const Name *name) {
    if (same_name(name, &s->entry) || find_constant(source, s, name) != s->constant_count) return true;
    for (size_t i = 0; i < s->resource_count; ++i)
        if (span_matches(source, s->resources[i].name, name)) return true;
    for (size_t i = 0; i < s->struct_count; ++i)
        if (span_matches(source, s->structs[i].name, name)) return true;
    return false;
}
static bool valid_span(Source *source, NameSpan span) {
    Name name;
    return span_name(source, span, &name) && valid_name(source, &name);
}
static bool unshadowed_type(Source *source, const Syntax *s, NameSpan span) {
    Name name;
    if (!span_name(source, span, &name)) return false;
    return !module_declared(source, s, &name) ||
        source_error(source, DOE_COMPILER_SEMANTIC_ERROR, span.offset,
                     "type annotation resolves to a module value, not a type");
}
static bool validate_declarations(Source *source, const Syntax *s) {
    if (!valid_name(source, &s->entry)) return false;
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
        Name vector = { .offset = parameter->name.offset }, scalar = { .text = "u32", .offset = parameter->name.offset };
        memcpy(vector.text, parameter->vector_alias ? "vec3u" : "vec3", parameter->vector_alias ? 6u : 5u);
        if ((parameter->kind != INPUT_LOCAL_INDEX && module_declared(source, s, &vector)) ||
            (!parameter->vector_alias && module_declared(source, s, &scalar)))
            return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, parameter->name.offset,
                                "compute input type is shadowed by a module declaration");
    }
    const size_t count = s->constant_count + s->resource_count + s->struct_count;
    for (size_t i = 0; i < count; ++i) {
        const NameSpan name = i < s->constant_count ? s->constants[i].name :
            i < s->constant_count + s->resource_count ? s->resources[i - s->constant_count].name :
            s->structs[i - s->constant_count - s->resource_count].name;
        if (!valid_span(source, name)) return false;
        if (span_matches(source, name, &s->entry))
            return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, s->entry.offset, "duplicate module declaration");
        for (size_t j = 0; j < i; ++j) {
            const NameSpan previous = j < s->constant_count ? s->constants[j].name :
                j < s->constant_count + s->resource_count ? s->resources[j - s->constant_count].name :
                s->structs[j - s->constant_count - s->resource_count].name;
            if (spans_equal(source, name, previous))
                return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, name.offset, "duplicate module declaration");
        }
    }
    for (size_t i = 0; i < s->struct_count; ++i) {
        const StructSyntax *type = &s->structs[i];
        for (size_t j = 0; j < type->member_count; ++j) {
            if (!valid_span(source, type->members[j].name) ||
                !unshadowed_type(source, s, type->members[j].annotation)) return false;
            for (size_t k = 0; k < j; ++k)
                if (spans_equal(source, type->members[j].name, type->members[k].name))
                    return source_error(source, DOE_COMPILER_SEMANTIC_ERROR,
                                        type->members[j].name.offset, "duplicate struct member name");
        }
    }
    for (size_t i = 0; i < s->resource_count; ++i) {
        const ResourceSyntax *binding = &s->resources[i];
        if (binding->kind == DOE_COMPILER_UNIFORM_BUFFER) {
            if (find_struct(source, s, binding->type_name) == s->struct_count)
                return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, binding->type_name.offset,
                                    "uniform type must resolve to a declared flat struct");
        } else {
            Name scalar = { .offset = binding->name.offset }, array = { .text = "array", .offset = binding->name.offset };
            memcpy(scalar.text, binding->scalar == DOE_COMPILER_F32 ? "f32" : "u32", 4u);
            if (module_declared(source, s, &scalar) || module_declared(source, s, &array))
                return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, binding->name.offset,
                                    "storage type is shadowed by a module declaration");
        }
    }
    if (s->has_alias) {
        if (!valid_name(source, &s->alias)) return false;
        if (find_parameter(s, &s->alias) != s->parameter_count)
            return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, s->alias.offset,
                                "index alias redeclares an entry parameter in the same scope");
        const size_t parameter = find_parameter(s, &s->alias_source);
        if (parameter == s->parameter_count || s->parameters[parameter].kind != INPUT_GLOBAL_INVOCATION)
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
    while (index < s->resource_count && !span_matches(source, s->resources[index].name, name)) ++index;
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
typedef struct { IrValue value; NumericValue number; bool runtime, known; } ExpressionValue;
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
static bool user_declared(const Source *source, const Syntax *s, const LocalScope *scope, const Name *name) {
    return module_declared(source, s, name) || find_parameter(s, name) != s->parameter_count ||
        (s->has_alias && same_name(name, &s->alias)) || find_local(scope, name) != NULL;
}
static bool expression_emit(Source *, IrProgram *, bool, IrInstruction, IrValue *);
static bool lower_input(Source *source, IrProgram *program, size_t parameter,
                        uint32_t component, size_t offset, IrValue *value) {
    return append(source, program, (IrInstruction){ .opcode = IR_INPUT,
        .type = IR_U32, .binding = (uint32_t)parameter, .literal = component,
        .source_offset = offset }, value);
}
static bool lower_member(Source *source, const Syntax *s, const MemberSyntax *member,
                         const LocalScope *scope, IrProgram *program, bool emit, IrValue *value) {
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
        return expression_emit(source, program, emit, (IrInstruction){ .opcode = IR_INPUT, .type = IR_U32,
            .binding = (uint32_t)parameter, .literal = (uint32_t)(field_name.text[0] - 'x'),
            .source_offset = object->offset }, value);
    }
    size_t binding = 0u;
    while (binding < s->resource_count && !span_matches(source, s->resources[binding].name, object)) ++binding;
    if (binding == s->resource_count || s->resources[binding].kind != DOE_COMPILER_UNIFORM_BUFFER)
        return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, object->offset,
                            "member object must resolve to a vector input or uniform resource");
    const IrStruct *type = &program->structs[program->bindings[binding].struct_index];
    size_t field = 0u;
    while (field < type->member_count && !same_name(&field_name, &type->members[field].name)) ++field;
    if (field == type->member_count)
        return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, field_name.offset,
                            "unknown uniform member");
    return expression_emit(source, program, emit, (IrInstruction){ .opcode = IR_UNIFORM_LOAD,
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
/* A const value has no IR identity until a concrete consumer needs one. This
 * keeps AbstractFloat at binary64 at every source expression boundary. */
static bool abstract_type(IrType type) { return type == IR_ABSTRACT_INT || type == IR_ABSTRACT_FLOAT; }
static bool integer_type(IrType type) { return type == IR_U32 || type == IR_I32 || type == IR_ABSTRACT_INT; }
static bool expression_emit(Source *source, IrProgram *program, bool emit,
                            IrInstruction instruction, IrValue *value) {
    if (emit) return append(source, program, instruction, value);
    if (value != NULL) *value = (IrValue){ .type = instruction.type, .source_offset = instruction.source_offset };
    return true;
}
static bool ensure_value(Source *source, IrProgram *program, ExpressionValue *value, bool emit) {
    if (!emit || value->value.id != 0u) return true;
    if (!value->known || abstract_type(value->number.type))
        return source_error(source, DOE_COMPILER_INPUT_ERROR, value->value.source_offset,
                            "unmaterialized scalar reached target lowering");
    return append(source, program, (IrInstruction){ .opcode = IR_CONSTANT,
        .type = value->number.type, .literal = (uint32_t)value->number.bits,
        .source_offset = value->value.source_offset }, &value->value);
}
static bool materialize(Source *source, ExpressionValue *value, IrType type) {
    if (value->number.type == type) return true;
    if (!abstract_type(value->number.type) || type == IR_BOOL ||
        (value->number.type == IR_ABSTRACT_FLOAT && type != IR_F32))
        return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, value->value.source_offset,
                            "scalar types have no implicit conversion");
    if (value->known && !numeric_materialize(source, value->value.source_offset,
                                            value->number, type, &value->number)) return false;
    value->number.type = type;
    value->value.type = type;
    return true;
}
static bool common_type(Source *source, ExpressionValue *left, ExpressionValue *right, size_t offset) {
    if (left->number.type == right->number.type) return true;
    if (abstract_type(left->number.type) && abstract_type(right->number.type)) {
        if (!materialize(source, left, IR_ABSTRACT_FLOAT) ||
            !materialize(source, right, IR_ABSTRACT_FLOAT)) return false;
    } else if (abstract_type(left->number.type)) {
        if (!materialize(source, left, right->number.type)) return false;
    } else if (abstract_type(right->number.type)) {
        if (!materialize(source, right, left->number.type)) return false;
    } else return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, offset,
                              "operator operands must have a common scalar type");
    return true;
}
static bool scalar_name(Source *source, const Syntax *s, const LocalScope *scope,
                        NameSpan span, IrType *type) {
    Name name;
    if (!span_name(source, span, &name)) return false;
    if (user_declared(source, s, scope, &name))
        return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, span.offset,
                            "scalar type name resolves to a user declaration");
    if (strcmp(name.text, "u32") == 0) *type = IR_U32;
    else if (strcmp(name.text, "i32") == 0) *type = IR_I32;
    else if (strcmp(name.text, "f32") == 0) *type = IR_F32;
    else if (strcmp(name.text, "bool") == 0) *type = IR_BOOL;
    else return source_error(source, DOE_COMPILER_UNSUPPORTED, span.offset,
                             "expected an admitted scalar type constructor");
    return true;
}
static bool expression_value(Source *, const Syntax *, uint32_t, const LocalScope *,
                             IrProgram *, bool, bool, ExpressionValue *);
static bool lower_expression(Source *source, const Syntax *s, uint32_t id,
                             const LocalScope *scope, IrProgram *program, ExpressionValue *out) {
    return expression_value(source, s, id, scope, program, true, true, out);
}
static bool lower_index(Source *source, const Syntax *s, uint32_t id, const LocalScope *scope,
                        IrProgram *program, bool evaluate, bool emit, IrValue *out) {
    ExpressionValue value;
    if (!expression_value(source, s, id, scope, program, evaluate, emit, &value)) return false;
    if (abstract_type(value.number.type) && !materialize(source, &value, IR_I32)) return false;
    if (!integer_type(value.number.type))
        return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, value.value.source_offset,
                            "storage index requires i32 or u32");
    if (!value.runtime && value.known && value.number.type == IR_I32 &&
        (value.number.bits & UINT64_C(0x80000000)) != 0u)
        return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, value.value.source_offset,
                            "constant storage index must not be negative");
    /* Nonnegative const indices share the existing unsigned target form;
     * source default materialization and its range check already happened. */
    if (!value.runtime && value.known) { value.number.type = IR_U32; value.value.type = IR_U32; }
    if (!ensure_value(source, program, &value, emit)) return false;
    *out = value.value;
    return true;
}
static bool lower_unsigned(Source *source, const Syntax *s, uint32_t id,
                           const LocalScope *scope, IrProgram *program, IrValue *value) {
    return lower_index(source, s, id, scope, program, true, true, value);
}
static IrOpcode binary_opcode(ExpressionKind kind) {
    switch (kind) {
        case EXPRESSION_ADD: return IR_ADD;
        case EXPRESSION_SUBTRACT: return IR_SUBTRACT;
        case EXPRESSION_MULTIPLY: return IR_MULTIPLY;
        case EXPRESSION_DIVIDE: return IR_DIVIDE;
        case EXPRESSION_REMAINDER: return IR_REMAINDER;
        case EXPRESSION_MAX: return IR_MAX;
        case EXPRESSION_COMPARE: return IR_COMPARE;
        case EXPRESSION_BIT_AND: return IR_BIT_AND;
        case EXPRESSION_BIT_OR: return IR_BIT_OR;
        case EXPRESSION_BIT_XOR: return IR_BIT_XOR;
        case EXPRESSION_SHIFT_LEFT: return IR_SHIFT_LEFT;
        case EXPRESSION_SHIFT_RIGHT: return IR_SHIFT_RIGHT;
        case EXPRESSION_LOAD: case EXPRESSION_INTEGER: case EXPRESSION_DECIMAL:
        case EXPRESSION_NAME: case EXPRESSION_MEMBER: case EXPRESSION_ABSTRACT_INTEGER:
        case EXPRESSION_BOOLEAN: case EXPRESSION_NOT: case EXPRESSION_AND: case EXPRESSION_OR:
        case EXPRESSION_LITERAL: case EXPRESSION_NEGATE: case EXPRESSION_COMPLEMENT:
        case EXPRESSION_CONSTRUCT: case EXPRESSION_BITCAST: break;
    }
    return IR_CONSTANT;
}
static bool binary_types(Source *source, ExpressionKind kind, Comparison comparison,
                         ExpressionValue *left, ExpressionValue *right, size_t offset) {
    const bool shift = kind == EXPRESSION_SHIFT_LEFT || kind == EXPRESSION_SHIFT_RIGHT;
    if (shift) {
        /* WGSL 6.1.3 step 2 excludes abstract candidates when any operand
         * is runtime. Among concrete shift overloads i32 has rank 3, u32 4. */
        if (right->runtime && left->number.type == IR_ABSTRACT_INT &&
            !materialize(source, left, IR_I32)) return false;
        if (!materialize(source, right, IR_U32)) return false;
        if (!integer_type(left->number.type) || right->number.type != IR_U32)
            return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, offset,
                                "shift requires integer left operand and u32 count");
        return true;
    }
    if (!common_type(source, left, right, offset)) return false;
    const IrType type = left->number.type;
    const bool bit = kind == EXPRESSION_BIT_AND || kind == EXPRESSION_BIT_OR || kind == EXPRESSION_BIT_XOR;
    if ((bit && !integer_type(type) && !(type == IR_BOOL && kind != EXPRESSION_BIT_XOR)) ||
        (type == IR_BOOL && !bit && !(kind == EXPRESSION_COMPARE &&
          (comparison == COMPARE_EQUAL || comparison == COMPARE_NOT_EQUAL))))
        return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, offset,
                            "operator is not defined for these scalar types");
    return true;
}
static bool mandatory_rhs(Source *source, const Syntax *s, const Expression *node,
                          const LocalScope *scope, IrProgram *program,
                          ExpressionValue *left, ExpressionValue *right) {
    const bool shift = node->kind == EXPRESSION_SHIFT_LEFT || node->kind == EXPRESSION_SHIFT_RIGHT;
    const bool division = node->kind == EXPRESSION_DIVIDE || node->kind == EXPRESSION_REMAINDER;
    if (right->runtime || (!shift && !(division && integer_type(left->number.type)))) return true;
    if (!right->known) {
        ExpressionValue evaluated;
        if (!expression_value(source, s, node->right, scope, program, true, false, &evaluated) ||
            !materialize(source, &evaluated, right->number.type)) return false;
        *right = evaluated;
    }
    if (division && right->number.bits == 0u)
        return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, node->source_offset,
                            "integer divisor is a zero const expression");
    if (shift && left->number.type != IR_ABSTRACT_INT && right->number.bits >= 32u)
        return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, node->source_offset,
                            "concrete shift count const expression must be below 32");
    return true;
}
static bool expression_value(Source *source, const Syntax *s, uint32_t id,
                             const LocalScope *scope, IrProgram *program,
                             bool evaluate, bool emit, ExpressionValue *out) {
    if (id >= s->expression_count)
        return source_error(source, DOE_COMPILER_INPUT_ERROR, source->length,
                            "invalid internal expression identity");
    const Expression *node = &s->expressions[id];
    *out = (ExpressionValue){ .value.source_offset = node->source_offset };
    switch (node->kind) {
        case EXPRESSION_LITERAL:
            if (!numeric_parse_literal(source, node->value.name, &out->number)) return false;
            out->known = true; out->value.type = out->number.type;
            return true;
        case EXPRESSION_BOOLEAN:
            out->known = true; out->number = (NumericValue){ .bits = node->value.integer, .type = IR_BOOL };
            out->value.type = IR_BOOL; return true;
        case EXPRESSION_LOAD: {
            const Reference *reference = &node->value.reference;
            uint32_t binding;
            IrValue index;
            if (!local_reference(source, s, reference, scope, &binding) ||
                !lower_index(source, s, reference->index, scope, program, evaluate, emit, &index)) return false;
            out->runtime = true; out->number.type = value_type(s->resources[binding].scalar);
            return expression_emit(source, program, emit, (IrInstruction){ .opcode = IR_ROBUST_LOAD,
                .type = out->number.type, .comparison_type = index.type, .binding = binding,
                .left = index.id, .source_offset = node->source_offset }, &out->value);
        }
        case EXPRESSION_MEMBER:
            out->runtime = true;
            if (!lower_member(source, s, &node->value.member, scope, program, emit, &out->value)) return false;
            out->number.type = out->value.type; return true;
        case EXPRESSION_NAME: {
            Name name;
            if (!span_name(source, node->value.name, &name)) return false;
            const LocalBinding *local = find_local(scope, &name);
            if (local != NULL) {
                if (!check_local_use(source, scope, local, node->source_offset)) return false;
                out->runtime = !local->constant; out->known = local->constant;
                out->number = local->constant_value;
                out->number.type = local->value.type;
                if (local->mutable)
                    return expression_emit(source, program, emit, (IrInstruction){ .opcode = IR_LOCAL_LOAD,
                        .type = local->value.type, .binding = local->local_id,
                        .source_offset = node->source_offset }, &out->value);
                out->value = local->value; out->value.source_offset = node->source_offset;
                return true;
            }
            if (s->has_alias && same_name(&name, &s->alias)) {
                out->runtime = true; out->number.type = IR_U32;
                out->value = (IrValue){ .id = 1u, .type = IR_U32, .source_offset = node->source_offset }; return true;
            }
            const size_t parameter = find_parameter(s, &name);
            if (parameter != s->parameter_count) {
                if (s->parameters[parameter].kind != INPUT_LOCAL_INDEX)
                    return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, name.offset,
                                        "vector input is not a scalar expression");
                out->runtime = true; out->number.type = IR_U32;
                return expression_emit(source, program, emit, (IrInstruction){ .opcode = IR_INPUT,
                    .type = IR_U32, .binding = (uint32_t)parameter,
                    .source_offset = name.offset }, &out->value);
            }
            const size_t constant = find_constant(source, s, &name);
            if (constant == s->constant_count || s->constants[constant].state != 2u)
                return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, name.offset,
                                    "scalar name does not resolve to an available declaration");
            out->known = true;
            out->number = (NumericValue){ .bits = s->constants[constant].value.bits, .type = s->constants[constant].type };
            out->value.type = out->number.type; return true;
        }
        case EXPRESSION_MAX: {
            Name name;
            if (!span_name(source, node->value.call.callee, &name)) return false;
            if (user_declared(source, s, scope, &name))
                return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, node->source_offset,
                                    "max resolves to a user declaration, not the builtin");
            break;
        }
        case EXPRESSION_CONSTRUCT:
        case EXPRESSION_BITCAST: {
            const bool bitcast = node->kind == EXPRESSION_BITCAST;
            IrType target;
            if (bitcast) {
                Name name;
                if (!span_name(source, node->value.call.callee, &name)) return false;
                if (user_declared(source, s, scope, &name))
                    return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, node->source_offset,
                                        "bitcast resolves to a user declaration, not the builtin");
            }
            if (!scalar_name(source, s, scope, bitcast ? node->value.call.type : node->value.call.callee, &target)) return false;
            out->number.type = target; out->value.type = target;
            if (bitcast && target == IR_BOOL)
                return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, node->source_offset, "bitcast requires numeric type");
            if (node->left == UINT32_MAX) { out->known = evaluate; return true; }
            ExpressionValue argument;
            if (!expression_value(source, s, node->left, scope, program, evaluate, emit, &argument)) return false;
            const IrType original = argument.number.type;
            /* f32 and bitcast select concrete source overloads. The special
             * AbstractInt->u32 bitcast overload instead checks its range. */
            if (abstract_type(original) && (target == IR_F32 || bitcast) &&
                !(bitcast && target == IR_U32 && original == IR_ABSTRACT_INT)) {
                if (!materialize(source, &argument, original == IR_ABSTRACT_INT ? IR_I32 : IR_F32)) return false;
            }
            if (bitcast && target == IR_U32 && original == IR_ABSTRACT_INT &&
                !materialize(source, &argument, IR_U32)) return false;
            if (bitcast && argument.number.type == IR_BOOL)
                return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, node->source_offset, "bitcast requires numeric argument");
            out->runtime = argument.runtime;
            if (!argument.runtime) {
                out->known = evaluate && argument.known;
                if (!out->known) return true;
                return bitcast ? numeric_bitcast(source, node->source_offset, argument.number, target, &out->number) :
                    numeric_convert(source, node->source_offset, argument.number, target, &out->number);
            }
            if (!ensure_value(source, program, &argument, emit)) return false;
            if (argument.number.type == target) { out->value = argument.value; return true; }
            return expression_emit(source, program, emit, (IrInstruction){ .opcode = bitcast ? IR_BITCAST : IR_CONVERT,
                .type = target, .comparison_type = argument.number.type, .left = argument.value.id,
                .source_offset = node->source_offset }, &out->value);
        }
        case EXPRESSION_INTEGER: case EXPRESSION_DECIMAL: case EXPRESSION_ABSTRACT_INTEGER:
            return source_error(source, DOE_COMPILER_INPUT_ERROR, node->source_offset, "obsolete internal literal node");
        case EXPRESSION_ADD: case EXPRESSION_SUBTRACT: case EXPRESSION_MULTIPLY:
        case EXPRESSION_COMPARE: case EXPRESSION_NOT: case EXPRESSION_AND: case EXPRESSION_OR:
        case EXPRESSION_NEGATE: case EXPRESSION_COMPLEMENT: case EXPRESSION_DIVIDE:
        case EXPRESSION_REMAINDER: case EXPRESSION_BIT_AND: case EXPRESSION_BIT_OR:
        case EXPRESSION_BIT_XOR: case EXPRESSION_SHIFT_LEFT: case EXPRESSION_SHIFT_RIGHT: break;
    }
    ExpressionValue left, right;
    if (!expression_value(source, s, node->left, scope, program, evaluate, emit, &left)) return false;
    const bool unary = node->kind == EXPRESSION_NOT || node->kind == EXPRESSION_NEGATE || node->kind == EXPRESSION_COMPLEMENT;
    if (unary) {
        const IrType type = left.number.type;
        if ((node->kind == EXPRESSION_NOT && type != IR_BOOL) ||
            (node->kind == EXPRESSION_COMPLEMENT && !integer_type(type)) ||
            (node->kind == EXPRESSION_NEGATE && (type == IR_BOOL || type == IR_U32)))
            return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, node->source_offset,
                                "unary operator is not defined for this scalar type");
        out->runtime = left.runtime; out->number.type = type; out->value.type = type;
        if (!left.runtime) {
            out->known = evaluate && left.known;
            return !out->known || numeric_unary(source, node->source_offset, node->kind, left.number, &out->number);
        }
        if (!ensure_value(source, program, &left, emit)) return false;
        return expression_emit(source, program, emit, (IrInstruction){ .opcode = node->kind == EXPRESSION_NOT ? IR_NOT :
            node->kind == EXPRESSION_NEGATE ? IR_NEGATE : IR_COMPLEMENT, .type = type,
            .left = left.value.id, .source_offset = node->source_offset }, &out->value);
    }
    const bool logical = node->kind == EXPRESSION_AND || node->kind == EXPRESSION_OR;
    bool skip = false;
    if (logical) {
        if (left.number.type != IR_BOOL)
            return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, node->source_offset, "logical operands require bool");
        skip = left.known && !left.runtime && (left.number.bits != 0u) == (node->kind == EXPRESSION_OR);
        if (left.runtime && emit && (!ensure_value(source, program, &left, true) ||
            !append(source, program, (IrInstruction){ .opcode = IR_SHORT_BEGIN, .left = left.value.id,
                .literal = node->kind == EXPRESSION_OR, .source_offset = node->source_offset }, NULL))) return false;
    }
    if (!expression_value(source, s, node->right, scope, program, evaluate && !skip, emit && !skip, &right)) return false;
    if (logical) {
        if (right.number.type != IR_BOOL)
            return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, node->source_offset, "logical operands require bool");
        out->runtime = left.runtime || right.runtime; out->number.type = IR_BOOL; out->value.type = IR_BOOL;
        if (skip) { out->known = true; out->number.bits = left.number.bits; return true; }
        if (!left.runtime) { *out = right; out->value.source_offset = node->source_offset; return true; }
        if (!ensure_value(source, program, &right, emit)) return false;
        return expression_emit(source, program, emit, (IrInstruction){ .opcode = IR_SHORT_END, .type = IR_BOOL,
            .left = left.value.id, .right = right.value.id, .source_offset = node->source_offset }, &out->value);
    }
    if (!binary_types(source, node->kind, node->value.comparison, &left, &right, node->source_offset) ||
        !mandatory_rhs(source, s, node, scope, program, &left, &right)) return false;
    out->runtime = left.runtime || right.runtime;
    out->number.type = node->kind == EXPRESSION_COMPARE ? IR_BOOL : left.number.type;
    out->value.type = out->number.type;
    if (!out->runtime) {
        out->known = evaluate && left.known && right.known;
        return !out->known || numeric_binary(source, node->source_offset, node->kind, node->value.comparison,
                                             left.number, right.number, &out->number);
    }
    if (!ensure_value(source, program, &left, emit) || !ensure_value(source, program, &right, emit)) return false;
    return expression_emit(source, program, emit, (IrInstruction){ .opcode = binary_opcode(node->kind),
        .type = out->number.type, .comparison_type = left.number.type, .comparison = node->value.comparison,
        .left = left.value.id, .right = right.value.id, .source_offset = node->source_offset }, &out->value);
}
static bool lower_boolean(Source *source, const Syntax *s, uint32_t id, const LocalScope *scope,
                          IrProgram *program, IrValue *value) {
    ExpressionValue result;
    if (!lower_expression(source, s, id, scope, program, &result)) return false;
    if (result.number.type != IR_BOOL)
        return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, result.value.source_offset, "control condition requires bool");
    if (!ensure_value(source, program, &result, true)) return false;
    *value = result.value; return true;
}
static ExpressionKind update_kind(UpdateKind operation) {
    switch (operation) {
        case UPDATE_ADD: case UPDATE_INCREMENT: return EXPRESSION_ADD;
        case UPDATE_SUBTRACT: case UPDATE_DECREMENT: return EXPRESSION_SUBTRACT;
        case UPDATE_MULTIPLY: return EXPRESSION_MULTIPLY;
        case UPDATE_DIVIDE: return EXPRESSION_DIVIDE;
        case UPDATE_REMAINDER: return EXPRESSION_REMAINDER;
        case UPDATE_BIT_AND: return EXPRESSION_BIT_AND;
        case UPDATE_BIT_OR: return EXPRESSION_BIT_OR;
        case UPDATE_BIT_XOR: return EXPRESSION_BIT_XOR;
        case UPDATE_SHIFT_LEFT: return EXPRESSION_SHIFT_LEFT;
        case UPDATE_SHIFT_RIGHT: return EXPRESSION_SHIFT_RIGHT;
        case UPDATE_SET: break;
    }
    return EXPRESSION_ADD;
}
static bool lower_assignment(Source *source, const Syntax *s, const Assignment *assignment,
                             const LocalScope *scope, IrProgram *program) {
    uint32_t binding = 0u;
    IrType type;
    IrValue index = {0};
    if (assignment->local) {
        Name name;
        if (!span_name(source, assignment->target.storage, &name)) return false;
        const LocalBinding *local = find_local(scope, &name);
        if (local == NULL || !local->mutable)
            return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, name.offset,
                                "assignment target must be a visible mutable local");
        if (!check_local_use(source, scope, local, name.offset)) return false;
        binding = local->local_id; type = local->value.type;
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
    ExpressionValue previous = { .runtime = true, .number.type = type }, result = {0};
    if (assignment->operation != UPDATE_SET) {
        if (increment && type != IR_U32 && type != IR_I32)
            return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, offset, "increment requires i32 or u32");
        if (!append(source, program, (IrInstruction){ .opcode = assignment->local ? IR_LOCAL_LOAD : IR_ROBUST_LOAD,
            .type = type, .comparison_type = index.type, .binding = binding,
            .left = index.id, .source_offset = offset }, &previous.value)) return false;
    }
    if (increment) {
        result = (ExpressionValue){ .known = true, .number = { .bits = 1u, .type = type },
                                    .value = { .type = type, .source_offset = offset } };
    } else if (!lower_expression(source, s, assignment->expression, scope, program, &result)) return false;
    if (assignment->operation != UPDATE_SET) {
        const ExpressionKind kind = update_kind(assignment->operation);
        const Expression node = { .kind = kind, .right = assignment->expression, .source_offset = (uint32_t)offset };
        if (!binary_types(source, kind, COMPARE_EQUAL, &previous, &result, offset) ||
            !mandatory_rhs(source, s, &node, scope, program, &previous, &result) ||
            !ensure_value(source, program, &result, true)) return false;
        if (!append(source, program, (IrInstruction){ .opcode = binary_opcode(kind), .type = type,
            .comparison_type = type, .left = previous.value.id, .right = result.value.id,
            .source_offset = offset }, &result.value)) return false;
        result.number.type = type;
    } else if (!materialize(source, &result, type) || !ensure_value(source, program, &result, true)) return false;
    if (result.number.type != type)
        return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, result.value.source_offset,
                            "assignment value and destination must have the same scalar type");
    return append(source, program, (IrInstruction){ .opcode = assignment->local ? IR_LOCAL_STORE : IR_ROBUST_STORE,
        .type = type, .comparison_type = index.type, .left = index.id, .right = result.value.id,
        .binding = binding, .source_offset = offset }, NULL);
}
static bool lower_local(Source *source, const Syntax *s, const LocalSyntax *local,
                        LocalScope *scope, IrProgram *program) {
    Name name;
    if (!span_name(source, local->name, &name) || !valid_name(source, &name)) return false;
    bool duplicate = false;
    for (size_t i = scope->scope_start; i < scope->count; ++i)
        duplicate = duplicate || same_name(&name, &scope->values[i].name);
    if (scope->level == 0u && (find_parameter(s, &name) != s->parameter_count ||
        (s->has_alias && same_name(&name, &s->alias)))) duplicate = true;
    if (duplicate)
        return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, name.offset,
                            "local declaration duplicates a binding in its scope");
    IrType annotation = local->type;
    if (local->annotated && !scalar_name(source, s, scope, local->annotation, &annotation)) return false;
    ExpressionValue result = { .known = true, .number.type = annotation,
                               .value = { .type = annotation, .source_offset = name.offset } };
    if (local->initialized) {
        if (!lower_expression(source, s, local->expression, scope, program, &result)) return false;
        if (local->constant && result.runtime)
            return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, name.offset,
                                "const initializer requires a const expression");
        if (local->annotated) {
            if (!materialize(source, &result, annotation)) return false;
        } else if (!local->constant && abstract_type(result.number.type) &&
                   !materialize(source, &result, result.number.type == IR_ABSTRACT_INT ? IR_I32 : IR_F32)) return false;
    }
    if (!local->constant && !ensure_value(source, program, &result, true)) return false;
    if (scope->count == scope->capacity)
        return source_error(source, DOE_COMPILER_INPUT_ERROR, name.offset, "local scope capacity exceeded");
    LocalBinding binding = { .name = name, .value = result.value, .constant_value = result.number,
        .mutable = local->mutable, .constant = local->constant, .declaration_offset = (uint32_t)name.offset };
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
            case STATEMENT_CONST:
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

/* Iterative dependency traversal uses each unresolved declaration's dead value
 * slot as its cursor and parent link. No declaration-count C recursion. Scanning
 * every initializer node also visits dependencies in short-circuited operands. */
static bool resolve_constants(Source *source, Syntax *s, IrProgram *program, const LocalScope *scope) {
    for (size_t root = 0; root < s->constant_count; ++root) {
        if (s->constants[root].state == 2u) continue;
        uint32_t current = (uint32_t)root;
        s->constants[current].state = 1u;
        s->constants[current].value.dependency.scan = s->constants[current].first_expression;
        s->constants[current].value.dependency.parent = UINT32_MAX;
        while (current != UINT32_MAX) {
            ConstantSyntax *constant = &s->constants[current];
            bool descend = false;
            while (constant->value.dependency.scan <= constant->expression) {
                const Expression *node = &s->expressions[constant->value.dependency.scan++];
                if (node->kind != EXPRESSION_NAME) continue;
                Name name;
                if (!span_name(source, node->value.name, &name)) return false;
                const size_t dependency = find_constant(source, s, &name);
                if (dependency == s->constant_count) continue;
                if (s->constants[dependency].state == 1u)
                    return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, node->source_offset,
                                        "module constant dependency cycle");
                if (s->constants[dependency].state == 2u) continue;
                ConstantSyntax *child = &s->constants[dependency];
                child->state = 1u;
                child->value.dependency.scan = child->first_expression;
                child->value.dependency.parent = current;
                current = (uint32_t)dependency; descend = true; break;
            }
            if (descend) continue;
            const uint32_t parent = constant->value.dependency.parent;
            ExpressionValue value;
            if (!expression_value(source, s, constant->expression, scope, program, true, false, &value)) return false;
            if (value.runtime || !value.known)
                return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, constant->name.offset,
                                    "module const initializer requires a const expression");
            if (constant->annotation.length != 0u) {
                IrType type;
                if (!scalar_name(source, s, scope, constant->annotation, &type) ||
                    !materialize(source, &value, type)) return false;
            }
            constant->value.bits = value.number.bits; constant->type = value.number.type; constant->state = 2u;
            current = parent;
        }
    }
    return true;
}
static bool resolve_attribute(Source *source, const Syntax *s, Integer *attribute,
                              IrProgram *program, const LocalScope *scope) {
    if (attribute->expression == UINT32_MAX) return true;
    ExpressionValue value;
    if (!expression_value(source, s, attribute->expression, scope, program, true, false, &value)) return false;
    if (value.runtime || !value.known || !integer_type(value.number.type))
        return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, attribute->offset,
                            "attribute requires a scalar integer const expression");
    if ((value.number.type == IR_ABSTRACT_INT && value.number.bits > UINT32_MAX) ||
        (value.number.type == IR_I32 && (value.number.bits & UINT64_C(0x80000000)) != 0u))
        return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, attribute->offset,
                            "attribute value must be a nonnegative 32-bit integer");
    attribute->value = (uint32_t)value.number.bits;
    return true;
}
static bool resolve_attributes(Source *source, Syntax *s, IrProgram *program, const LocalScope *scope) {
    uint32_t product = 1u;
    for (size_t i = 0; i < 3u; ++i) {
        Integer *dimension = &s->local_size[i];
        if (!resolve_attribute(source, s, dimension, program, scope)) return false;
        if (dimension->value == 0u || dimension->value > 256u || dimension->value > 256u / product)
            return source_error(source, DOE_COMPILER_UNSUPPORTED, dimension->offset,
                                "workgroup dimensions and product must be between 1 and 256");
        product *= dimension->value;
        program->local_size[i] = dimension->value;
    }
    for (size_t i = 0; i < s->resource_count; ++i) {
        ResourceSyntax *binding = &s->resources[i];
        if (!resolve_attribute(source, s, &binding->group, program, scope) ||
            !resolve_attribute(source, s, &binding->binding, program, scope)) return false;
        if (binding->group.value > 3u || binding->binding.value > 31u)
            return source_error(source, DOE_COMPILER_UNSUPPORTED,
                                binding->group.value > 3u ? binding->group.offset : binding->binding.offset,
                                "storage requires group 0..3 and binding 0..31");
        for (size_t j = 0; j < i; ++j)
            if (binding->group.value == s->resources[j].group.value && binding->binding.value == s->resources[j].binding.value)
                return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, binding->binding.offset,
                                    "duplicate storage group and binding pair");
        program->bindings[i].group = binding->group.value;
        program->bindings[i].binding = binding->binding.value;
    }
    return true;
}
bool validate_syntax(Source *source, Syntax *s, IrProgram *program, LocalBinding *bindings) {
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
        if (!span_name(source, syntax->name, &type->name)) return false;
        type->member_count = (uint32_t)syntax->member_count;
        type->size_bytes = 4u * type->member_count;
        type->alignment_bytes = 4u;
        for (size_t j = 0; j < syntax->member_count; ++j) {
            type->members[j] = (IrMember){ .scalar = syntax->members[j].scalar,
                .offset_bytes = 4u * (uint32_t)j, .size_bytes = 4u, .alignment_bytes = 4u };
            if (!span_name(source, syntax->members[j].name, &type->members[j].name)) return false;
        }
    }
    for (size_t i = 0; i < s->resource_count; ++i) {
        const ResourceSyntax *binding = &s->resources[i];
        IrBinding *ir = &program->bindings[i];
        *ir = (IrBinding){
            .group = binding->group.value, .binding = binding->binding.value,
            .scalar = binding->scalar, .access = binding->access, .kind = binding->kind,
            .struct_index = DOE_COMPILER_NO_STRUCT, .type_alignment_bytes = 4u };
        if (!span_name(source, binding->name, &ir->name)) return false;
        if (binding->kind == DOE_COMPILER_STORAGE_BUFFER) {
            ir->element_stride = 4u;
            ir->runtime_sized = 1u;
            ir->minimum_binding_size_bytes = 4u;
            ir->address_space_alignment_bytes = 4u;
        } else {
            ir->struct_index = (uint32_t)find_struct(source, s, binding->type_name);
            ir->minimum_binding_size_bytes = program->structs[ir->struct_index].size_bytes;
            ir->address_space_alignment_bytes = 16u;
        }
    }
    LocalScope scope = { .values = bindings, .capacity = source->limits->locals };
    if (!resolve_constants(source, s, program, &scope) || !resolve_attributes(source, s, program, &scope)) return false;
    if (s->has_alias) {
        IrValue alias;
        if (!lower_input(source, program, find_parameter(s, &s->alias_source),
                         0u, s->alias_source.offset, &alias)) return false;
    }
    /* Unreachable statements are lowered too: no dead-code validation bypass. */
    unsigned behavior;
    return lower_block(source, s, 0u, s->statement_count, &scope, program, NULL,
                       true, true, false, &behavior);
}
