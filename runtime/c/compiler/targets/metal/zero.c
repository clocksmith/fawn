/* SPDX-License-Identifier: MIT
 * Typed zero construction avoids relying on default native matrix initialization.
 * Aggregate helpers fill fields/columns, never padding or flattened repetitions. */
#include "private.h"

void metal_zero(MetalEmitter *e, TypeId type) {
    if (type >= 7u && type - 7u >= e->module->type_count) {
        metal_error(e, "Metal zero type identity is invalid");
        return;
    }
    if (type >= 7u) {
        const TypeRecord *t = &e->module->types[type - 7u];
        if (t->kind == TYPE_ARRAY || t->kind == TYPE_STRUCT) {
            metal_format(e, "doe_zero%u()", type);
            return;
        }
    }
    metal_type(e, type);
    metal_text(e, type_matrix(e->module, type) ? "(0.0f)" : "{}");
}
static bool zero_dependency(MetalEmitter *e, TypeId root, TypeId wanted) {
    const char *error = NULL;
    const bool used = target_memory_dependency(e->module, &e->closure, root, wanted,
        &e->traversal_left, &error);
    if (error != NULL) metal_error(e, error);
    return used;
}
static bool zero_spend(MetalEmitter *e) {
    const char *error = NULL;
    if (target_spend(&e->traversal_left, &error)) return true;
    metal_error(e, error);
    return false;
}
static bool zero_used(MetalEmitter *e, TypeId type) {
    const CompilerWorkspace *c = e->module;
    for (uint32_t i = 0u; i < c->workgroup_count && zero_spend(e); ++i)
        if (metal_selected_workgroup(e, i) && zero_dependency(e, c->workgroups[i].type, type)) return true;
    for (uint32_t i = 0u; i < c->function_count && !e->failed && zero_spend(e); ++i) {
        if (!target_function_used(&e->closure, i)) continue;
        const FunctionRecord *f = &c->functions[i];
        if (f->instruction_start > f->instruction_end || f->instruction_end > c->instruction_count ||
            f->local_start > c->local_count || f->local_count > c->local_count - f->local_start) {
            metal_error(e, "Metal zero root function range is invalid");
            return false;
        }
        for (uint32_t j = f->local_start; j < f->local_start + f->local_count && !e->failed; ++j)
            if (zero_dependency(e, c->locals[j].type, type)) return true;
        for (uint32_t j = f->instruction_start; j < f->instruction_end && !e->failed; ++j)
            if (c->instructions[j].result != 0u && zero_dependency(e, c->instructions[j].type, type)) return true;
    }
    return false;
}
void metal_zero_helpers(MetalEmitter *e) {
    const CompilerWorkspace *c = e->module;
    for (unsigned pass = 0u; pass < 2u; ++pass) {
        for (uint32_t i = 0u; i < c->type_count && !e->failed; ++i) {
            const TypeRecord *t = &c->types[i];
            if ((t->kind != TYPE_ARRAY && t->kind != TYPE_STRUCT) ||
                (t->flags & TYPE_CONSTRUCTIBLE) == 0u || (t->flags & TYPE_ABSTRACT) != 0u ||
                !zero_used(e, i + 7u)) continue;
            metal_text(e, "inline ");
            metal_type(e, i + 7u);
            metal_format(e, " doe_zero%u()", i + 7u);
            if (pass == 0u) { metal_text(e, ";\n"); continue; }
            metal_text(e, " {\n    ");
            metal_type(e, i + 7u);
            metal_text(e, " value;\n");
            if (t->kind == TYPE_ARRAY) {
                metal_format(e, "    for (uint i = 0u; i < %uu; ++i) value[i] = ", t->count);
                metal_zero(e, t->element);
                metal_text(e, ";\n");
            } else {
                for (uint32_t j = 0u; j < t->count; ++j) {
                    metal_format(e, "    value.m%u = ", j);
                    metal_zero(e, c->members[t->element + j].type);
                    metal_text(e, ";\n");
                }
            }
            metal_text(e, "    return value;\n}\n");
        }
    }
}
