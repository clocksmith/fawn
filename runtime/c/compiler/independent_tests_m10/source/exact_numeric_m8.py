"""Independent exact-rational IEEE nearest-even reference for scalar M8.

No compiler or host binary floating arithmetic calculates finite results.
"""
from __future__ import annotations

from fractions import Fraction
import re


def signed(value: int, width: int = 32) -> int:
    value &= (1 << width) - 1
    return value - (1 << width) if value >> (width - 1) else value


def trunc_div(left: int, right: int) -> int:
    if not right:
        raise ZeroDivisionError('integer division has zero divisor')
    return (abs(left) // abs(right)) * (-1 if (left < 0) != (right < 0) else 1)


def parts(width: int) -> tuple[int, int, int]:
    if width == 32:
        return 23, 8, 127
    if width == 64:
        return 52, 11, 1023
    raise ValueError('only binary32/binary64 are in the independent oracle')


def finite_value(bits: int, width: int = 32) -> Fraction:
    mantissa, exponent_bits, bias = parts(width)
    exponent = (bits >> mantissa) & ((1 << exponent_bits) - 1)
    if exponent == (1 << exponent_bits) - 1:
        raise ValueError('nonfinite payload has no finite rational value')
    significand = bits & ((1 << mantissa) - 1)
    if exponent:
        significand |= 1 << mantissa
    shift = (exponent or 1) - bias - mantissa
    value = Fraction(significand << shift) if shift >= 0 else Fraction(significand, 1 << -shift)
    return -value if bits >> (width - 1) else value


def rounded_integer(numerator: int, denominator: int) -> int:
    quotient, remainder = divmod(numerator, denominator)
    return quotient + int(2 * remainder > denominator or
                          (2 * remainder == denominator and quotient & 1))


def round_bits(value: Fraction | int, width: int = 32,
               negative_zero: bool = False) -> int:
    value = Fraction(value)
    mantissa, exponent_bits, bias = parts(width)
    sign = int(value < 0 or (not value and negative_zero)) << (width - 1)
    value = abs(value)
    if not value:
        return sign
    numerator, denominator = value.numerator, value.denominator
    exponent = numerator.bit_length() - denominator.bit_length()
    if exponent >= 0:
        if numerator < denominator << exponent:
            exponent -= 1
    elif numerator << -exponent < denominator:
        exponent -= 1
    scale = mantissa - max(exponent, 1 - bias)
    significand = (rounded_integer(numerator << scale, denominator) if scale >= 0
                   else rounded_integer(numerator, denominator << -scale))
    if exponent < 1 - bias:
        return sign | significand
    if significand >= 1 << (mantissa + 1):
        significand >>= 1
        exponent += 1
    if exponent > bias:
        return sign | (((1 << exponent_bits) - 1) << mantissa)
    return sign | ((exponent + bias) << mantissa) | (significand - (1 << mantissa))


def literal_fraction(text: str, truncate: bool = True) -> Fraction:
    """Convert already-valid floating literal spelling, without host strtod.

    The grammar checker is deliberately not duplicated here. Long significands
    discard low digits before conversion as selected by the frozen contract.
    """
    text = text.removesuffix('f')
    negative = text.startswith('-')
    text = text.lstrip('+-')
    if text.lower().startswith('0x'):
        pieces = re.split('[pP]', text[2:])
        significand, exponent = pieces[0], int(pieces[1]) if len(pieces) > 1 else 0
        whole, _, fraction = significand.partition('.')
        digits = (whole + fraction).lstrip('0') or '0'
        power = exponent - 4 * len(fraction)
        if truncate and len(digits) > 16:
            power += 4 * (len(digits) - 16)
            digits = digits[:16]
        value = Fraction(int(digits, 16))
        value = value * (1 << power) if power >= 0 else value / (1 << -power)
    else:
        pieces = re.split('[eE]', text)
        significand, exponent = pieces[0], int(pieces[1]) if len(pieces) > 1 else 0
        whole, _, fraction = significand.partition('.')
        digits = (whole + fraction).lstrip('0') or '0'
        power = exponent - len(fraction)
        if truncate and len(digits) > 20:
            power += len(digits) - 20
            digits = digits[:20]
        value = Fraction(int(digits))
        value = value * 10 ** power if power >= 0 else value / 10 ** -power
    return -value if negative else value


def literal_bits(text: str, width: int = 32) -> int:
    return round_bits(literal_fraction(text), width, text.startswith('-'))


def fp_operation(op: str, left: int, right: int,
                 width: int = 32) -> int:
    a, b = finite_value(left, width), finite_value(right, width)
    sign = 1 << (width - 1)
    if op == '+':
        result = a + b
        negative_zero = bool(left & right & sign)
    elif op == '-':
        result = a - b
        negative_zero = bool(left & sign and not right & sign)
    elif op == '*':
        result = a * b
        negative_zero = bool((left ^ right) & sign)
    elif op == '/':
        if not b:
            raise ZeroDivisionError('finite oracle division by zero')
        result = a / b
        negative_zero = bool((left ^ right) & sign)
    elif op == '%':
        if not b:
            raise ZeroDivisionError('finite oracle remainder by zero')
        ratio = a / b
        quotient = trunc_div(ratio.numerator, ratio.denominator)
        result = a - b * quotient
        negative_zero = bool(left & sign)
    elif op == 'max':
        return left if a >= b else right
    else:
        raise ValueError('unsupported exact floating operation ' + op)
    return round_bits(result, width, negative_zero and not result)


def self_test() -> None:
    assert signed(0xffffffff) == -1
    assert trunc_div(-7, 3) == -2
    known = {'0.1': 0x3dcccccd, '16777217.0': 0x4b800000,
             '16777219.0': 0x4b800002, '-0.0': 0x80000000,
             '0x1p-149': 1, '0x1p-150': 0, '0x1.fffffep127': 0x7f7fffff}
    for text, expected in known.items():
        assert literal_bits(text) == expected, (text, hex(literal_bits(text)))
    assert round_bits(Fraction(1) + Fraction(1, 1 << 24)) == 0x3f800000
    assert round_bits(Fraction(1) + Fraction(3, 1 << 24)) == 0x3f800002
    assert literal_bits('9007199254740993.0', 64) == 0x4340000000000000
    assert fp_operation('%', literal_bits('-7.5'), literal_bits('2.0')) == 0xbfc00000
    assert fp_operation('-', 0x80000000, 0) == 0x80000000
    assert fp_operation('*', 0x80000000, 0x3f800000) == 0x80000000


if __name__ == '__main__':
    self_test()
    print('Exact integer/Fraction binary32/binary64 controls passed.')
