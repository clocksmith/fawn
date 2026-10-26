#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Doe contributors
"""Uniform declaration/layout and raw-byte opcode developer trials, not GPU qualification."""
from __future__ import annotations

import hashlib
import json
import struct
import subprocess
import unittest

from test_cli import CLI, REPO, VALIDATOR, CompilerFixture, decode, execute, float_bits

ENTRY = "@compute @workgroup_size(1) fn go(@builtin(global_invocation_id) g:vec3u){"
STORAGE = "@group(0) @binding(2) var<storage,read_write> o:array<u32>;"


class TestUniforms(CompilerFixture):
    def compile(self, source: str) -> tuple[list, dict]:
        self.source.write_text(source, encoding="utf-8")
        self.out.unlink(missing_ok=True)
        self.ref.unlink(missing_ok=True)
        run = subprocess.run([str(CLI), "--shader-path", str(self.source), "--out", str(self.out),
                              "--reflection-v2", str(self.ref)], capture_output=True)
        self.assertEqual(run.returncode, 0, run.stderr.decode())
        run = subprocess.run([str(VALIDATOR), "--target-env", "vulkan1.1", str(self.out)], capture_output=True)
        self.assertEqual(run.returncode, 0, run.stderr.decode())
        return decode(self.out.read_bytes()), json.loads(self.ref.read_text(encoding="utf-8"))

    def reject(self, source: str) -> None:
        self.source.write_text(source, encoding="utf-8")
        self.out.unlink(missing_ok=True)
        self.ref.unlink(missing_ok=True)
        run = subprocess.run([str(CLI), "--shader-path", str(self.source), "--out", str(self.out),
                              "--reflection-v2", str(self.ref)], capture_output=True)
        self.assertNotEqual(run.returncode, 0, source)
        self.assertFalse(self.out.exists() or self.ref.exists())

    def test_unchanged_package_witness(self) -> None:
        path = REPO / "bench/kernels/package_vector_scale_add.wgsl"
        raw = path.read_bytes()
        self.assertEqual(hashlib.sha256(raw).hexdigest(),
                         "8f2b3e2bb6955c1f7507936773589195c80e6a119cbd50d975f652d4f834e8c1")
        instructions, reflection = self.compile(raw.decode())
        self.assertEqual(reflection["schema_version"], 2)
        self.assertEqual(reflection["bindings"][0]["minimum_binding_size_bytes"], 16)
        initial = [float_bits(x) for x in (1.0, -2.0, 4.0)]
        for count in (0, 1, 2, 3, 5, 0xFFFFFFFF):
            for size in (0, 1, 3):
                memory = {(0, 1): initial[:size], (0, 2): [float_bits(99.0)] * 6}
                uniform = {(0, 0): struct.pack("<4I", count, 17, 29, 43)}
                for lane in (0, 1, 2, 3, 4, 0xFFFFFFFF):
                    execute(instructions, memory, (lane, 19, 23), uniform)
                expected = [float_bits(99.0)] * 6
                for i in range(min(count, 5)):
                    value = (1.0, -2.0, 4.0)[i] if i < size else 0.0
                    expected[i] = float_bits(value * 1.5 + 0.25)
                self.assertEqual(memory[(0, 2)], expected)
                self.assertEqual(memory[(0, 1)], initial[:size])
        with self.assertRaisesRegex(AssertionError, "short uniform"):
            execute(instructions, {(0, 1): [], (0, 2): []}, 0, {(0, 0): b"\0" * 12})
        self.assertEqual(path.read_bytes(), raw)

    def test_all_members_offsets_types_and_shared_resources(self) -> None:
        for count in (1, 3, 4, 5, 8):
            fields = ",".join(f"m{i}:u32" for i in range(count))
            header = ("@binding(1) @group(0) var<uniform> second:P;"
                      "@binding(0) @group(0) var<uniform> first:P;"
                      f"struct P{{{fields},}}" + STORAGE)
            body = "".join(f"o[{i}u]=first.m{i}+second.m{i};" for i in range(count))
            instructions, reflection = self.compile(header + ENTRY + body + "}")
            self.assertEqual(reflection["structs"][0]["size_bytes"], 4 * count)
            self.assertEqual([m["offset_bytes"] for m in reflection["structs"][0]["members"]],
                             list(range(0, 4 * count, 4)))
            self.assertEqual([b["struct_index"] for b in reflection["bindings"]], [0, 0, None])
            memory = {(0, 2): [99] * (count + 1)}
            raw = {(0, 0): struct.pack(f"<{count}I", *range(1, count + 1)),
                   (0, 1): struct.pack(f"<{count}I", *range(11, count + 11))}
            execute(instructions, memory, 0, raw)
            self.assertEqual(memory[(0, 2)], [12 + 2 * i for i in range(count)] + [99])
            if count > 1:
                mutated = [(op, (args[0], args[1], args[2], 0))
                           if op == 72 and args[1] == 1 else (op, args) for op, args in instructions]
                wrong = {(0, 2): [99] * (count + 1)}
                execute(mutated, wrong, 0, raw)
                self.assertNotEqual(wrong, memory)
        header = ("struct P{x:u32,y:f32,z:u32}"
                  "@group(0) @binding(0) var<uniform> p:P;"
                  "@group(0) @binding(2) var<storage,read_write> o:array<f32>;")
        instructions, reflection = self.compile(header + ENTRY +
            "let x=p.x;let y=p.y;if(x<p.z){o[p.x+1u]=y*2.0;}" + "}")
        self.assertEqual([m["scalar"] for m in reflection["structs"][0]["members"]], ["u32", "f32", "u32"])
        memory = {(0, 2): [float_bits(99.0)] * 3}
        execute(instructions, memory, 0, {(0, 0): struct.pack("<IfI", 1, 2.25, 3)})
        self.assertEqual(memory[(0, 2)], [float_bits(99.0), float_bits(99.0), float_bits(4.5)])

    def test_explicit_alignment_and_entry_binding_reachability(self) -> None:
        header = "struct P{@align(16) x:u32,y:f32,z:u32}@group(0) @binding(0) var<uniform> p:P;"
        instructions, reflection = self.compile(header + STORAGE + ENTRY + "o[0]=p.x+p.z;}")
        self.assertEqual(reflection["bindings"][0]["minimum_binding_size_bytes"], 16)
        self.assertEqual(reflection["structs"][0]["alignment_bytes"], 16)
        self.assertEqual(reflection["structs"][0]["members"][0]["alignment_bytes"], 16)
        memory = {(0, 2): [99]}
        execute(instructions, memory, 0, {(0, 0): struct.pack("<4I", 2, 0, 3, 0)})
        self.assertEqual(memory[(0, 2)], [5])
        duplicates = header + "@group(0) @binding(0) var<uniform> duplicate:P;"
        # Binding collisions are scoped to resources used by one entry, not
        # to unrelated/unused declarations in the whole source module.
        _, reflection = self.compile(duplicates + ENTRY + "return;}")
        self.assertEqual(reflection["binding_count"], 2)
        self.reject(duplicates + STORAGE + ENTRY + "o[0]=p.x+duplicate.x;}")

    def test_boundaries_names_dead_code_and_uniform_only(self) -> None:
        header = "struct P{x:u32,y:f32,z:u32}@group(0) @binding(0) var<uniform> p:P;"
        self.compile(header + ENTRY + "let snap=p.x;return;}")
        self.compile(header + STORAGE + ENTRY + "let first=p.x;let second=p.x;o[0u]=first+second;}")
        for body in ("o[0u]=p;", "o[0u]=p.missing;", "o[p.y]=1u;", "if(p.y<1u){}",
                     "if(0u<p.y){}", "o[0u]=p.y+1u;",
                     "p.x=1u;", "p[0u]=1u;", "o[0u]=p[0u];", "return;o[0u]=p.missing;"):
            self.reject(header + STORAGE + ENTRY + body + "}")
        # These exact M6 CLI-v2 negatives become valid lexical shadowing under
        # M7's job profile, without changing their unchanged resource reflection.
        for body in ("let P=1u;", "let p=1u;"):
            instructions, reflection = self.compile(header + STORAGE + ENTRY + body + "}")
            memory = {(0,2): [99]}
            execute(instructions, memory, 0, {(0,0): struct.pack("<IfI", 1, 2.0, 3)})
            self.assertEqual(memory[(0,2)], [99])
            self.assertEqual(reflection["struct_count"], 1)
        invalid_headers = (
            header.replace("z:u32", "x:u32"), header.replace("x:u32", "x:i32"),
            header.replace("x:u32", "x:array<u32>"),
            header.replace("var<uniform> p:P", "var<uniform> p:u32"), header + "struct P{q:u32}",
            header.replace("struct P{", "struct Missing{"), header + "struct p{q:u32}",
            header.replace("struct P{x:u32,y:f32,z:u32}", "struct P{}"),
            header.replace("struct P{x:u32,y:f32,z:u32}", "struct P{" + ",".join(f"x{i}:u32" for i in range(9)) + "}"),
            header + "".join(f"struct S{i}{{x:u32}}" for i in range(8)),
            header + "".join(f"@group(1) @binding({i}) var<uniform> q{i}:P;" for i in range(8)),
        )
        for declarations in invalid_headers:
            self.reject(declarations + ENTRY + "return;}")
        eight = "".join(f"struct S{i}{{x:u32}}" for i in range(8))
        eight += "".join(f"@group(0) @binding({i}) var<uniform> r{i}:S{i};" for i in range(8))
        _, reflection = self.compile(eight + ENTRY + "return;}")
        self.assertEqual((reflection["struct_count"], reflection["binding_count"]), (8, 8))
        # Builtin spellings are permitted when not needed and cannot be bypassed.
        self.compile("struct f32{x:u32}@group(0) @binding(0) var<uniform> p:f32;" + ENTRY + "return;}")
        self.reject("struct max{x:u32}@group(0) @binding(0) var<uniform> p:max;" + STORAGE + ENTRY + "o[0u]=max(1u,2u);}")
        self.reject("struct u32{x:u32}@group(0) @binding(0) var<uniform> p:u32;" + ENTRY + "return;}")


if __name__ == "__main__":
    unittest.main()
