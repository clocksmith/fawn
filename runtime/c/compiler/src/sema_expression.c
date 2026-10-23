/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "sema.h"
#include <string.h>
bool sema_append(Source *s, IrInstruction instruction, IrValue *out) {
    CompilerWorkspace *c = s->compiler;
    if (c->instruction_count == c->limits.instructions)
        return source_pool_error(s, DOE_COMPILER_POOL_IR, (size_t)c->instruction_count+1u, instruction.source_offset, "typed instruction capacity exceeded");
    if (out != NULL) {
        instruction.result = c->instruction_count+1u;
        *out = (IrValue){instruction.result, instruction.type, instruction.source_offset};
    }
    c->instructions[c->instruction_count++] = instruction;
    return true;
}
bool sema_emit(Source *s, bool emit, IrInstruction instruction, IrValue *out) {
    if (emit) return sema_append(s, instruction, out);
    if (out != NULL) *out = (IrValue){.type = instruction.type, .source_offset = instruction.source_offset};
    return true;
}
bool sema_ensure_value(Source *s, ExpressionValue *v, bool emit) {
    if (!emit || v->value.id != 0u) return true;
    if (!v->known || type_abstract(s->compiler, v->constant.type))
        return source_error(s, DOE_COMPILER_INPUT_ERROR, v->value.source_offset, "unmaterialized value reached target lowering");
    const IrInstruction i = {.opcode = IR_CONSTANT, .type = v->constant.type, .source_offset = v->value.source_offset, .operands.scalar = {.left = (v->constant.flags&CONSTANT_AGGREGATE) != 0u?1u:0u, .binding = (v->constant.flags&CONSTANT_AGGREGATE) != 0u?(uint32_t)v->constant.bits:0u, .literal = (uint32_t)v->constant.bits}
    };
    return sema_append(s, i, &v->value);
}
bool sema_materialize(Source *s, ExpressionValue *v, TypeId type) {
    if (v->constant.type == type) return true;
    TypeId common;
    if (!type_abstract(s->compiler, v->constant.type) || !sema_common_type(s, v->constant.type, type, v->value.source_offset, &common) || common != type)
        return source_error(s, DOE_COMPILER_SEMANTIC_ERROR, v->value.source_offset, "value has no implicit conversion to destination type");
    if (v->known && !constant_materialize(s, v->value.source_offset, v->constant, type, &v->constant)) return false;
    v->constant.type = type;
    v->value.type = type;
    return true;
}
static bool is_integer(TypeId t) {
    return t == IR_U32 || t == IR_I32 || t == IR_ABSTRACT_INT;
}
static bool shaped_type(Source *s, TypeId leaf, uint32_t n, TypeId *out) {
    if (n == 1u) {
        *out = leaf;
        return true;
    }
    return type_intern(s, TYPE_VECTOR, leaf, n, out);
}
bool sema_constant_lane(Source *s, ConstantValue value, uint32_t index, NumericValue *out) {
    ConstantValue v = value;
    if (value.type >= 7u && !constant_project(s, value, index, &v)) return false;
    *out = (NumericValue){v.bits, (IrType)v.type};
    return true;
}
bool sema_finish_lanes(Source *s, TypeId type, ConstantValue lanes[4], uint32_t count, ConstantValue *out) {
    if (count == 1u) {
        *out = lanes[0];
        return true;
    }
    return constant_composite(s, type, lanes, count, false, out);
}
static bool identity_value(Source *s,NameSpan name,const LocalScope *scope,ValueIdentity identity,bool emit,ExpressionValue *out) {
    CompilerWorkspace *c=s->compiler;
    *out=(ExpressionValue){.value.source_offset=name.offset};
    switch (identity.kind) {
        case IDENTITY_LOCAL: {
            const LocalBinding *local=&scope->values[identity.index];
            if (!sema_local_use(s,scope,local,name.offset)) return false;
            out->constant=local->constant_value; out->constant.type=local->type;
            out->value=(IrValue){local->value,local->type,name.offset};
            out->runtime=(local->flags&LOCAL_CONSTANT)==0u; out->known=!out->runtime;
            if ((local->flags&LOCAL_MUTABLE)!=0u)
                return sema_emit(s,emit,(IrInstruction){.opcode=IR_LOCAL_LOAD,.type=local->type,.source_offset=name.offset,
                    .operands.scalar.binding=local->local_id},&out->value);
            return true;
        }
        case IDENTITY_FORMAL:
            out->runtime=true; out->constant.type=c->formals[identity.index].type.resolved.type;
            out->value=(IrValue){c->limits.instructions+1u+identity.index,out->constant.type,name.offset}; return true;
        case IDENTITY_ALIAS:
            out->runtime=true; out->constant.type=IR_U32;
            out->value=(IrValue){c->functions[c->current_function].value_base,IR_U32,name.offset}; return true;
        case IDENTITY_INPUT: {
            TypeId type=IR_U32;
            if (!input_scalar(c->entry_inputs[identity.index].kind) && !type_intern(s,TYPE_VECTOR,IR_U32,3u,&type)) return false;
            out->runtime=true; out->constant.type=type;
            return sema_emit(s,emit,(IrInstruction){.opcode=IR_INPUT,.type=type,.source_offset=name.offset,
                .operands.scalar={.binding=identity.index,.literal=type==IR_U32?0u:NO_ID}},&out->value);
        }
        case IDENTITY_CONSTANT: {
            const ConstantSyntax *d=&c->constant_decls[identity.index];
            if ((d->state&3u)!=2u) break;
            out->known=true; out->constant=(ConstantValue){d->type,(d->state&16u)!=0u?CONSTANT_AGGREGATE:0u,d->value.bits};
            out->value.type=d->type; return true;
        }
        case IDENTITY_RESOURCE:
        case IDENTITY_WORKGROUP: {
            const TypeId type=identity.kind==IDENTITY_RESOURCE?c->resources[identity.index].type:c->workgroups[identity.index].type;
            if (identity.kind == IDENTITY_RESOURCE && type_handle(c, type)) {
                out->runtime = true; out->constant.type = type;
                return sema_emit(s, emit, (IrInstruction){.opcode = IR_HANDLE, .type = type,
                    .source_offset = name.offset, .operands.scalar.binding = identity.index}, &out->value);
            }
            if (!type_constructible(c,type)) return source_error(s,DOE_COMPILER_SEMANTIC_ERROR,name.offset,"runtime-sized resource is not a value");
            out->runtime=true; out->constant.type=type;
            return sema_emit(s,emit,(IrInstruction){.opcode=IR_ACCESS_LOAD,.type=type,.source_offset=name.offset,
                .operands.access={.root=identity.index,.path=NO_ID,.category=identity.kind==IDENTITY_RESOURCE?ROOT_RESOURCE:ROOT_WORKGROUP}},&out->value);
        }
        case IDENTITY_NONE: break;
    }
    return source_error(s,DOE_COMPILER_SEMANTIC_ERROR,name.offset,"name does not resolve to an available value");
}
static bool capture_identity(Source *s, SourceReference *slot, ValueIdentity identity, uint32_t offset) {
    SourceReferenceKind kind=SOURCE_NONE;
    if (identity.kind==IDENTITY_RESOURCE) kind=SOURCE_RESOURCE;
    else if (identity.kind==IDENTITY_WORKGROUP) kind=SOURCE_WORKGROUP;
    else if (identity.kind==IDENTITY_INPUT) kind=SOURCE_INPUT;
    else if (identity.kind==IDENTITY_ALIAS) {
        kind=SOURCE_INPUT;
        identity.index=sema_find_parameter(s,s->compiler->alias_source);
    }
    return source_reference_record(s,slot,kind,kind==SOURCE_NONE?0u:identity.index,offset);
}
static bool name_value(Source *s,NameSpan name,const LocalScope *scope,bool emit,ExpressionValue *out,SourceReference *slot) {
    ValueIdentity identity;
    return sema_value_identity(s,scope,name,NULL,&identity) && capture_identity(s,slot,identity,name.offset) && identity_value(s,name,scope,identity,emit,out);
}
static bool root_reference(Source *s,NameSpan name,const LocalScope *scope,bool emit,ValueReference *ref,ExpressionValue *value,bool *reference,SourceReference *slot) {
    CompilerWorkspace *c=s->compiler; ValueIdentity identity;
    *reference=false;
    if (!sema_value_identity(s,scope,name,NULL,&identity) || !capture_identity(s,slot,identity,name.offset)) return false;
    if (identity.kind==IDENTITY_LOCAL) {
        const LocalBinding *local=&scope->values[identity.index];
        if ((local->flags&LOCAL_MUTABLE)!=0u) {
            if (!sema_local_use(s,scope,local,name.offset)) return false;
            *ref=(ValueReference){local->local_id,NO_ID,ROOT_LOCAL,local->type,true}; *reference=true; return true;
        }
    } else if (identity.kind==IDENTITY_RESOURCE) {
        const ResourceRecord *r=&c->resources[identity.index];
        *ref=(ValueReference){identity.index,NO_ID,ROOT_RESOURCE,r->type,r->access==DOE_COMPILER_READ_WRITE && !type_handle(c, r->type)}; *reference=true; return true;
    } else if (identity.kind==IDENTITY_WORKGROUP) {
        *ref=(ValueReference){identity.index,NO_ID,ROOT_WORKGROUP,c->workgroups[identity.index].type,true}; *reference=true; return true;
    }
    return identity_value(s,name,scope,identity,emit,value);
}
bool sema_reference_load(Source *s, ValueReference ref, uint32_t offset, bool emit, ExpressionValue *out) {
    CompilerWorkspace *c = s->compiler;
    if (!type_constructible(c, ref.type)) return source_error(s, DOE_COMPILER_SEMANTIC_ERROR, offset, "runtime-sized reference cannot be loaded by value");
    *out = (ExpressionValue){
        .runtime = true, .constant.type = ref.type, .value = {.type = ref.type, .source_offset = offset}
    };
    IrInstruction instruction = {.opcode = ref.category == ROOT_VALUE?IR_VALUE_ACCESS:IR_ACCESS_LOAD, .type = ref.type, .source_offset = offset, .operands.access = {ref.root, ref.path, 0u, ref.category}
    };
    if (ref.category == ROOT_LOCAL && ref.path == NO_ID) {
        instruction.opcode = IR_LOCAL_LOAD;
        instruction.operands.scalar.left = 0u;
        instruction.operands.scalar.right = 0u;
        instruction.operands.scalar.binding = ref.root;
        instruction.operands.scalar.literal = 0u;
    }
    if (ref.category == ROOT_VALUE) {
        instruction.operands.access.value = NO_ID;
        if (emit && ref.path != NO_ID) {
            uint32_t first = ref.path;
            while (c->accesses[first].parent != NO_ID) first = c->accesses[first].parent;
            const TypeId root_type = c->accesses[first].input_type;
            if (root_type >= 7u && c->types[root_type-7u].kind != TYPE_VECTOR) {
                if ((uint64_t)c->local_count >= (uint64_t)c->limits.statements+1u+c->limits.accesses)
                    return source_pool_error(s, DOE_COMPILER_POOL_ACCESS_STEPS, (size_t)c->access_count+1u, offset, "generated local capacity exceeded");
                instruction.operands.access.value = c->local_count;
                c->locals[c->local_count++] = (IrLocal){root_type, 0u};
            }
        }
    }
    return sema_emit(s, emit, instruction, &out->value);
}
static bool postfix_value(Source *, const Expression *, const LocalScope *, bool, bool, ValueReference *, ExpressionValue *, bool *, SourceReference *);
static bool access_base(Source *s, const Expression *node, const LocalScope *scope, bool evaluate, bool emit, ValueReference *ref, ExpressionValue *value, bool *reference, SourceReference *slot) {
    CompilerWorkspace *c = s->compiler;
    if (node->left == NO_ID) return root_reference(s, node->kind == EXPRESSION_MEMBER?node->value.member.object:node->value.reference.storage, scope, emit, ref, value, reference, slot);
    const Expression *base = &c->expressions[node->left];
    if (base->kind == EXPRESSION_MEMBER || base->kind == EXPRESSION_LOAD) return postfix_value(s, base, scope, evaluate, emit, ref, value, reference, &c->expression_references[node->left]);
    if (base->kind == EXPRESSION_NAME) return root_reference(s, base->value.name, scope, emit, ref, value, reference, &c->expression_references[node->left]);
    *reference = false;
    return sema_expression(s, node->left, scope, evaluate, emit, value);
}
static bool swizzle(Source *s, NameSpan name, uint32_t n, uint32_t *mask) {
    if (name.length == 0u || name.length>4u) return source_error(s, DOE_COMPILER_SEMANTIC_ERROR, name.offset, "swizzle must have one through four components");
    const char *const xyzw = "xyzw", *const rgba = "rgba";
    const char *alphabet = NULL;
    *mask = 0u;
    for (uint32_t i = 0u;i<name.length;++i) {
        const char c = s->source[name.offset+i];
        const char *found = strchr(xyzw, c);
        const char *current = xyzw;
        if (found == NULL) {
            found = strchr(rgba, c);
            current = rgba;
        }
        if (found == NULL || (alphabet != NULL && alphabet != current) || (uint32_t)(found-current) >= n)
            return source_error(s, DOE_COMPILER_SEMANTIC_ERROR, name.offset, "swizzle alphabet or component is invalid");
        alphabet = current;
        *mask|=(uint32_t)(found-current)<<(2u*i);
    }
    return true;
}
static bool postfix_value(Source *s, const Expression *node, const LocalScope *scope, bool evaluate, bool emit, ValueReference *ref, ExpressionValue *out, bool *reference, SourceReference *slot) {
    CompilerWorkspace *c = s->compiler;
    ExpressionValue base = {0};
    ValueReference r = {0};
    bool is_ref = false;
/* Preserve the old one-node/one-IR builtin component route through the
 * general typed input owner, without materializing an unused vector. */
    if (node->kind == EXPRESSION_MEMBER && node->left == NO_ID && node->value.member.member.length == 1u && sema_find_local(s, scope, node->value.member.object) == NULL && sema_find_formal(s, node->value.member.object) == NO_ID) {
        const uint32_t p = sema_find_parameter(s, node->value.member.object);
        if (p != NO_ID && !input_scalar(c->entry_inputs[p].kind)) {
            uint32_t mask;
            if (!source_reference_record(s,slot,SOURCE_INPUT,p,node->source_offset)) return false;
            if (!swizzle(s, node->value.member.member, 3u, &mask)) return false;
            *reference = false;
            *out = (ExpressionValue){.runtime = true, .constant.type = IR_U32};
            return sema_emit(s, emit, (IrInstruction){.opcode = IR_INPUT, .type = IR_U32, .source_offset = node->source_offset, .operands.scalar = {.binding = p, .literal = mask}}, &out->value);
        }
    }
    if (!access_base(s, node, scope, evaluate, emit, &r, &base, &is_ref, slot)) return false;
    TypeId input = is_ref?r.type:base.constant.type, output = IR_VOID;
    uint32_t kind = ACCESS_MEMBER, index = 0u;
    if (node->kind == EXPRESSION_MEMBER) {
        const NameSpan member = node->value.member.member;
        if (input<7u) return source_error(s, DOE_COMPILER_SEMANTIC_ERROR, member.offset, "scalar value has no member");
        const TypeRecord *type = &c->types[input-7u];
        if (type->kind == TYPE_STRUCT) {
            for (;index<type->count;++index) if (source_spans_equal(s, member, c->members[type->element+index].name)) break;
            if (index == type->count) return source_error(s, DOE_COMPILER_SEMANTIC_ERROR, member.offset, "unknown struct member");
            output = c->members[type->element+index].type;
        }
        else if (type->kind == TYPE_VECTOR) {
            uint32_t mask;
            if (!swizzle(s, member, type->count, &mask)) return false;
            output = type->element;
            index = mask;
            kind = ACCESS_COMPONENT;
            if (member.length>1u) {
                if (!type_intern(s, TYPE_VECTOR, output, member.length, &output)) return false;
                if (is_ref && !sema_reference_load(s, r, node->source_offset, emit, &base)) return false;
                *reference = false;
                return sema_swizzle_value(s, node, &base, output, mask, evaluate, emit, out);
            }
        }
        else return source_error(s, DOE_COMPILER_SEMANTIC_ERROR, member.offset, "array value has no named member");
    }
    else {
        if (input<7u || (c->types[input-7u].kind != TYPE_ARRAY && c->types[input-7u].kind != TYPE_RUNTIME_ARRAY && c->types[input-7u].kind != TYPE_VECTOR))
            return source_error(s, DOE_COMPILER_SEMANTIC_ERROR, node->source_offset, "index requires vector or array");
        ExpressionValue ix;
        if (!sema_expression(s, node->value.reference.index, scope, evaluate, emit, &ix) || (ix.constant.type == IR_ABSTRACT_INT && !sema_materialize(s, &ix, IR_I32))) return false;
        if (ix.constant.type != IR_I32 && ix.constant.type != IR_U32) return source_error(s, DOE_COMPILER_SEMANTIC_ERROR, ix.value.source_offset, "index requires i32 or u32");
        if (!ix.runtime && !ix.known) {
            if (!sema_expression(s, node->value.reference.index, scope, true, false, &ix) || (ix.constant.type == IR_ABSTRACT_INT && !sema_materialize(s, &ix, IR_I32))) return false;
        }
        const TypeRecord *t = &c->types[input-7u];
        output = t->element;
        if (!ix.runtime && ix.known) {
            if ((ix.constant.type == IR_I32 && (ix.constant.bits&UINT64_C(0x80000000)) != 0u) || (t->kind != TYPE_RUNTIME_ARRAY && ix.constant.bits >= t->count))
                return source_error(s, DOE_COMPILER_SEMANTIC_ERROR, ix.value.source_offset, "constant index is out of bounds");
            if (!is_ref && !base.runtime) {
                *reference = false;
                *out = (ExpressionValue){
                    .known = evaluate && base.known, .constant.type = output, .value = {.type = output, .source_offset = node->source_offset}
                };
                return !out->known || constant_project(s, base.constant, (uint32_t)ix.constant.bits, &out->constant);
            }
            ix.constant.type = IR_U32;
            ix.value.type = IR_U32;
        }
        if (!is_ref && type_abstract(c, input)) {
            TypeId concrete;
            if (!sema_default_type(s, input, node->source_offset, &concrete) || !sema_materialize(s, &base, concrete)) return false;
            input = concrete;
            output = c->types[input-7u].element;
        }
        if (!sema_ensure_value(s, &ix, emit)) return false;
        kind = ix.constant.type == IR_I32?ACCESS_INDEX_I32:ACCESS_INDEX_U32;
        index = ix.value.id;
    }
    if (!is_ref && !base.runtime && node->kind == EXPRESSION_MEMBER) {
        *reference = false;
        *out = (ExpressionValue){
            .known = evaluate && base.known, .constant.type = output, .value = {.type = output, .source_offset = node->source_offset}
        };
        return !out->known || constant_project(s, base.constant, index, &out->constant);
    }
    if (!is_ref) {
        if (!sema_ensure_value(s, &base, emit)) return false;
        r = (ValueReference){base.value.id, NO_ID, ROOT_VALUE, input, false};
    }
    if (emit) {
        if (c->access_count == c->limits.accesses) return source_pool_error(s, DOE_COMPILER_POOL_ACCESS_STEPS, (size_t)c->access_count+1u, node->source_offset, "access step capacity exceeded");
        c->accesses[c->access_count] = (AccessStep){r.path, node->source_offset, input, output, kind, index};
        r.path = c->access_count++;
    }
    r.type = output;
    *ref = r;
    *reference = true;
    return true;
}
bool sema_assignment_reference(Source *s, const Assignment *a, const LocalScope *scope, ValueReference *out, SourceReference *slot) {
    if (a->kind == TARGET_ROOT) {
        ExpressionValue ignored;
        bool reference;
        if (!root_reference(s, a->root, scope, true, out, &ignored, &reference, slot)) return false;
        if (!reference || !out->writable) return source_error(s, DOE_COMPILER_SEMANTIC_ERROR, a->root.offset, "assignment target is not writable");
        return true;
    }
    Expression e = {.source_offset = a->root.offset, .left = a->path};
    if (a->kind == TARGET_INDEX) {
        e.kind = EXPRESSION_LOAD;
        e.value.reference.storage = a->root;
        e.value.reference.index = a->index;
    }
    else {
        e.kind = EXPRESSION_MEMBER;
        e.value.member.object = a->root;
        e.value.member.member = source_name_at(s, a->index);
    }
    ExpressionValue ignored;
    bool reference;
    if (!postfix_value(s, &e, scope, true, true, out, &ignored, &reference, slot)) return false;
    if (!reference || !out->writable) return source_error(s, DOE_COMPILER_SEMANTIC_ERROR, a->root.offset, "assignment target is not writable or is a multi-component swizzle");
    const SourceReferenceKind kind=out->category==ROOT_RESOURCE?SOURCE_RESOURCE:
        out->category==ROOT_WORKGROUP?SOURCE_WORKGROUP:SOURCE_NONE;
    return source_reference_record(s,slot,kind,kind==SOURCE_NONE?0u:out->root,a->root.offset);
}
static IrOpcode binary_opcode(ExpressionKind op) {
    switch (op) {
        case EXPRESSION_ADD:return IR_ADD;
        case EXPRESSION_SUBTRACT:return IR_SUBTRACT;
        case EXPRESSION_MULTIPLY:return IR_MULTIPLY;
        case EXPRESSION_DIVIDE:return IR_DIVIDE;
        case EXPRESSION_REMAINDER:return IR_REMAINDER;
        case EXPRESSION_COMPARE:return IR_COMPARE;
        case EXPRESSION_BIT_AND:return IR_BIT_AND;
        case EXPRESSION_BIT_OR:return IR_BIT_OR;
        case EXPRESSION_BIT_XOR:return IR_BIT_XOR;
        case EXPRESSION_SHIFT_LEFT:return IR_SHIFT_LEFT;
        case EXPRESSION_SHIFT_RIGHT:return IR_SHIFT_RIGHT;
        case EXPRESSION_LOAD: case EXPRESSION_INTEGER: case EXPRESSION_DECIMAL:
        case EXPRESSION_NAME: case EXPRESSION_MEMBER: case EXPRESSION_MAX:
        case EXPRESSION_ABSTRACT_INTEGER: case EXPRESSION_BOOLEAN: case EXPRESSION_NOT:
        case EXPRESSION_AND: case EXPRESSION_OR: case EXPRESSION_LITERAL:
        case EXPRESSION_NEGATE: case EXPRESSION_COMPLEMENT: case EXPRESSION_CONSTRUCT:
        case EXPRESSION_BITCAST: case EXPRESSION_CALL: return IR_CONSTANT;
    }
    return IR_CONSTANT;
}
static bool binary_types(Source *s, ExpressionKind op, Comparison cmp, ExpressionValue *a, ExpressionValue *b, uint32_t offset, TypeId *result) {
    CompilerWorkspace *c = s->compiler;
    TypeId la = type_leaf(c, a->constant.type), lb = type_leaf(c, b->constant.type), leaf;
    const uint32_t wa = type_vector_width(c, a->constant.type), wb = type_vector_width(c, b->constant.type), n = wa>wb?wa:wb;
    const bool arithmetic = op == EXPRESSION_ADD || op == EXPRESSION_SUBTRACT || op == EXPRESSION_MULTIPLY || op == EXPRESSION_DIVIDE || op == EXPRESSION_REMAINDER;
    const bool shift = op == EXPRESSION_SHIFT_LEFT || op == EXPRESSION_SHIFT_RIGHT;
    if (la == IR_VOID || lb == IR_VOID || (wa != wb && (!arithmetic || (wa != 1u && wb != 1u))))
        return source_error(s, DOE_COMPILER_SEMANTIC_ERROR, offset, "operator operand shapes do not match an admitted overload");
    if (shift) {
        if (wa != wb || !is_integer(la)) return source_error(s, DOE_COMPILER_SEMANTIC_ERROR, offset, "shift operands require matching integer shapes");
        if (b->runtime && la == IR_ABSTRACT_INT) la = IR_I32;
        TypeId at, bt;
        if (!shaped_type(s, la, wa, &at) || !shaped_type(s, IR_U32, wb, &bt) || !sema_materialize(s, a, at) || !sema_materialize(s, b, bt)) return false;
        *result = at;
        return true;
    }
    if (!sema_common_type(s, la, lb, offset, &leaf)) return false;
    const bool bit = op == EXPRESSION_BIT_AND || op == EXPRESSION_BIT_OR || op == EXPRESSION_BIT_XOR;
    if ((bit && !is_integer(leaf) && !(leaf == IR_BOOL && op != EXPRESSION_BIT_XOR)) || (leaf == IR_BOOL && !bit && !(op == EXPRESSION_COMPARE && (cmp == COMPARE_EQUAL || cmp == COMPARE_NOT_EQUAL))))
        return source_error(s, DOE_COMPILER_SEMANTIC_ERROR, offset, "operator is not defined for these element types");
    TypeId at, bt;
    if (!shaped_type(s, leaf, wa, &at) || !shaped_type(s, leaf, wb, &bt) || !sema_materialize(s, a, at) || !sema_materialize(s, b, bt)) return false;
    return shaped_type(s, op == EXPRESSION_COMPARE?IR_BOOL:leaf, n, result);
}
bool sema_binary(Source *s, ExpressionKind op, Comparison cmp, ExpressionValue *a, ExpressionValue *b, uint32_t offset, bool evaluate, bool emit, ExpressionValue *out) {
    TypeId type;
    if (!binary_types(s, op, cmp, a, b, offset, &type)) return false;
    const TypeId leaf = type_leaf(s->compiler, a->constant.type);
    const uint32_t n = type_vector_width(s->compiler, type), wa = type_vector_width(s->compiler, a->constant.type), wb = type_vector_width(s->compiler, b->constant.type);
    const bool shift = op == EXPRESSION_SHIFT_LEFT || op == EXPRESSION_SHIFT_RIGHT;
    if (!b->runtime && b->known && (shift || ((op == EXPRESSION_DIVIDE || op == EXPRESSION_REMAINDER) && is_integer(leaf)))) {
        for (uint32_t i = 0u;i<wb;++i) {
            NumericValue rhs;
            if (!sema_constant_lane(s, b->constant, i, &rhs)) return false;
            if ((!shift && rhs.bits == 0u) || (shift && leaf != IR_ABSTRACT_INT && rhs.bits >= 32u))
                return source_error(s, DOE_COMPILER_SEMANTIC_ERROR, offset, shift?"concrete shift count const expression must be below 32":"integer divisor is a zero const expression");
        }
    }
    *out = (ExpressionValue){
        .runtime = a->runtime || b->runtime, .known = evaluate && a->known && b->known, .constant.type = type, .value = {.type = type, .source_offset = offset}
    };
    if (!out->runtime) {
        if (!out->known) return true;
        ConstantValue values[4];
        for (uint32_t i = 0u;i<n;++i) {
            NumericValue x, y, r;
            if (!sema_constant_lane(s, a->constant, wa == 1u?0u:i, &x) || !sema_constant_lane(s, b->constant, wb == 1u?0u:i, &y) || !numeric_binary(s, offset, op, cmp, x, y, &r)) return false;
            values[i] = (ConstantValue){.type = (TypeId)r.type, .bits = r.bits};
        }
        return sema_finish_lanes(s, type, values, n, &out->constant);
    }
    out->known = false;
    if (!sema_ensure_value(s, a, emit) || !sema_ensure_value(s, b, emit)) return false;
    return sema_emit(s, emit, (IrInstruction){.opcode = binary_opcode(op), .type = type, .source_offset = offset, .operands.scalar = {a->value.id, b->value.id, a->constant.type, op == EXPRESSION_COMPARE?(uint32_t)cmp:b->constant.type}}, &out->value);
}

bool sema_call_statement(Source *s, uint32_t id, const LocalScope *scope) {
    ExpressionValue ignored;
    return sema_call_value(s, id, scope, true, true, true, &ignored);
}
bool sema_expression(Source *s, uint32_t id, const LocalScope *scope, bool evaluate, bool emit, ExpressionValue *out) {
    CompilerWorkspace *c = s->compiler;
    if (id >= c->expression_count) return source_error(s, DOE_COMPILER_INPUT_ERROR, s->length, "invalid expression identity");
    const Expression *e = &c->expressions[id];
    *out = (ExpressionValue){.value.source_offset = e->source_offset};
    if (e->kind == EXPRESSION_CALL) return sema_call_value(s, id, scope, evaluate, emit, false, out);
    if (e->kind == EXPRESSION_LITERAL || e->kind == EXPRESSION_BOOLEAN) {
        NumericValue n;
        if (e->kind == EXPRESSION_BOOLEAN) n = (NumericValue){e->value.integer, IR_BOOL};
        else if (!numeric_parse_literal(s, e->value.name, &n)) return false;
        out->known = true;
        out->constant = (ConstantValue){.type = (TypeId)n.type, .bits = n.bits};
        out->value.type = (TypeId)n.type;
        return true;
    }
    if (e->kind == EXPRESSION_NAME) return name_value(s, e->value.name, scope, emit, out, &c->expression_references[id]);
    if (e->kind == EXPRESSION_MEMBER || e->kind == EXPRESSION_LOAD) {
        ValueReference ref;
        bool reference;
        if (!postfix_value(s, e, scope, evaluate, emit, &ref, out, &reference, &c->expression_references[id])) return false;
        return !reference || sema_reference_load(s, ref, e->source_offset, emit, out);
    }
    ExpressionValue a, b;
    if (!sema_expression(s, e->left, scope, evaluate, emit, &a)) return false;
    const bool unary = e->kind == EXPRESSION_NOT || e->kind == EXPRESSION_NEGATE || e->kind == EXPRESSION_COMPLEMENT;
    if (unary) return sema_unary_value(s, e, &a, evaluate, emit, out);
    const bool logical = e->kind == EXPRESSION_AND || e->kind == EXPRESSION_OR;
    bool skip = false;
    if (logical) {
        if (a.constant.type != IR_BOOL) return source_error(s, DOE_COMPILER_SEMANTIC_ERROR, e->source_offset, "logical operands require scalar bool");
        skip = !a.runtime && a.known && (a.constant.bits != 0u) == (e->kind == EXPRESSION_OR);
        if (a.runtime && emit && (!sema_ensure_value(s, &a, true) || !sema_append(s, (IrInstruction){.opcode = IR_SHORT_BEGIN, .source_offset = e->source_offset, .operands.scalar = {.left = a.value.id, .literal = e->kind == EXPRESSION_OR}}, NULL))) return false;
    }
/* Mutable LHS reads have already emitted their snapshot before RHS calls. */
    if (!sema_expression(s, e->right, scope, evaluate && !skip, emit && !skip, &b)) return false;
    if (logical) {
        if (b.constant.type != IR_BOOL) return source_error(s, DOE_COMPILER_SEMANTIC_ERROR, e->source_offset, "logical operands require scalar bool");
        *out = (ExpressionValue){
            .runtime = a.runtime || b.runtime, .constant.type = IR_BOOL, .value = {.type = IR_BOOL, .source_offset = e->source_offset}
        };
        if (skip) {
            out->known = true;
            out->constant.bits = a.constant.bits;
            return true;
        }
        if (!a.runtime) {
            *out = b;
            out->value.source_offset = e->source_offset;
            return true;
        }
        if (!sema_ensure_value(s, &b, emit)) return false;
        return sema_emit(s, emit, (IrInstruction){.opcode = IR_SHORT_END, .type = IR_BOOL, .source_offset = e->source_offset, .operands.scalar = {.left = a.value.id, .right = b.value.id}}, &out->value);
    }
    const bool mandatory = e->kind == EXPRESSION_SHIFT_LEFT || e->kind == EXPRESSION_SHIFT_RIGHT || ((e->kind == EXPRESSION_DIVIDE || e->kind == EXPRESSION_REMAINDER) && is_integer(type_leaf(c, a.constant.type)));
    if (mandatory && !b.runtime && !b.known && !sema_expression(s, e->right, scope, true, false, &b)) return false;
    return sema_binary(s, e->kind, e->value.comparison, &a, &b, e->source_offset, evaluate, emit, out);
}
