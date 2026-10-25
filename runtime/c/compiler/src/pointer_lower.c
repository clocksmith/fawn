/* SPDX-License-Identifier: MIT */
#include "pointer_lower.h"
#include <string.h>

bool pointer_lower_charge(Source *s, uint64_t *work, uint64_t amount) {
    if (amount > UNIFORMITY_WORK_LIMIT - *work)
        return source_error(s,DOE_COMPILER_UNSUPPORTED,s->length,"pointer specialization work limit exceeded");
    *work += amount;
    return true;
}
uint32_t pointer_clone_value(const PointerClone *p, uint32_t value) {
    const CompilerWorkspace *c = p->source->compiler;
    if (value > p->original.instruction_start && value <= p->original.instruction_end)
        return value - p->original.instruction_start + p->clone.instruction_start;
    const uint32_t base = c->limits.instructions + 1u;
    if (value >= base + p->original.formal_start &&
        value - base - p->original.formal_start < p->original.formal_count)
        return value - p->original.formal_start + p->clone.formal_start;
    return value;
}
/* Existing traversal capacity holds one prefix per original formal, followed
 * by the temporary path stack. No hidden allocation or recursion is introduced. */
bool pointer_clone_path(PointerClone *p, uint32_t path, uint32_t prefix,
                        bool argument, uint32_t *out) {
    CompilerWorkspace *c = p->source->compiler;
    uint32_t count = 0u;
    for (uint32_t at = path; at != NO_ID; at = c->accesses[at].parent) {
        if (!pointer_lower_charge(p->source,p->work,1u)) return false;
        if (at >= c->access_count) return source_error(p->source,DOE_COMPILER_INPUT_ERROR,p->source->length,"pointer path identity is invalid");
        if (p->original.formal_count + count == c->limits.traversal)
            return source_pool_error(p->source,DOE_COMPILER_POOL_TRAVERSAL,
                (size_t)p->original.formal_count+count+1u,p->source->length,"pointer path staging capacity exceeded");
        c->walk[p->original.formal_count+count++].object = at;
    }
    *out = prefix;
    while (count != 0u) {
        AccessStep step = c->accesses[c->walk[p->original.formal_count+--count].object];
        step.parent = *out;
        if (step.kind == ACCESS_INDEX_U32 || step.kind == ACCESS_INDEX_I32) {
            if (argument) {
                c->typed_operands[p->argument_first+p->argument_count++] = step.index;
                c->formals[p->extra_formal].type.resolved.type = step.kind == ACCESS_INDEX_U32 ? IR_U32 : IR_I32;
                c->formals[p->extra_formal].type.resolved.offset = NO_ID;
                step.index = c->limits.instructions + 1u + p->extra_formal++;
            } else step.index = pointer_clone_value(p,step.index);
        }
        if (c->access_count == c->limits.accesses)
            return source_pool_error(p->source,DOE_COMPILER_POOL_ACCESS_STEPS,(size_t)c->access_count+1u,
                step.source_offset,"specialized pointer access capacity exceeded");
        *out = c->access_count;
        c->accesses[c->access_count++] = step;
    }
    return true;
}
static bool operand_path(Source *s, uint32_t value, TypeId expected,
    uint32_t *path, TypeId *root_type, uint32_t *address, uint64_t *work) {
    CompilerWorkspace *c = s->compiler;
    *address = NO_ID;
    const uint32_t base = c->limits.instructions+1u;
    if (value >= base && value-base < c->formal_count) {
        const FormalRecord *formal = &c->formals[value-base];
        *root_type = c->types[formal->type.resolved.type-IR_PRIMITIVE_COUNT].element;
        *path = formal->type.resolved.offset;
    } else if (value != 0u && value <= c->instruction_count && c->instructions[value-1u].opcode == IR_ADDRESS) {
        const IrInstruction *ir = &c->instructions[value-1u];
        *address = value-1u;
        *path = ir->operands.access.path;
        if (ir->operands.access.category == ROOT_LOCAL) *root_type = c->locals[ir->operands.access.root].type;
        else if (ir->operands.access.category == ROOT_POINTER)
            *root_type = c->types[c->formals[ir->operands.access.root].type.resolved.type-IR_PRIMITIVE_COUNT].element;
        else return source_error(s,DOE_COMPILER_UNSUPPORTED,ir->source_offset,"pointer specialization requires a function root");
    } else return source_error(s,DOE_COMPILER_INPUT_ERROR,s->length,"pointer argument has no canonical identity");
    if (*root_type == expected) { *path = NO_ID; return true; }
    for (uint32_t at = *path; at != NO_ID; at = c->accesses[at].parent) {
        if (!pointer_lower_charge(s,work,1u)) return false;
        if (c->accesses[at].output_type == expected) { *path = at; return true; }
    }
    return source_error(s,DOE_COMPILER_INPUT_ERROR,s->length,"pointer argument path does not reach its formal type");
}
static bool specialize_call(Source *s, uint32_t index, uint64_t *work) {
    CompilerWorkspace *c = s->compiler;
    IrInstruction call = c->instructions[index];
    const FunctionRecord original = c->functions[call.operands.scalar.binding];
    if (!pointer_lower_charge(s,work,original.formal_count)) return false;
    bool any_path = false;
    for (uint32_t i = 0u; i < original.formal_count; ++i) {
        const TypeId type = c->formals[original.formal_start+i].type.resolved.type;
        if (!type_pointer(c,type)) continue;
        uint32_t path = NO_ID, address = NO_ID;
        TypeId root = IR_VOID;
        if (!operand_path(s,c->typed_operands[call.operands.scalar.left+i],
            c->types[type-IR_PRIMITIVE_COUNT].element,&path,&root,&address,work)) return false;
        any_path |= path != NO_ID;
    }
    if (!any_path) return true;
    if (original.formal_count > c->limits.traversal)
        return source_pool_error(s,DOE_COMPILER_POOL_TRAVERSAL,original.formal_count,call.source_offset,
            "pointer formal staging capacity exceeded");
    bool needed = false;
    uint32_t extra = 0u;
    for (uint32_t i = 0u; i < original.formal_count; ++i) {
        c->walk[i] = (WalkItem){.object=NO_ID,.parent=NO_ID};
        const TypeId type = c->formals[original.formal_start+i].type.resolved.type;
        if (!type_pointer(c,type)) continue;
        TypeId root = IR_VOID;
        uint32_t path = NO_ID, address = NO_ID;
        if (!operand_path(s,c->typed_operands[call.operands.scalar.left+i],
            c->types[type-IR_PRIMITIVE_COUNT].element,&path,&root,&address,work)) return false;
        c->walk[i] = (WalkItem){.kind=root,.object=path,.parent=address};
        if (path == NO_ID) continue;
        needed = true;
        for (uint32_t at = path; at != NO_ID; at = c->accesses[at].parent) {
            if (!pointer_lower_charge(s,work,1u)) return false;
            if (c->accesses[at].kind == ACCESS_INDEX_U32 || c->accesses[at].kind == ACCESS_INDEX_I32) ++extra;
        }
    }
    if (!needed) return true;
    const uint32_t count = original.formal_count+extra;
    if (count > UINT16_MAX-4u)
        return source_error(s,DOE_COMPILER_UNSUPPORTED,call.source_offset,"specialized function operand format exceeded");
    if (c->function_count == c->limits.tokens/6u || count > c->limits.tokens/4u-c->formal_count) {
        const size_t functions = ((size_t)c->function_count+1u)*6u;
        const size_t formals = ((size_t)c->formal_count+count)*4u;
        return source_pool_error(s,DOE_COMPILER_POOL_TOKENS,functions > formals ? functions : formals,
            call.source_offset,"specialized function or formal capacity exceeded");
    }
    if (count > c->limits.operands-c->operand_count)
        return source_pool_error(s,DOE_COMPILER_POOL_IR_OPERANDS,(size_t)c->operand_count+count,call.source_offset,
            "specialized call operand capacity exceeded");
    const uint32_t clone_index = c->function_count++;
    FunctionRecord clone = original;
    clone.formal_start = c->formal_count; clone.formal_count = count;
    clone.instruction_start = c->instruction_count;
    clone.local_start = c->local_count;
    if (original.local_count > (size_t)c->limits.statements+1u+c->limits.accesses-c->local_count)
        return source_pool_error(s,DOE_COMPILER_POOL_ACCESS_STEPS,(size_t)c->limits.accesses+1u,call.source_offset,
            "specialized local capacity exceeded");
    for (uint32_t i = 0u; i < original.local_count; ++i) c->locals[c->local_count++] = c->locals[original.local_start+i];
    const uint32_t first = c->operand_count;
    c->operand_count += count;
    memcpy(c->typed_operands+first,c->typed_operands+call.operands.scalar.left,original.formal_count*sizeof(uint32_t));
    memcpy(c->formals+clone.formal_start,c->formals+original.formal_start,original.formal_count*sizeof(FormalRecord));
    memset(c->formals+clone.formal_start+original.formal_count,0,extra*sizeof(FormalRecord));
    c->formal_count += count;
    PointerClone context = {s,original,clone,clone.formal_start+original.formal_count,first,original.formal_count,work};
    for (uint32_t i = 0u; i < original.formal_count; ++i) {
        const WalkItem argument = c->walk[i];
        uint32_t prefix = NO_ID;
        if (argument.object != NO_ID) {
            if (!pointer_clone_path(&context,argument.object,NO_ID,true,&prefix)) return false;
            TypeId pointer;
            if (!type_intern(s,TYPE_POINTER,argument.kind,1u,&pointer)) return false;
            c->formals[clone.formal_start+i].type.resolved.type = pointer;
        }
        c->walk[i].cursor = prefix;
        uint32_t logical = prefix;
        if (type_pointer(c,c->formals[original.formal_start+i].type.resolved.type) &&
            !pointer_clone_path(&context,c->formals[original.formal_start+i].type.resolved.offset,prefix,false,&logical)) return false;
        c->formals[clone.formal_start+i].type.resolved.offset = logical;
        if (argument.parent != NO_ID) {
            IrInstruction *address = &c->instructions[argument.parent];
            TypeId pointer;
            if (!type_intern(s,TYPE_POINTER,argument.kind,1u,&pointer)) return false;
            address->type = pointer;
            address->operands.access.value = 1u; /* Clear its source path after all calls were specialized. */
        }
    }
    for (uint32_t i = original.instruction_start; i < original.instruction_end; ++i) {
        if (!pointer_lower_charge(s,work,1u)) return false;
        const IrInstruction instruction = c->instructions[i];
        if (!pointer_clone_instruction(&context,&instruction)) return false;
    }
    clone.instruction_end = c->instruction_count;
    c->functions[clone_index] = clone;
    c->instructions[index].operands.scalar.left = first;
    c->instructions[index].operands.scalar.right = count;
    c->instructions[index].operands.scalar.binding = clone_index;
    return true;
}
bool pointer_lower(Source *s) {
    CompilerWorkspace *c = s->compiler;
    /* Formal declaration offsets have no consumers after semantic analysis.
     * Reuse this word as a logical subobject path for synthesized parameters. */
    for (uint32_t i = 0u; i < c->formal_count; ++i) c->formals[i].type.resolved.offset = NO_ID;
    uint64_t work = 0u;
    for (uint32_t i = 0u; i < c->instruction_count; ++i) {
        if (!pointer_lower_charge(s,&work,1u)) return false;
        if (c->instructions[i].opcode == IR_CALL && !specialize_call(s,i,&work)) return false;
    }
    for (uint32_t i = 0u; i < c->instruction_count; ++i) {
        IrInstruction *ir = &c->instructions[i];
        if (ir->opcode == IR_ADDRESS && ir->operands.access.value == 1u) {
            ir->operands.access.path = NO_ID;
            ir->operands.access.value = 0u;
        }
    }
    return true;
}
