"""Deliberate defects prove the independent oracle is sensitive to its claims."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import random
import struct

from fixtures import residual
from semantic_checks import reference_add, run_vectors
from typed_oracle import ExecutionError, Module, OracleLimit, StructuralError, bits_f32, f32_bits, float_equal


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument('--module', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    raw = args.module.read_bytes()
    words = list(struct.unpack('<' + 'I' * (len(raw) // 4), raw))
    _, expected = residual()
    baseline = Module(words)
    baseline.interface(expected)
    report = [dict(case='unmodified_residual_control', passed=True,
                   trials=run_vectors(baseline, expected))]
    ids = {key: rid for rid, key in baseline.resources.items()}

    def check(name: str, mutated: list[int], want_limit: bool = False) -> None:
        try:
            mod = Module(mutated)
            mod.interface(expected)
            run_vectors(mod, expected)
        except OracleLimit as exc:
            report.append(dict(case=name, passed=want_limit, classification='ORACLE_UNSUPPORTED', reason=str(exc)))
        except (ExecutionError, StructuralError) as exc:
            report.append(dict(case=name, passed=not want_limit, classification='DEFECT_DETECTED', reason=str(exc)))
        else:
            report.append(dict(case=name, passed=False, reason='defective control escaped'))

    for name, mutate in [
        ('wrong_integer_add_for_float', lambda op, a, off, out: out.__setitem__(off, (out[off] & ~65535) | 128) if op == 129 else None),
        ('unguarded_first_oob', lambda op, a, off, out: out.__setitem__(off, (out[off] & ~65535) | 178) if op == 176 else None),
        ('conflated_input_load', lambda op, a, off, out: out.__setitem__(off + 3, ids[(0, 0)]) if op == 65 and a[2] == ids[(0, 1)] else None),
        ('wrong_resource_length', lambda op, a, off, out: out.__setitem__(off + 3, ids[(0, 0)]) if op == 68 and a[2] == ids[(0, 1)] else None),
        ('duplicate_descriptor_identity', lambda op, a, off, out: out.__setitem__(off + 3, 0) if op == 71 and a[:2] == (ids[(0, 1)], 33) else None),
        ('missing_output_store', lambda op, a, off, out: out.__setitem__(slice(off, off + len(a) + 1), [1 << 16] * (len(a) + 1)) if op == 62 else None),
    ]:
        changed = words.copy()
        for op, a, off in baseline.instructions:
            mutate(op, a, off, changed)
        assert changed != words, name
        check(name, changed)
    changed = words.copy()
    changed.extend([(1 << 16) | 60000])
    check('unknown_opcode_not_compiler_defect', changed, want_limit=True)
    # Conditional-store omission models the prohibited all-buffer guard by reusing
    # an input condition instead of destination length, still interpreting code.
    comparisons = [a[1] for op, a, _ in baseline.instructions if op == 176]
    branches = [(a, off) for op, a, off in baseline.instructions if op == 250]
    if len(comparisons) >= 3 and len(branches) >= 3:
        changed = words.copy()
        changed[branches[-1][1] + 1] = comparisons[0]
        check('store_wrongly_gated_by_input', changed)
    rng = random.Random(0xd0e2026)
    for _ in range(100000):
        a, b = rng.getrandbits(32), rng.getrandbits(32)
        assert float_equal(f32_bits(bits_f32(a) + bits_f32(b)), reference_add(a, b))
    report.append(dict(case='exact_integer_binary32_reference_crosscheck', passed=True, trials=100000))
    assert reference_add(0x3f800000, 0x33800000) == 0x3f800000
    assert reference_add(0x3f800001, 0x33800000) == 0x3f800002
    assert reference_add(0x80000000, 0x80000000) == 0x80000000
    assert reference_add(1, 1) == 2
    assert reference_add(0x007fffff, 1) == 0x00800000
    assert reference_add(0x7f7fffff, 0x7f7fffff) == 0x7f800000
    report.append(dict(case='known_rounding_subnormal_zero_overflow_answers', passed=True, trials=6))
    args.out.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(report, indent=2))
    return int(not all(r['passed'] for r in report))


if __name__ == '__main__':
    raise SystemExit(main())
