#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Actual offline target consumers, publication and source attribution."""
from __future__ import annotations

import os
from pathlib import Path
import subprocess
import tempfile
import unittest

OWNER = Path(__file__).resolve().parents[1]
REPO = OWNER.parents[2]
BUILD = Path(os.environ.get("DOE_COMPILER_BUILD", OWNER / "build"))
SOURCE = REPO / "bench/inference-pipeline/kernels/residual.wgsl"


class TargetEmissionTests(unittest.TestCase):
    def run_cli(self, target: str, arguments: list[str]) -> subprocess.CompletedProcess[bytes]:
        return subprocess.run([str(BUILD / f"doe-emit-{target}-c"), *arguments],
                              capture_output=True, timeout=20)

    def test_file_and_stdout_are_identical_and_targets_validate(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for target in ("spirv", "msl", "hlsl"):
                with self.subTest(target=target):
                    stdout = self.run_cli(target, ["--shader-path", str(SOURCE)])
                    self.assertEqual(stdout.returncode, 0, stdout.stderr)
                    path = root / target
                    output = self.run_cli(target, ["--shader-path", str(SOURCE), "--out", str(path)])
                    self.assertEqual(output.returncode, 0, output.stderr)
                    self.assertEqual(path.read_bytes(), stdout.stdout)
                    self.assertTrue(stdout.stdout)
                    if target == "spirv":
                        run = subprocess.run([os.environ.get("SPIRV_VAL", "spirv-val"),
                                              "--target-env", "vulkan1.1", str(path)],
                                             capture_output=True, timeout=20)
                        self.assertEqual(run.returncode, 0, run.stderr)
                    else:
                        # Native SDK compilation is retained separately. These
                        # checks catch routing to the other language or SPIR-V.
                        self.assertIn(b"metal_stdlib" if target == "msl" else b"numthreads", stdout.stdout)
                    sentinel = path.read_bytes()
                    refused = self.run_cli(target, ["--shader-path", str(SOURCE), "--out", str(path)])
                    self.assertNotEqual(refused.returncode, 0)
                    self.assertEqual(path.read_bytes(), sentinel)
                    self.assertFalse(list(root.glob("*.tmp.*")))

    def test_invalid_source_reports_location_without_publication(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "invalid.wgsl"
            source.write_text("\n@compute @workgroup_size(1) fn main() { let x: u32 = false; }\n", encoding="utf-8")
            for target in ("spirv", "msl", "hlsl"):
                path = root / target
                result = self.run_cli(target, ["--shader-path", str(source), "--out", str(path)])
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(b":2:", result.stderr)
                self.assertFalse(path.exists())
                self.assertEqual(result.stdout, b"")
            self.assertEqual([p.name for p in root.iterdir()], ["invalid.wgsl"])

    def test_empty_valid_module_and_invalid_options(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "empty.wgsl"
            source.write_text("// entryless module\n", encoding="utf-8")
            for target in ("msl", "hlsl"):
                result = self.run_cli(target, ["--shader-path", str(source)])
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(result.stdout, b"")
                help_result = self.run_cli(target, ["--help"])
                self.assertEqual(help_result.returncode, 0)
                for arguments in (["--target", "dxil"], ["--target", "msl", "--target", "hlsl"],
                                  ["--reflection", "unused.json"]):
                    result = self.run_cli(target, ["--shader-path", str(source), *arguments])
                    self.assertNotEqual(result.returncode, 0)
                    self.assertEqual(result.stdout, b"")

    def test_text_job_allocation_failures_publish_nothing(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for target in ("msl", "hlsl"):
                output = root / target
                for call in range(1, 17):
                    env = dict(os.environ, DOE_CLI_FAULT="malloc", DOE_CLI_FAULT_CALL=str(call))
                    result = subprocess.run([str(BUILD / "test_cli_faults"), "--target", target,
                                             "--shader-path", str(SOURCE), "--out", str(output)],
                                            capture_output=True, timeout=20, env=env)
                    if result.returncode == 0:
                        self.assertGreater(call, 1)
                        self.assertTrue(output.read_bytes())
                        output.unlink()
                        break
                    self.assertEqual(result.returncode, 1, result.stderr)
                    self.assertIn(b"allocation", result.stderr)
                    self.assertEqual(list(root.iterdir()), [])
                else:
                    self.fail("allocation sweep did not reach success")

    def test_retained_source_size_boundary_and_diagnostic_offset(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "large.wgsl"
            limit = 2 * 1024 * 1024
            shader = b"\n@compute @workgroup_size(1) fn main() {}\n"
            source.write_bytes(b"//" + b" " * (limit - len(shader) - 2) + shader)
            for target in ("spirv", "msl", "hlsl"):
                result = self.run_cli(target, ["--shader-path", str(source)])
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertTrue(result.stdout)
            source.write_bytes(source.read_bytes() + b" ")
            for target in ("spirv", "msl", "hlsl"):
                output = root / target
                result = self.run_cli(target, ["--shader-path", str(source), "--out", str(output)])
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(b"2 MiB", result.stderr)
                self.assertEqual(result.stdout, b"")
                self.assertFalse(output.exists())
            prefix = b"//" + b" " * 70000 + b"\n"
            source.write_bytes(prefix + b"@compute @workgroup_size(1) fn main() { let x: u32 = false; }\n")
            for target in ("spirv", "msl", "hlsl"):
                result = self.run_cli(target, ["--shader-path", str(source)])
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(b":2:", result.stderr)
                offset = int(result.stderr.split(b"offset=", 1)[1].split(b":", 1)[0])
                self.assertGreater(offset, 70000)
                self.assertEqual(result.stdout, b"")


if __name__ == "__main__":
    unittest.main()
