"""Deterministic allowed numerical target model, separate from bound acceptance.

Consumes decoded SPIR-V operand values/types. Exact rational linear terms use
one final rounding, an allowed more-accurate fused model; it does not call the
const chosen-graph evaluator or WGSL interval acceptance routine.
"""
from __future__ import annotations
from fractions import Fraction
from exact_numeric_m8 import finite_value, round_bits
from interval_math_m9 import reference


def numeric_instruction(module: object, result_type: int, opcode: int, operands: list[object]) -> object:
    from target_m12 import Aggregate, Invalid
    arrays=[]
    for operand in operands:
        if isinstance(operand, Aggregate):
            child,width=module.children(operand.typ)
            if module.types[child]!=(22,(32,)):
                raise Invalid('numeric target operand vector element type')
            arrays.append([module.extract(operand,(i,)) for i in range(width)])
        else:
            arrays.append([operand])
    width=len(arrays[0])
    if any(len(x)!=width for x in arrays):
        raise Invalid('numeric target operand width mismatch')
    exact=[[finite_value(x) for x in a] for a in arrays]
    if opcode in {21,27,31}:
        result=[reference({21:'tanh',27:'exp',31:'sqrt'}[opcode],x)['nearest_bits'] for x in arrays[0]]
    elif opcode==148:
        if width==1 or len(arrays)!=2:
            raise Invalid('OpDot requires matching float vectors')
        result=[round_bits(sum(x*y for x,y in zip(*exact,strict=True)))]
    elif opcode==68:
        if width!=3 or len(arrays)!=2:
            raise Invalid('Cross requires two vec3 operands')
        x,y=exact
        result=[round_bits(x[i]*y[j]-x[j]*y[i]) for i,j in [(1,2),(2,0),(0,1)]]
    else:
        v=exact[0] if opcode!=67 else [x-y for x,y in zip(*exact,strict=True)]
        squared=round_bits(sum(x*x for x in v))
        length=reference('sqrt',squared)['nearest_bits']
        if opcode in {66,67}:
            result=[length]
        elif opcode==69 and width>1:
            denominator=finite_value(length)
            result=[round_bits(x/denominator) for x in v]
        else:
            raise Invalid('numeric target operation or shape')
    if len(result)==1:
        if module.types[result_type]!=(22,(32,)):
            raise Invalid('shape-reducing numeric result must be scalar f32')
        return result[0]
    if module.types[result_type][0]!=23:
        raise Invalid('numeric vector result has scalar TypeId')
    child,count=module.children(result_type)
    if count!=len(result) or module.types[child]!=(22,(32,)):
        raise Invalid('numeric vector result TypeId mismatch')
    return Aggregate(result_type,tuple(result))
