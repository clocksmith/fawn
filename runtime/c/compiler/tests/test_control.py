#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Doe contributors
"""Milestone-2 developer tests: emitted instructions, not WGSL interpretation."""
from __future__ import annotations

import hashlib
import unittest

from test_cli import CompilerFixture, REPO, execute, float_bits, invoke

HEADER = """@group(0) @binding(1) var<storage,read_write> out:array<u32>;
@group(0) @binding(0) var<storage,read> src:array<u32>;
@compute @workgroup_size(64) fn run(@builtin(global_invocation_id) gid:vec3u){"""

# These failed the M1 grammar; each is explicitly admitted by MILESTONE_2.md.
REJECTION_LIFTS = {
    "single_load": "out[gid.x]=src[gid.x];",
    "single_literal": "out[gid.x]=7u;",
    "constant_only_add": "out[gid.x]=7u+8u;",
    "literal_index": "out[0u]=src[1u]+1u;",
    "index_add": "out[gid.x+1u]=src[gid.x]+1u;",
    "index_subtract": "out[gid.x]=src[gid.x-1u]+1u;",
    "return_only": "return;",
    "early_return": "return; out[gid.x]=src[gid.x]+1u;",
    "sequential": "out[gid.x]=src[gid.x]+1u;out[gid.x]=out[gid.x]+2u;",
    "if_empty": "if(gid.x==0u){}",
    "if_else": "if(gid.x==0u){out[gid.x]=1u;}else{out[gid.x]=2u;}",
}


class TestControlFlow(CompilerFixture):
    def test_exact_neighbor_source_and_permutations(self) -> None:
        path = REPO / "bench/fixtures/wgsl-corpus/webgpu/sample-prefix-sum.wgsl"
        raw = path.read_bytes()
        self.assertEqual(hashlib.sha256(raw).hexdigest(),
                         "a8a642a54aaca908b707d20666566db8db62dbb5c9bfd518fbe98925da22c416")
        source = raw.decode()
        renamed = source.replace("input_values", "readings").replace(
            "output_values", "results").replace("index", "lane").replace(
            "gid", "invocation").replace("main", "adjacent")
        lines = renamed.splitlines()
        lines[0], lines[1] = lines[1], lines[0]
        for fixture in (path, renamed, "\n".join(lines)):
            instructions, reflection = self.compile(fixture)
            self.assertEqual(reflection["binding_count"], 2)
            memory = {(0, 0): [3, 5, 7, 11], (0, 1): [99] * 6}
            for lane in (0, 1, 3, 4, 0xFFFFFFFF):
                execute(instructions, memory, (lane, 51, 93))
            self.assertEqual(memory, {(0, 0): [3, 5, 7, 11],
                                      (0, 1): [3, 8, 99, 18, 11, 99]})
            memory = {(0, 0): [], (0, 1): [91, 92, 93]}
            execute(instructions, memory, 0)
            execute(instructions, memory, 1)
            self.assertEqual(memory[(0, 1)], [0, 0, 93])
            execute(instructions, {(0, 0): [3, 5], (0, 1): []}, 0)

    def test_explicit_m1_rejection_lifts(self) -> None:
        for name, body in REJECTION_LIFTS.items():
            with self.subTest(name=name):
                self.compile(HEADER + body + "}")

    def test_all_unsigned_comparisons(self) -> None:
        # Hand-authored expectations for x = 0, 2, 3, UINT_MAX against 2.
        cases = {"==": [22, 11, 22, 22], "!=": [11, 22, 11, 11],
                 "<": [11, 22, 22, 22], "<=": [11, 11, 22, 22],
                 ">": [22, 22, 11, 11], ">=": [22, 11, 11, 11]}
        for operator, expected in cases.items():
            instructions, _ = self.compile(HEADER +
                f"if(gid.x {operator} 2u){{out[0u]=11u;}}else{{out[0u]=22u;}}" + "}")
            for lane, result in zip((0, 2, 3, 0xFFFFFFFF), expected):
                memory = {(0, 0): [], (0, 1): [77, 88]}
                execute(instructions, memory, lane)
                self.assertEqual(memory[(0, 1)], [result, 88])

    def test_offsets_wrap_before_independent_bounds(self) -> None:
        instructions, _ = self.compile(HEADER +
            "out[gid.x+1u]=src[gid.x-1u]+src[2u];}")
        memory = {(0, 0): [3, 5, 7], (0, 1): [99] * 4}
        for lane in (0, 1, 0xFFFFFFFF):
            execute(instructions, memory, lane)
        self.assertEqual(memory, {(0, 0): [3, 5, 7], (0, 1): [7, 7, 10, 99]})
        instructions, _ = self.compile("const OFF:u32=1u;" + HEADER +
            "out[0u-OFF]=src[0u];out[4294967295u+OFF]=src[OFF]+42u;}")
        memory = {(0, 0): [3, 5, 7], (0, 1): [99, 98]}
        execute(instructions, memory, 0)
        self.assertEqual(memory[(0, 1)], [47, 98])

    def test_return_survives_discarded_store(self) -> None:
        instructions, _ = self.compile(HEADER +
            "if(gid.x==0u){out[99u]=src[0u];return;}out[0u]=9u;}")
        memory = {(0, 0): [3], (0, 1): [77, 88]}
        execute(instructions, memory, 0)
        self.assertEqual(memory[(0, 1)], [77, 88])
        execute(instructions, memory, 1)
        self.assertEqual(memory[(0, 1)], [9, 88])

    def test_nested_return_merges_and_sequential_observation(self) -> None:
        source = HEADER + """if(gid.x==0u){
            if(gid.x<1u){out[0u]=src[0u];return;}else{return;}
            out[1u]=99u;
        }else{if(gid.x>=2u){return;}else{out[0u]=7u;}}
        out[1u]=out[0u]+src[1u];return;out[0u]=98u;}"""
        instructions, _ = self.compile(source)
        for lane, expected in ((0, [3, 88]), (1, [7, 12]), (2, [77, 88])):
            memory = {(0, 0): [3, 5], (0, 1): [77, 88]}
            execute(instructions, memory, lane)
            self.assertEqual(memory[(0, 1)], expected)
        instructions, _ = self.compile(HEADER +
            "if(gid.x==0u){return;}else{return;}out[0u]=99u;}")
        for lane in (0, 1):
            memory = {(0, 0): [], (0, 1): [77]}
            execute(instructions, memory, lane)
            self.assertEqual(memory[(0, 1)], [77])

    def test_empty_arms_and_four_levels(self) -> None:
        for body in ("if(gid.x==0u){}else{}out[0u]=3u;",
                     "if(gid.x==0u){if(gid.x<2u){if(gid.x<=3u){"
                     "if(gid.x!=4u){out[0u]=3u;}}}}"):
            instructions, _ = self.compile(HEADER + body + "}")
            memory = {(0, 0): [], (0, 1): [77]}
            execute(instructions, memory, 0)
            self.assertEqual(memory[(0, 1)], [3])

    def test_float_copy_add_and_return_reflection(self) -> None:
        instructions, reflection = self.compile(HEADER.replace("u32", "f32") +
            "if(gid.x==0u){out[0u]=src[0u];return;}"
            "out[0u]=src[0u]+src[1u];}")
        for lane, expected in ((0, 1.25), (1, 3.75)):
            memory = {(0, 0): [float_bits(1.25), float_bits(2.5)], (0, 1): [99]}
            execute(instructions, memory, lane)
            self.assertEqual(memory[(0, 1)], [float_bits(expected)])
        _, reflection = self.compile(HEADER + "return;}")
        self.assertEqual([binding["name"] for binding in reflection["bindings"]], ["out", "src"])

    def test_maximum_value_shape(self) -> None:
        instructions, _ = self.compile(HEADER +
            "out[0u+1u]=src[1u-1u]+src[4294967295u+1u];" * 16 + "}")
        memory = {(0, 0): [3], (0, 1): [77, 88, 99]}
        execute(instructions, memory, 0)
        self.assertEqual(memory[(0, 1)], [77, 6, 99])

    def test_instruction_oracle_detects_deliberate_faults(self) -> None:
        instructions, _ = self.compile(HEADER + "out[gid.x]=src[gid.x-1u];}")
        # Reversing a real robustness guard must fail at pointer formation.
        bad_guard = [(174 if op == 176 else op, args) for op, args in instructions]
        with self.assertRaisesRegex(AssertionError, "unguarded element pointer"):
            execute(bad_guard, {(0, 0): [3], (0, 1): [77]}, 0)
        # A subtraction mutation remains executable but violates expected bytes.
        bad_subtract = [(128 if op == 130 else op, args) for op, args in instructions]
        memory = {(0, 0): [3, 5], (0, 1): [77]}
        execute(bad_subtract, memory, 0)
        self.assertNotEqual(memory[(0, 1)], [0])
        instructions, _ = self.compile(HEADER +
            "if(gid.x==0u){return;}out[0u]=9u;}")
        outer_merge = next(args[0] for op, args in instructions if op == 247)
        bad_return = [(249, (outer_merge,)) if op == 253 else (op, args)
                      for op, args in instructions]
        # Change only the early return: leave the final terminator intact.
        last_return = max(i for i, (op, _) in enumerate(instructions) if op == 253)
        bad_return[last_return] = instructions[last_return]
        memory = {(0, 0): [], (0, 1): [77]}
        execute(bad_return, memory, 0)
        self.assertNotEqual(memory[(0, 1)], [77])
        bad_unreachable = [(255, ()) if op == 253 else (op, args)
                           for op, args in instructions]
        with self.assertRaisesRegex(AssertionError, "executed unreachable"):
            execute(bad_unreachable, {(0, 0): [], (0, 1): [77]}, 0)

    def test_reject_outside_frozen_expansion(self) -> None:
        invalid = ["", "{}", "else{}", "return 1u;", "return;missing[0u]=1u;",
                   "return;out[0u]=MISSING;", "out[gid.y]=1u;", "out[(gid.x)]=1u;",
                   "out[gid.x+1u+2u]=1u;", "out[gid.x+gid.x]=1u;", "out[0]=1u;",
                   "out[1.0]=1u;", "out[0u]=src[0u]-1u;", "src[0u]=1u;",
                   "if(gid.x==0u){}else if(gid.x==1u){}", "if gid.x==0u {}",
                   "if(gid.x==0u)out[0u]=1u;", "if(gid.x==0u){let q=gid.x;}",
                   "if(gid.x= =0u){}", "if(gid.x! /*gap*/ =0u){}",
                   "if(gid.x< =0u){}", "if(gid.x> =0u){}", "if(gid.x==gid.x){}",
                   "if(gid.x==0u&&gid.x<1u){}", "if(gid.x==0u){return;}else{src[0u]=1u;}",
                   "return;" * 17, "if(gid.x==0u){" * 5 + "}" * 5]
        for body in invalid:
            with self.subTest(body=body):
                self.source.write_text(HEADER + body + "}", encoding="utf-8")
                self.out.unlink(missing_ok=True)
                self.ref.unlink(missing_ok=True)
                run = invoke(self.source, self.out, self.ref)
                self.assertNotEqual(run.returncode, 0)
                self.assertFalse(self.out.exists() or self.ref.exists())


if __name__ == "__main__":
    unittest.main()
