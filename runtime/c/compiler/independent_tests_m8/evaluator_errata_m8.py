"""Narrow reviewed evaluator corrections; sealed original inventory stays unchanged."""
from __future__ import annotations
from fractions import Fraction
from exact_numeric_m8 import literal_fraction,finite_value

OUTSIDE_FINITE={'literal_concrete_float_5','finite_just_below_overflow'}
REJECTION_CLASSES={
 'constructor_two_arguments':(2,4),
 'invalid_attribute_group_1':(3,4),
 'invalid_attribute_binding_1':(3,4),
 'invalid_attribute_workgroup_size_0':(3,4),
 'invalid_attribute_workgroup_size_1':(3,4),
 'invalid_attribute_workgroup_size_6':(3,4),
 'invalid_attribute_workgroup_size_7':(3,4),
}


def apply(positive: list,negative: list) -> tuple:
    # §3.5.2 literal conversion follows §15.7.6: outside exact finite range is
    # shader-creation error even when nearest-even would round back to max.
    assert literal_fraction('3.4028234663852886e38f')>finite_value(0x7f7fffff)
    assert literal_fraction('0x1.fffffe8p127')>finite_value(0x7f7fffff)
    removed=[(n,s,(4,)) for n,s,e in positive if n in OUTSIDE_FINITE]
    good=[(n,s,e) for n,s,e in positive if n not in OUTSIDE_FINITE]
    bad=[(n,s,REJECTION_CLASSES.get(n,status)) for n,s,status in negative]+removed
    return good,bad
