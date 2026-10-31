/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors
 * Index proofs and resource index clamps for SPIR-V access chains.
 *
 * Storage and uniform buffer roots clamp every dynamic index to its last valid
 * element, min(uint(index), last), where last is count - 1 for a fixed array or
 * vector and max(OpArrayLength, 1) - 1 for a runtime tail. Both are written with
 * core OpULessThan/OpSelect (no branch and no extended-instruction import): a
 * negative i32 index reinterprets as a large uint and clamps to last, and an
 * empty runtime tail's length - 1 wraps above the length, selecting the length
 * (0), so it clamps to element 0. The access then runs
 * unconditionally inside the declared extent; WGSL robustness permits any
 * in-bounds element. Workgroup, private, function and value roots keep their
 * guards (spirv.c access_condition). */
#include "spirv_internal.h"

bool spirv_literal_index(const CompilerWorkspace *c, uint32_t semantic, uint32_t *value) {
    if (semantic == 0u || semantic > c->instruction_count) return false;
    const IrInstruction *ir = &c->instructions[semantic - 1u];
    if (ir->opcode != IR_CONSTANT || ir->operands.scalar.left != 0u || (ir->type != IR_U32 && ir->type != IR_I32)) return false;
    if (ir->type == IR_I32 && ir->operands.scalar.literal > INT32_MAX) return false;
    *value = ir->operands.scalar.literal;
    return true;
}
/* A fixed-length index needs no guard or clamp when it is provably below the
 * length: a nonnegative literal, or a bitwise AND with a nonnegative literal
 * mask below the length (the result lies in [0, mask] for u32 and i32 alike). */
bool spirv_index_below(const CompilerWorkspace *c, uint32_t semantic, uint32_t length) {
    uint32_t value;
    if (spirv_literal_index(c, semantic, &value)) return value < length;
    if (semantic == 0u || semantic > c->instruction_count) return false;
    const IrInstruction *ir = &c->instructions[semantic - 1u];
    if (ir->opcode != IR_BIT_AND || (ir->type != IR_U32 && ir->type != IR_I32)) return false;
    return (spirv_literal_index(c, ir->operands.scalar.left, &value) && value < length) ||
        (spirv_literal_index(c, ir->operands.scalar.right, &value) && value < length);
}
bool spirv_clamped_access(const IrInstruction *ir) {
    return ir->operands.access.category == ROOT_RESOURCE &&
        (ir->opcode == IR_ACCESS_LOAD || ir->opcode == IR_ACCESS_STORE || atomic_opcode(ir->opcode));
}
/* Declares exactly the constants the clamps of one resource access use,
 * before any body is serialized. */
bool spirv_plan_clamps(Emitter *e, const IrInstruction *ir) {
    const CompilerWorkspace *c = e->program;
    if (!spirv_clamped_access(ir)) return true;
    for (uint32_t path = ir->operands.access.path; path != NO_ID; path = c->accesses[path].parent) {
        if (path >= c->access_count) return spirv_emit_error(e, 0u, "invalid typed access path");
        const AccessStep *a = &c->accesses[path];
        if (a->kind == ACCESS_MEMBER || a->kind == ACCESS_COMPONENT) continue;
        if (a->input_type < IR_PRIMITIVE_COUNT) return spirv_emit_error(e, a->source_offset, "indexed access has no aggregate type");
        const TypeRecord *input = &c->types[a->input_type - IR_PRIMITIVE_COUNT];
        if (input->kind == TYPE_RUNTIME_ARRAY) (void)spirv_unsigned_constant(e, 1u);
        else if (spirv_index_below(c, a->index, input->count)) continue;
        else if (input->count == 0u) return spirv_emit_error(e, a->source_offset, "indexed access has an empty extent");
        else if (spirv_unsigned_constant(e, input->count - 1u) == 0u) return false;
    }
    return !e->failed;
}
/* index is the u32 index (an i32 index already reinterpreted); *out is the
 * clamped index for the access chain. */
bool spirv_clamp_index(Emitter *e, const IrInstruction *ir, const TypeRecord *input, uint32_t index, uint32_t *out) {
    uint32_t last;
    if (input->kind == TYPE_RUNTIME_ARRAY) {
        const uint32_t length = spirv_reserve_ids(e, 1u), previous = spirv_reserve_ids(e, 1u);
        const uint32_t below = spirv_reserve_ids(e, 1u);
        last = spirv_reserve_ids(e, 1u);
        if (!spirv_runtime_length(e, ir->operands.access.root, length)) return false;
        /* last = min(length - 1, length): length - 1, or 0 for an empty tail. */
        EMIT(OP_ISUB, ID_U32, previous, length, spirv_unsigned_constant(e, 1u));
        EMIT(OP_ULESS_THAN, ID_BOOL, below, previous, length);
        EMIT(OP_SELECT, ID_U32, last, below, previous, length);
    } else last = spirv_unsigned_constant(e, input->count - 1u);
    const uint32_t inside = spirv_reserve_ids(e, 1u);
    *out = spirv_reserve_ids(e, 1u);
    EMIT(OP_ULESS_THAN, ID_BOOL, inside, index, last);
    EMIT(OP_SELECT, ID_U32, *out, inside, index, last);
    return !e->failed;
}
