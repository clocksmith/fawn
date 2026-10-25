/* SPDX-License-Identifier: MIT */
#include "override.h"
#include "compile_internal.h"
#include <string.h>

static bool key_matches(const Source *s, const OverrideRecord *record,
                        const DoeCompilerOverrideValueV13 *value) {
    if ((record->flags & DOE_COMPILER_OVERRIDE_HAS_ID) == 0u) {
        const NameSpan name = s->compiler->constant_decls[record->declaration].name;
        return value->key_length == name.length &&
            memcmp(value->key, s->source + name.offset, name.length) == 0;
    }
    if (value->key_length == 0u || value->key_length > 5u ||
        (value->key_length > 1u && value->key[0] == '0')) return false;
    uint32_t id = 0u;
    for (size_t i = 0u; i < value->key_length; ++i) {
        const unsigned char byte = (unsigned char)value->key[i];
        if (byte < '0' || byte > '9') return false;
        id = id * 10u + (uint32_t)(byte - '0');
    }
    return id == record->id;
}
static bool supplied_values(Source *s) {
    SpecializationContext *context = s->specialization;
    if (context->value_count > context->count)
        return source_error(s, DOE_COMPILER_SEMANTIC_ERROR, 0u, "more pipeline keys than module overrides");
    for (size_t i = 0u; i < context->value_count; ++i) {
        const DoeCompilerOverrideValueV13 *value = &context->values[i];
        OverrideRecord *matched = NULL;
        for (uint32_t j = 0u; j < context->count; ++j) {
            if (!override_charge(s, 1u)) return false;
            if (key_matches(s, &context->records[j], value)) matched = &context->records[j];
        }
        if (matched == NULL)
            return source_error(s, DOE_COMPILER_SEMANTIC_ERROR, 0u, "pipeline key does not identify a module override");
        const ConstantSyntax *declaration = &s->compiler->constant_decls[matched->declaration];
        if (matched->supplied != NO_ID)
            return source_error(s, DOE_COMPILER_SEMANTIC_ERROR, declaration->name.offset, "duplicate pipeline override key");
        NumericValue converted;
        if (!numeric_override(s, declaration->name.offset, value->value,
                (IrType)declaration->type, &converted)) return false;
        matched->supplied = (uint32_t)i;
    }
    return true;
}
static bool require_reference(Source *s, SourceReference reference) {
    if (!override_charge(s, 1u)) return false;
    SpecializationContext *context = s->specialization;
    if (reference.kind == SOURCE_OVERRIDE) {
        OverrideRecord *record = override_declaration(s, reference.index);
        if (record != NULL) record->required = 1u;
    } else if (reference.kind == SOURCE_WORKGROUP) context->workgroups[reference.index] = 1u;
    return s->diagnostic->status == DOE_COMPILER_OK;
}
static bool require_range(Source *s, uint32_t first, uint32_t end) {
    for (uint32_t i = first; i < end; ++i)
        if (!require_reference(s, s->compiler->expression_references[i])) return false;
    return true;
}
static bool require_functions(Source *s, uint32_t entry) {
    CompilerWorkspace *c = s->compiler;
    SpecializationContext *context = s->specialization;
    uint32_t current = c->entries[entry].function;
    context->functions[current] = 1u;
    c->functions[current].state.graph.parent = NO_ID;
    c->functions[current].state.graph.scan = c->functions[current].expression_start;
    while (current != NO_ID) {
        FunctionRecord *function = &c->functions[current];
        if (function->state.graph.scan < function->expression_end) {
            const SourceReference reference = c->expression_references[function->state.graph.scan++];
            if (!require_reference(s, reference)) return false;
            if (reference.kind == SOURCE_FUNCTION && context->functions[reference.index] == 0u) {
                FunctionRecord *child = &c->functions[reference.index];
                context->functions[reference.index] = 1u;
                child->state.graph.scan = child->expression_start;
                child->state.graph.parent = current;
                current = reference.index;
            }
            continue;
        }
        for (uint32_t i = function->statement_start; i < function->statement_end; ++i)
            if (!require_reference(s, c->statement_references[i])) return false;
        current = function->state.graph.parent;
    }
    const uint32_t *dimensions = context->entry_expressions + 6u*entry;
    for (uint32_t i = 0u; i < 3u; ++i)
        if (dimensions[2u*i] != NO_ID && !require_range(s, dimensions[2u*i], dimensions[2u*i + 1u])) return false;
    return true;
}
static bool require_workgroups(Source *s) {
    CompilerWorkspace *c = s->compiler;
    SpecializationContext *context = s->specialization;
    for (uint32_t i = 0u; i < c->workgroup_count; ++i) {
        if (!override_charge(s, 1u)) return false;
        if (context->workgroups[i] == 0u) continue;
        const TypeRecord *type = c->workgroups[i].type >= 7u ? &c->types[c->workgroups[i].type - 7u] : NULL;
        if (type != NULL && (type->flags & TYPE_DEFERRED) != 0u) {
            const GenericSyntax *syntax = &c->generic_syntax[type->aux];
            if (!require_range(s, syntax->flags, syntax->count_expression + 1u)) return false;
        }
    }
    return true;
}
static bool require_dependencies(Source *s) {
    SpecializationContext *context = s->specialization;
    CompilerWorkspace *c = s->compiler;
    for (uint32_t i = 0u; i < context->count; ++i) context->records[i].state = 0u;
    for (uint32_t root = 0u; root < context->count; ++root) {
        if (context->records[root].required == 0u || context->records[root].state != 0u) continue;
        uint32_t current = root;
        context->records[current].parent = NO_ID;
        while (current != NO_ID) {
            OverrideRecord *record = &context->records[current];
            const ConstantSyntax *declaration = &c->constant_decls[record->declaration];
            if (record->state == 0u) {
                record->scan = declaration->first_expression;
                record->state = 1u;
            }
            const uint32_t end = declaration->expression == OVERRIDE_NO_INITIALIZER ?
                declaration->first_expression : declaration->expression + 1u;
            bool descend = false;
            while (record->scan < end) {
                if (!override_charge(s, 1u)) return false;
                const SourceReference reference = c->expression_references[record->scan++];
                if (reference.kind != SOURCE_OVERRIDE) continue;
                OverrideRecord *child = override_declaration(s, reference.index);
                if (s->diagnostic->status != DOE_COMPILER_OK) return false;
                if (child == NULL)
                    return source_error(s, DOE_COMPILER_INPUT_ERROR, declaration->name.offset, "missing override dependency owner");
                child->required = 1u;
                if (child->state != 0u) continue;
                child->parent = current;
                current = (uint32_t)(child - context->records);
                descend = true;
                break;
            }
            if (descend) continue;
            record->state = 2u;
            current = record->parent;
        }
    }
    for (uint32_t i = 0u; i < context->count; ++i) {
        const OverrideRecord *record = &context->records[i];
        if (record->required != 0u && record->supplied == NO_ID &&
            (record->flags & DOE_COMPILER_OVERRIDE_HAS_INITIALIZER) == 0u)
            return source_error(s, DOE_COMPILER_SEMANTIC_ERROR,
                c->constant_decls[record->declaration].name.offset, "statically accessed override has no pipeline value");
    }
    return true;
}
bool override_plan(Source *s) {
    SpecializationContext *context = s->specialization;
    CompilerWorkspace *c = s->compiler;
    if (!compiler_select_entry(s, context->selection, &context->selected) || !supplied_values(s)) return false;
    if (context->metal_pipeline && context->selected != NO_ID &&
        (c->functions[c->entries[context->selected].function].flags & FUNCTION_FRAGMENT) == 0u)
        return source_error(s, DOE_COMPILER_INPUT_ERROR, 0u, "Metal pipeline specialization requires a fragment entry");
    if (c->entry_count == 0u || (!context->all_entries && context->selected == NO_ID))
        return source_error(s, DOE_COMPILER_SEMANTIC_ERROR, 0u, "specialization requires a shader entry");
    memset(context->functions, 0, (size_t)c->function_count * sizeof(*context->functions));
    memset(context->workgroups, 0, c->workgroup_count);
    if (context->all_entries) {
        for (uint32_t i = 0u; i < c->entry_count; ++i)
            if (!require_functions(s, i)) return false;
    } else if (!require_functions(s, context->selected)) return false;
    return require_workgroups(s) && require_dependencies(s);
}
