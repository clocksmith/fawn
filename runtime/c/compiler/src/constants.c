/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "sema.h"

bool constant_reserve(Source *s, TypeId type, uint32_t kind, uint32_t count, ConstantValue *out, uint32_t *first) {
    CompilerWorkspace *c = s->compiler;
    if (c->constant_count == c->limits.constants)
        return source_pool_error(s, DOE_COMPILER_POOL_CONSTANTS, (size_t)c->constant_count+1u, s->length, "aggregate constant capacity exceeded");
    const uint32_t edges = kind == CONSTANT_ZERO?0u:kind == CONSTANT_REPEAT?1u:count;
    if (edges>c->limits.edges-c->edge_count)
        return source_pool_error(s, DOE_COMPILER_POOL_CONSTANT_EDGES, (size_t)c->edge_count+edges, s->length, "constant edge capacity exceeded");
    *first = c->edge_count;
    c->constants[c->constant_count] = (ConstantRecord){.kind = kind, .type = type, .first = *first, .count = count, .target = NO_ID};
    *out = (ConstantValue){type, CONSTANT_AGGREGATE, c->constant_count++};
    c->edge_count+=edges;
    return true;
}
bool constant_zero(Source *s, TypeId type, ConstantValue *out) {
    if (type<IR_PRIMITIVE_COUNT) {
        *out = (ConstantValue){.type = type};
        return true;
    }
    CompilerWorkspace *c = s->compiler;
    for (uint32_t i = 0u; i<c->constant_count; ++i) {
        if (c->constants[i].kind == CONSTANT_ZERO && c->constants[i].type == type) {
            *out = (ConstantValue){type, CONSTANT_AGGREGATE, i};
            return true;
        }
    }
    uint32_t ignored;
    return constant_reserve(s, type, CONSTANT_ZERO, 0u, out, &ignored);
}
bool constant_composite(Source *s, TypeId type, const ConstantValue *values, uint32_t count, bool repeat, ConstantValue *out) {
    uint32_t first;
    if (!constant_reserve(s, type, repeat?CONSTANT_REPEAT:CONSTANT_COMPOSITE, count, out, &first)) return false;
    const uint32_t n = repeat?1u:count;
    for (uint32_t i = 0u;i<n;++i) s->compiler->constant_edges[first+i] = values[i];
    return true;
}
bool constant_project(Source *s, ConstantValue input, uint32_t index, ConstantValue *out) {
    CompilerWorkspace *c = s->compiler;
    if ((input.flags&CONSTANT_AGGREGATE) == 0u || input.bits >= c->constant_count || index >= type_children(c, input.type))
        return source_error(s, DOE_COMPILER_SEMANTIC_ERROR, s->length, "constant aggregate index is out of bounds");
    const ConstantRecord *node = &c->constants[input.bits];
    if (node->kind == CONSTANT_ZERO) return constant_zero(s, type_child(c, input.type, index), out);
    *out = c->constant_edges[node->first+(node->kind == CONSTANT_REPEAT?0u:index)];
    return true;
}
static bool scalar_materialize(Source *s, uint32_t offset, ConstantValue in, TypeId type, ConstantValue *out) {
    NumericValue value;
    if (!numeric_materialize(s, offset, (NumericValue){in.bits, (IrType)in.type}, (IrType)type, &value)) return false;
    *out = (ConstantValue){.type = type, .bits = value.bits};
    return true;
}
/* Each frame owns one source node and one committed destination node. Edges
 * remain compact, zeros stay one node, and completed conversions are shared. */
static bool convert_node(Source *s, uint32_t offset, ConstantValue in, TypeId type, ConstantValue *out, bool *pending) {
    CompilerWorkspace *c = s->compiler;
    *pending = false;
    if (in.type == type) {
        *out = in;
        return true;
    }
    if (in.type<IR_PRIMITIVE_COUNT && type<IR_PRIMITIVE_COUNT) return scalar_materialize(s, offset, in, type, out);
    if (in.type<IR_PRIMITIVE_COUNT || type<IR_PRIMITIVE_COUNT || !type_abstract(c, in.type))
        return source_error(s, DOE_COMPILER_SEMANTIC_ERROR, offset, "aggregate has no implicit conversion");
    const uint32_t source = (uint32_t)in.bits;
    for (uint32_t i = 0u; i<c->constant_count; ++i) {
        if (c->constants[i].converted_from == source+1u && c->constants[i].type == type) {
            *out = (ConstantValue){type, CONSTANT_AGGREGATE, i};
            return true;
        }
    }
    const ConstantRecord node = c->constants[source];
    if (node.kind == CONSTANT_ZERO) return constant_zero(s, type, out);
    uint32_t first;
    if (!constant_reserve(s, type, node.kind, node.count, out, &first)) return false;
    c->constants[out->bits].converted_from = source+1u;
    *pending = true;
    return true;
}
bool constant_materialize(Source *s, uint32_t offset, ConstantValue in, TypeId type, ConstantValue *out) {
    CompilerWorkspace *c = s->compiler;
    bool pending;
    if (!convert_node(s, offset, in, type, out, &pending)) return false;
    if (!pending) return true;
    const uint32_t base = c->walk_count;
    if (c->walk_count == c->limits.traversal) return source_pool_error(s, DOE_COMPILER_POOL_TRAVERSAL, (size_t)c->walk_count+1u, offset, "materialization traversal capacity exceeded");
    c->walk[c->walk_count++] = (WalkItem){.object = (uint32_t)in.bits, .parent = (uint32_t)out->bits};
    while (c->walk_count>base) {
        WalkItem *w = &c->walk[c->walk_count-1u];
        const ConstantRecord *src = &c->constants[w->object], *dst = &c->constants[w->parent];
        const uint32_t children = src->kind == CONSTANT_REPEAT?1u:src->count;
        if (w->cursor == children) {
            --c->walk_count;
            continue;
        }
        const uint32_t child = w->cursor++;
        const ConstantValue from = c->constant_edges[src->first+child];
        const TypeId target = type_child(c, dst->type, child);
        const uint32_t destination = dst->first+child;
        ConstantValue converted;
        if (!convert_node(s, offset, from, target, &converted, &pending)) return false;
        c->constant_edges[destination] = converted;
        if (pending) {
            if (c->walk_count == c->limits.traversal) return source_pool_error(s, DOE_COMPILER_POOL_TRAVERSAL, (size_t)c->walk_count+1u, offset, "materialization traversal capacity exceeded");
            c->walk[c->walk_count++] = (WalkItem){.object = (uint32_t)from.bits, .parent = (uint32_t)converted.bits};
        }
    }
    return true;
}
