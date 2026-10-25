/* SPDX-License-Identifier: MIT */
#include "sema.h"

static bool constant_result(Source *s, const ExpressionValue *args, bool insert, TypeId type,
                             ConstantValue *out) {
    const uint32_t offset = (uint32_t)args[insert ? 2u : 1u].constant.bits;
    const uint32_t count = (uint32_t)args[insert ? 3u : 2u].constant.bits;
    const uint32_t mask = count == 32u ? UINT32_MAX : count == 0u ? 0u : (UINT32_C(1) << count) - 1u;
    const uint32_t width = type_vector_width(s->compiler, type);
    const TypeId leaf = type_leaf(s->compiler, type);
    ConstantValue lanes[4];
    for (uint32_t i = 0u; i < width; ++i) {
        NumericValue base, replacement;
        if (!sema_constant_lane(s, args[0].constant, i, &base)) return false;
        uint32_t value = (uint32_t)base.bits;
        if (insert) {
            if (!sema_constant_lane(s, args[1].constant, i, &replacement)) return false;
            if (count != 0u) value = (value & ~(mask << offset)) | (((uint32_t)replacement.bits & mask) << offset);
        } else if (count == 0u) value = 0u;
        else {
            value = (value >> offset) & mask;
            if (leaf == IR_I32) {
                const uint32_t sign = UINT32_C(1) << (count - 1u);
                value = (value ^ sign) - sign;
            }
        }
        lanes[i] = (ConstantValue){.type=leaf,.bits=value};
    }
    return sema_finish_lanes(s, type, lanes, width, out);
}
static bool literal(Source *s, uint32_t offset, uint32_t bits, IrValue *out) {
    return sema_append(s, (IrInstruction){.opcode=IR_CONSTANT,.type=IR_U32,.source_offset=offset,
        .operands.scalar.literal=bits}, out);
}
static bool binary(Source *s, uint32_t offset, IrOpcode opcode, IrValue a, IrValue b, IrValue *out) {
    return sema_append(s, (IrInstruction){.opcode=opcode,.type=opcode == IR_COMPARE ? IR_BOOL : a.type,
        .source_offset=offset,.operands.scalar={a.id,b.id,a.type,opcode == IR_COMPARE ? COMPARE_EQUAL : b.type}}, out);
}
static bool minimum(Source *s, uint32_t offset, IrValue a, IrValue b, IrValue *out) {
    return sema_append(s, (IrInstruction){.opcode=IR_BUILTIN,.type=IR_U32,.source_offset=offset,
        .operands.scalar={a.id,b.id,0u,BUILTIN_MIN}}, out);
}
static bool cast(Source *s, uint32_t offset, TypeId type, IrValue input, IrValue *out) {
    if (input.type == type) { *out = input; return true; }
    return sema_append(s, (IrInstruction){.opcode=IR_BITCAST,.type=type,.source_offset=offset,
        .operands.scalar={.left=input.id,.binding=input.type}}, out);
}
static bool lane(Source *s, uint32_t offset, IrValue input, uint32_t index, IrValue *out) {
    const TypeId leaf = type_leaf(s->compiler, input.type);
    IrValue value = input;
    if (input.type != leaf && !sema_append(s, (IrInstruction){.opcode=IR_SWIZZLE,.type=leaf,
        .source_offset=offset,.operands.scalar={input.id,1u,input.type,index}}, &value)) return false;
    return cast(s, offset, IR_U32, value, out);
}
/* All shifts use saved unsigned values and counts in [0,31]. Count32 uses an
 * all-ones mask; count0 produces a zero mask without ever shifting by32.
 * Runtime bounds clamp independently, including UINT32_MAX inputs. */
static bool runtime_result(Source *s, uint32_t offset, ExpressionValue *args, bool insert,
                            ExpressionValue *out) {
    IrValue zero, one, bits, last, ones, start, count, remaining, shift, mask, empty, sign;
    if (!literal(s,offset,0u,&zero) || !literal(s,offset,1u,&one) ||
        !literal(s,offset,32u,&bits) || !literal(s,offset,31u,&last) ||
        !literal(s,offset,UINT32_MAX,&ones) ||
        !minimum(s,offset,args[insert ? 2u : 1u].value,bits,&start) ||
        !binary(s,offset,IR_SUBTRACT,bits,start,&remaining) ||
        !minimum(s,offset,args[insert ? 3u : 2u].value,remaining,&count) ||
        !binary(s,offset,IR_SUBTRACT,bits,count,&remaining) ||
        !binary(s,offset,IR_BIT_AND,remaining,last,&shift) ||
        !binary(s,offset,IR_SHIFT_RIGHT,ones,shift,&mask) ||
        !binary(s,offset,IR_COMPARE,count,zero,&empty) ||
        !sema_append(s,(IrInstruction){.opcode=IR_SELECT,.type=IR_U32,.source_offset=offset,
            .operands.scalar={mask.id,zero.id,empty.id,0u}},&mask) ||
        !binary(s,offset,IR_BIT_AND,start,last,&start)) return false;
    const TypeId type = out->constant.type, leaf = type_leaf(s->compiler,type);
    if (!insert && leaf == IR_I32 &&
        (!binary(s,offset,IR_SUBTRACT,count,one,&shift) ||
         !binary(s,offset,IR_BIT_AND,shift,last,&shift) ||
         !binary(s,offset,IR_SHIFT_LEFT,one,shift,&sign))) return false;
    IrValue displaced, retained;
    if (insert && (!binary(s,offset,IR_SHIFT_LEFT,mask,start,&displaced) ||
        !binary(s,offset,IR_BIT_XOR,displaced,ones,&retained))) return false;
    const uint32_t width = type_vector_width(s->compiler,type);
    CompilerWorkspace *c = s->compiler;
    uint32_t first = 0u;
    if (width != 1u) {
        if (width > c->limits.operands - c->operand_count)
            return source_pool_error(s,DOE_COMPILER_POOL_IR_OPERANDS,(size_t)c->operand_count+width,
                offset,"bitfield result operand capacity exceeded");
        first = c->operand_count;
        c->operand_count += width;
    }
    for (uint32_t i = 0u; i < width; ++i) {
        IrValue value, replacement, kept;
        if (!lane(s,offset,args[0].value,i,&value)) return false;
        if (insert) {
            if (!lane(s,offset,args[1].value,i,&replacement) ||
                !binary(s,offset,IR_BIT_AND,replacement,mask,&replacement) ||
                !binary(s,offset,IR_SHIFT_LEFT,replacement,start,&replacement) ||
                !binary(s,offset,IR_BIT_AND,value,retained,&kept) ||
                !binary(s,offset,IR_BIT_OR,kept,replacement,&value)) return false;
        } else {
            if (!binary(s,offset,IR_SHIFT_RIGHT,value,start,&value) ||
                !binary(s,offset,IR_BIT_AND,value,mask,&value)) return false;
            if (leaf == IR_I32 && (!binary(s,offset,IR_BIT_XOR,value,sign,&value) ||
                !binary(s,offset,IR_SUBTRACT,value,sign,&value))) return false;
        }
        if (!cast(s,offset,leaf,value,&value)) return false;
        if (width == 1u) out->value = value;
        else c->typed_operands[first+i] = value.id;
    }
    return width == 1u || sema_append(s,(IrInstruction){.opcode=IR_COMPOSITE,.type=type,
        .source_offset=offset,.operands.composite={first,width,1u,0u}},&out->value);
}
bool sema_bitfield_finish(Source *s, const Expression *node, const NumericBuiltin *builtin,
                          ExpressionValue *args, bool emit, ExpressionValue *out) {
    const bool insert = builtin->kind == BUILTIN_INSERT_BITS;
    if (!out->runtime) return !out->known || constant_result(s,args,insert,out->constant.type,&out->constant);
    for (uint32_t i = 0u; i < builtin->arity; ++i)
        if (!sema_ensure_value(s,&args[i],emit)) return false;
    return !emit || runtime_result(s,node->source_offset,args,insert,out);
}
