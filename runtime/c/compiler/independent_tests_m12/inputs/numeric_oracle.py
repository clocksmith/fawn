"""M12 independent numeric references, sealed before compiler implementation.

The accepted M8 integer/Fraction IEEE helper and M9 directed Decimal reference
are loaded through an externally supplied PYTHONPATH; their file hashes are sealed.
No candidate helper, host binary float arithmetic, or host libm is used.
"""
from __future__ import annotations
from fractions import Fraction
from functools import lru_cache
from itertools import combinations
from exact_numeric_m8 import finite_value, fp_operation, round_bits, signed
from interval_math_m9 import ideal_interval, reference


def integer_dot(left: list[int], right: list[int], leaf: str) -> int:
    result = sum(a * b for a, b in zip(left, right, strict=True))
    if leaf == 'AI':
        if not -(1 << 63) <= result < 1 << 63:
            raise OverflowError('final AbstractInt dot outside signed64')
        return result
    return signed(result) if leaf == 'i32' else result & 0xffffffff


def float_graph(kind: str, args: list[list[int]], width: int) -> list[set[int]]:
    """Selected finite const graph; approximations retain the qualified 2 ULP set."""
    def op(symbol: str, x: int, y: int) -> int:
        bits = fp_operation(symbol, x, y, width)
        finite_value(bits, width)
        return bits
    def dot(x: list[int], y: list[int]) -> int:
        terms = [op('*', a, b) for a, b in zip(x, y, strict=True)]
        result = terms[0]
        for term in terms[1:]:
            result = op('+', result, term)
        return result
    def length(x: list[int]) -> int:
        return reference('sqrt', dot(x, x), width)['nearest_bits']
    if kind in {'min', 'max', 'clamp'}:
        values = []
        for lanes in zip(*args, strict=True):
            x, y = lanes[:2]
            a, b = finite_value(x, width), finite_value(y, width)
            chosen = x if (a < b if kind == 'min' else a > b) else y
            if kind == 'clamp':
                z = lanes[2]
                chosen = chosen if finite_value(chosen, width) < finite_value(z, width) else z
            values.append({chosen})
        return values
    if kind in {'exp', 'tanh', 'sqrt'}:
        return [set(reference(kind, value, width)['allowed_bits']) for value in args[0]]
    if kind == 'dot':
        return [{dot(*args)}]
    if kind == 'length':
        return [{length(args[0])}]
    if kind == 'distance':
        return [{length([op('-', x, y) for x, y in zip(*args, strict=True)])}]
    if kind == 'normalize':
        divisor = length(args[0])
        return [{op('/', value, divisor)} for value in args[0]]
    if kind == 'cross':
        x, y = args
        return [{op('-', op('*', x[i], y[j]), op('*', x[j], y[i]))}
                for i, j in [(1, 2), (2, 0), (0, 1)]]
    raise ValueError(kind)


def const_results(kind: str, args: list[list[Fraction]], leaf: str) -> list[list[int]]:
    if leaf in {'AI', 'i32', 'u32'}:
        integers = [[int(x) for x in arg] for arg in args]
        if kind == 'dot':
            return [[integer_dot(*integers, leaf) & 0xffffffff]]
        result = []
        for lanes in zip(*integers, strict=True):
            value = min(lanes) if kind == 'min' else max(lanes) if kind == 'max' else min(max(lanes[0], lanes[1]), lanes[2])
            result.append([value & 0xffffffff])
        return result
    width = 64 if leaf == 'AF' else 32
    inputs = [[round_bits(x, width) for x in arg] for arg in args]
    return [sorted({round_bits(finite_value(bits, width), 32) for bits in options})
            for options in float_graph(kind, inputs, width)]


def next_bits(bits: int, direction: int) -> int:
    if bits & 0x7fffffff == 0:
        return 1 if direction > 0 else 0x80000001
    return bits + direction if not bits >> 31 else bits - direction


def outward(value: Fraction, direction: int) -> Fraction:
    bits = round_bits(value)
    rounded = finite_value(bits)
    if (rounded > value and direction < 0) or (rounded < value and direction > 0):
        bits = next_bits(bits, direction)
    return finite_value(bits)


def ulp(value: Fraction) -> Fraction:
    value = abs(value)
    bits = round_bits(value)
    at = finite_value(bits)
    gaps = []
    for direction in [-1, 1]:
        try:
            neighbor = finite_value(next_bits(bits, direction))
        except ValueError:
            continue
        if min(at, neighbor) <= value <= max(at, neighbor):
            gaps.append(abs(at - neighbor))
    if gaps:
        return min(gaps)
    return Fraction(1, 1 << 149)


def hull(items: list[tuple[Fraction, Fraction]]) -> tuple[Fraction, Fraction]:
    return min(x[0] for x in items), max(x[1] for x in items)


def arithmetic(symbol: str, a: tuple[Fraction, Fraction], b: tuple[Fraction, Fraction]) -> tuple[Fraction, Fraction]:
    if symbol == '+':
        bounds = a[0] + b[0], a[1] + b[1]
    elif symbol == '-':
        bounds = a[0] - b[1], a[1] - b[0]
    else:
        values = [x * y if symbol == '*' else x / y for x in a for y in b]
        if symbol == '/' and b[0] <= 0 <= b[1]:
            raise ValueError('division interval crosses zero')
        bounds = min(values), max(values)
    if symbol == '/':
        if not all(Fraction(1, 1 << 126) <= abs(x) <= 1 << 126 for x in b):
            raise ValueError('outside normative division denominator domain')
        error = Fraction(5, 2) * max(ulp(x) for x in bounds)
        bounds = bounds[0] - error, bounds[1] + error
    return outward(bounds[0], -1), outward(bounds[1], 1)


def runtime_dot(x: list[tuple[Fraction, Fraction]], y: list[tuple[Fraction, Fraction]]) -> tuple[Fraction, Fraction]:
    products = [arithmetic('*', a, b) for a, b in zip(x, y, strict=True)]
    @lru_cache(maxsize=16)
    def reduce(mask: int) -> tuple[Fraction, Fraction]:
        indices = [i for i in range(len(products)) if mask >> i & 1]
        if len(indices) == 1:
            return products[indices[0]]
        candidates = []
        first = indices[0]
        for subset in range(1, mask):
            if subset & mask != subset or not subset >> first & 1:
                continue
            other = mask ^ subset
            if other:
                candidates.append(arithmetic('+', reduce(subset), reduce(other)))
        # More-accurate fused answers lie in the hull of exact and separately rounded
        # reduction enclosures; include the exact sum of product interval endpoints.
        exact = [tuple(min(a*b for a in x[i] for b in y[i]) for i in indices),
                 tuple(max(a*b for a in x[i] for b in y[i]) for i in indices)]
        candidates.append((outward(sum(exact[0]), -1), outward(sum(exact[1]), 1)))
        return hull(candidates)
    return reduce((1 << len(products)) - 1)


def sqrt_runtime(interval: tuple[Fraction, Fraction]) -> tuple[Fraction, Fraction]:
    if interval[0] < 0:
        raise ValueError('negative runtime sqrt input')
    if interval == (0, 0):
        return interval
    low, high = ideal_interval('sqrt', interval[0])[0], ideal_interval('sqrt', interval[1])[1]
    inverse = Fraction(1, Fraction(high)), Fraction(1, Fraction(low))
    error = 2 * max(ulp(x) for x in inverse)
    inverse = inverse[0] - error, inverse[1] + error
    return arithmetic('/', (Fraction(1), Fraction(1)), inverse)


def unary_runtime(kind: str, value: Fraction) -> tuple[Fraction, Fraction]:
    lo, hi = (Fraction(x) for x in ideal_interval(kind, value))
    if kind == 'sqrt':
        return sqrt_runtime((value, value))
    if kind == 'exp':
        error = (3 + 2 * abs(value)) * max(ulp(lo), ulp(hi))
        return outward(lo - error, -1), outward(hi + error, 1)
    positive = unary_runtime('exp', value)
    negative = unary_runtime('exp', -value)
    half = Fraction(1, 2), Fraction(1, 2)
    numerator = arithmetic('*', arithmetic('-', positive, negative), half)
    denominator = arithmetic('*', arithmetic('+', positive, negative), half)
    inherited = arithmetic('/', numerator, denominator)
    return hull([inherited, (lo - Fraction(1, 100000), hi + Fraction(1, 100000))])


def runtime_bounds(kind: str, args: list[list[Fraction]]) -> list[tuple[Fraction, Fraction]]:
    a = [[(x, x) for x in arg] for arg in args]
    if kind in {'min', 'max', 'clamp'}:
        values = [min(v) if kind == 'min' else max(v) if kind == 'max' else min(max(v[0], v[1]), v[2]) for v in zip(*args, strict=True)]
        return [(x, x) for x in values]
    if kind in {'exp', 'tanh', 'sqrt'}:
        return [unary_runtime(kind, x) for x in args[0]]
    if kind == 'dot':
        return [runtime_dot(*a)]
    if kind == 'cross':
        x, y = a
        return [arithmetic('-', arithmetic('*', x[i], y[j]), arithmetic('*', x[j], y[i])) for i, j in [(1, 2), (2, 0), (0, 1)]]
    vector = a[0] if kind != 'distance' else [arithmetic('-', x, y) for x, y in zip(*a, strict=True)]
    length = sqrt_runtime(runtime_dot(vector, vector))
    if kind in {'length', 'distance'}:
        return [length]
    return [arithmetic('/', component, length) for component in vector]


def self_controls() -> dict:
    assert integer_dot([1 << 32] * 2, [1 << 32, -(1 << 32)], 'AI') == 0
    assert integer_dot([-(1 << 63), -(1 << 63), (1 << 63)-1, (1 << 63)-1], [(1 << 63)-1, -((1 << 63)-1), (1 << 63)-1, -((1 << 63)-1)], 'AI') == 0
    for x, y in [([-(1 << 63)] * 4, [-(1 << 63)] * 4), ([(1 << 63)-1, 1], [1, 1])]:
        try:
            integer_dot(x, y, 'AI')
        except OverflowError:
            pass
        else:
            raise AssertionError('missing AbstractInt range rejection')
    assert integer_dot([0xffffffff] * 4, [0xffffffff] * 4, 'u32') == 4
    assert const_results('cross', [[Fraction(x) for x in [1, 2, 3]], [Fraction(x) for x in [4, 5, 6]]], 'f32') == [[0xc0400000], [0x40c00000], [0xc0400000]]
    checks = 0
    for kind, args in [('dot', [[1, 2, 3], [4, 5, 6]]), ('length', [[3, 4]]), ('distance', [[-1, 2], [2, 6]]), ('normalize', [[3, 4]]), ('cross', [[1, 2, 3], [4, 5, 6]])]:
        args = [[Fraction(x) for x in row] for row in args]
        bounds = runtime_bounds(kind, args)
        for allowed, (low, high) in zip(const_results(kind, args, 'f32'), bounds, strict=True):
            assert all(low <= finite_value(bits) <= high for bits in allowed)
            assert not low <= 1234567 <= high
            checks += 1
    for kind in ['exp', 'tanh', 'sqrt']:
        for value in [Fraction(0), Fraction(1, 16), Fraction(1, 4), Fraction(1), Fraction(2), Fraction(4)]:
            low, high = unary_runtime(kind, value)
            bits = reference(kind, round_bits(value))['nearest_bits']
            assert low <= finite_value(bits) <= high
            checks += 1
    return {'passed': True, 'checks': checks, 'host_libm': False, 'const_runtime_oracles_separate': True}

if __name__ == '__main__':
    import json
    print(json.dumps(self_controls(), indent=2))
