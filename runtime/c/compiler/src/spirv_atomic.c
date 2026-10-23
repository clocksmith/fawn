/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "spirv_internal.h"

/* The caller has proved every index in range before forming this pointer.
 * GLSL450 uses Device for storage and Workgroup for shared memory; WGSL atomics
 * use Relaxed semantics without storage-class semantics bits. A strong compare
 * exchange is a permitted weak implementation, so equality reliably reports
 * whether the exchange occurred, without inventing success on spurious failure. */
bool spirv_atomic_operation(Emitter *e, const IrInstruction *ir, uint32_t pointer, uint32_t result) {
    const CompilerWorkspace *c = e->program;
    const TypeId scalar = atomic_element(c, ir);
    const uint32_t type = spirv_target_type(e, scalar);
    const uint32_t scope = ir->operands.access.category == ROOT_WORKGROUP ? ID_TWO_U32 : ID_ONE_U32;
    const uint32_t first = ir->operands.access.value;
    const uint32_t count = ir->opcode == IR_ATOMIC_LOAD ? 0u : ir->opcode == IR_ATOMIC_COMPARE_EXCHANGE ? 2u : 1u;
    if (first > c->operand_count || count > c->operand_count - first)
        return spirv_emit_error(e, ir->source_offset, "invalid atomic typed operand range");
    const uint32_t value = count == 0u ? 0u : e->values[c->typed_operands[first]];
    uint32_t opcode;
    switch ((uint32_t)ir->opcode) {
        case IR_ATOMIC_LOAD:
            EMIT(OP_ATOMIC_LOAD, type, result, pointer, scope, ID_ZERO_U32);
            return true;
        case IR_ATOMIC_STORE:
            EMIT(OP_ATOMIC_STORE, pointer, scope, ID_ZERO_U32, value);
            return true;
        case IR_ATOMIC_COMPARE_EXCHANGE: {
            const uint32_t old = spirv_reserve_ids(e, 1u), exchanged = spirv_reserve_ids(e, 1u);
            EMIT(OP_ATOMIC_COMPARE_EXCHANGE, type, old, pointer, scope, ID_ZERO_U32, ID_ZERO_U32,
                e->values[c->typed_operands[first + 1u]], value);
            EMIT(OP_IEQUAL, ID_BOOL, exchanged, old, value);
            EMIT(OP_COMPOSITE_CONSTRUCT, spirv_target_type(e, ir->type), result, old, exchanged);
            return true;
        }
        case IR_ATOMIC_ADD: opcode = OP_ATOMIC_ADD; break;
        case IR_ATOMIC_SUBTRACT: opcode = OP_ATOMIC_SUBTRACT; break;
        case IR_ATOMIC_MAX: opcode = scalar == IR_I32 ? OP_ATOMIC_SMAX : OP_ATOMIC_UMAX; break;
        case IR_ATOMIC_MIN: opcode = scalar == IR_I32 ? OP_ATOMIC_SMIN : OP_ATOMIC_UMIN; break;
        case IR_ATOMIC_AND: opcode = OP_ATOMIC_AND; break;
        case IR_ATOMIC_OR: opcode = OP_ATOMIC_OR; break;
        case IR_ATOMIC_XOR: opcode = OP_ATOMIC_XOR; break;
        case IR_ATOMIC_EXCHANGE: opcode = OP_ATOMIC_EXCHANGE; break;
        default: return spirv_emit_error(e, ir->source_offset, "invalid atomic operation identity");
    }
    EMIT(opcode, type, result, pointer, scope, ID_ZERO_U32, value);
    return true;
}
