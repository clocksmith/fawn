/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "private.h"
#include "../closure.h"
#include <inttypes.h>

static const TypeRecord *aggregate(MetalEmitter *e, TypeId type) {
    if (type < IR_PRIMITIVE_COUNT || type - IR_PRIMITIVE_COUNT >= e->module->type_count) {
        metal_error(e, "Metal aggregate type identity is out of range");
        return NULL;
    }
    return &e->module->types[type - IR_PRIMITIVE_COUNT];
}
static void type_name(MetalEmitter *e, TypeId type, unsigned depth) {
    static const char *const scalars[] = {"void", "bool", "int", "uint", "float", "", "", "half"};
    if (depth > DOE_METAL_MAX_DEPTH) {
        metal_error(e, "Metal type nesting exceeds target traversal budget");
        return;
    }
    if (type <= IR_F32 || type == IR_F16) {
        metal_text(e, scalars[type]);
        return;
    }
    const TypeRecord *t = aggregate(e, type);
    if (t == NULL) return;
    if (t->kind == TYPE_POINTER) {
        metal_text(e, "thread ");
        metal_type(e, t->element);
        metal_text(e, "*");
        return;
    }
    if (t->kind == TYPE_TEXTURE || t->kind == TYPE_SAMPLER) {
        metal_handle_type(e, type);
        return;
    }
    if (t->kind == TYPE_ATOMIC && (t->element == IR_I32 || t->element == IR_U32)) {
        metal_text(e, t->element == IR_I32 ? "atomic_int" : "atomic_uint");
        return;
    }
    if ((t->flags & (TYPE_CONSTRUCTIBLE | TYPE_ATOMIC_CONTENT)) == 0u || (t->flags & TYPE_ABSTRACT) != 0u) {
        metal_error(e, "Metal value type is not concrete and constructible");
        return;
    }
    switch (t->kind) {
        case TYPE_VECTOR:
            if (t->element < IR_BOOL || (t->element > IR_F32 && t->element != IR_F16) || t->count < 2u || t->count > 4u) {
                metal_error(e, "Metal vector shape is invalid");
                return;
            }
            metal_format(e, "%s%u", scalars[t->element], t->count);
            break;
        case TYPE_MATRIX: {
            const uint32_t rows = type_vector_width(e->module, t->element);
            if (t->count < 2u || t->count > 4u || rows < 2u || rows > 4u ||
                (type_leaf(e->module, t->element) != IR_F32 && type_leaf(e->module, t->element) != IR_F16)) {
                metal_error(e, "Metal matrix shape is invalid");
                return;
            }
            metal_format(e, "%s%ux%u", scalars[type_leaf(e->module, t->element)], t->count, rows);
            break;
        }
        case TYPE_ARRAY:
            metal_text(e, "array<");
            type_name(e, t->element, depth + 1u);
            metal_format(e, ", %u>", t->count);
            break;
        case TYPE_FREXP_RESULT:
        case TYPE_MODF_RESULT:
        case TYPE_ATOMIC_RESULT:
        case TYPE_STRUCT:
            metal_format(e, "DoeT%u", type);
            break;
        default:
            metal_error(e, "Metal runtime tail is not a by-value type");
            break;
    }
}
void metal_type(MetalEmitter *e, TypeId type) {
    type_name(e, type, 0u);
}
void metal_types(MetalEmitter *e) {
    const CompilerWorkspace *c = e->module;
    const char *error = NULL;
    if (!target_type_order_begin(&e->closure, &e->traversal_left, &error)) {
        metal_error(e, error);
        return;
    }
    for (unsigned depth = 1u; depth <= DOE_METAL_MAX_DEPTH && !e->failed; ++depth) {
        bool pending = false;
        for (uint32_t i = 0u; i < c->type_count && !e->failed; ++i) {
            const TypeRecord *t = &c->types[i];
            if ((t->kind != TYPE_STRUCT && t->kind != TYPE_ATOMIC_RESULT && t->kind != TYPE_FREXP_RESULT && t->kind != TYPE_MODF_RESULT) ||
                (t->flags & (TYPE_CONSTRUCTIBLE | TYPE_ATOMIC_CONTENT)) == 0u ||
                ((t->flags & TYPE_ATOMIC_CONTENT) != 0u && (t->flags & TYPE_FIXED) == 0u)) continue;
            unsigned d;
            if (t->kind == TYPE_ATOMIC_RESULT) d = 1u;
            else if (!target_type_depth(c, &e->closure, i + IR_PRIMITIVE_COUNT, &d, &e->traversal_left, &error)) {
                metal_error(e, error);
                return;
            }
            if (d > depth) pending = true;
            if (d != depth) continue;
            metal_format(e, "struct DoeT%u {\n", i + IR_PRIMITIVE_COUNT);
            for (uint32_t j = 0u; j < t->count; ++j) {
                metal_text(e, "    ");
                metal_type(e, type_child(c, i + IR_PRIMITIVE_COUNT, j));
                metal_format(e, " m%u;\n", j);
            }
            metal_text(e, "};\n");
        }
        if (!pending) break;
    }
}
void metal_constant(MetalEmitter *e, TypeId type, uint64_t bits, bool composite, unsigned depth) {
    if (depth > DOE_METAL_MAX_DEPTH) {
        metal_error(e, "Metal constant graph exceeds target traversal budget");
        return;
    }
    if (!composite) {
        switch (type) {
            case IR_BOOL: metal_text(e, bits == 0u ? "false" : "true"); break;
            case IR_U32: metal_format(e, "0x%08" PRIx32 "u", (uint32_t)bits); break;
            case IR_I32: metal_format(e, "as_type<int>(0x%08" PRIx32 "u)", (uint32_t)bits); break;
            case IR_F16: metal_format(e, "as_type<half>(ushort(0x%04" PRIx32 "u))", (uint32_t)bits); break;
            case IR_F32: metal_format(e, "as_type<float>(0x%08" PRIx32 "u)", (uint32_t)bits); break;
            default: metal_error(e, "Metal scalar constant has unsupported type"); break;
        }
        return;
    }
    if (bits >= e->module->constant_count) {
        metal_error(e, "Metal aggregate constant identity is invalid");
        return;
    }
    const ConstantRecord *k = &e->module->constants[bits];
    if (k->type != type) {
        metal_error(e, "Metal constant type identity mismatch");
        return;
    }
    if (k->kind == CONSTANT_ZERO) { metal_zero(e, type); return; }
    const bool matrix = type_matrix(e->module, type);
    metal_type(e, type);
    metal_text(e, matrix ? "(" : "{");
    if (k->kind != CONSTANT_ZERO) {
        const uint32_t edges = k->kind == CONSTANT_REPEAT ? 1u : k->count;
        if (k->first > e->module->edge_count || edges > e->module->edge_count - k->first) {
            metal_error(e, "Metal constant edge range is invalid");
            return;
        }
        for (uint32_t i = 0u; i < k->count && !e->failed; ++i) {
            if (i != 0u) metal_text(e, ", ");
            const ConstantValue v = e->module->constant_edges[k->first + (k->kind == CONSTANT_REPEAT ? 0u : i)];
            metal_constant(e, v.type, v.bits, (v.flags & CONSTANT_AGGREGATE) != 0u, depth + 1u);
        }
    }
    metal_text(e, matrix ? ")" : "}");
}
static bool memory_type(const CompilerWorkspace *c, TypeId type) {
    return ((type >= IR_I32 && type <= IR_F32) || type == IR_F16) ||
        (type >= IR_PRIMITIVE_COUNT && (c->types[type - IR_PRIMITIVE_COUNT].flags & (TYPE_HOST | TYPE_FIXED)) == (TYPE_HOST | TYPE_FIXED));
}
static void memory_signature(MetalEmitter *e, TypeId type, unsigned mode) {
    metal_text(e, "inline ");
    if (mode == 2u) metal_text(e, "void");
    else metal_type(e, type);
    metal_format(e, " doe_%s%u(%s uchar* p", mode == 2u ? "store" : mode == 1u ? "uniform" : "load", type,
                 mode == 2u ? "device" : mode == 1u ? "constant" : "device const");
    if (mode == 2u) {
        metal_text(e, ", ");
        metal_type(e, type);
        metal_text(e, " value");
    }
    metal_text(e, ")");
}
static void memory_array_child(MetalEmitter *e, TypeId child, unsigned mode, uint64_t stride) {
    if (mode == 2u) {
        metal_format(e, "        doe_store%u(p + %" PRIu64 "ul * i, value[i]);\n", child, stride);
    } else {
        metal_format(e, "        value[i] = doe_%s%u(p + %" PRIu64 "ul * i);\n",
                     mode == 1u ? "uniform" : "load", child, stride);
    }
}
static void memory_body(MetalEmitter *e, TypeId type, unsigned mode) {
    const TypeRecord *t = type >= IR_PRIMITIVE_COUNT ? &e->module->types[type - IR_PRIMITIVE_COUNT] : NULL;
    memory_signature(e, type, mode);
    metal_text(e, " {\n");
    if (t == NULL || t->kind == TYPE_VECTOR) {
        metal_text(e, "    ");
        if (mode != 2u) {
            metal_text(e, "return ");
            metal_type(e, type);
            metal_text(e, "(");
        }
        metal_format(e, "*(%s ", mode == 2u ? "device" : mode == 1u ? "constant" : "device const");
        if (t != NULL && t->count == 3u) metal_text(e, "packed_");
        metal_type(e, type);
        metal_text(e, "*)(p)");
        if (mode == 2u) metal_text(e, " = value;\n");
        else metal_text(e, ");\n");
    } else {
        if (mode != 2u) {
            metal_text(e, "    ");
            metal_type(e, type);
            metal_text(e, " value = ");
            metal_zero(e, type);
            metal_text(e, ";\n");
        }
        if (t->kind == TYPE_ARRAY || t->kind == TYPE_MATRIX) {
            metal_format(e, "    for (uint i = 0u; i < %uu; ++i) {\n", t->count);
            memory_array_child(e, t->element, mode, t->state.layout.stride);
            metal_text(e, "    }\n");
        } else if (t->kind == TYPE_STRUCT) {
            for (uint32_t j = 0u; j < t->count; ++j) {
                const MemberRecord *m = &e->module->members[t->element + j];
                if (mode == 2u) {
                    metal_format(e, "    doe_store%u(p + %" PRIu64 "ul, value.m%u);\n", m->type, m->offset, j);
                } else {
                    metal_format(e, "    value.m%u = doe_%s%u(p + %" PRIu64 "ul);\n", j,
                                 mode == 1u ? "uniform" : "load", m->type, m->offset);
                }
            }
        } else metal_error(e, "Metal memory helper type is unsupported");
        if (mode != 2u) metal_text(e, "    return value;\n");
    }
    metal_text(e, "}\n");
}
/* A 32-bit scalar resource helper is needed only when an aggregate helper's
 * recursion reaches it; direct accesses of that scalar are written at their site. */
static bool resource_helper_used(MetalEmitter *e, uint32_t resource, TypeId type, bool store, const char **error) {
    const CompilerWorkspace *c = e->module;
    if (!metal_direct_scalar(type))
        return target_access_helper_used(c, &e->closure, ROOT_RESOURCE, resource, type, store, &e->traversal_left, error);
    for (uint32_t function = 0u; function < c->function_count; ++function) {
        if (!target_spend(&e->traversal_left, error)) return false;
        if (!target_function_used(&e->closure, function)) continue;
        const FunctionRecord *f = &c->functions[function];
        for (uint32_t i = f->instruction_start; i < f->instruction_end; ++i) {
            if (!target_spend(&e->traversal_left, error)) return false;
            const IrInstruction *ir = &c->instructions[i];
            if (ir->opcode != (store ? IR_ACCESS_STORE : IR_ACCESS_LOAD) || ir->type == type ||
                ir->operands.access.category != ROOT_RESOURCE || ir->operands.access.root != resource) continue;
            if (target_memory_dependency(c, &e->closure, ir->type, type, &e->traversal_left, error)) return true;
            if (*error != NULL) return false;
        }
    }
    return false;
}
static bool memory_helper_used(MetalEmitter *e, TypeId type, unsigned mode) {
    const CompilerWorkspace *c = e->module;
    for (uint32_t i = 0u; i < c->resource_count; ++i) {
        const uint32_t kind = c->resources[i].kind;
        if ((mode == 1u && kind != DOE_COMPILER_UNIFORM_BUFFER) ||
            (mode != 1u && kind != DOE_COMPILER_STORAGE_BUFFER)) continue;
        const char *error = NULL;
        const bool used = resource_helper_used(e, i, type, mode == 2u, &error);
        if (error != NULL) { metal_error(e, error); return false; }
        if (used) return true;
    }
    if (mode == 1u) for (uint32_t i = 0u; i < c->workgroup_count; ++i) {
        if (c->workgroups[i].state.resolved.address_space != MODULE_IMMEDIATE) continue;
        const char *error = NULL;
        const bool used = target_access_helper_used(c,&e->closure,ROOT_WORKGROUP,i,type,false,&e->traversal_left,&error);
        if (error != NULL) { metal_error(e,error); return false; }
        if (used) return true;
    }
    return false;
}
void metal_memory_helpers(MetalEmitter *e) {
    for (unsigned pass = 0u; pass < 2u; ++pass) {
        for (TypeId type = IR_I32; type < IR_PRIMITIVE_COUNT + e->module->type_count && !e->failed; ++type) {
            if (!memory_type(e->module, type)) continue;
            for (unsigned mode = 0u; mode < 3u; ++mode) {
                if (!memory_helper_used(e, type, mode)) continue;
                if (pass == 0u) {
                    memory_signature(e, type, mode);
                    metal_text(e, ";\n");
                } else memory_body(e, type, mode);
            }
        }
    }
}
static void workgroup_signature(MetalEmitter *e, TypeId type, bool store) {
    metal_text(e, "inline ");
    if (store) metal_text(e, "void");
    else metal_type(e, type);
    metal_format(e, " doe_w%s%u(threadgroup %s", store ? "store" : "load", type, store ? "" : "const ");
    metal_type(e, type);
    metal_text(e, "* p");
    if (store) {
        metal_text(e, ", ");
        metal_type(e, type);
        metal_text(e, " value");
    }
    metal_text(e, ")");
}
static void workgroup_body(MetalEmitter *e, TypeId type, bool store) {
    const TypeRecord *t = type >= IR_PRIMITIVE_COUNT ? &e->module->types[type - IR_PRIMITIVE_COUNT] : NULL;
    workgroup_signature(e, type, store);
    metal_text(e, " {\n");
    if (t == NULL || t->kind == TYPE_VECTOR) {
        metal_text(e, store ? "    *p = value;\n" : "    return *p;\n");
    } else {
        if (!store) {
            metal_text(e, "    ");
            metal_type(e, type);
            metal_text(e, " value = ");
            metal_zero(e, type);
            metal_text(e, ";\n");
        }
        if (t->kind == TYPE_ARRAY || t->kind == TYPE_MATRIX) {
            metal_format(e, "    for (uint i = 0u; i < %uu; ++i) {\n", t->count);
            if (store) metal_format(e, "        doe_wstore%u(&((*p)[i]), value[i]);\n", t->element);
            else metal_format(e, "        value[i] = doe_wload%u(&((*p)[i]));\n", t->element);
            metal_text(e, "    }\n");
        } else if (t->kind == TYPE_STRUCT) {
            for (uint32_t i = 0u; i < t->count; ++i) {
                const TypeId child = e->module->members[t->element + i].type;
                if (store) metal_format(e, "    doe_wstore%u(&(p->m%u), value.m%u);\n", child, i, i);
                else metal_format(e, "    value.m%u = doe_wload%u(&(p->m%u));\n", i, child, i);
            }
        }
        if (!store) metal_text(e, "    return value;\n");
    }
    metal_text(e, "}\n");
}
static bool workgroup_helper_used(MetalEmitter *e, TypeId type, bool store) {
    const CompilerWorkspace *c = e->module;
    for (uint32_t i = 0u; i < c->workgroup_count; ++i) {
        if (!metal_selected_workgroup(e, i) || c->workgroups[i].state.resolved.address_space != MODULE_WORKGROUP) continue;
        const char *error = NULL;
        /* Cooperative zeroing stores each whole root before the first barrier. */
        bool used = store && target_memory_dependency(c, &e->closure, c->workgroups[i].type,
            type, &e->traversal_left, &error);
        if (error == NULL && !used) used = target_access_helper_used(c, &e->closure,
            ROOT_WORKGROUP, i, type, store, &e->traversal_left, &error);
        if (error != NULL) { metal_error(e, error); return false; }
        if (used) return true;
    }
    return false;
}
void metal_workgroup_helpers(MetalEmitter *e) {
    if (e->module->workgroup_count == 0u) return;
    for (unsigned pass = 0u; pass < 2u; ++pass) {
        for (TypeId type = IR_BOOL; type < IR_PRIMITIVE_COUNT + e->module->type_count && !e->failed; ++type) {
            if (type == IR_ABSTRACT_INT || type == IR_ABSTRACT_FLOAT) continue;
            if (type >= IR_PRIMITIVE_COUNT && ((type_flags(e->module, type) & TYPE_CONSTRUCTIBLE) == 0u ||
                (type_flags(e->module, type) & TYPE_ABSTRACT) != 0u)) continue;
            for (unsigned mode = 0u; mode < 2u; ++mode) {
                if (!workgroup_helper_used(e, type, mode != 0u)) continue;
                if (pass == 0u) {
                    workgroup_signature(e, type, mode != 0u);
                    metal_text(e, ";\n");
                } else workgroup_body(e, type, mode != 0u);
            }
        }
    }
}
