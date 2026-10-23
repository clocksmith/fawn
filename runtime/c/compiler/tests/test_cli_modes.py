#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Doe contributors
"""Developer tests for optional reflection and CLI-owned file cleanup.

Run as a module from the repository root. Real consumer import is unchanged;
no discovery scan or benchmark is run. Linker faults affect test builds only.
"""
from __future__ import annotations

import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from bench.gates.spirv_val_gate import emit_spirv_for_wgsl

COMPILER = Path(__file__).resolve().parents[1]
REPO = COMPILER.parents[2]
CLI = Path(os.environ.get("DOE_COMPILER_CLI", COMPILER / "build/doe-compiler-c"))
FAULT_CLI = Path(os.environ.get(
    "DOE_COMPILER_FAULT_CLI", COMPILER / "build/test_cli_faults"
))
VALIDATOR = os.environ["SPIRV_VAL"]
SOURCES = [
    REPO / "bench/inference-pipeline/kernels/residual.wgsl",
    REPO / "bench/kernels/compilation-corpus/simple_residual_add.wgsl",
    REPO / "bench/fixtures/wgsl-corpus/webgpu/sample-prefix-sum.wgsl",
    REPO / "bench/kernels/compilation-corpus/simple_scale_f32.wgsl",
    REPO / "bench/kernels/compilation-corpus/simple_relu.wgsl",
]


def snapshot(folder: Path) -> dict[str, bytes | str]:
    result = {}
    for path in folder.rglob("*"):
        key = str(path.relative_to(folder))
        if path.is_symlink():
            result[key] = "symlink:" + os.readlink(path)
        elif path.is_file():
            result[key] = path.read_bytes()
        else:
            result[key] = "directory"
    return result


class TestCliModes(unittest.TestCase):
    def setUp(self) -> None:
        self.temp = tempfile.TemporaryDirectory(prefix="cli-modes-")
        self.addCleanup(self.temp.cleanup)
        self.folder = Path(self.temp.name)
        self.source = self.folder / "input.wgsl"
        self.source.write_bytes(SOURCES[0].read_bytes())
        self.output = self.folder / "output.spv"
        self.reflection = self.folder / "reflection.json"

    def invoke(self, args: list[str], executable: Path = CLI,
               env: dict[str, str] | None = None
               ) -> subprocess.CompletedProcess[str]:
        return subprocess.run([str(executable), *args], capture_output=True,
                              text=True, timeout=15, env=env, cwd=self.folder)

    def args(self, reflected: bool) -> list[str]:
        args = ["--shader-path", str(self.source), "--out", str(self.output)]
        return args + ["--reflection", str(self.reflection)] if reflected else args

    def test_real_consumer_and_reflection_are_byte_identical(self) -> None:
        for index, source in enumerate(SOURCES):
            with self.subTest(source=source):
                before = source.read_bytes()
                directory = self.folder / str(index)
                directory.mkdir()
                output = directory / "binary.spv"
                ok, message = emit_spirv_for_wgsl(str(CLI), source, output)
                self.assertTrue(ok, message)
                self.assertEqual({p.name for p in directory.iterdir()},
                                 {"binary.spv"})
                reflected = directory / "reflected.spv"
                reflection = directory / "reflection.json"
                run = self.invoke([
                    "--reflection", str(reflection), "--out", str(reflected),
                    "--shader-path", str(source),
                ])
                self.assertEqual(run.returncode, 0, run.stderr)
                self.assertEqual(output.read_bytes(), reflected.read_bytes())
                obj = json.loads(reflection.read_text(encoding="utf-8"))
                self.assertEqual(set(obj), {"entry_point", "workgroup_size",
                                            "binding_count", "bindings"})
                self.assertEqual(obj["binding_count"], len(obj["bindings"]))
                for binding in obj["bindings"]:
                    self.assertEqual(set(binding), {
                        "name", "group", "binding", "scalar_type", "access",
                        "element_stride", "runtime_sized",
                    })
                validation = subprocess.run(
                    [VALIDATOR, "--target-env", "vulkan1.1", str(output)],
                    capture_output=True, text=True, timeout=15,
                )
                self.assertEqual(validation.returncode, 0, validation.stderr)
                self.assertEqual(source.read_bytes(), before)
                self.assertEqual({p.name for p in directory.iterdir()},
                                 {"binary.spv", "reflected.spv", "reflection.json"})

    def test_malformed_options_never_publish(self) -> None:
        base = self.args(False)
        cases = [[], ["--shader-path"], ["--out", str(self.output)],
                 ["--shader-path", str(self.source)],
                 base + ["--reflection"], base + ["--reflection", ""],
                 ["--shader-path", str(self.source), "--out", "--reflection"],
                 ["--shader-path", "--out", "--out", str(self.output)],
                 base + ["--reflection", "--out"],
                 base + ["--unknown", "value"], base + ["--unknown"],
                 base + ["--shader-path", str(self.source)],
                 base + ["--out", str(self.output)],
                 self.args(True) + ["--reflection", str(self.reflection)]]
        before = snapshot(self.folder)
        for args in cases:
            with self.subTest(args=args):
                run = self.invoke(args)
                self.assertEqual(run.returncode, 2, run.stderr)
                self.assertEqual(snapshot(self.folder), before)

    def test_option_named_paths_have_an_unambiguous_spelling(self) -> None:
        source = self.folder / "--out"
        source.write_bytes(self.source.read_bytes())
        for output in ["./--reflection", "--ordinary-filename"]:
            with self.subTest(output=output):
                run = self.invoke([
                    "--shader-path", "./--out", "--out", output,
                    "--reflection", "./--shader-path",
                ])
                self.assertEqual(run.returncode, 0, run.stderr)
                self.assertTrue((self.folder / output).is_file())
                self.assertTrue((self.folder / "--shader-path").is_file())
                self.assertEqual(source.read_bytes(), self.source.read_bytes())
                (self.folder / output).unlink()
                (self.folder / "--shader-path").unlink()

    def test_temporary_suffix_limit_cleans_both_modes(self) -> None:
        parent = self.folder
        while len(str(parent)) < 3800:
            parent /= "d" * 200
            parent.mkdir()
        destination = parent / ("e" * (4090 - len(str(parent)) - 1))
        # Keep each filename within the filesystem component limit.
        if len(destination.name) > 255:
            parent /= "f" * 200
            parent.mkdir()
            destination = parent / ("e" * (4090 - len(str(parent)) - 1))
        self.assertEqual(len(str(destination)), 4090)
        before = snapshot(self.folder)
        for option in ["--out", "--reflection"]:
            with self.subTest(option=option):
                args = self.args(option == "--reflection")
                args[args.index(option) + 1] = str(destination)
                run = self.invoke(args)
                self.assertEqual(run.returncode, 1, run.stderr)
                self.assertIn("temporary", run.stderr)
                self.assertEqual(snapshot(self.folder), before)

    def test_missing_bad_and_oversized_source_preserve_files(self) -> None:
        for reflected in [False, True]:
            for content in [None, b"", b"invalid", b" " * 65537,
                            SOURCES[0].read_bytes() + b"\0"]:
                with self.subTest(reflected=reflected, content_length=(
                        len(content) if content is not None else None)):
                    if content is None:
                        self.source.unlink(missing_ok=True)
                    else:
                        self.source.write_bytes(content)
                    before = snapshot(self.folder)
                    run = self.invoke(self.args(reflected))
                    self.assertNotEqual(run.returncode, 0)
                    self.assertEqual(snapshot(self.folder), before)

    def test_existing_destinations_and_invalid_paths(self) -> None:
        for reflected in [False, True]:
            for target in ["output", "reflection"] if reflected else ["output"]:
                for kind in ["file", "directory", "symlink", "dangling"]:
                    with self.subTest(reflected=reflected, target=target,
                                      kind=kind):
                        path = getattr(self, target)
                        if kind == "file":
                            path.write_bytes(b"unowned content")
                        elif kind == "directory":
                            path.mkdir()
                        else:
                            path.symlink_to(self.source if kind == "symlink"
                                            else self.folder / "absent")
                        before = snapshot(self.folder)
                        run = self.invoke(self.args(reflected))
                        self.assertNotEqual(run.returncode, 0)
                        self.assertEqual(snapshot(self.folder), before)
                        path.rmdir() if kind == "directory" else path.unlink()
            for value in ["", str(self.folder / "missing" / "output"),
                          str(self.source), "/" + "p" * 4096]:
                for option in ["--out", "--reflection"] if reflected else ["--out"]:
                    with self.subTest(reflected=reflected, option=option,
                                      path_length=len(value)):
                        args = self.args(reflected)
                        args[args.index(option) + 1] = value
                        before = snapshot(self.folder)
                        self.assertNotEqual(self.invoke(args).returncode, 0)
                        self.assertEqual(snapshot(self.folder), before)
        for reflection in [str(self.output), str(self.folder) + "/./output.spv"]:
            before = snapshot(self.folder)
            args = self.args(False) + ["--reflection", reflection]
            self.assertNotEqual(self.invoke(args).returncode, 0)
            self.assertEqual(snapshot(self.folder), before)

    def test_injected_failures_clean_only_owned_files(self) -> None:
        (self.folder / "unrelated.tmp.owned-by-someone-else").write_bytes(b"keep")
        for reflected in [False, True]:
            faults = [("write", 1), ("partial", 1), ("close", 1),
                      ("close", 2), ("link", 1), ("temporary", 1)]
            if reflected:
                faults += [("write", 2), ("close", 3), ("link", 2),
                           ("temporary", 2)]
            for operation, call in faults:
                with self.subTest(reflected=reflected, fault=(operation, call)):
                    before = snapshot(self.folder)
                    env = dict(os.environ, DOE_CLI_FAULT=operation,
                               DOE_CLI_FAULT_CALL=str(call))
                    run = self.invoke(self.args(reflected), FAULT_CLI, env)
                    self.assertEqual(run.returncode, 1, run.stderr)
                    self.assertEqual(snapshot(self.folder), before)

    def test_publication_race_preserves_foreign_destination(self) -> None:
        for reflected in [False, True]:
            for call in [1, 2] if reflected else [1]:
                for operation in ["race", "race_symlink"]:
                    with self.subTest(reflected=reflected, call=call,
                                      operation=operation):
                        before = snapshot(self.folder)
                        path = self.output if call == 1 else self.reflection
                        before[path.name] = (b"unowned destination" if operation
                                             == "race" else "symlink:unowned-target")
                        env = dict(os.environ, DOE_CLI_FAULT=operation,
                                   DOE_CLI_FAULT_CALL=str(call))
                        run = self.invoke(self.args(reflected), FAULT_CLI, env)
                        self.assertEqual(run.returncode, 1, run.stderr)
                        self.assertEqual(snapshot(self.folder), before)
                        path.unlink()

    def test_interrupted_and_short_writes_complete(self) -> None:
        for reflected in [False, True]:
            normal = self.invoke(self.args(reflected))
            self.assertEqual(normal.returncode, 0, normal.stderr)
            expected = snapshot(self.folder)
            for operation in ["eintr", "short"]:
                with self.subTest(reflected=reflected, operation=operation):
                    self.output.unlink()
                    self.reflection.unlink(missing_ok=True)
                    env = dict(os.environ, DOE_CLI_FAULT=operation,
                               DOE_CLI_FAULT_CALL="1")
                    run = self.invoke(self.args(reflected), FAULT_CLI, env)
                    self.assertEqual(run.returncode, 0, run.stderr)
                    self.assertEqual(snapshot(self.folder), expected)
            self.output.unlink()
            self.reflection.unlink(missing_ok=True)


if __name__ == "__main__":
    unittest.main()
