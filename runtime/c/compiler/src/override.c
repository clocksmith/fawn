/* SPDX-License-Identifier: MIT */
#include "override.h"
#include <string.h>

bool override_charge(Source *s, uint64_t count) {
    const uint64_t limit = UINT64_C(67108864);
    if (count > limit - s->specialization->work)
        return source_error(s, DOE_COMPILER_UNSUPPORTED, s->length, "override semantic work format exhausted");
    s->specialization->work += count;
    return true;
}
OverrideRecord *override_declaration(Source *s, uint32_t declaration) {
    SpecializationContext *context = s->specialization;
    if (context == NULL) return NULL;
    /* Parser order keeps declaration ordinals sorted; dependency resolution
     * never performs a cumulative linear lookup for each initializer edge. */
    uint32_t first = 0u, end = context->count;
    while (first < end) {
        if (!override_charge(s, 1u)) return NULL;
        const uint32_t middle = first + (end - first) / 2u;
        const uint32_t id = context->records[middle].declaration;
        if (id == declaration) return &context->records[middle];
        if (id < declaration) first = middle + 1u;
        else end = middle;
    }
    return NULL;
}
bool override_finish(Source *s, uint32_t declaration, const LocalScope *scope) {
    ConstantSyntax *syntax = &s->compiler->constant_decls[declaration];
    OverrideRecord *record = override_declaration(s, declaration);
    if (s->diagnostic->status != DOE_COMPILER_OK) return false;
    if (record == NULL)
        return source_error(s, DOE_COMPILER_INPUT_ERROR, syntax->name.offset, "missing override declaration owner");
    const bool deferred = s->specialization->specializing &&
        (record->required == 0u || record->supplied != NO_ID);
    TypeId annotation = deferred ? record->type : IR_VOID;
    if (syntax->annotation.offset != NO_ID &&
        !sema_type_reference(s, syntax->annotation, scope, &annotation)) return false;
    ExpressionValue value = {.constant.type = annotation, .value = {.type = annotation,
        .source_offset = syntax->name.offset}};
    const bool evaluate = !s->specialization->specializing ||
        (record->required != 0u && record->supplied == NO_ID);
    if (!deferred && syntax->expression != OVERRIDE_NO_INITIALIZER) {
        if (!sema_expression(s, syntax->expression, scope, evaluate, false, &value)) return false;
        if (value.runtime)
            return source_error(s, DOE_COMPILER_SEMANTIC_ERROR, syntax->name.offset,
                "override initializer must be an override expression");
        if (annotation == IR_VOID &&
            !sema_default_type(s, value.constant.type, syntax->name.offset, &annotation)) return false;
        if (!sema_materialize(s, &value, annotation)) return false;
    }
    if (annotation < IR_BOOL || annotation > IR_F32)
        return source_error(s, DOE_COMPILER_SEMANTIC_ERROR, syntax->name.offset,
                            "override requires a concrete scalar type");
    if (record->id_expression != NO_ID) {
        if (!integer_attribute(s, record->id_expression, scope, true, &record->id)) return false;
        if (record->id > 65535u)
            return source_error(s, DOE_COMPILER_SEMANTIC_ERROR, syntax->name.offset,
                                "override id must be between 0 and 65535");
        for (uint32_t i = 0u; i < s->specialization->count; ++i) {
            if (!override_charge(s, 1u)) return false;
            const OverrideRecord *other = &s->specialization->records[i];
            if (other != record && other->id == record->id)
                return source_error(s, DOE_COMPILER_SEMANTIC_ERROR, syntax->name.offset,
                                    "duplicate override id");
        }
    }
    if (s->specialization->specializing && record->supplied != NO_ID) {
        NumericValue converted;
        if (!numeric_override(s, syntax->name.offset,
                s->specialization->values[record->supplied].value, (IrType)annotation, &converted)) return false;
        value.known = true;
        value.constant = (ConstantValue){annotation, 0u, converted.bits};
    }
    if (s->specialization->specializing && record->required != 0u && !value.known)
        return source_error(s, DOE_COMPILER_SEMANTIC_ERROR, syntax->name.offset,
                            "statically accessed override has no pipeline value");
    syntax->type = annotation;
    record->type = annotation;
    syntax->value.bits = value.known ? value.constant.bits : 0u;
    syntax->state = 2u | DECLARATION_OVERRIDE |
        (s->specialization->specializing && value.known &&
            (record->required != 0u || record->supplied != NO_ID) ? DECLARATION_KNOWN : 0u);
    return true;
}
bool override_integer(Source *s, uint32_t expression, const LocalScope *scope,
                      uint32_t *out, bool *deferred) {
    ExpressionValue value;
    if (!sema_expression(s, expression, scope, true, false, &value)) return false;
    if (value.constant.type == IR_ABSTRACT_INT && !sema_materialize(s, &value, IR_I32)) return false;
    if (value.runtime || (value.constant.type != IR_U32 && value.constant.type != IR_I32))
        return source_error(s, DOE_COMPILER_SEMANTIC_ERROR, value.value.source_offset,
                            "pipeline count requires an integer override expression");
    *deferred = value.overridable && !value.known;
    if (*deferred) return true;
    if (!value.known || (value.constant.type == IR_I32 &&
            (value.constant.bits & UINT64_C(0x80000000)) != 0u) || value.constant.bits > UINT32_MAX)
        return source_error(s, DOE_COMPILER_SEMANTIC_ERROR, value.value.source_offset,
                            "pipeline count must be a nonnegative 32-bit integer");
    *out = (uint32_t)value.constant.bits;
    return true;
}

bool override_array_type(Source *s, TypeId element, uint32_t syntax, TypeId *out) {
    CompilerWorkspace *c = s->compiler;
    const uint32_t expression = c->generic_syntax[syntax].count_expression;
    if ((type_flags(c, element) & TYPE_FIXED) == 0u || type_abstract(c, element))
        return source_error(s, DOE_COMPILER_SEMANTIC_ERROR, c->expressions[expression].source_offset,
                            "override-sized array element requires a creation-fixed footprint");
    /* Expression identity is significant for override-sized array equivalence.
     * No element count, byte size or constructible value is fabricated. */
    for (uint32_t i = 0u; i < c->type_count; ++i) {
        const TypeRecord *type = &c->types[i];
        if ((type->flags & TYPE_DEFERRED) != 0u && type->element == element && type->aux == syntax) {
            *out = i + IR_PRIMITIVE_COUNT;
            return true;
        }
    }
    if (c->type_count == c->limits.types)
        return source_pool_error(s, DOE_COMPILER_POOL_TYPES, (size_t)c->type_count + 1u,
            c->expressions[expression].source_offset, "deferred array type capacity exceeded");
    *out = c->type_count + IR_PRIMITIVE_COUNT;
    c->types[c->type_count++] = (TypeRecord){.kind = TYPE_ARRAY,
        .flags = TYPE_RESOLVED | TYPE_DEFERRED | (type_flags(c, element) & TYPE_ATOMIC_CONTENT),
        .element = element, .count = NO_ID, .aux = syntax,
        .state.layout.alignment = type_alignment(c, element)};
    return true;
}

bool override_workgroup_size(Source *s, EntryRecord *entry, const LocalScope *scope) {
    ExpressionValue values[3];
    TypeId common = IR_ABSTRACT_INT;
    for (uint32_t i = 0u; i < 3u; ++i) {
        const Integer *syntax = &entry->state.dimensions[i];
        values[i] = (ExpressionValue){.known=true, .constant={IR_ABSTRACT_INT, 0u, syntax->value},
            .value={.type=IR_ABSTRACT_INT,.source_offset=syntax->offset}};
        if (syntax->expression != NO_ID && !sema_expression(s, syntax->expression, scope, true, false, &values[i])) return false;
        const TypeId type = values[i].constant.type;
        if (values[i].runtime || (type != IR_U32 && type != IR_I32 && type != IR_ABSTRACT_INT))
            return source_error(s, DOE_COMPILER_SEMANTIC_ERROR, values[i].value.source_offset,
                                "workgroup dimensions require integer override expressions");
        if (!sema_common_type(s, common, type, values[i].value.source_offset, &common)) return false;
    }
    if (!sema_default_type(s, common, values[0].value.source_offset, &common)) return false;
    uint32_t dimensions[3], product = 1u;
    for (uint32_t i = 0u; i < 3u; ++i) {
        ExpressionValue *value = &values[i];
        if (!sema_materialize(s, value, common)) return false;
        if (!value->known) {
            dimensions[i] = NO_ID;
            continue;
        }
        if ((common == IR_I32 && (value->constant.bits & UINT64_C(0x80000000)) != 0u) ||
            value->constant.bits == 0u || value->constant.bits > UINT32_MAX)
            return source_error(s, DOE_COMPILER_SEMANTIC_ERROR, value->value.source_offset,
                                "workgroup dimension must be positive");
        dimensions[i] = (uint32_t)value->constant.bits;
        if (s->specialization->specializing) {
            if (dimensions[i] > UINT32_MAX / product)
                return source_error(s, DOE_COMPILER_UNSUPPORTED, value->value.source_offset,
                                    "workgroup dimension product must fit u32");
            product *= dimensions[i];
        }
    }
    memset(&entry->state, 0, sizeof(entry->state));
    for (uint32_t i = 0u; i < 3u; ++i) entry->state.resolved.workgroup_size[i] = dimensions[i];
    return true;
}
