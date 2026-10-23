"""Independent integer/Fraction binary32 nearest-even reference, without decimal floats."""
from __future__ import annotations
from fractions import Fraction


def rounded_integer(n: int, d: int) -> int:
    q, r = divmod(n, d)
    return q + int(2*r > d or (2*r == d and q % 2 == 1))


def round_binary32(value: Fraction) -> int:
    sign = 0x80000000 if value < 0 else 0
    value = abs(value)
    if not value:
        return sign
    n, d = value.numerator, value.denominator
    exponent = n.bit_length() - d.bit_length()
    if exponent >= 0:
        if n < d << exponent:
            exponent -= 1
    elif n << -exponent < d:
        exponent -= 1
    scale = 23 - max(exponent, -126)
    significand = rounded_integer(n << scale, d) if scale >= 0 else rounded_integer(n, d << -scale)
    if exponent < -126:
        return sign | significand
    if significand >= 1 << 24:
        significand >>= 1
        exponent += 1
    if exponent > 127:
        return sign | 0x7f800000
    return sign | ((exponent + 127) << 23) | (significand - (1 << 23))


def decimal_bits(text: str) -> int:
    whole, frac = text.removesuffix('f').split('.')
    return round_binary32(Fraction(int(whole+frac), 10**len(frac)))


def exact_value(bits: int) -> Fraction:
    exponent = (bits >> 23) & 255
    if exponent == 255:
        raise ValueError('non-finite outside new M4 numerical oracle')
    significand = (bits & 0x7fffff) | ((1 << 23) if exponent else 0)
    shift = (exponent - 150) if exponent else -149
    result = Fraction(significand << shift, 1) if shift >= 0 else Fraction(significand, 1 << -shift)
    return -result if bits & 0x80000000 else result


def operation(opcode: int, left: int, right: int) -> int:
    a, b = exact_value(left), exact_value(right)
    result = {129: lambda:a+b, 131:lambda:a-b, 133:lambda:a*b}[opcode]()
    if not result:
        # Exact zero from cancellation is +0 under RNE; multiplication keeps sign.
        if opcode == 133:
            return (left ^ right) & 0x80000000
        if opcode == 129 and left & right & 0x80000000:
            return 0x80000000
        if opcode == 131 and left & 0x80000000 and not right & 0x80000000:
            return 0x80000000
    return round_binary32(result)


def self_test() -> None:
    known={'0.1':0x3dcccccd, '16777217.0':0x4b800000, '16777219.0':0x4b800002,
           '16777216.9':0x4b800000, '16777217.1':0x4b800001, '0.0':0,
           '0.00000001':0x322bcc77, '99999999.9':0x4cbebc20}
    for text, want in known.items():
        assert decimal_bits(text) == want, (text, hex(decimal_bits(text)), hex(want))
    assert operation(133, 0x3f800001, 0x3f7ffffe) == 0x3f800000
    assert operation(131, 0x3f800000, 0x3f000000) == 0x3f000000
    assert operation(129, 0x3f800000, 0x33800000) == 0x3f800000
    assert operation(129, 0x3f800001, 0x33800000) == 0x3f800002
    assert round_binary32(Fraction(1, 1<<149)) == 1
    assert round_binary32(Fraction(1, 1<<150)) == 0

if __name__ == '__main__':
    self_test()
    print('Independent exact-rational oracle controls passed.')
