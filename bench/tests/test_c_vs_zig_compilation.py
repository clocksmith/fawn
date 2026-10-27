"""Protect sampling symmetry, failure retention and SDK receipt admission."""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

from bench.tools import compare_c_vs_zig_compilation as compiler


class CompilerProcessTests(unittest.TestCase):
    def test_retains_exact_output_stderr_and_exit_code(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            sample = compiler.process_sample([
                sys.executable, "-c",
                "import sys; sys.stdout.buffer.write(b'code\\x00'); "
                "sys.stderr.write('first failure'); sys.exit(7)",
            ], Path(temporary) / "sample", 5)
            self.assertEqual(sample["exitCode"], 7)
            self.assertEqual(Path(sample["stdout"]).read_bytes(), b"code\0")
            self.assertEqual(Path(sample["stderr"]).read_text(), "first failure")
            self.assertGreater(sample["wallNs"], 0)
            self.assertIsNotNone(sample["cpuMs"])

    def test_timeout_is_retained_as_failure(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            sample = compiler.process_sample([
                sys.executable, "-c", "import time; time.sleep(30)",
            ], Path(temporary) / "sample", 0.05)
            self.assertTrue(sample["timedOut"])
            self.assertNotEqual(sample["exitCode"], 0)

    def test_spirv_requires_complete_declared_entry_set(self) -> None:
        name = b"main\0\0\0\0"
        words = [0x07230203, 0x00010300, 0, 4, 0, (5 << 16) | 15,
                 5, 1, *struct.unpack("<2I", name)]
        data = struct.pack(f"<{len(words)}I", *words)
        compiler.check_entries(data, "spirv", [{"name": "main", "stage": "compute"}])
        with self.assertRaisesRegex(ValueError, "entry set"):
            compiler.check_entries(data, "spirv", [
                {"name": "main", "stage": "compute"},
                {"name": "second", "stage": "compute"},
            ])
        with self.assertRaisesRegex(ValueError, "framing"):
            compiler.spirv_entries(data + b"x")

    @unittest.skipIf(os.name == "nt", "POSIX executable fixture for existing aliases")
    def test_warmups_order_validation_binding_and_source_drift(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "bench/kernels/compilation-corpus/trivial_noop_compute.wgsl"
            source.parent.mkdir(parents=True)
            source.write_text("@compute @workgroup_size(1) fn main(){}")
            usage = root / "bench/lib/child_usage.py"
            usage.parent.mkdir()
            usage.write_text("# fixture identity only\n")
            catalog = root / "catalog.json"
            catalog.write_text(json.dumps({"workloads": [{
                "id": "fixture", "runnerType": "compilation",
                "shaderPath": str(source.relative_to(root)),
            }]}))
            tools = root / "bin"
            tools.mkdir()
            for name in ("doe-emit-msl", "doe-emit-msl-c"):
                executable = tools / name
                executable.write_text(
                    "#!/bin/sh\nprintf 'kernel void main() {}\\n'\n",
                )
                executable.chmod(0o755)
            subprocess.run(["git", "init", "-q", str(root)], check=True)
            subprocess.run(["git", "-C", str(root), "-c", "user.name=Test",
                            "-c", "user.email=test@example.invalid", "commit",
                            "--allow-empty", "-qm", "fixture"], check=True)
            output = root / "result"
            args = argparse.Namespace(out=output, workloads=catalog,
                                      c_bin_dir=tools, zig_bin_dir=tools,
                                      targets=["msl"], warmup=1, iterations=2,
                                      timeout=5)
            with patch.object(compiler, "ROOT", root):
                self.assertEqual(compiler.measure(args), 0)
            report_path = output / "report.json"
            report = json.loads(report_path.read_text())
            row = report["rows"][0]
            for group in row["providers"].values():
                self.assertEqual(len(group["warmup"]), 1)
                self.assertEqual(len(group["samples"]), 2)
                self.assertEqual(group["statistics"]["count"], 2)
                self.assertEqual(group["warmup"][0]["providerOrder"], ["c", "zig"])
                self.assertEqual(group["samples"][0]["providerOrder"], ["zig", "c"])
            self.assertEqual(compiler.finalize(report_path, []), 1)
            self.assertNotIn("zigOverCProcessMedianRatio", json.loads(
                report_path.read_text())["rows"][0])
            receipt = root / "validation.json"
            validation = {
                "manifestSha256": compiler.digest(output / "validation-manifest.json"),
                "validatorScriptSha256": compiler.digest(Path(compiler.__file__)),
                "target": "msl", "rows": [
                    {"id": "fixture", "provider": provider, "status": "passed",
                     "artifactSha256": group["samples"][0]["outputSha256"]}
                    for provider, group in row["providers"].items()],
            }
            compiler.write_json(receipt, validation)
            self.assertEqual(compiler.finalize(report_path, [receipt]), 0)
            source.write_text("changed source")
            self.assertEqual(compiler.finalize(report_path, [receipt]), 1)
            drifted = json.loads(report_path.read_text())
            self.assertIn(str(source.resolve()), drifted["drift"])
            self.assertEqual(drifted["rows"][0]["comparisonStatus"], "incomparable")
            self.assertNotIn("zigOverCProcessMedianRatio", drifted["rows"][0])
            validation["manifestSha256"] = "stale"
            compiler.write_json(receipt, validation)
            with self.assertRaisesRegex(ValueError, "frozen campaign"):
                compiler.finalize(report_path, [receipt])

    def test_bundle_paths_cannot_escape_and_missing_entries_fail(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            with self.assertRaisesRegex(ValueError, "escapes"):
                compiler.inside(Path(temporary), "../outside")
        with self.assertRaisesRegex(ValueError, "entry set"):
            compiler.check_entries(b"kernel void other(){}", "msl", [
                {"name": "main", "stage": "compute"},
            ])
        with self.assertRaisesRegex(ValueError, "declared entry"):
            compiler.declared_entries("bench/kernels/new-unreviewed.wgsl")


if __name__ == "__main__":
    unittest.main()
