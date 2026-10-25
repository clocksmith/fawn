/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "spirv_internal.h"

/* Baseline Vulkan Workgroup objects cannot share explicit-layout array/struct
 * declarations with buffers. Keep semantic identity and the canonical buffer
 * byte layout, but emit undecorated aggregate target types for Workgroup memory.
 * Vectors/matrices/scalars have no such decorations and retain their target ID. */
static bool separate_type(const CompilerWorkspace *c, TypeId type) {
    return type >= IR_PRIMITIVE_COUNT && (c->types[type - IR_PRIMITIVE_COUNT].kind == TYPE_ARRAY ||
                         c->types[type - IR_PRIMITIVE_COUNT].kind == TYPE_STRUCT);
}
static const TargetDeclaration *workgroup_declaration(const Emitter *e, TypeId type,
                                                     SpirvDeclarationKind kind) {
    const CompilerWorkspace *c = e->program;
    for (uint32_t i = 0u; i < c->target_count; ++i)
        if (c->targets[i].kind == (uint32_t)kind && c->targets[i].type == type)
            return &c->targets[i];
    return NULL;
}
uint32_t spirv_workgroup_type(const Emitter *e, TypeId type) {
    if (!separate_type(e->program, type)) return spirv_target_type(e, type);
    const TargetDeclaration *d = workgroup_declaration(e, type, DECL_WORKGROUP_TYPE);
    return d == NULL ? 0u : d->id;
}
bool spirv_workgroup_plan(Emitter *e, TypeId type) {
    CompilerWorkspace *c = e->program;
    if (!separate_type(c, type) || spirv_workgroup_type(e, type) != 0u) return true;
    if (!spirv_push_walk(e, WALK_TYPE, type)) return false;
    while (c->walk_count != 0u) {
        WalkItem *w = &c->walk[c->walk_count - 1u];
        const TypeRecord *t = &c->types[w->object - IR_PRIMITIVE_COUNT];
        /* An array has one type dependency regardless of its element count. */
        const uint32_t count = t->kind == TYPE_STRUCT ? t->count : 1u;
        if (w->cursor < count) {
            const TypeId child = type_child(c, w->object, w->cursor++);
            if (separate_type(c, child) && spirv_workgroup_type(e, child) == 0u &&
                !spirv_push_walk(e, WALK_TYPE, child)) return false;
            continue;
        }
        const uint32_t length = t->kind == TYPE_ARRAY ? spirv_unsigned_constant(e, t->count) : 0u;
        TargetDeclaration *d = spirv_append_declaration(e, DECL_WORKGROUP_TYPE, w->object);
        if (d == NULL) return false;
        d->first = length;
        --c->walk_count;
    }
    return !e->failed;
}
uint32_t spirv_workgroup_zero(Emitter *e, TypeId type) {
    if (!separate_type(e->program, type) && type >= IR_PRIMITIVE_COUNT) return spirv_zero_constant(e, type);
    const TargetDeclaration *existing = workgroup_declaration(e, type, DECL_WORKGROUP_ZERO);
    if (existing != NULL) return existing->id;
    TargetDeclaration *d = spirv_append_declaration(e, DECL_WORKGROUP_ZERO, type);
    return d == NULL ? 0u : d->id;
}
/* Conversion flattens only differently represented arrays/structures. Each
 * leaf is a scalar/vector/matrix with the same target type on either side.
 * Existing traversal and serialized-word pools bound the full operation. No
 * recursion, per-element staging allocation or source expression replay occurs. */
bool spirv_workgroup_convert(Emitter *e, TypeId type, uint32_t input, uint32_t result,
                            bool to_workgroup) {
    CompilerWorkspace *c = e->program;
    const uint32_t destination = to_workgroup ? spirv_workgroup_type(e, type) : spirv_target_type(e, type);
    if (!separate_type(c, type)) {
        EMIT(OP_COPY_OBJECT, destination, result, input);
        return true;
    }
    uint32_t composite = to_workgroup ? spirv_workgroup_zero(e, type) : spirv_zero_constant(e, type);
    c->walk_count = 0u;
    if (!spirv_push_walk(e, WALK_TYPE, type)) return false;
    while (c->walk_count != 0u) {
        WalkItem *w = &c->walk[c->walk_count - 1u];
        if (!separate_type(c, w->object)) {
            const uint32_t extracted = spirv_reserve_ids(e, 1u), inserted = spirv_reserve_ids(e, 1u);
            uint32_t *words;
            if (!spirv_instruction_space(e, OP_COMPOSITE_EXTRACT, (size_t)c->walk_count + 2u, &words)) return false;
            words[0] = spirv_target_type(e, w->object);
            words[1] = extracted;
            words[2] = input;
            for (uint32_t i = 0u; i + 1u < c->walk_count; ++i) words[3u + i] = c->walk[i].cursor - 1u;
            if (!spirv_instruction_space(e, OP_COMPOSITE_INSERT, (size_t)c->walk_count + 3u, &words)) return false;
            words[0] = destination;
            words[1] = inserted;
            words[2] = extracted;
            words[3] = composite;
            for (uint32_t i = 0u; i + 1u < c->walk_count; ++i) words[4u + i] = c->walk[i].cursor - 1u;
            composite = inserted;
            --c->walk_count;
            continue;
        }
        if (w->cursor == type_children(c, w->object)) {
            --c->walk_count;
            continue;
        }
        const TypeId child = type_child(c, w->object, w->cursor++);
        if (!spirv_push_walk(e, WALK_TYPE, child)) return false;
    }
    EMIT(OP_COPY_OBJECT, destination, result, composite);
    return !e->failed;
}
