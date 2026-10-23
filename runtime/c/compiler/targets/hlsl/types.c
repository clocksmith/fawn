/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "private.h"
#include <inttypes.h>

static const TypeRecord *aggregate(HlslEmitter *e, TypeId type) {
    if (type < 7u || type - 7u >= e->module->type_count) {
        hlsl_error(e, "HLSL aggregate type identity is invalid");
        return NULL;
    }
    return &e->module->types[type - 7u];
}
void hlsl_type(HlslEmitter *e, TypeId type) {
    static const char *const scalars[] = {"void", "bool", "int", "uint", "float"};
    if (type <= IR_F32) {
        hlsl_text(e, scalars[type]);
        return;
    }
    const TypeRecord *t = aggregate(e, type);
    if (t == NULL) return;
    if (t->kind == TYPE_TEXTURE || t->kind == TYPE_SAMPLER) {
        hlsl_handle_type(e, type);
        return;
    }
    if ((t->flags & TYPE_CONSTRUCTIBLE) == 0u || (t->flags & TYPE_ABSTRACT) != 0u) {
        hlsl_error(e, "HLSL value type must be concrete and constructible");
        return;
    }
    if (t->kind == TYPE_VECTOR) {
        if (t->element < IR_BOOL || t->element > IR_F32 || t->count < 2u || t->count > 4u) {
            hlsl_error(e, "HLSL vector shape is invalid");
            return;
        }
        hlsl_format(e, "%s%u", scalars[t->element], t->count);
    } else if (t->kind == TYPE_ARRAY || t->kind == TYPE_STRUCT) {
        hlsl_format(e, "DoeT%u", type);
    } else hlsl_error(e, "HLSL type kind has no value representation");
}
void hlsl_zero(HlslEmitter *e, TypeId type) {
    hlsl_text(e, "(");
    hlsl_type(e, type);
    hlsl_text(e, ")0");
}
void hlsl_types(HlslEmitter *e) {
    const CompilerWorkspace *c = e->module;
    const char *error = NULL;
    if (!target_type_order_begin(&e->closure, &e->traversal_left, &error)) {
        hlsl_error(e, error);
        return;
    }
    for (unsigned depth = 1u; depth <= DOE_HLSL_MAX_DEPTH && !e->failed; ++depth) {
        bool pending = false;
        for (uint32_t i = 0u; i < c->type_count && !e->failed; ++i) {
            const TypeRecord *t = &c->types[i];
            if ((t->kind != TYPE_STRUCT && t->kind != TYPE_ARRAY) ||
                (t->flags & TYPE_CONSTRUCTIBLE) == 0u || (t->flags & TYPE_ABSTRACT) != 0u) continue;
            unsigned d;
            if (!target_type_depth(c, &e->closure, i + 7u, &d, &e->traversal_left, &error)) {
                hlsl_error(e, error);
                return;
            }
            if (d > depth) pending = true;
            if (d != depth) continue;
            hlsl_format(e, "struct DoeT%u {\n", i + 7u);
            if (t->kind == TYPE_ARRAY) {
                hlsl_text(e, "    ");
                hlsl_type(e, t->element);
                hlsl_format(e, " elements[%u];\n", t->count);
            } else {
                for (uint32_t j = 0u; j < t->count; ++j) {
                    hlsl_text(e, "    ");
                    hlsl_type(e, c->members[t->element + j].type);
                    hlsl_format(e, " m%u;\n", j);
                }
            }
            hlsl_text(e, "};\n");
        }
        if (!pending) break;
    }
}
void hlsl_constant(HlslEmitter *e, TypeId type, uint64_t bits, bool composite) {
    if (composite) {
        if (bits >= e->module->constant_count || e->module->constants[bits].type != type) {
            hlsl_error(e, "HLSL constant identity or type is invalid");
            return;
        }
        hlsl_format(e, "doe_constant%u()", (uint32_t)bits);
        return;
    }
    switch (type) {
        case IR_BOOL: hlsl_text(e, bits == 0u ? "false" : "true"); break;
        case IR_U32: hlsl_format(e, "0x%08" PRIx32 "u", (uint32_t)bits); break;
        case IR_I32: hlsl_format(e, "asint(0x%08" PRIx32 "u)", (uint32_t)bits); break;
        case IR_F32: hlsl_format(e, "asfloat(0x%08" PRIx32 "u)", (uint32_t)bits); break;
        default: hlsl_error(e, "HLSL scalar constant type is unsupported"); break;
    }
}
static void member_suffix(HlslEmitter *e, const TypeRecord *t, uint32_t index) {
    if (t->kind == TYPE_STRUCT) hlsl_format(e, ".m%u", index);
    else if (t->kind == TYPE_ARRAY) hlsl_format(e, ".elements[%u]", index);
    else if (t->kind == TYPE_VECTOR) hlsl_format(e, "[%u]", index);
    else hlsl_error(e, "HLSL constructor kind is unsupported");
}
void hlsl_constants(HlslEmitter *e) {
    const CompilerWorkspace *c = e->module;
    for (unsigned pass = 0u; pass < 2u; ++pass) {
        for (uint32_t i = 0u; i < c->constant_count && !e->failed; ++i) {
            const ConstantRecord *k = &c->constants[i];
            if (k->type < 7u || (type_flags(c, k->type) & TYPE_ABSTRACT) != 0u) continue;
            const TypeRecord *t = aggregate(e, k->type);
            if (t == NULL) return;
            hlsl_type(e, k->type);
            hlsl_format(e, " doe_constant%u()", i);
            if (pass == 0u) {
                hlsl_text(e, ";\n");
                continue;
            }
            hlsl_text(e, " {\n    ");
            hlsl_type(e, k->type);
            hlsl_text(e, " value = ");
            hlsl_zero(e, k->type);
            hlsl_text(e, ";\n");
            if (k->kind != CONSTANT_ZERO) {
                if ((k->kind != CONSTANT_REPEAT && k->kind != CONSTANT_COMPOSITE) ||
                    k->count != t->count || k->first > c->edge_count ||
                    (k->kind == CONSTANT_REPEAT ? 1u : k->count) > c->edge_count - k->first) {
                    hlsl_error(e, "HLSL constant child range is invalid");
                    return;
                }
                for (uint32_t j = 0u; j < k->count && !e->failed; ++j) {
                    const ConstantValue *v = &c->constant_edges[k->first + (k->kind == CONSTANT_REPEAT ? 0u : j)];
                    hlsl_text(e, "    value");
                    member_suffix(e, t, j);
                    hlsl_text(e, " = ");
                    hlsl_constant(e, v->type, v->bits, (v->flags & CONSTANT_AGGREGATE) != 0u);
                    hlsl_text(e, ";\n");
                }
            }
            hlsl_text(e, "    return value;\n}\n");
        }
    }
}
void hlsl_composite(HlslEmitter *e, const IrInstruction *ir) {
    const uint32_t first = ir->operands.composite.first, count = ir->operands.composite.count;
    const TypeRecord *t = aggregate(e, ir->type);
    if (t == NULL) return;
    if (first > e->module->operand_count || count > e->module->operand_count - first) {
        hlsl_error(e, "HLSL composite operand range is invalid");
        return;
    }
    hlsl_indent(e);
    hlsl_value(e, ir->result);
    hlsl_text(e, " = ");
    if (t->kind == TYPE_VECTOR && count != 0u) {
        hlsl_type(e, ir->type);
        hlsl_text(e, "(");
        for (uint32_t i = 0u; i < count; ++i) {
            if (i != 0u) hlsl_text(e, ", ");
            hlsl_value(e, e->module->typed_operands[first + i]);
        }
        hlsl_text(e, ");\n");
        return;
    }
    hlsl_zero(e, ir->type);
    hlsl_text(e, ";\n");
    if (count != 0u && count != t->count) {
        hlsl_error(e, "HLSL aggregate constructor arity is invalid");
        return;
    }
    for (uint32_t i = 0u; i < count && !e->failed; ++i) {
        hlsl_indent(e);
        hlsl_value(e, ir->result);
        member_suffix(e, t, i);
        hlsl_text(e, " = ");
        hlsl_value(e, e->module->typed_operands[first + i]);
        hlsl_text(e, ";\n");
    }
}
