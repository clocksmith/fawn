/* SPDX-License-Identifier: MIT
 * Typed zero construction avoids relying on default native matrix initialization.
 * Aggregate helpers fill fields/columns, never padding or flattened repetitions. */
#include "private.h"

void metal_zero(MetalEmitter *e, TypeId type) {
    if (type >= IR_PRIMITIVE_COUNT && type - IR_PRIMITIVE_COUNT >= e->module->type_count) {
        metal_error(e, "Metal zero type identity is invalid");
        return;
    }
    if (type >= IR_PRIMITIVE_COUNT) {
        const TypeRecord *t = &e->module->types[type - IR_PRIMITIVE_COUNT];
        if (t->kind == TYPE_POINTER) { metal_text(e, "nullptr"); return; }
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
                !zero_used(e, i + IR_PRIMITIVE_COUNT)) continue;
            metal_text(e, "inline ");
            metal_type(e, i + IR_PRIMITIVE_COUNT);
            metal_format(e, " doe_zero%u()", i + IR_PRIMITIVE_COUNT);
            if (pass == 0u) { metal_text(e, ";\n"); continue; }
            metal_text(e, " {\n    ");
            metal_type(e, i + IR_PRIMITIVE_COUNT);
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

/* Atomic objects are never copied or returned by value. Initialize their
 * selected threadgroup tree in place, on lane zero before the existing barrier. */
static bool workgroup_zero_used(MetalEmitter *e, TypeId type) {
    for (uint32_t i = 0u; i < e->module->workgroup_count && zero_spend(e); ++i)
        if (metal_selected_workgroup(e, i) && zero_dependency(e, e->module->workgroups[i].type, type)) return true;
    return false;
}
static void workgroup_zero_child(MetalEmitter *e, TypeId type) {
    if ((type_flags(e->module, type) & TYPE_ATOMIC_CONTENT) != 0u)
        metal_format(e, "doe_wzero%u(", type);
    else metal_text(e, "*(");
}
static void workgroup_zero_finish(MetalEmitter *e, TypeId type) {
    if ((type_flags(e->module, type) & TYPE_ATOMIC_CONTENT) != 0u) metal_text(e, ");\n");
    else {
        metal_text(e, ") = ");
        metal_zero(e, type);
        metal_text(e, ";\n");
    }
}
void metal_workgroup_zero_helpers(MetalEmitter *e) {
    const CompilerWorkspace *c = e->module;
    for (unsigned pass = 0u; pass < 2u; ++pass) {
        for (uint32_t i = 0u; i < c->type_count && !e->failed; ++i) {
            const TypeRecord *t = &c->types[i];
            const TypeId type = i + IR_PRIMITIVE_COUNT;
            if ((t->flags & (TYPE_ATOMIC_CONTENT | TYPE_FIXED)) != (TYPE_ATOMIC_CONTENT | TYPE_FIXED) ||
                !workgroup_zero_used(e, type)) continue;
            metal_format(e, "inline void doe_wzero%u(threadgroup ", type);
            metal_type(e, type);
            metal_text(e, "* p)");
            if (pass == 0u) { metal_text(e, ";\n"); continue; }
            metal_text(e, " {\n");
            if (t->kind == TYPE_ATOMIC)
                metal_format(e, "    atomic_store_explicit(p, %s(0), memory_order_relaxed);\n",
                             t->element == IR_I32 ? "int" : "uint");
            else if (t->kind == TYPE_ARRAY) {
                metal_format(e, "    for (uint i = 0u; i < %uu; ++i) ", t->count);
                workgroup_zero_child(e, t->element);
                metal_text(e, "&((*p)[i])");
                workgroup_zero_finish(e, t->element);
            } else if (t->kind == TYPE_STRUCT) {
                for (uint32_t j = 0u; j < t->count; ++j) {
                    const TypeId child = type_child(c, type, j);
                    metal_text(e, "    ");
                    workgroup_zero_child(e, child);
                    metal_format(e, "&(p->m%u)", j);
                    workgroup_zero_finish(e, child);
                }
            } else metal_error(e, "Metal atomic workgroup initializer has invalid type");
            metal_text(e, "}\n");
        }
    }
}
