#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Doe contributors
"""Developer CLI/encoding/CPU checks adapted from the retained C seed tests.

This instruction interpreter checks emitted behavior, not physical GPU execution.
The separate spirv-val process checks the Vulkan 1.1 target contract.
"""
from __future__ import annotations

import json
import math
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
REPO = ROOT.parents[2]
CLI = ROOT / "build/doe-compiler-c"
VALIDATOR = Path(os.environ["SPIRV_VAL"])
SEED = """@group(0) @binding(0) var<storage, read_write> values: array<u32>;
@compute @workgroup_size(1) fn main(@builtin(global_invocation_id) id: vec3<u32>) {
values[id.x] = values[id.x] + 1u; }"""
Instruction = tuple[int, tuple[int, ...]]
Descriptor = tuple[int, int]


def invoke(source: Path, out: Path, reflection: Path) -> subprocess.CompletedProcess[bytes]:
    return subprocess.run(
        [str(CLI), "--shader-path", str(source), "--out", str(out),
         "--reflection", str(reflection)], capture_output=True, check=False,
    )


def decode(data: bytes) -> list[Instruction]:
    assert len(data) % 4 == 0
    words = struct.unpack(f"<{len(data) // 4}I", data)
    assert words[0:2] == (0x07230203, 0x00010300) and words[4] == 0
    instructions = []
    cursor = 5
    while cursor < len(words):
        size, opcode = words[cursor] >> 16, words[cursor] & 0xFFFF
        assert size > 0 and cursor + size <= len(words)
        instructions.append((opcode, words[cursor + 1:cursor + size]))
        cursor += size
    assert cursor == len(words)
    return instructions


def f32(bits: int) -> float:
    return struct.unpack("<f", struct.pack("<I", bits))[0]


def float_bits(value: float) -> int:
    try:
        return struct.unpack("<I", struct.pack("<f", value))[0]
    except OverflowError:
        return 0xFF800000 if value < 0 else 0x7F800000


class UniformLayout:
    """Read nested scalar-struct layout from emitted types and decorations."""

    def __init__(self) -> None:
        self.scalars: set[int] = set()
        self.members: dict[int, tuple[int, ...]] = {}
        self.offsets: dict[tuple[int, int], int] = {}

    def record(self, op: int, args: tuple[int, ...]) -> None:
        if op in (21, 22) and args[1] == 32:
            self.scalars.add(args[0])
        elif op == 30:
            self.members[args[0]] = args[1:]
        elif op == 72 and args[2] == 35:
            self.offsets[(args[0], args[1])] = args[3]

    def extent(self, type_id: int, parents: tuple[int, ...] = ()) -> int:
        if type_id in self.scalars:
            return 4
        assert type_id not in parents, "cyclic uniform type"
        fields = self.members[type_id]
        return max(self.offsets[(type_id, i)] + self.extent(child, parents + (type_id,))
                   for i, child in enumerate(fields))

    def offset(self, type_id: int, indexes: list[int]) -> int:
        offset = 0
        for index in indexes:
            fields = self.members[type_id]
            assert 0 <= index < len(fields), "uniform member index out of range"
            offset += self.offsets[(type_id, index)]
            type_id = fields[index]
        assert type_id in self.scalars, "uniform load is not a 32-bit scalar"
        return offset


def execute(instructions: list[Instruction], memory: dict[Descriptor, list[int]],
            invocation: int | tuple[int, int, int],
            uniforms: dict[Descriptor, bytes] | None = None,
            inputs: dict[int, int | tuple[int, int, int]] | None = None) -> None:
    """Execute actual instructions, asserting bounds at pointer and load/store use."""
    values: dict[int, int | tuple] = {}
    labels: dict[int, int] = {}
    decorations: dict[int, dict[int, int]] = {}
    readonly: set[int] = set()
    variables = []
    pointers: dict[int, int] = {}
    uniform_layout = UniformLayout()
    uniforms = {} if uniforms is None else uniforms
    inputs = ({28: (invocation, 0, 0) if isinstance(invocation, int) else invocation}
              if inputs is None else inputs)
    entry = None
    body_start = None
    for pc, (op, args) in enumerate(instructions):
        uniform_layout.record(op, args)
        if op == 43:  # Constant scalar bit pattern.
            values[args[1]] = args[2]
        elif op == 71:
            if args[1] == 24:
                readonly.add(args[0])
            elif len(args) == 3:
                decorations.setdefault(args[0], {})[args[1]] = args[2]
        elif op == 32:
            pointers[args[0]] = args[2]
        elif op == 59:
            variables.append((args[1], args[2], args[0]))
        elif op == 15:
            entry = args[1]
        elif op == 54:
            assert args[1] == entry
            body_start = pc + 1
        elif op == 248:
            labels[args[0]] = pc
    assert entry is not None and body_start is not None
    for ident, storage, pointer_type in variables:
        if storage == 1:
            values[ident] = ("input", decorations[ident][11])
        elif storage == 12:
            descriptor = (decorations[ident][34], decorations[ident][33])
            assert descriptor in memory
            values[ident] = ("buffer", descriptor, ident)
        elif storage == 2:
            descriptor = (decorations[ident][34], decorations[ident][33])
            type_id = pointers[pointer_type]
            minimum = uniform_layout.extent(type_id)
            assert len(uniforms[descriptor]) >= minimum, "short uniform binding rejected before invocation"
            values[ident] = ("uniform", descriptor, type_id)
        else:
            raise AssertionError(("unexpected storage class", storage))
    pc, steps = body_start, 0
    label = previous = None
    while True:
        steps += 1
        assert steps < 4096
        op, args = instructions[pc]
        pc += 1
        if op == 248:
            label = args[0]
        elif op == 247:
            continue
        elif op == 61:
            pointer = values[args[2]]
            if pointer[0] == "input":
                values[args[1]] = inputs[pointer[1]]
            elif pointer[0] == "member":
                values[args[1]] = struct.unpack_from("<I", uniforms[pointer[1]], pointer[2])[0]
            else:
                assert pointer[0] == "element"
                buffer, index = memory[pointer[1]], pointer[2]
                assert 0 <= index < len(buffer) or (not buffer and index == 0), "out of bounds read"
                # An empty range reads zero under robustBufferAccess (see op 65).
                values[args[1]] = buffer[index] if buffer else 0
        elif op == 81:
            values[args[1]] = values[args[2]][args[3]]
        elif op == 68:
            pointer = values[args[2]]
            assert pointer[0] == "buffer" and args[3] == 0
            values[args[1]] = len(memory[pointer[1]])
        elif op in (170, 171, 172, 174, 176, 178):
            left, right = values[args[2]], values[args[3]]
            comparisons = {170: left == right, 171: left != right,
                           172: left > right, 174: left >= right,
                           176: left < right, 178: left <= right}
            values[args[1]] = comparisons[op]
        elif op == 250:
            previous = label
            pc = labels[args[1] if values[args[0]] else args[2]]
        elif op == 249:
            previous = label
            pc = labels[args[0]]
        elif op == 245:
            matches = [args[i] for i in range(2, len(args), 2)
                       if args[i + 1] == previous]
            assert len(matches) == 1
            values[args[1]] = values[matches[0]]
        elif op == 65:
            pointer = values[args[2]]
            if pointer[0] == "uniform":
                offset = uniform_layout.offset(pointer[2], [values[index] for index in args[3:]])
                assert offset + 4 <= len(uniforms[pointer[1]])
                values[args[1]] = ("member", pointer[1], offset)
            else:
                assert pointer[0] == "buffer" and values[args[3]] == 0
                index = values[args[4]]
                # Storage indices clamp to the last element (compiler/src/spirv_clamp.c),
                # so every admitted binding (at least the reflected one-element
                # minimum) stays in range. An empty range is below that minimum:
                # dispatch rejects it and the Vulkan device's required
                # robustBufferAccess makes its loads zero and drops its stores.
                buffer = memory[pointer[1]]
                assert 0 <= index < len(buffer) or (not buffer and index == 0), "unclamped element pointer"
                values[args[1]] = ("element", pointer[1], index, pointer[2])
        elif op == 128:
            values[args[1]] = (values[args[2]] + values[args[3]]) & 0xFFFFFFFF
        elif op == 130:
            values[args[1]] = (values[args[2]] - values[args[3]]) & 0xFFFFFFFF
        elif op == 132:
            values[args[1]] = (values[args[2]] * values[args[3]]) & 0xFFFFFFFF
        elif op in (129, 131, 133):
            left, right = f32(values[args[2]]), f32(values[args[3]])
            values[args[1]] = float_bits(left + right if op == 129 else
                                       left - right if op == 131 else left * right)
        elif op == 186:
            values[args[1]] = f32(values[args[2]]) > f32(values[args[3]])
        elif op == 169:
            values[args[1]] = values[args[3] if values[args[2]] else args[4]]
        elif op == 62:
            pointer = values[args[0]]
            assert pointer[0] == "element" and pointer[3] not in readonly
            buffer, index = memory[pointer[1]], pointer[2]
            assert 0 <= index < len(buffer) or (not buffer and index == 0), "out of bounds write"
            if buffer:
                buffer[index] = values[args[1]]
        elif op == 253:
            return
        elif op == 255:
            raise AssertionError("executed unreachable merge")
        else:
            raise AssertionError(("unexpected instruction", op, args))


class CompilerFixture(unittest.TestCase):
    def setUp(self) -> None:
        self.temp = tempfile.TemporaryDirectory(dir=ROOT / "build", prefix="cli-tests-")
        self.folder = Path(self.temp.name)
        self.source = self.folder / "shader.wgsl"
        self.out = self.folder / "out.spv"
        self.ref = self.folder / "reflection.json"

    def tearDown(self) -> None:
        self.temp.cleanup()

    def compile(self, source: str | Path) -> tuple[list[Instruction], dict]:
        if isinstance(source, str):
            self.source.write_text(source, encoding="utf-8")
            source = self.source
        self.out.unlink(missing_ok=True)
        self.ref.unlink(missing_ok=True)
        run = invoke(source, self.out, self.ref)
        self.assertEqual(run.returncode, 0, run.stderr.decode())
        run = subprocess.run([str(VALIDATOR), "--target-env", "vulkan1.1", str(self.out)],
                             capture_output=True, check=False)
        self.assertEqual(run.returncode, 0, run.stderr.decode())
        return decode(self.out.read_bytes()), json.loads(self.ref.read_text(encoding="utf-8"))


class TestCompilerCli(CompilerFixture):
    def test_retained_seed_addends_and_workgroups(self) -> None:
        for addend in (0, 1, 42, 0xFFFFFFFF):
            for local_size in (1, 64, 256):
                with self.subTest(addend=addend, local_size=local_size):
                    source = SEED.replace("+ 1u", f"+ {addend}u").replace(
                        "workgroup_size(1)", f"workgroup_size({local_size})")
                    instructions, reflection = self.compile(source)
                    self.assertEqual(reflection, {
                        "entry_point": "main", "workgroup_size": [local_size, 1, 1],
                        "binding_count": 1, "bindings": [{
                            "name": "values", "group": 0, "binding": 0,
                            "scalar_type": "u32", "access": "read_write",
                            "element_stride": 4, "runtime_sized": True,
                        }],
                    })
                    for length in (0, 1, 4, 17):
                        initial = [(0xFFFFFFFE + i * 11) & 0xFFFFFFFF for i in range(length)]
                        for dispatched in (length // 2, length + 19):
                            memory = {(0, 0): initial[:]}
                            for invocation in range(dispatched):
                                execute(instructions, memory, invocation)
                            execute(instructions, memory, 0xFFFFFFFF)
                            # Indices past the binding, including 0xFFFFFFFF, clamp to
                            # the last element (compiler/src/spirv_clamp.c); an empty range is untouched.
                            expected = initial[:]
                            for invocation in [*range(dispatched), 0xFFFFFFFF]:
                                if length:
                                    j = min(invocation, length - 1)
                                    expected[j] = (expected[j] + addend) & 0xFFFFFFFF
                            self.assertEqual(memory[(0, 0)], expected)

    def test_named_constants_and_literal_left_operand(self) -> None:
        for value in (0, 1, 42, 0xFFFFFFFF):
            for expression in ("values[id.x] + INC", "INC + values[id.x]"):
                source = f"const INC:u32={value}u;\n" + SEED.replace("values[id.x] + 1u", expression)
                instructions, _ = self.compile(source)
                memory = {(0, 0): [0, 1, 0xFFFFFFFF]}
                for i in range(5):
                    execute(instructions, memory, i)
                # Invocations 3 and 4 clamp to values[2] (compiler/src/spirv_clamp.c).
                self.assertEqual(memory[(0, 0)], [value, (1 + value) & 0xFFFFFFFF,
                                                 (0xFFFFFFFF + 3 * value) & 0xFFFFFFFF])
        instructions, _ = self.compile(SEED.replace("values[id.x] + 1u", "42u + values[id.x]"))
        memory = {(0, 0): [0xFFFFFFFF]}
        execute(instructions, memory, 0)
        self.assertEqual(memory[(0, 0)], [41])

    def test_both_unchanged_real_residual_sources(self) -> None:
        paths = [REPO / "bench/inference-pipeline/kernels/residual.wgsl",
                 REPO / "bench/kernels/compilation-corpus/simple_residual_add.wgsl"]
        self.assertEqual(paths[0].read_bytes(), paths[1].read_bytes())
        for path in paths:
            instructions, reflection = self.compile(path)
            self.assertEqual(reflection["entry_point"], "main")
            self.assertEqual(reflection["workgroup_size"], [256, 1, 1])
            self.assertEqual(reflection["binding_count"], 3)
            self.assertEqual(reflection["bindings"], [{
                "name": name, "group": 0, "binding": i, "scalar_type": "f32",
                "access": "read_write" if i == 2 else "read",
                "element_stride": 4, "runtime_sized": True,
            } for i, name in enumerate(("a", "b", "out"))])
            # Hand-specified IEEE-754 cases include signed zero and subnormals.
            left = [0x3F800000, 1, 0x80000000, 0, 0x7F800000, 0x7F800000,
                    0x7FC12345, 0x7F7FFFFF, 0x00800000]
            right = [0x40000000, 2, 0x80000000, 0x80000000, 0x3F800000,
                     0xFF800000, 0x3F800000, 0x7F7FFFFF, 0x80800000]
            expected = [0x40400000, 3, 0x80000000, 0, 0x7F800000, None,
                        None, 0x7F800000, 0]
            memory = {(0, 0): left[:], (0, 1): right[:], (0, 2): [123] * len(left)}
            for invocation in range(len(left) + 3):
                execute(instructions, memory, invocation)
            execute(instructions, memory, 0xFFFFFFFF)
            for actual, wanted in zip(memory[(0, 2)], expected):
                if wanted is None:
                    self.assertTrue(math.isnan(f32(actual)))
                else:
                    self.assertEqual(actual, wanted)
            self.assertEqual(memory[(0, 0)], left)
            self.assertEqual(memory[(0, 1)], right)
            for lengths in ((0, 0, 0), (0, 3, 5), (5, 0, 3), (1, 3, 7), (7, 2, 0)):
                for dispatch in (2, 10):
                    a_count, b_count, out_count = lengths
                    memory = {(0, 0): [0x3F800000] * a_count,
                              (0, 1): [0x40000000] * b_count,
                              (0, 2): [0x7FC000A5] * out_count}
                    for invocation in range(dispatch):
                        execute(instructions, memory, invocation)
                    execute(instructions, memory, 0xFFFFFFFF)
                    # Each index clamps to its own binding's last element (compiler/src/spirv_clamp.c), so
                    # every read sees 1.0/2.0 (an empty range reads zero) and
                    # invocations past out, including 0xFFFFFFFF, write its last word.
                    total = (1.0 if a_count else 0.0) + (2.0 if b_count else 0.0)
                    wanted = [float_bits(total) if i < dispatch or i == out_count - 1 else 0x7FC000A5
                              for i in range(out_count)]
                    self.assertEqual(memory[(0, 2)], wanted)

    def test_sparse_reordered_and_unused_bindings(self) -> None:
        declarations = [("result", 3, 31, "read_write"), ("rhs", 2, 19, "read"),
                        ("lhs", 0, 9, "read")]
        declarations += [(f"unused{i}", 1, i, "read") for i in range(5)]
        source = "\n".join(f"@binding({binding}) @group({group}) var<storage,{access}> {name}:array<u32>;"
                           for name, group, binding, access in declarations)
        source += "\n@compute @workgroup_size(64) fn run(@builtin(global_invocation_id) id:vec3u){"
        source += "let idx=id.x;result[idx]=lhs[id.x]+rhs[idx];}"
        instructions, reflection = self.compile(source)
        self.assertEqual(reflection["binding_count"], 8)
        for reflected, declared in zip(reflection["bindings"], declarations):
            name, group, binding, access = declared
            self.assertEqual(reflected, {"name": name, "group": group, "binding": binding,
                                         "scalar_type": "u32", "access": access,
                                         "element_stride": 4, "runtime_sized": True})
        memory = {(group, binding): [] for _, group, binding, _ in declarations}
        memory[(0, 9)], memory[(2, 19)], memory[(3, 31)] = [0xFFFFFFFF, 5], [1], [99, 99, 99]
        for i in range(5):
            execute(instructions, memory, i)
        # Each binding clamps its own index (compiler/src/spirv_clamp.c): rhs[i]
        # reads rhs[0] = 1, lhs[i>1] reads lhs[1] = 5, result[i>2] writes result[2].
        self.assertEqual(memory[(3, 31)], [0, 6, 6])

    def test_cli_failure_preserves_destinations(self) -> None:
        self.source.write_text(SEED, encoding="utf-8")
        self.compile(SEED)
        saved = self.out.read_bytes(), self.ref.read_bytes()
        for source in (SEED, "invalid", SEED + "\0", " " * 65537):
            self.source.write_text(source, encoding="utf-8")
            self.assertNotEqual(invoke(self.source, self.out, self.ref).returncode, 0)
            self.assertEqual((self.out.read_bytes(), self.ref.read_bytes()), saved)
        self.out.unlink()
        self.ref.unlink()
        for source in ("", "invalid", SEED + "\0", " " * 65537):
            self.source.write_text(source, encoding="utf-8")
            self.assertNotEqual(invoke(self.source, self.out, self.ref).returncode, 0)
            self.assertFalse(self.out.exists() or self.ref.exists())
        self.source.write_text(SEED, encoding="utf-8")
        self.assertNotEqual(invoke(self.source, self.out, self.folder / "absent/ref.json").returncode, 0)
        self.assertFalse(self.out.exists())
        self.assertNotEqual(invoke(self.source, self.out, self.out).returncode, 0)
        self.assertNotEqual(invoke(self.source, self.out, self.folder / "./out.spv").returncode, 0)
        self.assertFalse(self.out.exists())
        self.ref.symlink_to(self.folder / "missing-target")
        self.assertNotEqual(invoke(self.source, self.out, self.ref).returncode, 0)
        self.assertTrue(self.ref.is_symlink())
        self.assertFalse(self.out.exists())
        self.ref.unlink()
        self.out.write_bytes(b"existing binary")
        self.assertNotEqual(invoke(self.source, self.out, self.ref).returncode, 0)
        self.assertEqual(self.out.read_bytes(), b"existing binary")
        self.assertFalse(self.ref.exists())
        self.out.unlink()
        self.ref.write_bytes(b"existing reflection")
        self.assertNotEqual(invoke(self.source, self.out, self.ref).returncode, 0)
        self.assertEqual(self.ref.read_bytes(), b"existing reflection")
        self.assertFalse(self.out.exists())
        self.ref.unlink()
        self.assertNotEqual(invoke(self.folder / "missing-source", self.out, self.ref).returncode, 0)
        self.assertFalse(self.out.exists() or self.ref.exists())
        self.assertEqual(list(self.folder.glob("*.tmp.*")), [])
        for args in ([], ["--shader-path"], ["--unknown", "x"],
                     ["--shader-path", "x", "--shader-path", "y"]):
            run = subprocess.run([str(CLI), *args], capture_output=True, check=False)
            self.assertEqual(run.returncode, 2)


if __name__ == "__main__":
    unittest.main()
