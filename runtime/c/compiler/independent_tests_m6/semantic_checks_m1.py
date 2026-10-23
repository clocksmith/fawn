"""Exact integer binary32 reference and independent instruction test vectors."""
from __future__ import annotations

from typed_oracle import ExecutionError, Module, float_equal


def reference_add(left: int, right: int) -> int:
    """IEEE binary32 RN-even, using exact integers, not host float addition."""
    sign_a, sign_b = left >> 31, right >> 31
    exp_a, exp_b = (left >> 23) & 255, (right >> 23) & 255
    frac_a, frac_b = left & 0x7fffff, right & 0x7fffff
    if (exp_a == 255 and frac_a) or (exp_b == 255 and frac_b):
        return 0x7fc00000
    if exp_a == 255 or exp_b == 255:
        if exp_a == exp_b == 255 and sign_a != sign_b:
            return 0x7fc00000
        return left if exp_a == 255 else right
    ma = frac_a | (0x800000 if exp_a else 0)
    mb = frac_b | (0x800000 if exp_b else 0)
    ea = exp_a - 150 if exp_a else -149
    eb = exp_b - 150 if exp_b else -149
    emin = min(ea, eb)
    total = (-1 if sign_a else 1) * (ma << (ea - emin))
    total += (-1 if sign_b else 1) * (mb << (eb - emin))
    if not total:
        return 0x80000000 if not ma and not mb and sign_a and sign_b else 0
    sign = 0x80000000 if total < 0 else 0
    total = abs(total)
    top = total.bit_length() - 1 + emin
    step = max(-149, top - 23)
    shift = step - emin
    if shift > 0:
        whole, remainder = divmod(total, 1 << shift)
        half = 1 << (shift - 1)
        total = whole + int(remainder > half or (remainder == half and whole & 1))
    else:
        total <<= -shift
    if total >= 1 << 24:
        total >>= 1
        step += 1
    if step + 23 > 127:
        return sign | 0x7f800000
    if total < 1 << 23:
        return sign | total
    return sign | ((step + 150) << 23) | (total & 0x7fffff)


FLOAT_A = [0x3f800000, 0xbf800000, 0, 0x80000000, 0x80000000,
           1, 0x007fffff, 0x00800000, 0x7f7fffff, 0x7f800000,
           0xff800000, 0x7fc12345, 0x3f800000, 0x3f800001,
           0x4b800000, 0x80800000, 0xff7fffff, 0x7f800000,
           0x3eaaaaab, 0x80000001, 0x7f800001]
FLOAT_B = [0x40000000, 0x3f800000, 0, 0x80000000, 0,
           1, 1, 0x807fffff, 0x7f7fffff, 0x3f800000,
           0xbf800000, 0x3f800000, 0x33800000, 0x33800000,
           0x3f800000, 0x007fffff, 0xff7fffff, 0xff800000,
           0x3eaaaaab, 1, 0x3f800000]
UINT_A = [0, 0xffffffff, 1, 0x80000000, 0x7fffffff, 42, 0xfffffffe]
UINT_B = [17, 1, 0xffffffff, 0x80000000, 1, 0xfffffff0, 3]


def run_vectors(module: Module, expected: dict) -> int:
    output = tuple(expected['output'])
    resources = [(b['group'], b['binding']) for b in expected['bindings']]
    readonly = {(b['group'], b['binding']) for b in expected['bindings'] if b['access'] == 1}
    operands = expected['operands']
    patterns = [FLOAT_A, FLOAT_B] if expected['scalar'] == 2 else [UINT_A, UINT_B]
    primary = [tuple(x) for x in operands if not isinstance(x, int)]
    lengths = [(0, 0, 0), (1, 1, 1), (5, 5, 5), (0, 3, 5),
               (5, 0, 3), (1, 4, 5), (5, 5, 1), (0, 0, 5),
               (5, 5, 0), (23, 23, 23), (17, 21, 23)]
    trials = 0
    for len_a, len_b, len_out in lengths:
        sizes = {key: 7 for key in resources}
        sizes[output] = len_out
        for i, key in enumerate(primary):
            if key != output:
                sizes[key] = [len_a, len_b][min(i, 1)]
        initial = {key: [0x4355a000 if expected['scalar'] == 2 else 0xfeed1234] * sizes[key]
                   for key in resources}
        for i, key in enumerate(primary):
            initial[key] = [patterns[min(i, 1)][j % len(patterns[min(i, 1)])]
                            for j in range(sizes[key])]
        indexes = {0, 0xffffffff, 0xfffffffe}
        for size in sizes.values():
            indexes.update(range(size + 2))
            if size:
                indexes.add(size - 1)
        for x in sorted(indexes):
            actual = {key: list(values) for key, values in initial.items()}
            want = {key: list(values) for key, values in initial.items()}
            if x < sizes[output]:
                args = [operand if isinstance(operand, int) else
                        initial[tuple(operand)][x] if x < sizes[tuple(operand)] else 0
                        for operand in operands]
                want[output][x] = reference_add(*args) if expected['scalar'] == 2 else sum(args) & 0xffffffff
            accesses = module.execute(actual, x, readonly)
            for key in resources:
                if len(actual[key]) != len(want[key]):
                    raise ExecutionError('resource length mutated')
                for index, (a, b) in enumerate(zip(actual[key], want[key])):
                    is_float_result = expected['scalar'] == 2 and key == output and index == x
                    equal = float_equal(a, b) if is_float_result else a == b
                    if not equal:
                        raise ExecutionError(f'wrong data lengths={sizes} invocation={x} descriptor={key} element={index}: {a:#010x} != {b:#010x}')
            if x < sizes[output] and ('write', output, x) not in accesses:
                raise ExecutionError(f'in-range output omitted store lengths={sizes} x={x}')
            trials += 1
        actual = {key: list(values) for key, values in initial.items()}
        want = {key: list(values) for key, values in initial.items()}
        for x in range(max(sizes.values()) + 2):
            if x < sizes[output]:
                args = [operand if isinstance(operand, int) else
                        want[tuple(operand)][x] if x < sizes[tuple(operand)] else 0
                        for operand in operands]
                want[output][x] = reference_add(*args) if expected['scalar'] == 2 else sum(args) & 0xffffffff
            module.execute(actual, x, readonly)
        for key in resources:
            for a, b in zip(actual[key], want[key]):
                if not (float_equal(a, b) if expected['scalar'] == 2 and key == output else a == b):
                    raise ExecutionError(f'full dispatch mismatch descriptor={key}')
        trials += 1
    return trials
