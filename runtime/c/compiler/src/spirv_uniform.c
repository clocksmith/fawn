/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "spirv_internal.h"

/* WGSL two-row matrix columns have stride 8. Vulkan's baseline uniform layout
 * cannot represent that as OpTypeMatrix. Affected resources retain their exact
 * source byte extent in a physical uvec4 prefix plus a scalar/vector tail. Loads
 * address individual u32 lanes, never padding or a wider out-of-range vector.
 * Logical types, reflection, storage/workgroup resources and source IR stay intact. */
static const TargetDeclaration *raw_descriptor(const Emitter *e, TypeId type) {
    const CompilerWorkspace *c = e->program;
    for (uint32_t i = 0u; i < c->target_count; ++i)
        if (c->targets[i].kind == DECL_RAW_UNIFORM && c->targets[i].type == type) return &c->targets[i];
    return NULL;
}
bool spirv_uniform_raw(const Emitter *e, const ResourceRecord *r) {
    return r->kind == DOE_COMPILER_UNIFORM_BUFFER && raw_descriptor(e, r->type) != NULL;
}
static bool contains_narrow(Emitter *e, TypeId type, bool *found) {
    CompilerWorkspace *c = e->program;
    *found = false;
    if (type < 7u) return true;
    if (!spirv_push_walk(e, WALK_TYPE, type)) return false;
    while (c->walk_count != 0u) {
        WalkItem *w = &c->walk[c->walk_count - 1u];
        const TypeRecord *t = &c->types[w->object - 7u];
        if (t->kind == TYPE_MATRIX && type_vector_width(c, t->element) == 2u) {
            *found = true;
            c->walk_count = 0u;
            return true;
        }
        const uint32_t count = t->kind == TYPE_STRUCT ? t->count : 1u;
        if (w->cursor == count) {
            --c->walk_count;
            continue;
        }
        const TypeId child = type_child(c, w->object, w->cursor++);
        if (child >= 7u && !spirv_push_walk(e, WALK_TYPE, child)) return false;
    }
    return true;
}
static uint32_t raw_vector(Emitter *e, uint32_t width) {
    if (width == 1u) return ID_U32;
    CompilerWorkspace *c = e->program;
    for (uint32_t i = 0u; i < c->type_count; ++i)
        if (c->types[i].kind == TYPE_VECTOR && c->types[i].element == IR_U32 && c->types[i].count == width)
            return c->types[i].target;
    for (uint32_t i = 0u; i < c->target_count; ++i)
        if (c->targets[i].kind == DECL_RAW_VECTOR && c->targets[i].count == width) return c->targets[i].id;
    TargetDeclaration *d = spirv_append_declaration(e, DECL_RAW_VECTOR, IR_VOID);
    if (d == NULL) return 0u;
    d->count = width;
    return d->id;
}
bool spirv_uniform_plan(Emitter *e, ResourceRecord *r, bool *raw) {
    *raw = false;
    if (r->kind != DOE_COMPILER_UNIFORM_BUFFER) return true;
    if (!contains_narrow(e, r->type, raw) || !*raw) return !e->failed;
    if (r->minimum > UINT32_MAX)
        return source_error(e->source, DOE_COMPILER_UNSUPPORTED, r->source_offset,
            "physical uniform byte offsets exceed the u32 target format");
    const TargetDeclaration *existing = raw_descriptor(e, r->type);
    uint32_t structure;
    if (existing != NULL) structure = existing->id;
    else {
        const uint32_t bytes = (uint32_t)r->minimum, tail = (bytes % 16u) / 4u;
        const uint32_t vector = raw_vector(e, 4u), tail_type = tail == 0u ? 0u : raw_vector(e, tail);
        const uint32_t count = spirv_unsigned_constant(e, bytes / 16u);
        TargetDeclaration *d = spirv_append_declaration(e, DECL_RAW_UNIFORM, r->type);
        if (d == NULL) return false;
        structure = d->id;
        if (spirv_reserve_ids(e, 1u) != structure + 1u) return spirv_emit_error(e, 0u, "invalid physical uniform ID pair");
        d->first = vector;
        d->count = count;
        d->storage = tail_type;
        d->payload = bytes;
    }
    TargetDeclaration *pointer = spirv_append_declaration(e, DECL_POINTER, NO_ID);
    if (pointer == NULL) return false;
    pointer->storage = 2u;
    pointer->payload = structure;
    r->reserved = pointer->id;
    (void)spirv_pointer_type(e, IR_U32, 2u);
    (void)spirv_unsigned_constant(e, 0u);
    (void)spirv_unsigned_constant(e, 1u);
    (void)spirv_unsigned_constant(e, 2u);
    (void)spirv_unsigned_constant(e, 3u);
    (void)spirv_unsigned_constant(e, 4u);
    (void)spirv_unsigned_constant(e, 16u);
    (void)spirv_unsigned_constant(e, (uint32_t)r->minimum / 16u);
    return !e->failed;
}
bool spirv_uniform_annotations(Emitter *e, const TargetDeclaration *d) {
    EMIT(OP_DECORATE, d->id, 2u);
    EMIT(OP_DECORATE, d->id + 1u, 6u, 16u);
    EMIT(OP_MEMBER_DECORATE, d->id, 0u, 35u, 0u);
    if (d->storage != 0u) { EMIT(OP_MEMBER_DECORATE, d->id, 1u, 35u, (uint32_t)d->payload / 16u * 16u); }
    return true;
}
bool spirv_uniform_declaration(Emitter *e, const TargetDeclaration *d) {
    if (d->kind == DECL_RAW_VECTOR) { EMIT(OP_TYPE_VECTOR, d->id, ID_U32, d->count); }
    else {
        EMIT(OP_TYPE_ARRAY, d->id + 1u, d->first, d->count);
        if (d->storage == 0u) { EMIT(OP_TYPE_STRUCT, d->id, d->id + 1u); }
        else { EMIT(OP_TYPE_STRUCT, d->id, d->id + 1u, d->storage); }
    }
    return true;
}
static uint32_t child_offset(const CompilerWorkspace *c, TypeId type, uint32_t index) {
    const TypeRecord *t = &c->types[type - 7u];
    if (t->kind == TYPE_STRUCT) return (uint32_t)c->members[t->element + index].offset;
    return index * (t->kind == TYPE_VECTOR ? 4u : (uint32_t)t->state.layout.stride);
}
/* The same bounded leaf traversal supplies planning constants and emitted loads.
 * WalkItem.parent is the source-relative byte offset, not another storage pool. */
static bool begin_leaves(Emitter *e, TypeId type) {
    e->program->walk_count = 0u;
    return spirv_push_walk(e, WALK_TYPE, type);
}
static bool next_leaf(Emitter *e, TypeId *type, uint32_t *offset, bool *found) {
    CompilerWorkspace *c = e->program;
    *found = false;
    while (c->walk_count != 0u) {
        WalkItem *w = &c->walk[c->walk_count - 1u];
        if (w->object < 7u) {
            if (w->cursor++ == 0u) {
                *type = w->object;
                *offset = w->parent;
                *found = true;
                return true;
            }
            --c->walk_count;
            continue;
        }
        const uint32_t count = type_children(c, w->object);
        if (w->cursor == count) {
            --c->walk_count;
            continue;
        }
        const uint32_t index = w->cursor++;
        const TypeId child = type_child(c, w->object, index);
        const uint32_t child_byte = w->parent + child_offset(c, w->object, index);
        if (!spirv_push_walk(e, WALK_TYPE, child)) return false;
        c->walk[c->walk_count - 1u].parent = child_byte;
    }
    return true;
}
bool spirv_uniform_access_plan(Emitter *e, const IrInstruction *ir) {
    CompilerWorkspace *c = e->program;
    if (ir->operands.access.category != ROOT_RESOURCE ||
        !spirv_uniform_raw(e, &c->resources[ir->operands.access.root])) return true;
    for (uint32_t path = ir->operands.access.path; path != NO_ID; path = c->accesses[path].parent) {
        const AccessStep *a = &c->accesses[path];
        const uint32_t amount = a->kind == ACCESS_MEMBER ? child_offset(c, a->input_type, a->index) :
            a->kind == ACCESS_COMPONENT ? a->index * 4u : child_offset(c, a->input_type, 1u);
        (void)spirv_unsigned_constant(e, amount);
    }
    if (!begin_leaves(e, ir->type)) return false;
    bool found;
    TypeId type;
    uint32_t offset;
    while (next_leaf(e, &type, &offset, &found) && found) (void)spirv_unsigned_constant(e, offset);
    return !e->failed;
}
static bool raw_lane(Emitter *e, const ResourceRecord *r, uint32_t offset, uint32_t *out) {
    const uint32_t block = spirv_reserve_ids(e, 1u), lane = spirv_reserve_ids(e, 1u);
    EMIT(OP_UDIV, ID_U32, block, offset, spirv_unsigned_constant(e, 16u));
    /* Convert byte offset to word offset before selecting its vec4 lane. */
    const uint32_t word = spirv_reserve_ids(e, 1u);
    EMIT(OP_SHIFT_RIGHT_LOGICAL, ID_U32, word, offset, spirv_unsigned_constant(e, 2u));
    EMIT(OP_BIT_AND, ID_U32, lane, word, spirv_unsigned_constant(e, 3u));
    const uint32_t pointer = spirv_reserve_ids(e, 1u), loaded = spirv_reserve_ids(e, 1u);
    const uint32_t tail = (uint32_t)r->minimum % 16u;
    uint32_t prefix_label = 0u, tail_label = 0u, merge = 0u;
    if (tail != 0u) {
        const uint32_t condition = spirv_reserve_ids(e, 1u);
        prefix_label = spirv_reserve_ids(e, 1u);
        tail_label = spirv_reserve_ids(e, 1u);
        merge = spirv_reserve_ids(e, 1u);
        EMIT(OP_ULESS_THAN, ID_BOOL, condition, block, spirv_unsigned_constant(e, (uint32_t)r->minimum / 16u));
        EMIT(OP_SELECTION_MERGE, merge, 0u);
        EMIT(OP_BRANCH_CONDITIONAL, condition, prefix_label, tail_label);
        EMIT(OP_LABEL, prefix_label);
    }
    EMIT(OP_ACCESS_CHAIN, spirv_pointer_type(e, IR_U32, 2u), pointer, r->target, ID_ZERO_U32, block, lane);
    EMIT(OP_LOAD, ID_U32, loaded, pointer);
    *out = loaded;
    if (tail != 0u) {
        EMIT(OP_BRANCH, merge);
        EMIT(OP_LABEL, tail_label);
        const uint32_t tail_pointer = spirv_reserve_ids(e, 1u), tail_value = spirv_reserve_ids(e, 1u);
        if (tail == 4u) { EMIT(OP_ACCESS_CHAIN, spirv_pointer_type(e, IR_U32, 2u), tail_pointer, r->target, ID_ONE_U32); }
        else { EMIT(OP_ACCESS_CHAIN, spirv_pointer_type(e, IR_U32, 2u), tail_pointer, r->target, ID_ONE_U32, lane); }
        EMIT(OP_LOAD, ID_U32, tail_value, tail_pointer);
        EMIT(OP_BRANCH, merge);
        EMIT(OP_LABEL, merge);
        *out = spirv_reserve_ids(e, 1u);
        EMIT(OP_PHI, ID_U32, *out, loaded, prefix_label, tail_value, tail_label);
        e->current_label = merge;
    }
    return true;
}
bool spirv_uniform_load(Emitter *e, const IrInstruction *ir, uint32_t result) {
    CompilerWorkspace *c = e->program;
    const ResourceRecord *r = &c->resources[ir->operands.access.root];
    uint32_t base = ID_ZERO_U32;
    for (uint32_t i = c->walk_count; i != 0u; --i) {
        const WalkItem *w = &c->walk[i - 1u];
        const AccessStep *a = &c->accesses[w->object];
        uint32_t offset;
        if (a->kind == ACCESS_MEMBER || a->kind == ACCESS_COMPONENT)
            offset = spirv_unsigned_constant(e, child_offset(c, a->input_type, a->index));
        else {
            offset = spirv_reserve_ids(e, 1u);
            EMIT(OP_IMUL, ID_U32, offset, w->cursor, spirv_unsigned_constant(e, child_offset(c, a->input_type, 1u)));
        }
        const uint32_t next = spirv_reserve_ids(e, 1u);
        EMIT(OP_IADD, ID_U32, next, base, offset);
        base = next;
    }
    if (!begin_leaves(e, ir->type)) return false;
    uint32_t composite = spirv_zero_constant(e, ir->type), offset;
    TypeId type;
    bool found;
    while (next_leaf(e, &type, &offset, &found) && found) {
        uint32_t address = base, value;
        if (offset != 0u) {
            address = spirv_reserve_ids(e, 1u);
            EMIT(OP_IADD, ID_U32, address, base, spirv_unsigned_constant(e, offset));
        }
        if (!raw_lane(e, r, address, &value)) return false;
        if (type != IR_U32) {
            const uint32_t converted = spirv_reserve_ids(e, 1u);
            EMIT(OP_BITCAST, spirv_scalar_type(type), converted, value);
            value = converted;
        }
        if (ir->type < 7u) composite = value;
        else {
            const uint32_t inserted = spirv_reserve_ids(e, 1u);
            uint32_t *words;
            if (!spirv_instruction_space(e, OP_COMPOSITE_INSERT, (size_t)c->walk_count + 3u, &words)) return false;
            words[0] = spirv_target_type(e, ir->type);
            words[1] = inserted;
            words[2] = value;
            words[3] = composite;
            for (uint32_t i = 0u; i + 1u < c->walk_count; ++i) words[4u + i] = c->walk[i].cursor - 1u;
            composite = inserted;
        }
    }
    if (e->failed) return false;
    EMIT(OP_COPY_OBJECT, spirv_target_type(e, ir->type), result, composite);
    return true;
}
