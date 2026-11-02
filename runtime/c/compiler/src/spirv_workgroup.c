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

/* Flatten only array dimensions along each typed leaf path. Struct members
 * remain independent typed stores; no padding writes or atomic aggregate copy.
 * The existing caller-owned walk/word/declaration pools bound this traversal. */
static bool initialize_leaf(Emitter *e, uint32_t root, uint32_t lane, uint32_t stride, bool plan) {
    CompilerWorkspace *c = e->program;
    uint32_t count = 1u;
    for (uint32_t j = 0u; j + 1u < c->walk_count; ++j) {
        const TypeRecord *t = &c->types[c->walk[j].object - IR_PRIMITIVE_COUNT];
        if (t->kind != TYPE_ARRAY) continue;
        if (!t->count || count > UINT32_MAX / t->count)
            return spirv_emit_error(e, 0u, "workgroup initialization extent overflows");
        count *= t->count;
    }
    const uint32_t bound = spirv_unsigned_constant(e, count);
    if (plan) {
        uint32_t divisor = count;
        for (uint32_t j = 0u; j + 1u < c->walk_count; ++j) {
            const WalkItem *w = &c->walk[j];
            const TypeRecord *t = &c->types[w->object - IR_PRIMITIVE_COUNT];
            if (t->kind == TYPE_ARRAY) {
                divisor /= t->count;
                (void)spirv_unsigned_constant(e, divisor);
                (void)spirv_unsigned_constant(e, t->count);
            } else (void)spirv_unsigned_constant(e, w->cursor - 1u);
            (void)spirv_pointer_type(e, c->walk[j+1u].object, MODULE_WORKGROUP);
        }
        const TypeId type = c->walk[c->walk_count-1u].object;
        const TypeRecord *leaf = type >= IR_PRIMITIVE_COUNT ? &c->types[type-IR_PRIMITIVE_COUNT] : NULL;
        if (leaf && leaf->kind == TYPE_ATOMIC) {
            (void)spirv_unsigned_constant(e, 0u);
            (void)spirv_zero_constant(e, leaf->element);
        } else (void)spirv_workgroup_zero(e, type);
        return !e->failed;
    }
    const uint32_t ids = spirv_reserve_ids(e, 10u);
    const uint32_t header = ids, body = ids+1u, continuing = ids+2u, merge = ids+3u;
    const uint32_t index = ids+4u, next = ids+5u, condition = ids+6u;
    EMIT(OP_BRANCH, header);
    EMIT(OP_LABEL, header);
    EMIT(OP_PHI, ID_U32, index, lane, e->current_label, next, continuing);
    EMIT(OP_ULESS_THAN, ID_BOOL, condition, index, bound);
    EMIT(OP_LOOP_MERGE, merge, continuing, 0u);
    EMIT(OP_BRANCH_CONDITIONAL, condition, body, merge);
    EMIT(OP_LABEL, body);
    uint32_t pointer = root, divisor = count;
    for (uint32_t j = 0u; j + 1u < c->walk_count; ++j) {
        const WalkItem *w = &c->walk[j];
        const TypeRecord *t = &c->types[w->object - IR_PRIMITIVE_COUNT];
        uint32_t element;
        if (t->kind == TYPE_ARRAY) {
            divisor /= t->count;
            const uint32_t divided = spirv_reserve_ids(e, 1u);
            element = spirv_reserve_ids(e, 1u);
            EMIT(OP_UDIV, ID_U32, divided, index, spirv_unsigned_constant(e, divisor));
            EMIT(OP_UMOD, ID_U32, element, divided, spirv_unsigned_constant(e, t->count));
        } else element = spirv_unsigned_constant(e, w->cursor - 1u);
        const uint32_t child = spirv_reserve_ids(e, 1u);
        EMIT(OP_ACCESS_CHAIN, spirv_pointer_type(e, c->walk[j+1u].object, MODULE_WORKGROUP), child, pointer, element);
        pointer = child;
    }
    const TypeId type = c->walk[c->walk_count-1u].object;
    const TypeRecord *leaf = type >= IR_PRIMITIVE_COUNT ? &c->types[type-IR_PRIMITIVE_COUNT] : NULL;
    if (leaf && leaf->kind == TYPE_ATOMIC) {
        EMIT(OP_ATOMIC_STORE, pointer, ID_TWO_U32, ID_ZERO_U32, spirv_zero_constant(e, leaf->element));
    } else EMIT(OP_STORE, pointer, spirv_workgroup_zero(e, type));
    EMIT(OP_BRANCH, continuing);
    EMIT(OP_LABEL, continuing);
    /* Select the bound for the last step instead of wrapping an index back
     * into the domain when count + workgroup stride exceeds uint32. */
    EMIT(OP_ISUB, ID_U32, ids+7u, bound, index);
    EMIT(OP_UGREATER_THAN, ID_BOOL, ids+8u, ids+7u, stride);
    EMIT(OP_IADD, ID_U32, ids+9u, index, stride);
    EMIT(OP_SELECT, ID_U32, next, ids+8u, ids+9u, bound);
    EMIT(OP_BRANCH, header);
    EMIT(OP_LABEL, merge);
    e->current_label = merge;
    return !e->failed;
}
static bool initialize_workgroups(Emitter *e, bool plan) {
    CompilerWorkspace *c = e->program;
    uint32_t size = 1u;
    for (unsigned dimension = 0u; dimension < 3u; ++dimension) {
        const uint32_t value = e->entry->state.resolved.workgroup_size[dimension];
        if (!value || size > UINT32_MAX / value)
            return spirv_emit_error(e, 0u, "workgroup initialization stride overflows");
        size *= value;
    }
    const uint32_t stride = spirv_unsigned_constant(e, size);
    const uint32_t lane = plan ? 0u : spirv_reserve_ids(e, 1u);
    if (!plan) {
        EMIT(OP_LOAD, ID_U32, lane, e->entry->state.resolved.local_index_target);
        e->entry->state.resolved.target_input_mask |= 1u << INPUT_LOCAL_INDEX;
    }
    for (uint32_t i = 0u; i < e->entry->state.resolved.workgroup_count; ++i) {
        const WorkgroupRoot *root = &c->workgroups[c->entry_references[e->entry->state.resolved.workgroup_first+i]];
        if (root->state.resolved.address_space != MODULE_WORKGROUP) continue;
        c->walk_count = 0u;
        if (!spirv_push_walk(e, WALK_TYPE, root->type)) return false;
        while (c->walk_count) {
            WalkItem *w = &c->walk[c->walk_count-1u];
            if (w->object >= IR_PRIMITIVE_COUNT + c->type_count)
                return spirv_emit_error(e, 0u, "workgroup zero type identity is invalid");
            const TypeRecord *t = w->object >= IR_PRIMITIVE_COUNT ? &c->types[w->object-IR_PRIMITIVE_COUNT] : NULL;
            const uint32_t children = t && t->kind == TYPE_ARRAY ? 1u : t && t->kind == TYPE_STRUCT ? t->count : 0u;
            if (t && t->kind == TYPE_STRUCT && (t->element > c->member_count || t->count > c->member_count-t->element))
                return spirv_emit_error(e, 0u, "workgroup zero member range is invalid");
            if (!children || w->cursor == children) {
                if (!children && !initialize_leaf(e, root->target, lane, stride, plan)) return false;
                --c->walk_count;
            } else {
                const TypeId child = type_child(c, w->object, w->cursor++);
                if (!spirv_push_walk(e, WALK_TYPE, child)) return false;
            }
        }
    }
    return !e->failed;
}

bool spirv_initialize_workgroups(Emitter *e) { return initialize_workgroups(e, false); }
bool spirv_plan_workgroup_initialization(Emitter *e) {
    for (uint32_t i = 0u; i < e->program->entry_count; ++i) {
        e->entry = &e->program->entries[i];
        if (e->entry->state.resolved.workgroup_storage_bytes && !initialize_workgroups(e, true)) return false;
    }
    e->entry = NULL;
    return !e->failed;
}
