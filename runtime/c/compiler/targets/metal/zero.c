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
        if (metal_selected_workgroup(e, i) && c->workgroups[i].state.resolved.address_space == MODULE_PRIVATE &&
            zero_dependency(e, c->workgroups[i].type, type)) return true;
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

/* Each typed leaf path flattens its array dimensions into one index domain.
 * Every element has one writer even inside arrays of structs or nested arrays;
 * vectors/matrices stay typed values, and atomic objects are stored in place. */
typedef struct { TypeId type; uint32_t child; } ZeroFrame;
static void workgroup_zero_leaf(MetalEmitter *e, const ZeroFrame *path, uint32_t depth, TypeId type) {
    const CompilerWorkspace *c = e->module;
    uint32_t count = 1u;
    for (uint32_t j = 0u; j < depth && zero_spend(e); ++j) {
        const TypeRecord *t = &c->types[path[j].type - IR_PRIMITIVE_COUNT];
        if (t->kind != TYPE_ARRAY) continue;
        if (!t->count || count > UINT32_MAX / t->count) {
            metal_error(e, "Metal workgroup initialization extent overflows"); return;
        }
        count *= t->count;
    }
    metal_format(e, "    for (uint z = lane; z < %uu;) {\n        ", count);
    const TypeRecord *leaf = type >= IR_PRIMITIVE_COUNT ? &c->types[type - IR_PRIMITIVE_COUNT] : NULL;
    const bool atomic = leaf != NULL && leaf->kind == TYPE_ATOMIC;
    if (atomic) metal_text(e, "atomic_store_explicit(&(");
    metal_text(e, "(*p)");
    uint32_t divisor = count;
    for (uint32_t j = 0u; j < depth && zero_spend(e); ++j) {
        const TypeRecord *t = &c->types[path[j].type - IR_PRIMITIVE_COUNT];
        if (t->kind == TYPE_ARRAY) {
            divisor /= t->count;
            metal_format(e, "[(z / %uu) %% %uu]", divisor, t->count);
        } else metal_format(e, ".m%u", path[j].child - 1u);
    }
    if (atomic) metal_format(e, "), %s(0), memory_order_relaxed);\n", leaf->element == IR_I32 ? "int" : "uint");
    else { metal_text(e, " = "); metal_zero(e, type); metal_text(e, ";\n"); }
    metal_format(e, "        if (%uu - z <= stride) break;\n        z += stride;\n    }\n", count);
}
static void workgroup_zero_body(MetalEmitter *e, TypeId type) {
    ZeroFrame path[TARGET_CLOSURE_DEPTH] = {{type, 0u}};
    uint32_t depth = 0u;
    for (;;) {
        if (!zero_spend(e)) return;
        const TypeId current = path[depth].type;
        if (current >= IR_PRIMITIVE_COUNT + e->module->type_count) {
            metal_error(e, "Metal workgroup zero type identity is invalid"); return;
        }
        const TypeRecord *t = current >= IR_PRIMITIVE_COUNT ? &e->module->types[current - IR_PRIMITIVE_COUNT] : NULL;
        const uint32_t children = t && t->kind == TYPE_ARRAY ? 1u : t && t->kind == TYPE_STRUCT ? t->count : 0u;
        if (t && t->kind == TYPE_STRUCT && (t->element > e->module->member_count ||
            t->count > e->module->member_count - t->element)) {
            metal_error(e, "Metal workgroup zero member range is invalid"); return;
        }
        if (!children || path[depth].child == children) {
            if (!children) workgroup_zero_leaf(e, path, depth, current);
            if (!depth || e->failed) return;
            --depth; continue;
        }
        const TypeId child = type_child(e->module, current, path[depth].child++);
        if (depth + 1u == TARGET_CLOSURE_DEPTH) {
            metal_error(e, "Metal workgroup zero exceeds bounded nesting"); return;
        }
        path[++depth] = (ZeroFrame){child, 0u};
    }
}
void metal_workgroup_zero_helpers(MetalEmitter *e) {
    const CompilerWorkspace *c = e->module;
    for (TypeId type = IR_BOOL; type < IR_PRIMITIVE_COUNT + c->type_count && !e->failed; ++type) {
        bool used = false;
        for (uint32_t i = 0u; i < c->workgroup_count && zero_spend(e); ++i)
            if (metal_selected_workgroup(e, i) && c->workgroups[i].type == type &&
                c->workgroups[i].state.resolved.address_space == MODULE_WORKGROUP) used = true;
        if (!used) continue;
        metal_format(e, "inline void doe_wzero%u(threadgroup ", type);
        metal_type(e, type);
        metal_text(e, "* p, uint lane, uint stride) {\n");
        workgroup_zero_body(e, type);
        metal_text(e, "}\n");
    }
}
