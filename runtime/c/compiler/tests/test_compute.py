# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Doe contributors
"""Unified scalar contexts and semantic compute inputs, using actual opcodes."""
from __future__ import annotations

import itertools
import json
import os
from pathlib import Path
import struct
import subprocess

from runtime.c.compiler.tests.test_cli import (
    CLI as DEFAULT_CLI, REPO, VALIDATOR, CompilerFixture, decode, execute, float_bits,
)

CLI = Path(os.environ.get("DOE_COMPILER_CLI", str(DEFAULT_CLI)))

STORAGE = ("@group(0) @binding(0) var<storage,read> src:array<u32>;"
           "@group(0) @binding(1) var<storage,read_write> out:array<u32>;")
ENTRY = "@compute @workgroup_size(1) fn run(@builtin(global_invocation_id) gid:vec3u){"
INPUTS = (("num_workgroups", "counts", 24), ("local_invocation_index", "flat", 29),
          ("workgroup_id", "group", 26), ("global_invocation_id", "global", 28),
          ("local_invocation_id", "local", 27))


def parameters() -> str:
    return ",".join(f"@builtin({kind}) {name}:" + ("u32" if code == 29 else "vec3<u32>")
                    for kind, name, code in INPUTS) + ","


class TestComputeExpressions(CompilerFixture):
    def compile(self, source: str) -> tuple[list, dict]:
        self.source.write_text(source, encoding="utf-8")
        self.out.unlink(missing_ok=True)
        self.ref.unlink(missing_ok=True)
        run = subprocess.run([str(CLI), "--shader-path", str(self.source), "--out", str(self.out),
                              "--reflection-v2", str(self.ref)], capture_output=True)
        self.assertEqual(run.returncode, 0, run.stderr.decode())
        run = subprocess.run([str(VALIDATOR), "--target-env", "vulkan1.1", str(self.out)],
                             capture_output=True)
        self.assertEqual(run.returncode, 0, run.stderr.decode())
        return decode(self.out.read_bytes()), json.loads(self.ref.read_text(encoding="utf-8"))

    def reject(self, source: str, diagnostic: str = "") -> None:
        self.source.write_text(source, encoding="utf-8")
        self.out.unlink(missing_ok=True)
        self.ref.unlink(missing_ok=True)
        run = subprocess.run([str(CLI), "--shader-path", str(self.source), "--out", str(self.out),
                              "--reflection-v2", str(self.ref)], capture_output=True)
        self.assertNotEqual(run.returncode, 0, source)
        self.assertFalse(self.out.exists() or self.ref.exists())
        self.assertIn(diagnostic, run.stderr.decode())

    def test_six_unchanged_witnesses(self) -> None:
        paths = ("bench/inference-pipeline/kernels/gather.wgsl",
                 "bench/inference-pipeline/kernels/copy_buffer.wgsl",
                 "bench/kernels/compilation-corpus/trivial_noop_compute.wgsl",
                 "bench/kernels/copy_textures_x32.wgsl", "bench/kernels/dispatch_noop.wgsl",
                 "bench/kernels/draw_call_proxy_noop.wgsl")
        for path in paths:
            source = (REPO / path).read_text(encoding="utf-8")
            instructions, reflection = self.compile(source)
            if path.endswith("gather.wgsl"):
                self.check_gather(instructions)
            elif path.endswith("copy_buffer.wgsl"):
                for size in (0, 1, 3):
                    memory = {(0, 0): [float_bits(x) for x in (2, 3, 5)][:size],
                              (0, 1): [float_bits(99)] * 5}
                    for x in range(5):
                        execute(instructions, memory, 0, inputs={26: (x, 0, 0)})
                    self.assertEqual(memory[(0, 1)], memory[(0, 0)] + [0] * (5 - size))
            else:
                self.assertEqual(reflection["binding_count"], 0)
                self.assertFalse(any(op == 62 for op, _ in instructions))
                declared = [args for op, args in instructions if op == 59 and args[2] == 1]
                self.assertEqual(len(declared), source.count("@builtin"))
                execute(instructions, {}, (0, 0, 0))

    def check_gather(self, instructions: list) -> None:
        # Legal global tuples for LocalSize=(256,1,1), groups=(1,3,1).
        for tokens in ([], [1], [1, 0], [0xFFFFFFFF, 2, 0]):
            for table in ([], [11, 13, 17, 19, 23]):
                original = {(0, 1): tokens[:], (0, 2): [float_bits(x) for x in table]}
                memory = {key: value[:] for key, value in original.items()}
                memory[(0, 3)] = [float_bits(99)] * 8
                uniform = {(0, 0): struct.pack("<4I", 3, 2, 7, 11)}
                expected = memory[(0, 3)][:]
                for row in range(3):
                    for column in range(2):
                        token = tokens[row] if row < len(tokens) else 0
                        index = (token * 2 + column) & 0xFFFFFFFF
                        expected[row * 2 + column] = float_bits(table[index]) if index < len(table) else 0
                for y in range(3):
                    for x in range(256):
                        execute(instructions, memory, (x, y, 0), uniform)
                self.assertEqual(memory[(0, 3)], expected)
                self.assertTrue(all(memory[key] == value for key, value in original.items()))
        with self.assertRaisesRegex(AssertionError, "short uniform"):
            execute(instructions, {(0, 1): [], (0, 2): [], (0, 3): []}, 0,
                    {(0, 0): bytes(12)})

    def test_every_compute_builtin_with_coherent_dispatch(self) -> None:
        terms = [f"{name}.{axis}*{weight}u"
                 for name, base in (("global", 1), ("local", 11), ("group", 101), ("counts", 1009))
                 for axis, weight in zip("xyz", (base, base * 2, base * 3))]
        body = "out[global.x+global.y*4u+global.z*24u]=" + "+".join(terms + ["flat*10007u"]) + ";"
        source = STORAGE + "@compute @workgroup_size(2,3,4,) fn " + "n" * 63 + "(" + parameters() + "){" + body + "}"
        instructions, reflection = self.compile(source)
        self.assertEqual(reflection["workgroup_size"], [2, 3, 4])
        builtins = {args[2] for op, args in instructions if op == 71 and args[1] == 11}
        self.assertEqual(builtins, {24, 26, 27, 28, 29})
        memory = {(0, 0): [], (0, 1): [0xDEADBEEF] * 193}
        expected = memory[(0, 1)][:]
        for group in itertools.product(range(2), repeat=3):
            for local in itertools.product(range(2), range(3), range(4)):
                gx, gy, gz = (group[i] * (2, 3, 4)[i] + local[i] for i in range(3))
                lx, ly, lz = local
                wx, wy, wz = group
                flat = lx + 2 * (ly + 3 * lz)
                expected[gx + gy * 4 + gz * 24] = (gx + 2 * gy + 3 * gz +
                    11 * (lx + 2 * ly + 3 * lz) + 101 * (wx + 2 * wy + 3 * wz) +
                    1009 * 12 + flat * 10007)
                execute(instructions, memory, 0, inputs={24: (2, 2, 2), 26: group,
                    27: local, 28: (gx, gy, gz), 29: flat})
        self.assertEqual(memory[(0, 1)], expected)
        # Semantic decoration and component mutations must alter behavior.
        for wrong in ([(op, (args[0], 11, 26)) if op == 71 and args[1:] == (11, 28)
                       else (op, args) for op, args in instructions],
                      [(op, (*args[:3], 0)) if op == 81 else (op, args)
                       for op, args in instructions]):
            trial = {(0, 0): [], (0, 1): [0xDEADBEEF] * 193}
            execute(wrong, trial, 0, inputs={24: (2, 2, 2), 26: (1, 1, 1),
                27: (1, 2, 3), 28: (3, 5, 7), 29: 23})
            self.assertNotEqual(trial[(0, 1)][191], expected[191])

    def test_nested_robustness_wrap_and_post_oob_valid_store(self) -> None:
        body = "out[src[gid.x]*2u+1u]=src[src[gid.x]*2u+1u]+7u;"
        instructions, _ = self.compile(STORAGE + ENTRY + body + "}")
        # Each case is an independent synthetic uint32 input injection, not a dispatch claim.
        for token in (0, 1, 0x80000000, 0xFFFFFFFF):
            for lane in (0, 1, 0xFFFFFFFF):
                memory = {(0, 0): [token], (0, 1): [91, 92, 93, 94]}
                selected = ((token if lane == 0 else 0) * 2 + 1) & 0xFFFFFFFF
                expected = memory[(0, 1)][:]
                if selected < len(expected):
                    expected[selected] = ((token if selected == 0 else 0) + 7) & 0xFFFFFFFF
                execute(instructions, memory, lane)
                self.assertEqual(memory[(0, 1)], expected)
                self.assertEqual(memory[(0, 0)], [token])
        instructions, _ = self.compile(STORAGE + ENTRY + "out[src[99u]]=src[src[99u]];}")
        memory = {(0, 0): [37], (0, 1): [99]}
        execute(instructions, memory, 0)
        self.assertEqual(memory[(0, 1)], [37])
        # A malformed Phi predecessor cannot silently select a fallback value.
        bad = [(op, (*args[:3], 0, *args[4:])) if op == 245 else (op, args)
               for op, args in instructions]
        with self.assertRaises(AssertionError):
            execute(bad, {(0, 0): [37], (0, 1): [99]}, 0)

    def test_general_comparisons_scopes_and_snapshot_order(self) -> None:
        body = ("let old=out[0u];out[0u]=9u;"
                "if(src[src[9u]]+1u<max(src[1u],2u)){"
                "if(out[0u]>src[9u]){out[1u]=old;}else{return;}}"
                "else{out[1u]=77u;}out[2u]=out[1u];")
        instructions, _ = self.compile(STORAGE + ENTRY + body + "}")
        for values, expected in (([2, 7], [9, 33, 33]), ([], [9, 33, 33]), ([8, 7], [9, 77, 77])):
            memory = {(0, 0): values[:], (0, 1): [33, 44, 55]}
            execute(instructions, memory, 0)
            self.assertEqual(memory[(0, 1)], expected)
        for body in ("out[1.0]=1u;", "if(1.0==1u){}", "if(1u==1.0){}",
                     "if(1u<2u<3u){}", "if(1u){}", "if(1u==2u||1u==1u){}",
                     "return;out[src[1.0]]=1u;", "let x=1u;if(x==1u){let x=2u;}",
                     "out[0u]=src[missing];", "if(1u==1u){let q=1u;}out[q]=1u;"):
            self.reject(STORAGE + ENTRY + body + "}")

    def test_precise_retained_negative_lifts(self) -> None:
        bodies = ("", "out[gid.y]=1u;", "out[(gid.x)]=1u;", "out[gid.x+1u+2u]=1u;",
                  "out[gid.x+gid.x]=1u;", "if(gid.x==gid.x){}",
                  "let offset=1u;out[gid.x+offset]=1u;", "let rhs=1u;if(gid.x==rhs){}")
        for body in bodies:
            instructions, _ = self.compile(STORAGE + ENTRY + body + "}")
            memory = {(0, 0): [], (0, 1): [99] * 5}
            execute(instructions, memory, (0, 2, 0))
            expected = [99] * 5
            destinations = {bodies[1]: 2, bodies[2]: 0, bodies[3]: 3, bodies[4]: 0, bodies[6]: 1}
            if body in destinations:
                expected[destinations[body]] = 1
            self.assertEqual(memory[(0, 1)], expected)
        header = "struct P{x:u32,z:u32}@group(0) @binding(2) var<uniform> p:P;" + STORAGE
        for body in ("out[p.x+p.z]=1u;", "if(p.x<p.z+1u){}", "if(p.x<gid.x){}"):
            instructions, _ = self.compile(header + ENTRY + body + "}")
            memory = {(0, 0): [], (0, 1): [99] * 4}
            execute(instructions, memory, 0, {(0, 2): struct.pack("<2I", 1, 2)})
            self.assertEqual(memory[(0, 1)], [99, 99, 99, 1] if body.startswith("out") else [99] * 4)

    def test_dimensions_zero_resources_inputs_and_noop(self) -> None:
        for dims, expected in (("1", [1, 1, 1]), ("2,3", [2, 3, 1]),
                               ("2,3,4", [2, 3, 4]), ("256,", [256, 1, 1]),
                               ("1,16,16,", [1, 16, 16])):
            instructions, reflection = self.compile(f"@compute @workgroup_size({dims}) fn none(){{}}")
            self.assertEqual(reflection["workgroup_size"], expected)
            self.assertEqual(reflection["binding_count"], 0)
            self.assertFalse(any(op == 59 for op, _ in instructions))
            execute(instructions, {}, 0)
        for dims in ("", "0", "257", "256,2", "16,16,2", "1,0,1", "1,1,1,1", "1,,", "1u"):
            self.reject(f"@compute @workgroup_size({dims}) fn none(){{}}")
        for kind, name, code in INPUTS:
            declaration = f"@builtin({kind}) {name}:" + ("u32" if code == 29 else "vec3u")
            instructions, _ = self.compile(f"@compute @workgroup_size(1) fn none({declaration}){{}}")
            self.assertEqual(sum(op == 59 and args[2] == 1 for op, args in instructions), 1)
            self.assertFalse(any(op == 61 for op, _ in instructions))

    def test_parameter_scope_types_duplicates_and_alias_accounting(self) -> None:
        for declaration in ("@builtin(local_invocation_index) i:vec3u", "@builtin(workgroup_id) w:u32",
                            "@builtin(global_invocation_id) g:vec3<f32>", "@builtin(position) p:vec3u",
                            "@builtin(workgroup_id) a:vec3u,@builtin(workgroup_id) b:vec3u",
                            "@builtin(workgroup_id) a:vec3u,@builtin(local_invocation_id) a:vec3u",
                            "@builtin(workgroup_id) @builtin(global_invocation_id) a:vec3u"):
            self.reject("@compute @workgroup_size(1) fn f(" + declaration + "){}")
        header = STORAGE + "@compute @workgroup_size(1) fn f(" + parameters() + "){"
        for body in ("out[0u]=global;", "out[flat.x]=1u;", "out[global.w]=1u;",
                     "let global=1u;", "let flat=1u;", "let group=global.x;"):
            self.reject(header + body + "}")
        self.compile(header + "let lane=global.x;" + "out[0u]=1u;" * 16 + "}")
        for first in ("let lane=global.y;", "let lane=(global.x);", "let lane:u32=global.x;",
                      "let lane=local.x;", "let lane=flat;"):
            self.reject(header + first + "out[0u]=1u;" * 16 + "}", "16 statements")
        # Parameter declarations resolve their own types in module context.
        self.compile(STORAGE + "@compute @workgroup_size(1) fn f(@builtin(local_invocation_index) u32:u32){out[0u]=u32;}")
        self.reject(STORAGE + "@compute @workgroup_size(1) fn f(@builtin(local_invocation_index) u32:u32){let q:u32=1u;}")
        self.reject(STORAGE + ENTRY.replace("gid:vec3u", "src:vec3u") + "out[0u]=src[0u];}")

    def test_exact_unified_capacity_and_depth(self) -> None:
        expression = "+".join(["1u"] * 10)  # 19 nodes, plus one destination index.
        body = f"out[0u]={expression};" * 16
        self.compile(STORAGE + ENTRY + body + "}")  # Exactly 320 nodes.
        self.reject(STORAGE + ENTRY + body.replace("=1u", "=src[0u]", 1) + "}", "320 expression nodes")
        for depth in (18, 19):
            for expression in ("(" * (depth - 1) + "1u" + ")" * (depth - 1),
                               "src[" * (depth - 1) + "0u" + "]" * (depth - 1)):
                source = STORAGE + ENTRY + f"out[0u]={expression};}}"
                if depth == 18:
                    self.compile(source)
                else:
                    self.reject(source, "18 levels")
