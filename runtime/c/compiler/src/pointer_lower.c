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
        return value - p->original.instruction_start + p->value_start;
    const uint32_t base = c->limits.instructions + 1u;
    if (value >= base + p->original.formal_start &&
        value - base - p->original.formal_start < p->original.formal_count)
        return c->walk[2u*(value-base-p->original.formal_start)+1u].object;
    return value;
}
/* Two traversal records per original formal retain its canonical root/path and
 * new value identity. The remaining charged traversal pool stages paths. */
bool pointer_clone_path(PointerClone *p, uint32_t path, uint32_t prefix,
                        bool argument, uint32_t *out) {
    CompilerWorkspace *c = p->source->compiler;
    const uint32_t first = 2u*p->original.formal_count;
    uint32_t count = 0u;
    for (uint32_t at = path; at != NO_ID; at = c->accesses[at].parent) {
        if (!pointer_lower_charge(p->source,p->work,1u)) return false;
        if (at >= c->access_count) return source_error(p->source,DOE_COMPILER_INPUT_ERROR,p->source->length,"pointer path identity is invalid");
        if (first + count == c->limits.traversal)
            return source_pool_error(p->source,DOE_COMPILER_POOL_TRAVERSAL,
                (size_t)first+count+1u,p->source->length,"pointer path staging capacity exceeded");
        c->walk[first+count++].object = at;
    }
    *out = prefix;
    while (count != 0u) {
        AccessStep step = c->accesses[c->walk[first+--count].object];
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
typedef struct { uint32_t path, address, category, root; TypeId root_type; } Argument;
static bool operand_path(Source *s, uint32_t value, TypeId expected, Argument *a, uint64_t *work) {
    CompilerWorkspace *c = s->compiler;
    *a = (Argument){.path=NO_ID,.address=NO_ID,.category=ROOT_POINTER,.root=NO_ID};
    const uint32_t base = c->limits.instructions+1u;
    if (value >= base && value-base < c->formal_count) {
        a->root = value-base;
        const FormalRecord *formal = &c->formals[a->root];
        a->root_type = c->types[formal->type.resolved.type-IR_PRIMITIVE_COUNT].element;
        a->path = formal->type.resolved.offset;
    } else if (value != 0u && value <= c->instruction_count && c->instructions[value-1u].opcode == IR_ADDRESS) {
        const IrInstruction *ir = &c->instructions[value-1u];
        a->address = value-1u; a->path = ir->operands.access.path;
        a->category = ir->operands.access.category; a->root = ir->operands.access.root;
        if (a->category == ROOT_LOCAL) a->root_type = c->locals[a->root].type;
        else if (a->category == ROOT_POINTER)
            a->root_type = c->types[c->formals[a->root].type.resolved.type-IR_PRIMITIVE_COUNT].element;
        else if (a->category == ROOT_RESOURCE) a->root_type = c->resources[a->root].type;
        else if (a->category == ROOT_WORKGROUP) a->root_type = c->workgroups[a->root].type;
        else return source_error(s,DOE_COMPILER_INPUT_ERROR,ir->source_offset,"pointer root identity is invalid");
    } else return source_error(s,DOE_COMPILER_INPUT_ERROR,s->length,"pointer argument has no canonical identity");
    if (a->root_type == expected) { a->path = NO_ID; return true; }
    for (uint32_t at = a->path; at != NO_ID; at = c->accesses[at].parent) {
        if (!pointer_lower_charge(s,work,1u)) return false;
        if (c->accesses[at].output_type == expected) { a->path = at; return true; }
    }
    return source_error(s,DOE_COMPILER_INPUT_ERROR,s->length,"pointer argument path does not reach its formal type");
}
static bool global_root(uint32_t category) { return category == ROOT_RESOURCE || category == ROOT_WORKGROUP; }
static bool specialize_call(Source *s, uint32_t index, uint64_t *work) {
    CompilerWorkspace *c = s->compiler;
    const IrInstruction call = c->instructions[index];
    const FunctionRecord original = c->functions[call.operands.scalar.binding];
    bool needed = false;
    uint32_t removed = 0u, extra = 0u;
    for (uint32_t i = 0u; i < original.formal_count; ++i) {
        if (!pointer_lower_charge(s,work,1u)) return false;
        const TypeId type = c->formals[original.formal_start+i].type.resolved.type;
        if (!type_pointer(c,type)) continue;
        Argument a;
        if (!operand_path(s,c->typed_operands[call.operands.scalar.left+i],c->types[type-IR_PRIMITIVE_COUNT].element,&a,work)) return false;
        /* An original generic helper is not executable until its caller fixes
         * the global root. Its cloned calls are revisited below. */
        if (a.category == ROOT_POINTER && c->types[type-IR_PRIMITIVE_COUNT].count != POINTER_FUNCTION) return true;
        needed |= a.path != NO_ID || global_root(a.category);
        removed += global_root(a.category) ? 1u : 0u;
        for (uint32_t at = a.path; at != NO_ID; at = c->accesses[at].parent) {
            if (!pointer_lower_charge(s,work,1u)) return false;
            if (c->accesses[at].kind == ACCESS_INDEX_U32 || c->accesses[at].kind == ACCESS_INDEX_I32) ++extra;
        }
    }
    if (!needed) return true;
    if ((uint64_t)original.formal_count*2u > c->limits.traversal)
        return source_pool_error(s,DOE_COMPILER_POOL_TRAVERSAL,(size_t)original.formal_count*2u,call.source_offset,
            "pointer formal staging capacity exceeded");
    const uint32_t retained = original.formal_count-removed, count = retained+extra;
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
    clone.instruction_start = c->instruction_count; clone.local_start = c->local_count;
    if (original.local_count > (size_t)c->limits.statements+1u+c->limits.accesses-c->local_count)
        return source_pool_error(s,DOE_COMPILER_POOL_ACCESS_STEPS,(size_t)c->limits.accesses+1u,call.source_offset,
            "specialized local capacity exceeded");
    for (uint32_t i = 0u; i < original.local_count; ++i) c->locals[c->local_count++] = c->locals[original.local_start+i];
    const uint32_t first = c->operand_count;
    c->operand_count += count;
    memset(c->formals+clone.formal_start,0,count*sizeof(FormalRecord));
    c->formal_count += count;
    uint32_t at = 0u;
    for (uint32_t i = 0u; i < original.formal_count; ++i) {
        const TypeId type = c->formals[original.formal_start+i].type.resolved.type;
        Argument a = {.path=NO_ID,.address=NO_ID,.category=ROOT_LOCAL};
        if (type_pointer(c,type) && !operand_path(s,c->typed_operands[call.operands.scalar.left+i],
            c->types[type-IR_PRIMITIVE_COUNT].element,&a,work)) return false;
        c->walk[2u*i] = (WalkItem){.kind=a.root_type,.object=a.path,.parent=a.address,.cursor=NO_ID};
        c->walk[2u*i+1u] = (WalkItem){.kind=a.category,.parent=a.root,.object=0u};
        if (global_root(a.category)) continue;
        c->typed_operands[first+at] = c->typed_operands[call.operands.scalar.left+i];
        c->formals[clone.formal_start+at] = c->formals[original.formal_start+i];
        c->walk[2u*i+1u].object = c->limits.instructions+1u+clone.formal_start+at++;
    }
    PointerClone context = {.source=s,.original=original,.clone=clone,.value_start=clone.instruction_start+removed,
        .extra_formal=clone.formal_start+retained,.argument_first=first,.argument_count=retained,.work=work};
    for (uint32_t i = 0u; i < original.formal_count; ++i) {
        const WalkItem argument = c->walk[2u*i];
        uint32_t prefix = NO_ID;
        if (argument.object != NO_ID && !pointer_clone_path(&context,argument.object,NO_ID,true,&prefix)) return false;
        c->walk[2u*i].cursor = prefix;
        WalkItem *map = &c->walk[2u*i+1u];
        if (global_root(map->kind)) {
            if (c->instruction_count == c->limits.instructions)
                return source_pool_error(s,DOE_COMPILER_POOL_IR,(size_t)c->instruction_count+1u,call.source_offset,"global pointer descriptor capacity exceeded");
            map->object = c->instruction_count+1u;
            c->instructions[c->instruction_count++] = (IrInstruction){.opcode=IR_ADDRESS,
                .type=c->formals[original.formal_start+i].type.resolved.type,.result=map->object,.source_offset=call.source_offset,
                .operands.access={.root=map->parent,.path=prefix,.category=map->kind}};
            continue;
        }
        const uint32_t formal = map->object-c->limits.instructions-1u;
        if (argument.object != NO_ID) {
            TypeId pointer;
            if (!type_intern(s,TYPE_POINTER,argument.kind,POINTER_FUNCTION,&pointer)) return false;
            c->formals[formal].type.resolved.type = pointer;
        }
        uint32_t logical = prefix;
        if (type_pointer(c,c->formals[original.formal_start+i].type.resolved.type) &&
            !pointer_clone_path(&context,c->formals[original.formal_start+i].type.resolved.offset,prefix,false,&logical)) return false;
        c->formals[formal].type.resolved.offset = logical;
        if (argument.parent != NO_ID) {
            IrInstruction *address = &c->instructions[argument.parent];
            TypeId pointer;
            if (!type_intern(s,TYPE_POINTER,argument.kind,POINTER_FUNCTION,&pointer)) return false;
            address->type = pointer; address->operands.access.value = 1u;
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
    for (uint32_t i = 0u; i < c->formal_count; ++i) c->formals[i].type.resolved.offset = NO_ID;
    uint64_t work = 0u;
    for (uint32_t i = 0u; i < c->instruction_count; ++i) {
        if (!pointer_lower_charge(s,&work,1u)) return false;
        if (c->instructions[i].opcode == IR_CALL && !specialize_call(s,i,&work)) return false;
    }
    for (uint32_t i = 0u; i < c->instruction_count; ++i) {
        IrInstruction *ir = &c->instructions[i];
        if (ir->opcode == IR_ADDRESS && ir->operands.access.value == 1u) {
            ir->operands.access.path = NO_ID; ir->operands.access.value = 0u;
        }
    }
    for (uint32_t f = 0u; f < c->function_count; ++f) {
        c->functions[f].flags &= ~(uint32_t)FUNCTION_REACHABLE;
        if ((c->functions[f].flags & FUNCTION_ENTRY) != 0u) c->functions[f].flags |= FUNCTION_REACHABLE;
    }
    bool changed = true;
    while (changed) {
        changed = false;
        for (uint32_t f = 0u; f < c->function_count; ++f) {
            const FunctionRecord *fn = &c->functions[f];
            if (!pointer_lower_charge(s,&work,1u)) return false;
            if ((fn->flags & FUNCTION_REACHABLE) == 0u) continue;
            for (uint32_t i = fn->instruction_start; i < fn->instruction_end; ++i) {
                if (!pointer_lower_charge(s,&work,1u)) return false;
                const IrInstruction *ir = &c->instructions[i];
                if (ir->opcode != IR_CALL) continue;
                FunctionRecord *callee = &c->functions[ir->operands.scalar.binding];
                if ((callee->flags & FUNCTION_REACHABLE) == 0u) { callee->flags |= FUNCTION_REACHABLE; changed = true; }
            }
        }
    }
    return true;
}
