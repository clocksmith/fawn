/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "sema.h"

uint32_t atomic_builtin_kind(const Source *source, NameSpan name) {
    static const struct { const char *name; IrOpcode opcode; } operations[] = {
        {"atomicLoad", IR_ATOMIC_LOAD}, {"atomicStore", IR_ATOMIC_STORE},
        {"atomicAdd", IR_ATOMIC_ADD}, {"atomicSub", IR_ATOMIC_SUBTRACT},
        {"atomicMax", IR_ATOMIC_MAX}, {"atomicMin", IR_ATOMIC_MIN},
        {"atomicAnd", IR_ATOMIC_AND}, {"atomicOr", IR_ATOMIC_OR},
        {"atomicXor", IR_ATOMIC_XOR}, {"atomicExchange", IR_ATOMIC_EXCHANGE},
        {"atomicCompareExchangeWeak", IR_ATOMIC_COMPARE_EXCHANGE}
    };
    for (size_t i = 0u; i < sizeof(operations) / sizeof(*operations); ++i)
        if (source_span_is(source, name, operations[i].name)) return (uint32_t)operations[i].opcode;
    return NO_ID;
}
bool atomic_opcode(IrOpcode opcode) {
    return opcode >= IR_ATOMIC_LOAD && opcode <= IR_ATOMIC_COMPARE_EXCHANGE;
}
TypeId atomic_element(const CompilerWorkspace *c, const IrInstruction *ir) {
    const TypeId type = ir->operands.access.path != NO_ID ? c->accesses[ir->operands.access.path].output_type :
        ir->operands.access.category == ROOT_RESOURCE ? c->resources[ir->operands.access.root].type :
        c->workgroups[ir->operands.access.root].type;
    return c->types[type - IR_PRIMITIVE_COUNT].element;
}
