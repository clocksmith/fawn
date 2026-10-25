/* SPDX-License-Identifier: MIT */
#include "pointer_lower.h"

static bool operands(PointerClone *p, uint32_t old, uint32_t count, uint32_t *out) {
    CompilerWorkspace *c = p->source->compiler;
    if (count > c->limits.operands-c->operand_count)
        return source_pool_error(p->source,DOE_COMPILER_POOL_IR_OPERANDS,(size_t)c->operand_count+count,
            p->source->length,"specialized instruction operand capacity exceeded");
    *out = c->operand_count;
    for (uint32_t i = 0u; i < count; ++i)
        c->typed_operands[c->operand_count++] = pointer_clone_value(p,c->typed_operands[old+i]);
    return true;
}
static bool access(PointerClone *p, IrInstruction *ir) {
    CompilerWorkspace *c = p->source->compiler;
    uint32_t prefix = NO_ID;
    if (ir->operands.access.category == ROOT_POINTER) {
        const uint32_t formal = ir->operands.access.root-p->original.formal_start;
        if (formal >= p->original.formal_count)
            return source_error(p->source,DOE_COMPILER_INPUT_ERROR,ir->source_offset,"specialized pointer formal is invalid");
        prefix = c->walk[2u*formal].cursor;
        const WalkItem map = c->walk[2u*formal+1u];
        if (map.kind == ROOT_RESOURCE || map.kind == ROOT_WORKGROUP) {
            ir->operands.access.category = map.kind;
            ir->operands.access.root = map.parent;
        } else ir->operands.access.root = map.object-c->limits.instructions-1u;
    } else if (ir->operands.access.category == ROOT_LOCAL)
        ir->operands.access.root += p->clone.local_start-p->original.local_start;
    else if (ir->operands.access.category == ROOT_VALUE)
        ir->operands.access.root = pointer_clone_value(p,ir->operands.access.root);
    if (!pointer_clone_path(p,ir->operands.access.path,prefix,false,&ir->operands.access.path)) return false;
    if (ir->opcode == IR_ADDRESS) ir->operands.access.value = 0u;
    else if (atomic_opcode(ir->opcode)) {
        const uint32_t count = ir->opcode == IR_ATOMIC_LOAD ? 0u : ir->opcode == IR_ATOMIC_COMPARE_EXCHANGE ? 2u : 1u;
        if (!operands(p,ir->operands.access.value,count,&ir->operands.access.value)) return false;
    } else if (ir->operands.access.category == ROOT_VALUE) {
        if (ir->operands.access.value != NO_ID)
            ir->operands.access.value += p->clone.local_start-p->original.local_start;
    } else ir->operands.access.value = pointer_clone_value(p,ir->operands.access.value);
    return true;
}
bool pointer_clone_instruction(PointerClone *p, const IrInstruction *original) {
    CompilerWorkspace *c = p->source->compiler;
    IrInstruction ir = *original;
    ir.result = pointer_clone_value(p,ir.result);
    switch (ir.opcode) {
        case IR_ACCESS_LOAD: case IR_ACCESS_STORE: case IR_VALUE_ACCESS: case IR_ADDRESS:
        case IR_ATOMIC_LOAD: case IR_ATOMIC_STORE: case IR_ATOMIC_ADD: case IR_ATOMIC_SUBTRACT:
        case IR_ATOMIC_MAX: case IR_ATOMIC_MIN: case IR_ATOMIC_AND: case IR_ATOMIC_OR:
        case IR_ATOMIC_XOR: case IR_ATOMIC_EXCHANGE: case IR_ATOMIC_COMPARE_EXCHANGE:
            if (!access(p,&ir)) return false;
            break;
        case IR_COMPOSITE: case IR_IMAGE:
            if (!operands(p,ir.operands.composite.first,ir.operands.composite.count,&ir.operands.composite.first)) return false;
            break;
        case IR_CALL:
            if (!operands(p,ir.operands.scalar.left,ir.operands.scalar.right,&ir.operands.scalar.left)) return false;
            break;
        case IR_BUILTIN: {
            const NumericBuiltin *builtin = builtin_kind((NumericBuiltinKind)ir.operands.scalar.literal);
            if (builtin == NULL) return source_error(p->source,DOE_COMPILER_INPUT_ERROR,ir.source_offset,"specialized builtin identity is invalid");
            if (builtin->arity > 0u) ir.operands.scalar.left = pointer_clone_value(p,ir.operands.scalar.left);
            if (builtin->arity > 1u) ir.operands.scalar.right = pointer_clone_value(p,ir.operands.scalar.right);
            if (builtin->arity > 2u) ir.operands.scalar.binding = pointer_clone_value(p,ir.operands.scalar.binding);
            break;
        }
        case IR_LOCAL_LOAD: case IR_LOCAL_STORE:
            ir.operands.scalar.binding += p->clone.local_start-p->original.local_start;
            if (ir.opcode == IR_LOCAL_STORE) ir.operands.scalar.right = pointer_clone_value(p,ir.operands.scalar.right);
            break;
        case IR_SELECT:
            ir.operands.scalar.binding = pointer_clone_value(p,ir.operands.scalar.binding);
            ir.operands.scalar.left = pointer_clone_value(p,ir.operands.scalar.left);
            ir.operands.scalar.right = pointer_clone_value(p,ir.operands.scalar.right);
            break;
        case IR_ADD: case IR_SUBTRACT: case IR_MULTIPLY: case IR_COMPARE:
        case IR_DIVIDE: case IR_REMAINDER: case IR_BIT_AND: case IR_BIT_OR: case IR_BIT_XOR:
        case IR_SHIFT_LEFT: case IR_SHIFT_RIGHT: case IR_SHORT_END:
            ir.operands.scalar.left = pointer_clone_value(p,ir.operands.scalar.left);
            ir.operands.scalar.right = pointer_clone_value(p,ir.operands.scalar.right);
            break;
        case IR_IF: case IR_RETURN: case IR_NOT: case IR_SHORT_BEGIN:
        case IR_LOOP_TEST: case IR_BREAK_IF: case IR_NEGATE: case IR_COMPLEMENT:
        case IR_CONVERT: case IR_BITCAST: case IR_SWIZZLE:
            ir.operands.scalar.left = pointer_clone_value(p,ir.operands.scalar.left);
            break;
        case IR_ARRAY_LENGTH:
            if (ir.operands.scalar.literal == ROOT_POINTER) {
                const uint32_t formal = ir.operands.scalar.binding-p->original.formal_start;
                if (formal >= p->original.formal_count || c->walk[2u*formal+1u].kind != ROOT_RESOURCE)
                    return source_error(p->source,DOE_COMPILER_INPUT_ERROR,ir.source_offset,"runtime array formal lacks its resource root");
                ir.operands.scalar.binding = c->walk[2u*formal+1u].parent;
                ir.operands.scalar.literal = ROOT_RESOURCE;
            }
            break;
        case IR_CONSTANT: case IR_ELSE: case IR_END_IF: case IR_LOOP_BEGIN:
        case IR_LOOP_CONTINUING: case IR_END_LOOP: case IR_BREAK: case IR_CONTINUE:
        case IR_WORKGROUP_BARRIER: case IR_HANDLE: case IR_DISCARD:
            break;
        case IR_INPUT: case IR_STAGE_INPUT: case IR_ROBUST_LOAD: case IR_UNIFORM_LOAD:
        case IR_MAX: case IR_ROBUST_STORE: case IR_DEFERRED_OVERRIDE:
            return source_error(p->source,DOE_COMPILER_INPUT_ERROR,ir.source_offset,"non-helper instruction reached pointer specialization");
    }
    if (c->instruction_count == c->limits.instructions)
        return source_pool_error(p->source,DOE_COMPILER_POOL_IR,(size_t)c->instruction_count+1u,ir.source_offset,
            "specialized function instruction capacity exceeded");
    c->instructions[c->instruction_count++] = ir;
    return true;
}
