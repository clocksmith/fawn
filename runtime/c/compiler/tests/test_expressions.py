#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Doe contributors
"""Bounded expression/local developer checks; no physical execution claim."""
from __future__ import annotations

from fractions import Fraction
import hashlib
import random
import unittest

from test_cli import CompilerFixture, REPO, execute, float_bits, invoke
from test_control import HEADER

FLOAT_HEADER = HEADER.replace("array<u32>", "array<f32>")
# Exact earlier negative fixtures lifted by M4, not ignored historical tests.
M4_REJECTION_LIFTS = (
    "out[0u]=src[0u]-1u;",
    "if(gid.x==0u){let q=gid.x;}",
)


def binary32_fraction(bits: int) -> Fraction:
    """Read a nonnegative normal-or-zero binary32 encoding exactly."""
    if bits == 0:
        return Fraction(0)
    exponent = (bits >> 23) - 127 - 23
    significand = (bits & 0x7FFFFF) | 0x800000
    return Fraction(significand) * Fraction(2) ** exponent


def nearest_binary32(decimal: str) -> int:
    """Independent monotone encoding search with exact rational distances."""
    value = Fraction(decimal.removesuffix("f"))
    if value == 0:
        return 0
    lower, upper = 0x00800000, 0x7F7FFFFF
    while lower + 1 < upper:
        middle = (lower + upper) // 2
        if binary32_fraction(middle) <= value:
            lower = middle
        else:
            upper = middle
    distances = (value - binary32_fraction(lower),
                 binary32_fraction(upper) - value)
    if distances[0] == distances[1]:
        return lower if lower % 2 == 0 else upper
    return lower if distances[0] < distances[1] else upper


def balanced_sum(count: int) -> str:
    if count == 1:
        return "1u"
    return f"({balanced_sum(count // 2)}+{balanced_sum(count - count // 2)})"


class TestScalarExpressions(CompilerFixture):
    def reject(self, body: str, header: str = HEADER) -> None:
        self.source.write_text(header + body + "}", encoding="utf-8")
        self.out.unlink(missing_ok=True)
        self.ref.unlink(missing_ok=True)
        run = invoke(self.source, self.out, self.ref)
        self.assertNotEqual(run.returncode, 0, body)
        self.assertFalse(self.out.exists() or self.ref.exists())

    def test_unchanged_real_sources_and_variants(self) -> None:
        fixtures = (
            ("simple_scale_f32.wgsl", "21f18f6bad8c79bd9246f27b0eb25b714f666b7c8ffbd4f56ae0841589d4cab3"),
            ("simple_relu.wgsl", "a79c6bb744a3c13f734f1e17a2f9f617c05aac7ae3d64f67c9cc98945f459acc"),
        )
        for filename, digest in fixtures:
            path = REPO / "bench/kernels/compilation-corpus" / filename
            raw = path.read_bytes()
            self.assertEqual(hashlib.sha256(raw).hexdigest(), digest)
            source = raw.decode()
            renamed = source.replace("data", "numbers").replace("input", "readings").replace(
                "output", "results").replace("idx", "lane").replace("gid", "thread").replace(
                "main", "compute_scalar").replace(" = ", " /*between*/ = ")
            reordered = renamed.splitlines()
            if filename == "simple_relu.wgsl":
                reordered[0], reordered[1] = reordered[1], reordered[0]
            for variant in (path, renamed, "\n".join(reordered)):
                instructions, _ = self.compile(variant)
                initial = [float_bits(x) for x in (-3.0, 0.0, 0.25, 17.0)]
                if filename == "simple_scale_f32.wgsl":
                    memory = {(0, 0): initial[:]}
                    expected = [float_bits(x) for x in (-6.0, 0.0, 0.5, 34.0)]
                    destination = (0, 0)
                else:
                    memory = {(0, 0): initial[:], (0, 1): [float_bits(99.0)] * 6}
                    expected = [float_bits(x) for x in (0.0, 0.0, 0.25, 17.0, 0.0, 99.0)]
                    destination = (0, 1)
                for lane in (0, 1, 2, 3, 4, 0xFFFFFFFF):
                    execute(instructions, memory, (lane, 19, 27))
                self.assertEqual(memory[destination], expected)
                if filename == "simple_relu.wgsl":
                    self.assertEqual(memory[(0, 0)], initial)
                    memory = {(0, 0): [], (0, 1): [float_bits(99.0)]}
                    execute(instructions, memory, 0)
                    self.assertEqual(memory[(0, 1)], [0])
            self.assertEqual(path.read_bytes(), raw)

    def test_precedence_associativity_wrap_and_max(self) -> None:
        cases = {
            "2u+3u*4u": 14,
            "(2u+3u)*4u": 20,
            "20u-3u-4u": 13,
            "20u-(3u-4u)": 21,
            "4294967295u*2u+3u": 1,
            "max(3u,7u)": 7,
            "max(7u,3u)": 7,
            "max(4294967295u,0u)": 0xFFFFFFFF,
            "gid.x*3u+1u": 10,
        }
        for expression, expected in cases.items():
            with self.subTest(expression=expression):
                instructions, _ = self.compile(HEADER + f"out[0u]={expression};}}")
                memory = {(0, 0): [], (0, 1): [77, 88]}
                execute(instructions, memory, 3)
                self.assertEqual(memory[(0, 1)], [expected, 88])
        for body in M4_REJECTION_LIFTS:
            instructions, _ = self.compile(HEADER + body + "}")
            memory = {(0, 0): [3], (0, 1): [77]}
            execute(instructions, memory, 0)
            self.assertEqual(memory[(0, 1)], [2] if "src[0u]-1u" in body else [77])
        # The exact old nested-let rejection has no side effect; this extension
        # additionally proves the new declaration resolves to the runtime lane.
        instructions, _ = self.compile(HEADER +
            "if(gid.x==1u){let q=gid.x;out[0u]=q*3u;}else{out[0u]=9u;}}")
        for lane, expected in ((0, 9), (1, 3)):
            memory = {(0, 0): [], (0, 1): [77]}
            execute(instructions, memory, lane)
            self.assertEqual(memory[(0, 1)], [expected])

    def test_float_grouping_rounding_and_max_order(self) -> None:
        cases = (
            ("src[0u]*2.0+src[1u]", [1.5, 0.25], 3.25),
            ("src[0u]*(2.0+src[1u])", [1.5, 0.25], 3.375),
            ("src[0u]-src[1u]-1.0", [9.0, 3.0], 5.0),
            ("max(src[0u],0.0)", [-3.0, 1.0], 0.0),
            ("max(0.0,src[0u])", [-3.0, 1.0], 0.0),
            ("max(src[0u],src[1u])", [-3.0, -1.0], -1.0),
            ("(src[0u]+1.0)-src[0u]", [16777216.0, 0.0], 0.0),
            ("src[0u]+(1.0-src[0u])", [16777216.0, 0.0], 1.0),
        )
        for expression, inputs, expected in cases:
            instructions, _ = self.compile(FLOAT_HEADER + f"out[0u]={expression};}}")
            memory = {(0, 0): [float_bits(x) for x in inputs], (0, 1): [float_bits(99.0)]}
            execute(instructions, memory, 0)
            self.assertEqual(memory[(0, 1)], [float_bits(expected)], expression)

    def test_local_snapshots_branch_scopes_and_indices(self) -> None:
        body = ("let first=out[0u];out[0u]=9u;"
                "let index:u32=(gid.x);if(index==0u){let copy=first*2u;out[1u]=copy;}"
                "else{let copy=3u;out[1u]=copy;}out[2u]=first;")
        instructions, _ = self.compile(HEADER + body + "}")
        for lane, expected in ((0, [9, 14, 7]), (1, [9, 3, 7])):
            memory = {(0, 0): [], (0, 1): [7, 77, 88]}
            execute(instructions, memory, lane)
            self.assertEqual(memory[(0, 1)], expected)
        body = ("let index:u32=gid.x+1u;if(index==1u){let x=src[index];"
                "if(index>0u){out[0u]=x;return;}else{return;}}"
                "let x=7u;out[0u]=x;")
        instructions, _ = self.compile(HEADER + body + "}")
        for lane, expected in ((0, 5), (1, 7), (0xFFFFFFFF, 7)):
            memory = {(0, 0): [3, 5], (0, 1): [77]}
            execute(instructions, memory, lane)
            self.assertEqual(memory[(0, 1)], [expected])
        self.compile(FLOAT_HEADER + "let x=0.1;out[0u]=x*2.0;}")
        self.compile(HEADER + "return;let x=3u;out[0u]=x;}")

    def test_scope_and_builtin_lookup_fail_closed(self) -> None:
        for body in (
            "out[0u]=x;let x=1u;", "let x=x;", "let x=1u;let x=2u;",
            "let x=1u;if(gid.x==0u){let x=2u;}", "let out=1u;",
            "let src=1u;", "let gid=1u;", "let run=1u;", "let x=gid;",
            "let x=1u;x=2u;", "if(gid.x==0u){let x=2u;}out[0u]=x;",
            "if(gid.x==0u){let x=2u;}else{out[0u]=x;}",
            "let max=1u;out[0u]=max(1u,2u);", "let max=gid.x;out[0u]=max(1u,2u);",
            "let u32=1u;let x:u32=2u;", "let lane=gid.x;let lane=2u;",
            "let u32=gid.x;let x:u32=1u;", "let offset=1u;out[gid.x+offset]=1u;",
            "let rhs=1u;if(gid.x==rhs){}", "let x:f32=1u;", "let x:u32=1.0;",
            "return;let x=missing;", "let x=1.0;out[x]=1u;",
        ):
            with self.subTest(body=body):
                self.reject(body)
        self.reject("out[0u]=max(1u,2u);", HEADER.replace("fn run", "fn max"))
        self.reject("out[0u]=max(1u,2u);", HEADER.replace(" gid:", " max:"))
        self.reject("out[0u]=max(1u,2u);", "const max:u32=1u;" + HEADER)
        self.reject("out[0u]=max(1u,2u);", HEADER.replace(" src:", " max:"))
        for body in ("let max=1u;out[0u]=max;", "let u32:u32=1u;out[0u]=u32;",
                     "if(gid.x==0u){let max=1u;}out[0u]=max(1u,2u);"):
            self.compile(HEADER + body + "}")
        self.compile(FLOAT_HEADER + "let f32=1u;let x=0.1;out[0u]=x;}")
        self.reject("let f32=1u;let x:f32=0.1;", FLOAT_HEADER)

    def test_numerical_and_grammar_exclusions(self) -> None:
        for literal in (".1", "1.", "1.0h", "1.0u", "1.0ff", "1.0e1", "1e-1",
                        "0x1.0p+0", "1.000000000", "1.0.0", "1f", "-1.0", "+1.0"):
            self.reject(f"out[0u]={literal};", FLOAT_HEADER)
        for expression in ("0.1+0.2", "max(0.1,0.2)", "1.0f*2.0f",
                           "src[0u]*(0.1+0.2)", "src[0u]*(16777217.0-16777216.0)",
                           "max(src[0u],max(0.1,0.2))", "src[0u]+1u", "1.0+1u",
                           "src[0u]/2.0", "f32(1u)", "min(src[0u],0.0)"):
            self.reject(f"out[0u]={expression};", FLOAT_HEADER)
        for expression in ("1u+1.0", "max(1u,1.0)", "u32(1.0)"):
            self.reject(f"out[0u]={expression};")

    def test_decimal_materialization_exact_oracle(self) -> None:
        literals = ["0.0", "000.000", "0.00000001", "99999999.9", "0.1", "0.2f",
                    "16777217.0", "16777219.0", "33554430.0", "33554431.0",
                    "33554432.0", "33554433.0", "33554434.0", "33554435.0"]
        randomizer = random.Random(412)
        for _ in range(150):
            digits = str(randomizer.randrange(10, 1000000000))
            point = randomizer.randrange(1, len(digits))
            literals.append(digits[:point] + "." + digits[point:])
        for literal in literals:
            with self.subTest(literal=literal):
                instructions, _ = self.compile(FLOAT_HEADER + f"out[0u]={literal};}}")
                memory = {(0, 0): [], (0, 1): [0xDEADBEEF]}
                execute(instructions, memory, 0)
                self.assertEqual(memory[(0, 1)], [nearest_binary32(literal)])

    def test_exact_expression_and_statement_limits(self) -> None:
        for depth in (15, 16):
            body = "out[0u]=" + "(" * depth + "1u" + ")" * depth + ";"
            if depth == 15:
                self.compile(HEADER + body + "}")
            else:
                self.reject(body)
        for terms in (16, 17):
            body = "out[0u]=" + "+".join(["1u"] * terms) + ";"
            if terms == 16:
                self.compile(HEADER + body + "}")
            else:
                self.reject(body)
        expression = balanced_sum(32)
        self.compile(HEADER + f"let a=1u;out[0u]={expression};}}")  # 64 nodes.
        self.reject(f"let a=1u;let b=2u;out[0u]={expression};")  # 65 nodes.
        self.compile(HEADER + "let lane=gid.x;" + "out[0u]=1u;" * 16 + "}")
        self.compile(HEADER + "let lane:u32=gid.x;" + "out[0u]=1u;" * 15 + "}")
        self.reject("let lane:u32=gid.x;" + "out[0u]=1u;" * 16)
        self.reject("let lane=(gid.x);" + "out[0u]=1u;" * 16)

    def test_new_opcode_oracle_controls(self) -> None:
        cases = (("out[0u]=src[0u]*2.0;", 133, 129, [3.0], 6.0),
                 ("out[0u]=src[0u]-2.0;", 131, 129, [3.0], 1.0),
                 ("out[0u]=max(src[0u],0.0);", 186, 172, [-3.0], 0.0))
        for body, original, wrong, inputs, expected in cases:
            instructions, _ = self.compile(FLOAT_HEADER + body + "}")
            bad = [(wrong if op == original else op, args) for op, args in instructions]
            memory = {(0, 0): [float_bits(x) for x in inputs], (0, 1): [0]}
            execute(bad, memory, 0)
            self.assertNotEqual(memory[(0, 1)], [float_bits(expected)])
        instructions, _ = self.compile(HEADER + "out[0u]=3u*4u;}")
        bad = [(128 if op == 132 else op, args) for op, args in instructions]
        memory = {(0, 0): [], (0, 1): [0]}
        execute(bad, memory, 0)
        self.assertNotEqual(memory[(0, 1)], [12])
        bad = [(65535 if op == 132 else op, args) for op, args in instructions]
        with self.assertRaisesRegex(AssertionError, "unexpected instruction"):
            execute(bad, {(0, 0): [], (0, 1): [0]}, 0)


if __name__ == "__main__":
    unittest.main()
